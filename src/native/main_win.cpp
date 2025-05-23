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
#include <processthreadsapi.h>
#ifndef HAVE_STD_CLAMP
// Provide clamp for pre-C++17 compilers
template <typename T>
constexpr const T& clamp(const T& v, const T& lo, const T& hi) {
    return (v < lo) ? lo : (hi < v) ? hi : v;
}
#endif

// --- Shared code moved to main_common.h / main_common.cpp ---
#include "main_common.h"

extern std::vector<std::vector<short>> audioBuffers;
extern unsigned int BUFFER_FRAMES;
extern int numBuffers;
extern std::atomic<bool> running;
extern std::vector<Dexed*> allSynths; // Changed from unisonSynths
extern std::mutex synthMutex;
extern int num_modules;      // Added
extern int unisonVoices;     // Added

// Windows-specific audio and MIDI hooks
HWAVEOUT hWaveOut = NULL;
std::vector<WAVEHDR> waveHeaders;

bool win_open_audio(int audioDev) {
    std::cout << "[DEBUG] win_open_audio called" << std::endl;
    UINT numAudioDevs = waveOutGetNumDevs();
    std::cout << "Available audio output devices:" << std::endl;
    for (UINT i = 0; i < numAudioDevs; ++i) {
        WAVEOUTCAPS caps;
        waveOutGetDevCaps(i, &caps, sizeof(caps));
        std::wcout << L"  [" << i << L"] " << caps.szPname << std::endl;
    }
    std::cout << "[INFO] Using audio device: " << audioDev << std::endl;
    std::cout << "[DEBUG] Before waveOutOpen" << std::endl;
    WAVEFORMATEX wfx = {};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 2;
    wfx.nSamplesPerSec = SAMPLE_RATE;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = wfx.nChannels * wfx.wBitsPerSample / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    wfx.cbSize = 0;
    MMRESULT result = waveOutOpen(&hWaveOut, audioDev, &wfx, 0, 0, CALLBACK_NULL);
    if (result != MMSYSERR_NOERROR) {
        std::cout << "[ERROR] waveOutOpen failed with code: " << result << std::endl;
    } else {
        std::cout << "[DEBUG] After waveOutOpen" << std::endl;
        std::cout << "[DEBUG] audioBuffers.size() = " << audioBuffers.size() << ", numBuffers = " << numBuffers << ", BUFFER_FRAMES = " << BUFFER_FRAMES << std::endl;
        std::cout << "[DEBUG] Before buffer preparation loop" << std::endl;
    }
    waveHeaders.resize(numBuffers);
    for (int i = 0; i < numBuffers; ++i) {
        std::cout << "[DEBUG] Preparing buffer " << i << ", audioBuffers[i].size() = " << (i < (int)audioBuffers.size() ? audioBuffers[i].size() : -1) << std::endl;
        waveHeaders[i] = {};
        waveHeaders[i].lpData = reinterpret_cast<LPSTR>(audioBuffers[i].data());
        waveHeaders[i].dwBufferLength = BUFFER_FRAMES * 2 * sizeof(short);
        waveOutPrepareHeader(hWaveOut, &waveHeaders[i], sizeof(WAVEHDR));
    }
    std::cout << "[DEBUG] After buffer preparation loop" << std::endl;
    std::cout << "[DEBUG] win_open_audio completed successfully" << std::endl;
    return true;
}

void win_submit_audio_buffer(int bufferIndex) {
    waveOutWrite(hWaveOut, &waveHeaders[bufferIndex], sizeof(WAVEHDR));
}

void win_close_audio() {
    if (hWaveOut) {
        waveOutReset(hWaveOut);
        for (auto& hdr : waveHeaders) {
            waveOutUnprepareHeader(hWaveOut, &hdr, sizeof(WAVEHDR));
        }
        waveOutClose(hWaveOut);
        hWaveOut = nullptr;
    }
}

