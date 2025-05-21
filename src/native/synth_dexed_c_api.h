#pragma once
#include <cstdint>
#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle for the synth instance
typedef void* synth_handle;

__declspec(dllexport) synth_handle synth_create(uint32_t sample_rate, uint8_t max_notes);
__declspec(dllexport) void synth_destroy(synth_handle handle);
__declspec(dllexport) void synth_send_midi(synth_handle handle, const uint8_t* data, uint32_t length);
__declspec(dllexport) void synth_start_audio(synth_handle handle);
__declspec(dllexport) void synth_stop_audio(synth_handle handle);
__declspec(dllexport) void synth_load_init_voice(synth_handle handle);

#ifdef __cplusplus
}
#endif
