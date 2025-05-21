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

// Windows-specific audio and MIDI hooks
HWAVEOUT hWaveOut = nullptr;
std::vector<WAVEHDR> waveHeaders;

bool win_open_audio(int audioDev) {
    // Always print device list and info
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
        if (DEBUG_ENABLED) {
            std::cout << "[DEBUG] Preparing buffer " << i << ", audioBuffers[i].size() = " << (i < (int)audioBuffers.size() ? audioBuffers[i].size() : -1) << std::endl;
        }
        waveHeaders[i] = {};
        waveHeaders[i].lpData = reinterpret_cast<LPSTR>(audioBuffers[i].data());
        waveHeaders[i].dwBufferLength = BUFFER_FRAMES * 2 * sizeof(short);
        waveOutPrepareHeader(hWaveOut, &waveHeaders[i], sizeof(WAVEHDR));
    }
    if (DEBUG_ENABLED) {
        std::cout << "[DEBUG] After buffer preparation loop" << std::endl;
        std::cout << "[DEBUG] win_open_audio completed successfully" << std::endl;
    }
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
void CALLBACK midiInProc(HMIDIIN, UINT uMsg, DWORD_PTR, DWORD_PTR dwParam1, DWORD_PTR, DWORD_PTR) {
    if (DEBUG_ENABLED)
        std::cout << "[MIDI DEBUG] Entered midiInProc, uMsg=" << uMsg << std::endl;
    std::lock_guard<std::mutex> lock(synthMutex);
    if (unisonSynths.empty()) return;
    if (uMsg == MIM_DATA) {
        DWORD msg = (DWORD)dwParam1;
        uint8_t status = msg & 0xF0;
        uint8_t channel = msg & 0x0F;
        uint8_t data1 = (msg >> 8) & 0x7F;
        uint8_t data2 = (msg >> 16) & 0x7F;
        if (DEBUG_ENABLED)
            std::cout << "[MIDI DEBUG] Short msg: status=0x" << std::hex << (int)status << ", channel=" << std::dec << (int)(channel+1) << ", data1=" << (int)data1 << ", data2=" << (int)data2 << std::endl;
        for (size_t v = 0; v < unisonSynths.size(); ++v) {
            if (DEBUG_ENABLED)
                std::cout << "[MIDI DEBUG] Passing to Dexed instance " << v << " at " << unisonSynths[v] << std::endl;
            unisonSynths[v]->midiDataHandler(channel + 1, status, data1, data2);
        }
    } else if (uMsg == MIM_LONGDATA) {
        MIDIHDR* midiHdr = (MIDIHDR*)dwParam1;
        if (midiHdr && midiHdr->dwBytesRecorded > 0) {
            uint8_t* sysex = (uint8_t*)midiHdr->lpData;
            uint16_t len = (uint16_t)midiHdr->dwBytesRecorded;
            if (DEBUG_ENABLED) {
                std::cout << "[MIDI DEBUG] Sysex msg: len=" << len << ", first bytes: ";
                for (int i = 0; i < std::min(8, (int)len); ++i) std::cout << std::hex << (int)sysex[i] << " ";
                std::cout << std::dec << std::endl;
            }
            for (size_t v = 0; v < unisonSynths.size(); ++v) {
                if (DEBUG_ENABLED)
                    std::cout << "[MIDI DEBUG] Passing sysex to Dexed instance " << v << " at " << unisonSynths[v] << std::endl;
                unisonSynths[v]->midiDataHandler(0, sysex, len);
            }
        }
    }
}

bool win_open_midi(int midiDev) {
    // Check if we're being called from Python - environment variable will be set by Python script
    if (std::getenv("DEXED_PYTHON_HOST") != nullptr) {
        std::cout << "[INFO] Running under Python - MIDI will be handled by Python script" << std::endl;
        return true; // Return success but don't actually open MIDI port
    }

    // Only reach here if not running under Python
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
    
    std::cout << "[DEBUG] Starting win_audio_thread_loop with useSynth=" << (useSynth ? "true" : "false") << std::endl;
    
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
        std::cout << "[DEBUG] Setting sine wave test mode for prefill" << std::endl;
    } else {
        // Ensure the environment variable is cleared if synth mode is enabled
        _putenv_s("DEXED_TEST_SINE", "");
    }
    
    std::cout << "[DEBUG] win_prefill_audio_buffers: Filling " << numBuffers << " buffers with useSynth=" << (useSynth ? "true" : "false") << std::endl;
    
    for (int i = 0; i < numBuffers; ++i) {
        // Use our enhanced fill_audio_buffer function with detailed diagnostics
        fill_audio_buffer(i, useSynth);
        
        // Verify buffer has data before submission
        int zeroCount = 0;
        for (int j = 0; j < 32 && j*2 < audioBuffers[i].size(); j++) {
            if (audioBuffers[i][j*2] == 0) zeroCount++;
        }
        
        std::cout << "[DEBUG] Buffer " << i << " has " << zeroCount << "/32 zero samples before submission" << std::endl;
        
        // Submit buffer to sound card
        waveOutWrite(hWaveOut, &waveHeaders[i], sizeof(WAVEHDR));
        
        std::cout << "[DEBUG] Buffer " << i << " submitted to sound card" << std::endl;
    }
    
    std::cout << "[DEBUG] All buffers initialized and submitted" << std::endl;
}

// Remove the main() function from this file to avoid duplicate symbol errors.

#endif // ARDUINO