// MIDI input callback
void CALLBACK midiInProc(HMIDIIN hMidiIn, UINT wMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    if (wMsg == MIM_DATA) {
        if (DEBUG_ENABLED) printf("[DEBUG MIDI] midiInProc: wMsg=MIM_DATA, dwParam1=0x%lX, dwParam2=0x%lX\n", dwParam1, dwParam2);

        std::lock_guard<std::mutex> lock(synthMutex);
        if (allSynths.empty() || num_modules == 0) {
            if (DEBUG_ENABLED) printf("[DEBUG MIDI] No synths or modules initialized, ignoring MIDI.\n");
            return;
        }

        uint8_t status_byte = dwParam1 & 0xFF;
        uint8_t data1 = (dwParam1 >> 8) & 0xFF;
        uint8_t data2 = (dwParam1 >> 16) & 0xFF;
        
        if (DEBUG_ENABLED) printf("[DEBUG MIDI] Raw Data: Status=0x%02X, Data1=0x%02X, Data2=0x%02X\n", status_byte, data1, data2);

        uint8_t midi_data[3] = {status_byte, data1, data2};
        int len = 3;

        // Determine message length based on status byte
        if ((status_byte & 0xF0) == 0xC0 || (status_byte & 0xF0) == 0xD0) { // Program Change, Channel Pressure
            len = 2;
        }
        if (DEBUG_ENABLED) printf("[DEBUG MIDI] Determined message length: %d\n", len);

        if (status_byte >= 0xF0) { // System messages (SysEx, System Common, System Real-Time)
            if (DEBUG_ENABLED) printf("[DEBUG MIDI] System Message received.\n");
            // For system messages, it's a bit more complex as they don't have a channel in the status byte
            // and dwParam1 might contain the whole message or part of it for SysEx.
            // This simplistic handling might need to be improved for full SysEx support via native MIDI.
            // For now, broadcast to all modules with their respective channels.
            for (int module_idx = 0; module_idx < num_modules; ++module_idx) {
                int module_channel_one_based = module_idx + 1;
                for (int voice_idx = 0; voice_idx < unisonVoices; ++voice_idx) {
                    int synth_idx = module_idx * unisonVoices + voice_idx;
                    if (synth_idx < allSynths.size() && allSynths[synth_idx]) {
                        // For system messages, pass the original channel (or 0 if not applicable)
                        // and the original midi_data.
                        // Dexed's midiDataHandler might not use the channel for system messages,
                        // but we pass it for consistency.
                        allSynths[synth_idx]->midiDataHandler(0, midi_data, len);
                    }
                }
            }
        } else { // Channel messages
            int msg_midi_channel_zero_based = status_byte & 0x0F;
            int incoming_channel_one_based = msg_midi_channel_zero_based + 1;
            if (DEBUG_ENABLED) printf("[DEBUG MIDI] Channel Message: Extracted incoming_channel_one_based = %d\n", incoming_channel_one_based);

            if (incoming_channel_one_based == 16) { // OMNI Channel (16)
                if (DEBUG_ENABLED) printf("[DEBUG MIDI] OMNI Detected (Channel 16)\n");
                uint8_t original_msg_type = status_byte & 0xF0; // e.g., 0x90 for Note On, 0x80 for Note Off

                for (int module_idx = 0; module_idx < num_modules; ++module_idx) {
                    int module_channel_zero_based = module_idx; // Module 0, 1, 2...
                    uint8_t modified_status_byte = original_msg_type | module_channel_zero_based;
                    
                    uint8_t modified_midi_message[3];
                    modified_midi_message[0] = modified_status_byte;
                    modified_midi_message[1] = data1;
                    modified_midi_message[2] = data2;
                    
                    // The first argument to midiDataHandler is the 1-based channel for the module
                    int module_target_channel_one_based = module_idx + 1;

                    if (DEBUG_ENABLED) printf("[DEBUG MIDI] OMNI: Routing to module %d (channel %d) with Status=0x%02X, D1=0x%02X, D2=0x%02X, Len=%d\n",
                           module_idx, module_target_channel_one_based, modified_status_byte, data1, data2, len);

                    for (int voice_idx = 0; voice_idx < unisonVoices; ++voice_idx) {
                        int synth_idx = module_idx * unisonVoices + voice_idx;
                        if (synth_idx < allSynths.size() && allSynths[synth_idx]) {
                            allSynths[synth_idx]->midiDataHandler(module_target_channel_one_based, modified_midi_message, len);
                        }
                    }
                }
            } else if (incoming_channel_one_based >= 1 && incoming_channel_one_based <= num_modules) {
                if (DEBUG_ENABLED) printf("[DEBUG MIDI] Specific Channel: Routing to module_idx %d (channel %d)\n", incoming_channel_one_based - 1, incoming_channel_one_based);
                int target_module_idx = incoming_channel_one_based - 1;
                for (int voice_idx = 0; voice_idx < unisonVoices; ++voice_idx) {
                    int synth_idx = target_module_idx * unisonVoices + voice_idx;
                    if (synth_idx < allSynths.size() && allSynths[synth_idx]) {
                        allSynths[synth_idx]->midiDataHandler(incoming_channel_one_based, midi_data, len);
                    }
                }
            } else {
                if (DEBUG_ENABLED) printf("[DEBUG MIDI] Channel Message for channel %d is outside the configured num_modules (%d) or not OMNI. Ignoring.\n", incoming_channel_one_based, num_modules);
            }
        }
    }
}

