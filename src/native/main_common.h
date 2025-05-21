#ifndef ARDUINO

#pragma once
#include <vector>
#include <atomic>
#include <mutex>
#include "dexed.h"

extern int unisonVoices;
extern float unisonSpread;
extern float unisonDetune;
extern std::vector<Dexed*> unisonSynths;
extern unsigned int SAMPLE_RATE;
extern unsigned int BUFFER_FRAMES;
extern int numBuffers;
extern std::atomic<bool> running;
extern Dexed* synth;
extern std::mutex synthMutex;
extern std::vector<std::vector<short>> audioBuffers;
extern uint8_t fmpiano_sysex[156];
extern bool DEBUG_ENABLED;

void signal_handler(int signal);
void setup_unison_synths();
void cleanup_unison_synths();
void parse_common_args(int argc, char* argv[], int& audioDev, int& midiDev, bool& useSynth);
void fill_audio_buffers(bool useSynth);
void fill_audio_buffer(int bufferIndex, bool useSynth);
void handle_midi_channel_print(uint8_t channel);

struct PlatformHooks {
    bool (*open_audio)(int audioDev);
    void (*close_audio)();
    void (*submit_audio_buffer)(int bufferIndex);
    bool (*open_midi)(int midiDev);
    void (*close_midi)();
};

int main_common_entry(int argc, char* argv[], PlatformHooks hooks);

#endif