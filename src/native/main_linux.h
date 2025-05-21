#ifndef ARDUINO

#pragma once
#include "main_common.h"
PlatformHooks get_linux_hooks();
void linux_prefill_audio_buffers(bool useSynth, PlatformHooks& hooks);

#endif