#pragma once
#include "main_common.h"
PlatformHooks get_mac_hooks();
void mac_prefill_audio_buffers(bool useSynth, PlatformHooks& hooks);
