#ifndef ARDUINO

// Linux-specific main implementation using ALSA for audio and MIDI

#include "main_common.h"
#include <alsa/asoundlib.h>
#include <alsa/seq.h>
#include <poll.h>
#include <map>
#include <iostream>
#include <pthread.h>
#include <sched.h>
#include <thread>

#ifndef SND_SEQ_EVENT_PROGRAMCHANGE
#define SND_SEQ_EVENT_PROGRAMCHANGE 192
#endif

extern double global_sine_phase;
extern int num_modules;
extern int unisonVoices;
extern std::vector<Dexed*> allSynths;

static snd_pcm_t* pcm_handle = nullptr;
static snd_seq_t* seq_handle = nullptr;
static int midi_in_port = -1;

bool linux_open_audio(int audioDev) {
    std::cout << "[DEBUG] linux_open_audio called" << std::endl;
    void **hints;
    std::vector<std::string> deviceNames, deviceDescs;
    if (snd_device_name_hint(-1, "pcm", &hints) >= 0) {
        void **n = hints;
        std::cout << "Available audio output devices:" << std::endl;
        int idx = 0;
        while (*n != nullptr) {
            char *name = snd_device_name_get_hint(*n, "NAME");
            char *desc = snd_device_name_get_hint(*n, "DESC");
            if (name && std::string(name) != "null" && std::string(name).find("_voice") == std::string::npos) {
                deviceNames.push_back(name);
                deviceDescs.push_back(desc ? desc : name);
                std::cout << "  [" << idx << "] " << (desc ? desc : name) << " (" << name << ")" << std::endl;
                ++idx;
            }
            if (name) free(name);
            if (desc) free(desc);
            ++n;
        }
        snd_device_name_free_hint(hints);
    }
    if (deviceNames.empty()) deviceNames.push_back("default");
    if (audioDev < 0 || audioDev >= (int)deviceNames.size()) audioDev = 0;
    int err = snd_pcm_open(&pcm_handle, deviceNames[audioDev].c_str(), SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        std::cerr << "[ERROR] Failed to open ALSA PCM device: " << snd_strerror(err) << std::endl;
        return false;
    }
    snd_pcm_set_params(pcm_handle, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 2, SAMPLE_RATE, 1, 10000);
    return true;
}

void linux_submit_audio_buffer(int bufferIndex) {
    if (!pcm_handle) return;
    if (bufferIndex < 0 || bufferIndex >= (int)audioBuffers.size()) return;
    snd_pcm_writei(pcm_handle, audioBuffers[bufferIndex].data(), BUFFER_FRAMES);
}

void linux_close_audio() {
    if (DEBUG_ENABLED) std::cout << "[DEBUG] linux_close_audio called. pcm_handle=" << pcm_handle << std::endl;
    if (pcm_handle) {
        snd_pcm_drain(pcm_handle);
        snd_pcm_close(pcm_handle);
        pcm_handle = nullptr;
        if (DEBUG_ENABLED) std::cout << "[DEBUG] ALSA PCM handle closed." << std::endl;
    } else {
        if (DEBUG_ENABLED) std::cout << "[DEBUG] ALSA PCM handle already null." << std::endl;
    }
}

bool linux_open_midi(int midiDev) {
    if (snd_seq_open(&seq_handle, "default", SND_SEQ_OPEN_INPUT, 0) < 0) {
        std::cerr << "[ERROR] Failed to open ALSA sequencer." << std::endl;
        return false;
    }
    snd_seq_set_client_name(seq_handle, "Synth_Dexed");
    midi_in_port = snd_seq_create_simple_port(seq_handle, "Input",
        SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_APPLICATION);
    if (midi_in_port < 0) {
        std::cerr << "[ERROR] Failed to create ALSA MIDI input port." << std::endl;
        return false;
    }
    std::cout << "Available MIDI input devices: (connect with aconnect or your ALSA tool)" << std::endl;
    // Optionally connect to a MIDI source here
    return true;
}

void linux_close_midi() {
    if (DEBUG_ENABLED) std::cout << "[DEBUG] linux_close_midi called. seq_handle=" << seq_handle << std::endl;
    if (seq_handle) {
        snd_seq_close(seq_handle);
        seq_handle = nullptr;
        if (DEBUG_ENABLED) std::cout << "[DEBUG] ALSA sequencer handle closed." << std::endl;
    } else {
        if (DEBUG_ENABLED) std::cout << "[DEBUG] ALSA sequencer handle already null." << std::endl;
    }
}

// Pre-fill all audio buffers before starting the audio thread
void linux_prefill_audio_buffers(bool useSynth, PlatformHooks& hooks) {
    for (int i = 0; i < numBuffers; ++i) {
        fill_audio_buffers(useSynth);
        hooks.submit_audio_buffer(i);
    }
}

