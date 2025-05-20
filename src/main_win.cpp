#ifndef ARDUINO

// Windows-specific main implementation

#define NOMINMAX
#include <algorithm>
#include <mutex>
#include <cstdlib>
#include <csignal>
#include "dexed.h"
#include <windows.h>
#include <mmsystem.h>
#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#pragma comment(lib, "winmm.lib")
unsigned int SAMPLE_RATE = 48000;
unsigned int BUFFER_FRAMES = 1024; // Reduce for lower latency
constexpr uint8_t MAX_NOTES = 16;
int numBuffers = 4;
std::atomic<bool> running{true};
void signal_handler(int signal) {
    if (signal == SIGINT) {
        running = false;
        std::cout << "\n[INFO] Caught Ctrl+C, exiting..." << std::endl;
    }
}
Dexed* synth = nullptr;
std::mutex synthMutex;
std::vector<std::vector<short>> audioBuffers;
std::vector<WAVEHDR> waveHeaders;
HWAVEOUT hWaveOut = nullptr;
void CALLBACK midiInProc(HMIDIIN, UINT uMsg, DWORD_PTR, DWORD_PTR dwParam1, DWORD_PTR, DWORD_PTR) {
    std::lock_guard<std::mutex> lock(synthMutex);
    if (!synth) return;
    if (uMsg == MIM_DATA) {
        DWORD msg = (DWORD)dwParam1;
        uint8_t status = msg & 0xF0;
        uint8_t note = (msg >> 8) & 0x7F;
        uint8_t velocity = (msg >> 16) & 0x7F;
        std::cout << "MIDI: status=0x" << std::hex << (int)status
                  << " note=" << std::dec << (int)note
                  << " velocity=" << (int)velocity << std::endl;
        if (status == 0x90 && velocity > 0) {
            synth->keydown(note, velocity);
        } else if ((status == 0x80) || (status == 0x90 && velocity == 0)) {
            synth->keyup(note);
        }
    }
}
// Sample voice from ./examples/SimplePlay/SimplePlay.ino
uint8_t fmpiano_sysex[156] = {
    95, 29, 20, 50, 99, 95, 00, 00, 41, 00, 19, 00, 00, 03, 00, 06, 79, 00, 01, 00, 14, // OP6 eg_rate_1-4, level_1-4, kbd_lev_scl_brk_pt, kbd_lev_scl_lft_depth, kbd_lev_scl_rht_depth, kbd_lev_scl_lft_curve, kbd_lev_scl_rht_curve, kbd_rate_scaling, amp_mod_sensitivity, key_vel_sensitivity, operator_output_level, osc_mode, osc_freq_coarse, osc_freq_fine, osc_detune
    95, 20, 20, 50, 99, 95, 00, 00, 00, 00, 00, 00, 00, 03, 00, 00, 99, 00, 01, 00, 00, // OP5
    95, 29, 20, 50, 99, 95, 00, 00, 00, 00, 00, 00, 00, 03, 00, 06, 89, 00, 01, 00, 07, // OP4
    95, 20, 20, 50, 99, 95, 00, 00, 00, 00, 00, 00, 00, 03, 00, 02, 99, 00, 01, 00, 07, // OP3
    95, 50, 35, 78, 99, 75, 00, 00, 00, 00, 00, 00, 00, 03, 00, 07, 58, 00, 14, 00, 07, // OP2
    96, 25, 25, 67, 99, 75, 00, 00, 00, 00, 00, 00, 00, 03, 00, 02, 99, 00, 01, 00, 10, // OP1
    94, 67, 95, 60, 50, 50, 50, 50,                                                     // 4 * pitch EG rates, 4 * pitch EG level
    04, 06, 00,                                                                         // algorithm, feedback, osc sync       
    34, 33, 00, 00, 00, 04,                                                             // lfo speed, lfo delay, lfo pitch_mod_depth, lfo_amp_mod_depth, lfo_sync, lfo_waveform
    03, 24,                                                                             // pitch_mod_sensitivity, transpose    
    70, 77, 45, 80, 73, 65, 78, 79, 00, 00                                              // 10 * char for name ("DEFAULT   ")   
  }; // FM-Piano
