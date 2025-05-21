#include "synth_dexed_c_api.h"
#include "../dexed.h"
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <vector>
#include <iostream>

// Platform-specific includes for audio output
#if defined(_WIN32)
#include <windows.h>
#include <mmsystem.h>
#include "main_win.h"
#endif
#include "main_common.h"

struct SynthWrapper {
    Dexed* synth = nullptr;
    std::atomic<bool> running{false};
    std::thread audio_thread;
    std::mutex mutex;
    uint32_t sample_rate = 48000;
    uint8_t max_notes = 16;
};

// Audio thread loop, matching main_win.cpp
#if defined(_WIN32)
static void synth_audio_thread(SynthWrapper* wrapper) {
    HANDLE hThread = GetCurrentThread();
    SetThreadPriority(hThread, THREAD_PRIORITY_HIGHEST);
    int bufferIndex = 0;
    while (wrapper->running) {
        WAVEHDR& hdr = waveHeaders[bufferIndex];
        // Wait for buffer to be done (WHDR_INQUEUE cleared)
        while (hdr.dwFlags & WHDR_INQUEUE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if (!wrapper->running) return;
        }
        fill_audio_buffers(true); // true = useSynth
        waveOutWrite(hWaveOut, &hdr, sizeof(WAVEHDR));
        bufferIndex = (bufferIndex + 1) % numBuffers;
    }
}
#endif

extern uint8_t fmpiano_sysex[156];

extern "C" {

synth_handle synth_create(uint32_t sample_rate, uint8_t max_notes) {
    auto* wrapper = new SynthWrapper();
    wrapper->sample_rate = sample_rate;
    wrapper->max_notes = max_notes;
    wrapper->synth = new Dexed(max_notes, sample_rate);
    // Set up global synth pointer for fill_audio_buffers
    synth = wrapper->synth;
    SAMPLE_RATE = sample_rate;
    BUFFER_FRAMES = 512; // Set a reasonable default if not set elsewhere
    numBuffers = 4;      // Set a reasonable default if not set elsewhere
    // Allocate audio buffers
    audioBuffers.resize(numBuffers);
    for (auto& buf : audioBuffers) {
        buf.resize(BUFFER_FRAMES * 2); // stereo
    }
    std::cout << "[DEBUG] synth_create: after audioBuffers resize, audioBuffers.size()=" << audioBuffers.size() << std::endl;
    std::cout << "[DEBUG] synth_create: synth=" << synth << ", SAMPLE_RATE=" << SAMPLE_RATE << ", BUFFER_FRAMES=" << BUFFER_FRAMES << ", numBuffers=" << numBuffers << std::endl;
    setup_unison_synths();
    std::cout << "[DEBUG] synth_create: after setup_unison_synths, audioBuffers.size()=" << audioBuffers.size() << std::endl;
    return reinterpret_cast<synth_handle>(wrapper);
}

void synth_destroy(synth_handle handle) {
    if (!handle) return;
    auto* wrapper = reinterpret_cast<SynthWrapper*>(handle);
    synth_stop_audio(handle);
    cleanup_unison_synths();
    delete wrapper->synth;
    delete wrapper;
}

void synth_send_midi(synth_handle handle, const uint8_t* data, uint32_t length) {
    if (!handle || !data || length == 0) return;
    auto* wrapper = reinterpret_cast<SynthWrapper*>(handle);
    std::lock_guard<std::mutex> lock(wrapper->mutex);
    wrapper->synth->midiDataHandler(0, const_cast<uint8_t*>(data), static_cast<int16_t>(length));
}

void synth_start_audio(synth_handle handle) {
#if defined(_WIN32)
    if (!handle) return;
    auto* wrapper = reinterpret_cast<SynthWrapper*>(handle);
    if (wrapper->running) return;
    PlatformHooks hooks = get_win_hooks();
    hooks.open_audio(0); // Use default device
    win_prefill_audio_buffers(true); // Pre-fill all audio buffers as in main_win.cpp
    wrapper->running = true;
    wrapper->audio_thread = std::thread([wrapper, hooks]() mutable {
        win_audio_thread_loop(wrapper->running, true, hooks);
    });
#else
    (void)handle;
    std::cerr << "[ERROR] synth_start_audio is only implemented on Windows." << std::endl;
#endif
}

void synth_stop_audio(synth_handle handle) {
#if defined(_WIN32)
    if (!handle) return;
    auto* wrapper = reinterpret_cast<SynthWrapper*>(handle);
    if (!wrapper->running) return;
    wrapper->running = false;
    if (wrapper->audio_thread.joinable()) {
        wrapper->audio_thread.join();
    }
    PlatformHooks hooks = get_win_hooks();
    hooks.close_audio();
#else
    (void)handle;
    std::cerr << "[ERROR] synth_stop_audio is only implemented on Windows." << std::endl;
#endif
}

void synth_load_init_voice(synth_handle handle) {
    if (!handle) return;
    auto* wrapper = reinterpret_cast<SynthWrapper*>(handle);
    wrapper->synth->loadInitVoice();
}

#if defined(_WIN32)
SYNTHDEXED_API void synth_load_epiano_patch(synth_handle handle) {
    if (!handle) return;
    auto* wrapper = reinterpret_cast<SynthWrapper*>(handle);
    wrapper->synth->loadVoiceParameters(fmpiano_sysex);
    wrapper->synth->setGain(2.0f);
}
#endif

} // extern "C"
