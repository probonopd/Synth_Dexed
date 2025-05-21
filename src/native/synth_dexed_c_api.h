#pragma once
#include <cstdint>
#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle for the synth instance
typedef void* synth_handle;

#if defined(_WIN32)
  #define SYNTHDEXED_API __declspec(dllexport)
#else
  #define SYNTHDEXED_API
#endif

SYNTHDEXED_API synth_handle synth_create(uint32_t sample_rate, uint8_t max_notes);
SYNTHDEXED_API void synth_destroy(synth_handle handle);
SYNTHDEXED_API void synth_send_midi(synth_handle handle, const uint8_t* data, uint32_t length);
SYNTHDEXED_API void synth_start_audio(synth_handle handle);
SYNTHDEXED_API void synth_stop_audio(synth_handle handle);
SYNTHDEXED_API void synth_load_init_voice(synth_handle handle);

#ifdef __cplusplus
}
#endif