bool win_open_midi(int midiDev) {
    if (std::getenv("DEXED_PYTHON_HOST") != nullptr) {
        std::cout << "[INFO] Running under Python - MIDI will be handled by Python script" << std::endl;
        return true;
    }
    std::cout << "[DEBUG] win_open_midi called" << std::endl;
    UINT numMidiDevs = midiInGetNumDevs();
    std::cout << "Available MIDI input devices:" << std::endl;
    for (UINT i = 0; i < numMidiDevs; ++i) {
        MIDIINCAPS caps;
        midiInGetDevCaps(i, &caps, sizeof(caps));
        std::wcout << L"  [" << i << L"] " << caps.szPname << std::endl;
    }
    std::cout << "[INFO] Using MIDI input device: " << midiDev << std::endl;
    HMIDIIN hMidiIn = nullptr;
    if (numMidiDevs > 0) {
        UINT selectedMidiDev = (midiDev < (int)numMidiDevs) ? midiDev : 0;
        MMRESULT res = midiInOpen(&hMidiIn, selectedMidiDev, (DWORD_PTR)midiInProc, 0, CALLBACK_FUNCTION);
        if (res == MMSYSERR_NOERROR) {
            midiInStart(hMidiIn);
            std::cout << "[INFO] MIDI input started on device " << selectedMidiDev << "." << std::endl;
            return true;
        } else {
            std::cerr << "[ERROR] Failed to open MIDI input device!" << std::endl;
        }
    } else {
        std::cerr << "[WARN] No MIDI input devices found. MIDI will be disabled." << std::endl;
    }
    return false;
}

void win_close_midi() {
    // Implement if needed
}

PlatformHooks get_win_hooks() {
    PlatformHooks hooks = {};
    hooks.open_audio = win_open_audio;
    hooks.close_audio = win_close_audio;
    hooks.submit_audio_buffer = win_submit_audio_buffer;
    hooks.open_midi = win_open_midi;
    hooks.close_midi = win_close_midi;
    return hooks;
}

// Windows-specific audio thread loop
void win_audio_thread_loop(std::atomic<bool>& running, bool useSynth, PlatformHooks& hooks) {
    HANDLE hThread = GetCurrentThread();
    SetThreadPriority(hThread, THREAD_PRIORITY_HIGHEST);
    int bufferIndex = 0;
    
    // Only set the environment variable if --sine was specified
    if (!useSynth) {
        _putenv_s("DEXED_TEST_SINE", "1");
        std::cout << "[DEBUG] Setting sine wave test mode from command line parameter" << std::endl;
    }
    
    if (DEBUG_ENABLED) std::cout << "[DEBUG] Starting win_audio_thread_loop with useSynth=" << (useSynth ? "true" : "false") << std::endl;
    
    while (running) {
        WAVEHDR& hdr = waveHeaders[bufferIndex];
        // Wait for buffer to be done (WHDR_INQUEUE cleared)
        while (hdr.dwFlags & WHDR_INQUEUE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if (!running) return;
        }
        
        // Use the single-buffer fill with enhanced diagnostics
        fill_audio_buffer(bufferIndex, useSynth);
        
        // Submit the filled buffer
        hooks.submit_audio_buffer(bufferIndex);
        
        bufferIndex = (bufferIndex + 1) % numBuffers;
    }
}

// Pre-fill all audio buffers before starting the audio thread
void win_prefill_audio_buffers(bool useSynth) {
    // Only enable sine wave test if synth mode is disabled
    if (!useSynth) {
        _putenv_s("DEXED_TEST_SINE", "1");
        if (DEBUG_ENABLED) std::cout << "[DEBUG] Setting sine wave test mode for prefill" << std::endl;
    } else {
        _putenv_s("DEXED_TEST_SINE", "");
    }
    if (DEBUG_ENABLED) std::cout << "[DEBUG] win_prefill_audio_buffers: Filling " << numBuffers << " buffers with useSynth=" << (useSynth ? "true" : "false") << std::endl;
    for (int i = 0; i < numBuffers; ++i) {
        fill_audio_buffer(i, useSynth);
        int zeroCount = 0;
        for (int j = 0; j < 32 && j*2 < audioBuffers[i].size(); j++) {
            if (audioBuffers[i][j*2] == 0) zeroCount++;
        }
        if (DEBUG_ENABLED) std::cout << "[DEBUG] Buffer " << i << " has " << zeroCount << "/32 zero samples before submission" << std::endl;
        waveOutWrite(hWaveOut, &waveHeaders[i], sizeof(WAVEHDR));
        if (DEBUG_ENABLED) std::cout << "[DEBUG] Buffer " << i << " submitted to sound card" << std::endl;
    }
    if (DEBUG_ENABLED) std::cout << "[DEBUG] All buffers initialized and submitted" << std::endl;
}

// Remove the main() function from this file to avoid duplicate symbol errors.

#endif // ARDUINO