int main(int argc, char* argv[]) {
    // std::cout << "[DEBUG] Entered main_win.cpp main()" << std::endl;
    std::signal(SIGINT, signal_handler);
    int audioDev = 0;
    int midiDev = 0;
    bool useSynth = true;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "  -h, --help           Show this help message and exit\n"
                      << "  -a, --audio-device N Select audio device index (default: 0)\n"
                      << "  -m, --midi-device N  Select MIDI input device index (default: 0)\n"
                      << "  --sample-rate N      Set sample rate (default: 48000)\n"
                      << "  --buffer-frames N    Set audio buffer size in frames (default: 1024)\n"
                      << "  --num-buffers N      Set number of audio buffers (default: 4)\n"
                      << "  --sine               Output test sine wave instead of synth\n"
                      << "  --synth              Use synth (default)\n";
            return 0;
        } else if ((arg == "--audio-device" || arg == "-a") && i + 1 < argc) {
            audioDev = std::atoi(argv[++i]);
        } else if ((arg == "--midi-device" || arg == "-m") && i + 1 < argc) {
            midiDev = std::atoi(argv[++i]);
        } else if (arg == "--sample-rate" && i + 1 < argc) {
            SAMPLE_RATE = std::atoi(argv[++i]);
        } else if (arg == "--buffer-frames" && i + 1 < argc) {
            BUFFER_FRAMES = std::atoi(argv[++i]);
        } else if (arg == "--num-buffers" && i + 1 < argc) {
            numBuffers = std::atoi(argv[++i]);
        } else if (arg == "--sine") {
            useSynth = false;
        } else if (arg == "--synth") {
            useSynth = true;
        }
    }
    UINT numAudioDevs = waveOutGetNumDevs();
    std::cout << "Available audio output devices:" << std::endl;
    for (UINT i = 0; i < numAudioDevs; ++i) {
        WAVEOUTCAPS caps;
        waveOutGetDevCaps(i, &caps, sizeof(caps));
        std::wcout << L"  [" << i << L"] " << caps.szPname << std::endl;
    }
    std::cout << "[INFO] Using audio device: " << audioDev << std::endl;
    WAVEFORMATEX wfx = {};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 2;
    wfx.nSamplesPerSec = SAMPLE_RATE;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = wfx.nChannels * wfx.wBitsPerSample / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    wfx.cbSize = 0;
    if (waveOutOpen(&hWaveOut, audioDev, &wfx, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        std::cerr << "[ERROR] Failed to open audio device!\n";
        return 1;
    }
    std::cout << "[INFO] Audio device opened successfully." << std::endl;
    audioBuffers.resize(numBuffers);
    waveHeaders.resize(numBuffers);
    for (int i = 0; i < numBuffers; ++i) {
        audioBuffers[i].resize(BUFFER_FRAMES * 2);
        waveHeaders[i] = {};
        waveHeaders[i].lpData = reinterpret_cast<LPSTR>(audioBuffers[i].data());
        waveHeaders[i].dwBufferLength = BUFFER_FRAMES * 2 * sizeof(short);
        waveOutPrepareHeader(hWaveOut, &waveHeaders[i], sizeof(WAVEHDR));
    }
    // std::cout << "[DEBUG] Audio buffers prepared" << std::endl;
    HMIDIIN hMidiIn = nullptr;
    if (useSynth) {
        // std::cout << "[DEBUG] Creating synth" << std::endl;
        {
            std::lock_guard<std::mutex> lock(synthMutex);
            synth = new Dexed(MAX_NOTES, SAMPLE_RATE);
            // std::cout << "[DEBUG] Synth created" << std::endl;
            synth->resetControllers();
            synth->setVelocityScale(MIDI_VELOCITY_SCALING_OFF);
            // Load sample voice from SimplePlay.ino
            synth->loadVoiceParameters(fmpiano_sysex);
            // Set gain even higher for testing
            synth->setGain(2.0f);
            // std::cout << "[DEBUG] Sample voice loaded and gain set to 1.0f" << std::endl;
        }
        // std::cout << "[DEBUG] MIDI: midiInGetNumDevs()" << std::endl;
        UINT numMidiDevs = midiInGetNumDevs();
        std::cout << "Available MIDI input devices:" << std::endl;
        for (UINT i = 0; i < numMidiDevs; ++i) {
            MIDIINCAPS caps;
            midiInGetDevCaps(i, &caps, sizeof(caps));
            std::wcout << L"  [" << i << L"] " << caps.szPname << std::endl;
        }
        std::cout << "[INFO] Using MIDI input device: " << midiDev << std::endl;
        try {
            if (numMidiDevs > 0) {
                UINT selectedMidiDev = (midiDev < (int)numMidiDevs) ? midiDev : 0;
                MMRESULT res = midiInOpen(&hMidiIn, selectedMidiDev, (DWORD_PTR)midiInProc, 0, CALLBACK_FUNCTION);
                if (res == MMSYSERR_NOERROR) {
                    midiInStart(hMidiIn);
                    std::cout << "[INFO] MIDI input started on device " << selectedMidiDev << "." << std::endl;
                } else {
                    std::cerr << "[ERROR] Failed to open MIDI input device!" << std::endl;
                }
            } else {
                std::cerr << "[WARN] No MIDI input devices found. MIDI will be disabled." << std::endl;
            }
        } catch (const std::exception& ex) {
            std::cerr << "[EXCEPTION] MIDI setup: " << ex.what() << std::endl;
        } catch (...) {
            std::cerr << "[EXCEPTION] MIDI setup: unknown exception" << std::endl;
        }
        // std::cout << "[DEBUG] MIDI setup complete" << std::endl;
    }
    // std::cout << "[DEBUG] Before starting audio thread" << std::endl;
    // Pre-fill all audio buffers before starting audio thread
    std::vector<int16_t> monoBuffer(BUFFER_FRAMES);
    for (int i = 0; i < numBuffers; ++i) {
        if (useSynth) {
            std::lock_guard<std::mutex> lock(synthMutex);
            if (synth) synth->getSamples(monoBuffer.data(), BUFFER_FRAMES);
            for (int j = 0; j < BUFFER_FRAMES; ++j) {
                audioBuffers[i][2*j] = monoBuffer[j];
                audioBuffers[i][2*j+1] = monoBuffer[j];
            }
        } else {
            static double phase = 0.0;
            double freq = 440.0;
            double phaseInc = 2.0 * 3.141592653589793 * freq / 48000.0;
            for (int j = 0; j < BUFFER_FRAMES; ++j) {
                int16_t sample = (int16_t)(std::sin(phase) * 16000.0);
                phase += phaseInc;
                if (phase > 2.0 * 3.141592653589793) phase -= 2.0 * 3.141592653589793;
                audioBuffers[i][2*j] = sample;
                audioBuffers[i][2*j+1] = sample;
                monoBuffer[j] = sample;
            }
        }
        waveOutWrite(hWaveOut, &waveHeaders[i], sizeof(WAVEHDR));
    }
    std::thread audio([&]() {
        int bufferIndex = 0;
        std::vector<int16_t> monoBuffer(BUFFER_FRAMES);
        while (running) {
            WAVEHDR& hdr = waveHeaders[bufferIndex];
            // Wait for buffer to be done (WHDR_INQUEUE cleared)
            while (hdr.dwFlags & WHDR_INQUEUE) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                if (!running) return;
            }
            if (useSynth) {
                std::lock_guard<std::mutex> lock(synthMutex);
                if (synth) synth->getSamples(monoBuffer.data(), BUFFER_FRAMES);
                for (int i = 0; i < BUFFER_FRAMES; ++i) {
                    audioBuffers[bufferIndex][2*i] = monoBuffer[i];
                    audioBuffers[bufferIndex][2*i+1] = monoBuffer[i];
                }
            } else {
                static double phase = 0.0;
                double freq = 440.0;
                double phaseInc = 2.0 * 3.141592653589793 * freq / 48000.0;
                for (int i = 0; i < BUFFER_FRAMES; ++i) {
                    int16_t sample = (int16_t)(std::sin(phase) * 16000.0);
                    phase += phaseInc;
                    if (phase > 2.0 * 3.141592653589793) phase -= 2.0 * 3.141592653589793;
                    audioBuffers[bufferIndex][2*i] = sample;
                    audioBuffers[bufferIndex][2*i+1] = sample;
                    monoBuffer[i] = sample;
                }
            }
            waveOutWrite(hWaveOut, &hdr, sizeof(WAVEHDR));
            bufferIndex = (bufferIndex + 1) % numBuffers;
        }
    });
    // std::cout << "[DEBUG] After starting audio thread" << std::endl;
    std::cout << "[INFO] Synth_Dexed running. Press Ctrl+C to quit.\n";
    // std::cout << "[DEBUG] Before while(running) loop" << std::endl;
    while (running) {
        // std::cout << "[DEBUG] Inside while(running) loop" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    // std::cout << "[DEBUG] Exiting main normally" << std::endl;
    running = false;
    audio.join();
    waveOutReset(hWaveOut);
    for (int i = 0; i < numBuffers; ++i) {
        waveOutUnprepareHeader(hWaveOut, &waveHeaders[i], sizeof(WAVEHDR));
    }
    waveOutClose(hWaveOut);
    if (hMidiIn) {
        midiInStop(hMidiIn);
        midiInClose(hMidiIn);
    }
    {
        std::lock_guard<std::mutex> lock(synthMutex);
        if (synth) {
            delete synth;
            synth = nullptr;
        }
    }
    return 0;
}

#endif