// High-priority audio thread loop for Linux
void linux_audio_thread_loop(std::atomic<bool>& running, bool useSynth, PlatformHooks& hooks) {
    // Set audio thread to high priority (SCHED_FIFO, priority 20)
    struct sched_param sch_params;
    sch_params.sched_priority = 20;
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &sch_params);
    int bufferIndex = 0;
    int underrunCount = 0;
    while (running) {
        fill_audio_buffers(useSynth);
        int err = snd_pcm_writei(pcm_handle, audioBuffers[bufferIndex].data(), BUFFER_FRAMES);
        if (err < 0) {
            std::cerr << "[AUDIO][LINUX] snd_pcm_writei error: " << snd_strerror(err) << " on buffer " << bufferIndex << std::endl;
            underrunCount++;
            snd_pcm_prepare(pcm_handle);
        }
        if (DEBUG_ENABLED) std::cout << "[AUDIO][LINUX] Submitted buffer " << bufferIndex << std::endl;
        bufferIndex = (bufferIndex + 1) % numBuffers;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (DEBUG_ENABLED) std::cout << "[AUDIO][LINUX] Exiting audio thread. Underruns: " << underrunCount << std::endl;
}

// Add this function to poll and forward all ALSA MIDI messages to Dexed
void linux_midi_poll_loop() {
    struct pollfd *pfds;
    int npfds = snd_seq_poll_descriptors_count(seq_handle, POLLIN);
    pfds = (struct pollfd*)alloca(npfds * sizeof(struct pollfd));
    snd_seq_poll_descriptors(seq_handle, pfds, npfds, POLLIN);
    while (running) {
        if (poll(pfds, npfds, 100) > 0) {
            snd_seq_event_t *ev = nullptr;
            while (snd_seq_event_input(seq_handle, &ev) > 0) {
                if (!ev) continue;
                if (ev->type == SND_SEQ_EVENT_SYSEX) {
                    uint8_t* data = (uint8_t*)ev->data.ext.ptr;
                    uint16_t len = ev->data.ext.len;
                    for (int module_idx = 0; module_idx < num_modules; ++module_idx) {
                        for (int voice_idx = 0; voice_idx < unisonVoices; ++voice_idx) {
                            int synth_idx = module_idx * unisonVoices + voice_idx;
                            if (synth_idx < allSynths.size() && allSynths[synth_idx]) {
                                allSynths[synth_idx]->midiDataHandler(module_idx + 1, data, len);
                            }
                        }
                    }
                } else if (ev->type == SND_SEQ_EVENT_NOTEON || ev->type == SND_SEQ_EVENT_NOTEOFF ||
                           ev->type == SND_SEQ_EVENT_CONTROLLER || ev->type == SND_SEQ_EVENT_PITCHBEND ||
                           ev->type == SND_SEQ_EVENT_CHANPRESS || ev->type == SND_SEQ_EVENT_KEYPRESS ||
                           ev->type == SND_SEQ_EVENT_PROGRAMCHANGE) {
                    uint8_t midi_bytes[3];
                    midi_bytes[0] = ev->data.control.param;
                    midi_bytes[1] = ev->data.control.value & 0x7F;
                    midi_bytes[2] = (ev->data.control.value >> 7) & 0x7F;
                    int channel_one_based = ev->data.control.channel + 1;
                    int msg_type_nibble = midi_bytes[0] & 0xF0;
                    int msg_channel = midi_bytes[0] & 0x0F;
                    if (channel_one_based == 16) { // OMNI
                        for (int module_idx = 0; module_idx < num_modules; ++module_idx) {
                            uint8_t modified_msg[3] = { (uint8_t)(msg_type_nibble | module_idx), midi_bytes[1], midi_bytes[2] };
                            for (int voice_idx = 0; voice_idx < unisonVoices; ++voice_idx) {
                                int synth_idx = module_idx * unisonVoices + voice_idx;
                                if (synth_idx < allSynths.size() && allSynths[synth_idx]) {
                                    allSynths[synth_idx]->midiDataHandler(module_idx + 1, modified_msg, 3);
                                }
                            }
                        }
                    } else if (channel_one_based >= 1 && channel_one_based <= num_modules) {
                        int module_idx = channel_one_based - 1;
                        for (int voice_idx = 0; voice_idx < unisonVoices; ++voice_idx) {
                            int synth_idx = module_idx * unisonVoices + voice_idx;
                            if (synth_idx < allSynths.size() && allSynths[synth_idx]) {
                                allSynths[synth_idx]->midiDataHandler(channel_one_based, midi_bytes, 3);
                            }
                        }
                    }
                }
                snd_seq_free_event(ev);
            }
        }
    }
}

PlatformHooks get_linux_hooks() {
    PlatformHooks hooks = {};
    hooks.open_audio = linux_open_audio;
    hooks.close_audio = linux_close_audio;
    hooks.submit_audio_buffer = linux_submit_audio_buffer;
    hooks.open_midi = linux_open_midi;
    hooks.close_midi = linux_close_midi;
    return hooks;
}

#endif // ARDUINO