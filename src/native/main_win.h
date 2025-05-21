#ifndef ARDUINO

#pragma once
#include "main_common.h"
#include <vector>
#include <windows.h>

PlatformHooks get_win_hooks();

// Expose these for use in other translation units
extern HWAVEOUT hWaveOut;
extern std::vector<WAVEHDR> waveHeaders;

// Add declarations for audio thread helpers
void win_prefill_audio_buffers(bool useSynth);
void win_audio_thread_loop(std::atomic<bool>& running, bool useSynth, PlatformHooks& hooks);

#endif