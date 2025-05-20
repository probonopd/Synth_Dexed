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
#include <cmath>
#ifndef HAVE_STD_CLAMP
// Provide clamp for pre-C++17 compilers
template <typename T>
constexpr const T& clamp(const T& v, const T& lo, const T& hi) {
    return (v < lo) ? lo : (hi < v) ? hi : v;
}
#endif
// Unison parameters (defaults) - must be before any function that uses them
int unisonVoices = 1; // 1-4
float unisonSpread = 0.0f; // 0-99
float unisonDetune = 0.0f; // cents
std::vector<Dexed*> unisonSynths;
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
    if (unisonSynths.empty()) return;
    if (uMsg == MIM_DATA) {
        DWORD msg = (DWORD)dwParam1;
        uint8_t status = msg & 0xF0;
        uint8_t channel = msg & 0x0F;
        uint8_t data1 = (msg >> 8) & 0x7F;
        uint8_t data2 = (msg >> 16) & 0x7F;
//        std::cout << "[MIDI DEBUG] status=0x" << std::hex << (int)status
//                  << " channel=" << std::dec << (int)channel
//                  << " data1=" << (int)data1
//                  << " data2=" << (int)data2 << std::endl;
        for (size_t v = 0; v < unisonSynths.size(); ++v) {
            // Pass channel+1 to match Dexed's 1-based channel logic
            bool handled = unisonSynths[v]->midiDataHandler(channel + 1, status, data1, data2);
//            std::cout << "[MIDI DEBUG] midiDataHandler for voice " << v << " returned " << handled << std::endl;
        }
    } else if (uMsg == MIM_LONGDATA) {
        MIDIHDR* midiHdr = (MIDIHDR*)dwParam1;
        if (midiHdr && midiHdr->dwBytesRecorded > 0) {
            uint8_t* sysex = (uint8_t*)midiHdr->lpData;
            uint16_t len = (uint16_t)midiHdr->dwBytesRecorded;
            std::cout << "[MIDI DEBUG] SysEx received, len=" << len << std::endl;
            for (size_t v = 0; v < unisonSynths.size(); ++v) {
                bool handled = unisonSynths[v]->midiDataHandler(0, sysex, len);
                std::cout << "[MIDI DEBUG] midiDataHandler (SysEx) for voice " << v << " returned " << handled << std::endl;
            }
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
                      << "  --synth              Use synth (default)\n"
                      << "  --unison-voices N    Set number of unison voices (1-4, default: 1)\n"
                      << "  --unison-spread N    Set unison spread (0-99, default: 0)\n"
                      << "  --unison-detune N    Set unison detune (cents, default: 0)\n";
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
        } else if (arg == "--unison-voices" && i + 1 < argc) {
            unisonVoices = clamp(std::atoi(argv[++i]), 1, 4);
        } else if (arg == "--unison-spread" && i + 1 < argc) {
            unisonSpread = clamp((float)std::atof(argv[++i]), 0.0f, 99.0f);
        } else if (arg == "--unison-detune" && i + 1 < argc) {
            unisonDetune = (float)std::atof(argv[++i]);
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
            // Only use FM-PIANO patch for all unison voices
            uint8_t* voices[] = {fmpiano_sysex, fmpiano_sysex, fmpiano_sysex, fmpiano_sysex};
            int numVoices = sizeof(voices)/sizeof(voices[0]);
            for (int v = 0; v < unisonVoices; ++v) {
                Dexed* s = new Dexed(MAX_NOTES, SAMPLE_RATE);
                s->resetControllers();
                s->setVelocityScale(MIDI_VELOCITY_SCALING_OFF);
                s->loadVoiceParameters(voices[v < numVoices ? v : numVoices-1]);
                // Calculate detune in cents for this voice using master tune
                float detuneCents = 0.0f;
                if (unisonVoices == 2) {
                    detuneCents = (v == 0 ? -1.0f : 1.0f) * (unisonDetune / 2.0f);
                } else if (unisonVoices == 3) {
                    detuneCents = (v - 1) * (unisonDetune / 1.0f);
                } else if (unisonVoices == 4) {
                    detuneCents = (v - 1.5f) * (unisonDetune / 1.5f);
                }
                int8_t masterTune = static_cast<int8_t>(detuneCents);
                s->setMasterTune(masterTune);
                int detuneValue = 7;
                if (unisonVoices == 2) {
                    detuneValue = (v == 0) ? 0 : 14;
                } else if (unisonVoices == 3) {
                    detuneValue = v * 7;
                } else if (unisonVoices == 4) {
                    detuneValue = v * 5;
                }
                for (int op = 0; op < 6; ++op) {
                    s->setOPDetune(op, detuneValue);
                }
                s->setGain(2.0f);
                unisonSynths.push_back(s);
            }
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
    std::vector<std::vector<int16_t>> unisonBuffers(unisonVoices, std::vector<int16_t>(BUFFER_FRAMES));
    for (int i = 0; i < numBuffers; ++i) {
        if (useSynth) {
            std::lock_guard<std::mutex> lock(synthMutex);
            std::vector<float> left(BUFFER_FRAMES, 0.0f), right(BUFFER_FRAMES, 0.0f);
            for (int v = 0; v < unisonVoices; ++v) {
                unisonSynths[v]->getSamples(unisonBuffers[v].data(), BUFFER_FRAMES);
                // Calculate pan and detune
                float pan = 0.0f;
                float detune = 0.0f;
                if (unisonVoices == 2) {
                    pan = (v == 0 ? -1.0f : 1.0f) * (unisonSpread / 99.0f);
                    detune = (v == 0 ? -1.0f : 1.0f) * (unisonDetune / 2.0f);
                } else if (unisonVoices == 3) {
                    pan = (v - 1) * (unisonSpread / 99.0f);
                    detune = (v - 1) * (unisonDetune / 2.0f);
                } else if (unisonVoices == 4) {
                    pan = (v - 1.5f) * (unisonSpread / 99.0f);
                    detune = (v - 1.5f) * (unisonDetune / 1.5f);
                }
                float panL = std::cos((pan * 0.5f + 0.5f) * 3.14159265f / 2.0f);
                float panR = std::sin((pan * 0.5f + 0.5f) * 3.14159265f / 2.0f);
                for (int j = 0; j < BUFFER_FRAMES; ++j) {
                    left[j] += unisonBuffers[v][j] * panL;
                    right[j] += unisonBuffers[v][j] * panR;
                }
            }
            for (int j = 0; j < BUFFER_FRAMES; ++j) {
                audioBuffers[i][2*j] = std::clamp((int)left[j], -32768, 32767);
                audioBuffers[i][2*j+1] = std::clamp((int)right[j], -32768, 32767);
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
        std::vector<std::vector<int16_t>> unisonBuffers(unisonVoices, std::vector<int16_t>(BUFFER_FRAMES));
        while (running) {
            WAVEHDR& hdr = waveHeaders[bufferIndex];
            // Wait for buffer to be done (WHDR_INQUEUE cleared)
            while (hdr.dwFlags & WHDR_INQUEUE) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                if (!running) return;
            }
            if (useSynth) {
                std::lock_guard<std::mutex> lock(synthMutex);
                std::vector<float> left(BUFFER_FRAMES, 0.0f), right(BUFFER_FRAMES, 0.0f);
                for (int v = 0; v < unisonVoices; ++v) {
                    unisonSynths[v]->getSamples(unisonBuffers[v].data(), BUFFER_FRAMES);
                    float pan = 0.0f;
                    float detune = 0.0f;
                    if (unisonVoices == 2) {
                        pan = (v == 0 ? -1.0f : 1.0f) * (unisonSpread / 99.0f);
                        detune = (v == 0 ? -1.0f : 1.0f) * (unisonDetune / 2.0f);
                    } else if (unisonVoices == 3) {
                        pan = (v - 1) * (unisonSpread / 99.0f);
                        detune = (v - 1) * (unisonDetune / 2.0f);
                    } else if (unisonVoices == 4) {
                        pan = (v - 1.5f) * (unisonSpread / 99.0f);
                        detune = (v - 1.5f) * (unisonDetune / 1.5f);
                    }
                    float panL = std::cos((pan * 0.5f + 0.5f) * 3.14159265f / 2.0f);
                    float panR = std::sin((pan * 0.5f + 0.5f) * 3.14159265f / 2.0f);
                    for (int i = 0; i < BUFFER_FRAMES; ++i) {
                        left[i] += unisonBuffers[v][i] * panL;
                        right[i] += unisonBuffers[v][i] * panR;
                    }
                }
                for (int i = 0; i < BUFFER_FRAMES; ++i) {
                    audioBuffers[bufferIndex][2*i] = std::clamp((int)left[i], -32768, 32767);
                    audioBuffers[bufferIndex][2*i+1] = std::clamp((int)right[i], -32768, 32767);
                }
            } else {
                static double phase = 0.0;
                double freq = 440.0;
                double phaseInc = 2.0 * 3.141592653589793 * freq / 48000.0;
                for (int j = 0; j < BUFFER_FRAMES; ++j) {
                    int16_t sample = (int16_t)(std::sin(phase) * 16000.0);
                    phase += phaseInc;
                    if (phase > 2.0 * 3.141592653589793) phase -= 2.0 * 3.141592653589793;
                    audioBuffers[bufferIndex][2*j] = sample;
                    audioBuffers[bufferIndex][2*j+1] = sample;
                    monoBuffer[j] = sample;
                }
            }
            waveOutWrite(hWaveOut, &hdr, sizeof(WAVEHDR));
            bufferIndex = (bufferIndex + 1) % numBuffers;
        }
    });
    std::cout << "[INFO] Synth_Dexed running. Press Ctrl+C to quit.\n";
    // Main thread: just wait for audio thread to finish
    audio.join();
    // std::cout << "[DEBUG] Audio thread joined" << std::endl;
    if (useSynth) {
        std::lock_guard<std::mutex> lock(synthMutex);
        // std::cout << "[DEBUG] Deleting synth instances" << std::endl;
        for (size_t v = 0; v < unisonSynths.size(); ++v) {
            delete unisonSynths[v];
        }
        unisonSynths.clear();
    }
    // std::cout << "[DEBUG] Exiting main()" << std::endl;
    return 0;
}

#endif