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

AudioUnit audioUnit;
MIDIClientRef midiClient;
MIDIEndpointRef midiIn;

bool mac_open_audio(int audioDev) {
    // List and select audio device (optional, can be expanded)
    std::cout << "[INFO] Using audio device: " << audioDev << std::endl;
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
    AudioOutputUnitStop(audioUnit);
    AudioUnitUninitialize(audioUnit);
    AudioComponentInstanceDispose(audioUnit);
}

bool mac_open_midi(int midiDev) {
    MIDIClientCreate(CFSTR("Synth_Dexed"), nullptr, nullptr, &midiClient);
    midiIn = MIDIGetSource(midiDev);
    MIDIPortRef inPort;
    MIDIInputPortCreate(midiClient, CFSTR("Input"), nullptr, nullptr, &inPort);
    MIDIPortConnectSource(inPort, midiIn, nullptr);
    return true;
}

void mac_close_midi() {
    MIDIClientDispose(midiClient);
}

// Add this CoreMIDI callback to forward all MIDI messages to Dexed
void mac_midi_input_callback(const MIDIPacketList* pktlist, void* readProcRefCon, void* srcConnRefCon) {
    for (unsigned int i = 0; i < pktlist->numPackets; ++i) {
        const MIDIPacket* pkt = &pktlist->packet[i];
        const uint8_t* data = pkt->data;
        uint16_t len = pkt->length;
        for (size_t v = 0; v < unisonSynths.size(); ++v) {
            unisonSynths[v]->midiDataHandler(0, data, len);
        }
    }
}

// High-priority audio thread loop for macOS
void mac_audio_thread_loop(std::atomic<bool>& running, bool useSynth, PlatformHooks& hooks) {
    // Set audio thread to high priority (nice value -20)
    setpriority(PRIO_PROCESS, 0, -20);
    int bufferIndex = 0;
    int underrunCount = 0;
    while (running) {
        fill_audio_buffers(useSynth);
        // No direct buffer submission, but log for debug
        std::cout << "[AUDIO][MAC] Submitted buffer " << bufferIndex << std::endl;
        bufferIndex = (bufferIndex + 1) % numBuffers;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::cout << "[AUDIO][MAC] Exiting audio thread. Underruns: " << underrunCount << std::endl;
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