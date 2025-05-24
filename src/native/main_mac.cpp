#ifndef ARDUINO

#include "main_common.h"
#include <iostream> // Added iostream for std::cout and std::endl
#include <CoreAudio/CoreAudio.h>
#include <AudioUnit/AudioUnit.h>
#include <CoreMIDI/CoreMIDI.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <thread>

extern double global_sine_phase;
extern int num_modules;
extern int unisonVoices;
extern std::vector<StereoDexed*> allSynths;

AudioUnit audioUnit;
MIDIClientRef midiClient;
MIDIEndpointRef midiIn;

// Forward declaration for MIDI callback
void mac_midi_input_callback(const MIDIPacketList* pktlist, void* readProcRefCon, void* srcConnRefCon);

bool mac_open_audio(int audioDev) {
    std::cout << "[DEBUG] mac_open_audio called" << std::endl;
    // List and select audio device (optional, can be expanded)
    std::cout << "[DEBUG] Using audio device: " << audioDev << std::endl;
    AudioComponentDescription desc = { kAudioUnitType_Output, kAudioUnitSubType_DefaultOutput, kAudioUnitManufacturer_Apple, 0, 0 };
    AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
    AudioComponentInstanceNew(comp, &audioUnit);
    AudioStreamBasicDescription asbd = {};
    asbd.mSampleRate = SAMPLE_RATE;
    asbd.mFormatID = kAudioFormatLinearPCM;
    asbd.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    asbd.mFramesPerPacket = 1;
    asbd.mChannelsPerFrame = 2;
    asbd.mBitsPerChannel = 16;
    asbd.mBytesPerFrame = 4;
    asbd.mBytesPerPacket = 4;
    AudioUnitSetProperty(audioUnit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &asbd, sizeof(asbd));
    // You would set a render callback here
    AudioUnitInitialize(audioUnit);
    AudioOutputUnitStart(audioUnit);
    return true;
}

void mac_submit_audio_buffer(int bufferIndex) {
    // CoreAudio uses callbacks, so this may be a no-op or trigger a buffer update
}

void mac_close_audio() {
    if (DEBUG_ENABLED) std::cout << "[DEBUG] mac_close_audio called" << std::endl;
    AudioOutputUnitStop(audioUnit);
    AudioUnitUninitialize(audioUnit);
    AudioComponentInstanceDispose(audioUnit);
}

bool mac_open_midi(int midiDev) {
    MIDIClientCreate(CFSTR("Synth_Dexed"), nullptr, nullptr, &midiClient);
    midiIn = MIDIGetSource(midiDev);
    MIDIPortRef inPort;
    // Register the callback for incoming MIDI
    MIDIInputPortCreate(midiClient, CFSTR("Input"), mac_midi_input_callback, nullptr, &inPort);
    MIDIPortConnectSource(inPort, midiIn, nullptr);
    std::cout << "Available MIDI input devices: (see Audio MIDI Setup for details)" << std::endl;
    return true;
}

void mac_close_midi() {
    if (DEBUG_ENABLED) std::cout << "[DEBUG] mac_close_midi called" << std::endl;
    MIDIClientDispose(midiClient);
}

// Add this CoreMIDI callback to forward all MIDI messages to Dexed
void mac_midi_input_callback(const MIDIPacketList* pktlist, void* readProcRefCon, void* srcConnRefCon) {
    for (unsigned int i = 0; i < pktlist->numPackets; ++i) {
        const MIDIPacket* pkt = &pktlist->packet[i];
        const uint8_t* data = pkt->data;
        uint16_t len = pkt->length;
        if (len == 0) continue;
        uint8_t status = data[0];
        if (status >= 0xF0) { // System message: broadcast to all modules
            for (int module_idx = 0; module_idx < num_modules; ++module_idx) {
                for (int voice_idx = 0; voice_idx < unisonVoices; ++voice_idx) {
                    int synth_idx = module_idx * unisonVoices + voice_idx;
                    if (synth_idx < allSynths.size() && allSynths[synth_idx]) {
                        allSynths[synth_idx]->midiDataHandler(module_idx + 1, const_cast<uint8_t*>(data), len);
                    }
                }
            }
        } else if ((status & 0xF0) >= 0x80 && (status & 0xF0) <= 0xE0) { // Channel message
            int msg_type_nibble = status & 0xF0;
            int msg_channel_zero_based = status & 0x0F;
            int channel_one_based = msg_channel_zero_based + 1;
            if (channel_one_based == 16) { // OMNI
                for (int module_idx = 0; module_idx < num_modules; ++module_idx) {
                    uint8_t modified_msg[3] = {
                        static_cast<uint8_t>(msg_type_nibble | module_idx),
                        static_cast<uint8_t>(len > 1 ? data[1] : 0),
                        static_cast<uint8_t>(len > 2 ? data[2] : 0)
                    };
                    for (int voice_idx = 0; voice_idx < unisonVoices; ++voice_idx) {
                        int synth_idx = module_idx * unisonVoices + voice_idx;
                        if (synth_idx < allSynths.size() && allSynths[synth_idx]) {
                            allSynths[synth_idx]->midiDataHandler(module_idx + 1, modified_msg, len);
                        }
                    }
                }
            } else if (channel_one_based >= 1 && channel_one_based <= num_modules) {
                int module_idx = channel_one_based - 1;
                for (int voice_idx = 0; voice_idx < unisonVoices; ++voice_idx) {
                    int synth_idx = module_idx * unisonVoices + voice_idx;
                    if (synth_idx < allSynths.size() && allSynths[synth_idx]) {
                        allSynths[synth_idx]->midiDataHandler(channel_one_based, const_cast<uint8_t*>(data), len);
                    }
                }
            }
        }
    }
}

// High-priority audio thread loop for macOS
void mac_audio_thread_loop(std::atomic<bool>& running, bool useSynth, PlatformHooks& hooks) {
    setpriority(PRIO_PROCESS, 0, -20);
    int bufferIndex = 0;
    int underrunCount = 0;
    while (running) {
        fill_audio_buffers(useSynth);
        if (DEBUG_ENABLED) std::cout << "[AUDIO][MAC] Submitted buffer " << bufferIndex << std::endl;
        bufferIndex = (bufferIndex + 1) % numBuffers;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (DEBUG_ENABLED) std::cout << "[AUDIO][MAC] Exiting audio thread. Underruns: " << underrunCount << std::endl;
}

// Pre-fill all audio buffers before starting the audio thread
void mac_prefill_audio_buffers(bool useSynth, PlatformHooks& hooks) {
    for (int i = 0; i < numBuffers; ++i) {
        fill_audio_buffers(useSynth);
        hooks.submit_audio_buffer(i);
    }
}

PlatformHooks get_mac_hooks() {
    PlatformHooks hooks = {};
    hooks.open_audio = mac_open_audio;
    hooks.close_audio = mac_close_audio;
    hooks.submit_audio_buffer = mac_submit_audio_buffer;
    hooks.open_midi = mac_open_midi;
    hooks.close_midi = mac_close_midi;
    return hooks;
}

#endif