#ifndef ARDUINO

// macOS-specific main implementation using CoreAudio and CoreMIDI

#include <CoreAudio/CoreAudio.h>
#include <AudioUnit/AudioUnit.h>
#include <CoreMIDI/CoreMIDI.h>
#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <csignal>
#include <cstdlib>
#include <cmath>
#include <string>
#include "dexed.h"
// --- Unison parameters ---
int unisonVoices = 1; // 1-4
float unisonSpread = 0.0f; // 0-99
float unisonDetune = 0.0f; // cents
std::vector<Dexed*> unisonSynths;
unsigned int SAMPLE_RATE = 48000;
unsigned int BUFFER_FRAMES = 1024;
constexpr uint8_t MAX_NOTES = 16;
int numBuffers = 4;
std::atomic<bool> running{true};
void signal_handler(int signal) {
    if (signal == SIGINT) {
        running = false;
        std::cout << "\n[INFO] Caught Ctrl+C, exiting..." << std::endl;
    }
}
Dexed* synth = nullptr;
std::mutex synthMutex;
std::vector<std::vector<short>> audioBuffers;
AudioUnit audioUnit = nullptr;
MIDIClientRef midiClient = 0;
MIDIEndpointRef midiIn = 0;
int midiDev = 0;

// Sample voice from ./examples/SimplePlay/SimplePlay.ino
uint8_t fmpiano_sysex[156] = {
    95, 29, 20, 50, 99, 95, 00, 00, 41, 00, 19, 00, 00, 03, 00, 06, 79, 00, 01, 00, 14,
    95, 20, 20, 50, 99, 95, 00, 00, 00, 00, 00, 00, 00, 03, 00, 00, 99, 00, 01, 00, 00,
    95, 29, 20, 50, 99, 95, 00, 00, 00, 00, 00, 00, 00, 03, 00, 06, 89, 00, 01, 00, 07,
    95, 20, 20, 50, 99, 95, 00, 00, 00, 00, 00, 00, 00, 03, 00, 02, 99, 00, 01, 00, 07,
    95, 50, 35, 78, 99, 75, 00, 00, 00, 00, 00, 00, 00, 03, 00, 07, 58, 00, 14, 00, 07,
    96, 25, 25, 67, 99, 75, 00, 00, 00, 00, 00, 00, 00, 03, 00, 02, 99, 00, 01, 00, 10,
    94, 67, 95, 60, 50, 50, 50, 50,
    04, 06, 00,
    34, 33, 00, 00, 00, 04,
    03, 24,
    70, 77, 45, 80, 73, 65, 78, 79, 00, 00
}; // FM-Piano

// MIDI input callback
void midiReadProc(const MIDIPacketList* pktlist, void* readProcRefCon, void* srcConnRefCon) {
    std::lock_guard<std::mutex> lock(synthMutex);
    if (unisonSynths.empty()) return;
    for (unsigned int i = 0; i < pktlist->numPackets; ++i) {
        const MIDIPacket* pkt = &pktlist->packet[i];
        const uint8_t* data = pkt->data;
        uint8_t status = data[0] & 0xF0;
        uint8_t note = data[1];
        uint8_t velocity = (pkt->length > 2) ? data[2] : 0;
        if (status == 0x90 && velocity > 0) {
            for (auto* s : unisonSynths) s->keydown(note, velocity);
        } else if (status == 0x80 || (status == 0x90 && velocity == 0)) {
            for (auto* s : unisonSynths) s->keyup(note);
        } else if (status == 0xB0 && pkt->length > 2) {
            uint8_t midi[3] = { data[0], data[1], data[2] };
            for (auto* s : unisonSynths) s->midiDataHandler(1, midi, 3);
        } else if (status == 0xF0) {
            // SysEx
            for (auto* s : unisonSynths) s->midiDataHandler(0, const_cast<uint8_t*>(data), pkt->length);
        }
    }
}

// Audio render callback
OSStatus audioRenderProc(void* inRefCon, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 inNumberFrames, AudioBufferList* ioData) {
    static std::vector<std::vector<int16_t>> unisonBuffers;
    if (unisonBuffers.size() != (size_t)unisonVoices)
        unisonBuffers.assign(unisonVoices, std::vector<int16_t>(inNumberFrames));
    bool useSynth = *(bool*)inRefCon;
    if (useSynth) {
        std::lock_guard<std::mutex> lock(synthMutex);
        for (int v = 0; v < unisonVoices; ++v) {
            unisonSynths[v]->getSamples(unisonBuffers[v].data(), inNumberFrames);
        }
        for (UInt32 i = 0; i < inNumberFrames; ++i) {
            float left = 0.0f, right = 0.0f;
            for (int v = 0; v < unisonVoices; ++v) {
                float pan = 0.0f;
                if (unisonVoices == 2) pan = (v == 0 ? -1.0f : 1.0f) * (unisonSpread / 99.0f);
                else if (unisonVoices == 3) pan = (v - 1) * (unisonSpread / 99.0f);
                else if (unisonVoices == 4) pan = (v - 1.5f) * (unisonSpread / 99.0f);
                float panL = std::cos((pan * 0.5f + 0.5f) * M_PI / 2.0f);
                float panR = std::sin((pan * 0.5f + 0.5f) * M_PI / 2.0f);
                left += unisonBuffers[v][i] * panL;
                right += unisonBuffers[v][i] * panR;
            }
            ((int16_t*)ioData->mBuffers[0].mData)[i] = std::clamp((int)left, -32768, 32767);
            ((int16_t*)ioData->mBuffers[1].mData)[i] = std::clamp((int)right, -32768, 32767);
        }
    } else {
        static double phase = 0.0;
        double freq = 440.0;
        double phaseInc = 2.0 * M_PI * freq / SAMPLE_RATE;
        for (UInt32 i = 0; i < inNumberFrames; ++i) {
            int16_t sample = (int16_t)(std::sin(phase) * 16000.0);
            phase += phaseInc;
            if (phase > 2.0 * M_PI) phase -= 2.0 * M_PI;
            ((int16_t*)ioData->mBuffers[0].mData)[i] = sample;
            ((int16_t*)ioData->mBuffers[1].mData)[i] = sample;
        }
    }
    return noErr;
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    int audioDev = 0;
    midiDev = 0;
    bool useSynth = true;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "  -h, --help           Show this help message and exit\n"
                      << "  -a, --audio-device N Select audio device index (default: 0)\n"
                      << "  -m, --midi-device N  Select MIDI input device index (default: 0)\n"
                      << "  --sample-rate N      Set sample rate (default: 48000)\n"
                      << "  --buffer-frames N    Set audio buffer size in frames (default: 1024)\n"
                      << "  --num-buffers N      Set number of audio buffers (default: 4)\n"
                      << "  --sine               Output test sine wave instead of synth\n"
                      << "  --synth              Use synth (default)\n"
                      << "  --unison-voices N    Set number of unison voices (1-4, default: 1)\n"
                      << "  --unison-spread N    Set unison spread (0-99, default: 0)\n"
                      << "  --unison-detune N    Set unison detune (cents, default: 0)\n";
            return 0;
        } else if ((arg == "--audio-device" || arg == "-a") && i + 1 < argc) {
            audioDev = std::atoi(argv[++i]);
        } else if ((arg == "--midi-device" || arg == "-m") && i + 1 < argc) {
            midiDev = std::atoi(argv[++i]);
        } else if (arg == "--sample-rate" && i + 1 < argc) {
            SAMPLE_RATE = std::atoi(argv[++i]);
        } else if (arg == "--buffer-frames" && i + 1 < argc) {
            BUFFER_FRAMES = std::atoi(argv[++i]);
        } else if (arg == "--num-buffers" && i + 1 < argc) {
            numBuffers = std::atoi(argv[++i]);
        } else if (arg == "--sine") {
            useSynth = false;
        } else if (arg == "--synth") {
            useSynth = true;
        } else if (arg == "--unison-voices" && i + 1 < argc) {
            unisonVoices = std::clamp(std::atoi(argv[++i]), 1, 4);
        } else if (arg == "--unison-spread" && i + 1 < argc) {
            unisonSpread = std::clamp((float)std::atof(argv[++i]), 0.0f, 99.0f);
        } else if (arg == "--unison-detune" && i + 1 < argc) {
            unisonDetune = (float)std::atof(argv[++i]);
        }
    }
    audioBuffers.resize(numBuffers);
    // List audio devices (CoreAudio)
    UInt32 size = 0;
    AudioObjectPropertyAddress addr = { kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMaster };
    AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &addr, 0, nullptr, &size);
    int numAudioDevs = size / sizeof(AudioDeviceID);
    std::vector<AudioDeviceID> devices(numAudioDevs);
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &size, devices.data());
    std::cout << "Available audio output devices:" << std::endl;
    for (int i = 0; i < numAudioDevs; ++i) {
        char name[256];
        size = sizeof(name);
        AudioObjectPropertyAddress nameAddr = { kAudioDevicePropertyDeviceName, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMaster };
        AudioObjectGetPropertyData(devices[i], &nameAddr, 0, nullptr, &size, name);
        std::cout << "  [" << i << "] " << name << std::endl;
    }
    std::cout << "[INFO] Using audio device: " << audioDev << std::endl;
    // List MIDI input devices
    ItemCount midiSrcCount = MIDIGetNumberOfSources();
    std::cout << "Available MIDI input devices:" << std::endl;
    for (ItemCount i = 0; i < midiSrcCount; ++i) {
        MIDIEndpointRef src = MIDIGetSource(i);
        char name[256];
        MIDIObjectGetStringProperty(src, kMIDIPropertyName, (CFStringRef*)&name);
        std::cout << "  [" << i << "] " << name << std::endl;
    }
    std::cout << "[INFO] Using MIDI input device: " << midiDev << std::endl;
    // Setup synth
    if (useSynth) {
        std::lock_guard<std::mutex> lock(synthMutex);
        for (int v = 0; v < unisonVoices; ++v) {
            Dexed* s = new Dexed(MAX_NOTES, SAMPLE_RATE);
            s->resetControllers();
            s->setVelocityScale(MIDI_VELOCITY_SCALING_OFF);
            s->loadVoiceParameters(fmpiano_sysex);
            float detuneCents = 0.0f;
            if (unisonVoices == 2) {
                detuneCents = (v == 0 ? -1.0f : 1.0f) * (unisonDetune / 2.0f);
            } else if (unisonVoices == 3) {
                detuneCents = (v - 1) * (unisonDetune / 1.0f);
            } else if (unisonVoices == 4) {
                detuneCents = (v - 1.5f) * (unisonDetune / 1.5f);
            }
            int8_t masterTune = static_cast<int8_t>(detuneCents);
            s->setMasterTune(masterTune);
            int detuneValue = 7;
            if (unisonVoices == 2) {
                detuneValue = (v == 0) ? 0 : 14;
            } else if (unisonVoices == 3) {
                detuneValue = v * 7;
            } else if (unisonVoices == 4) {
                detuneValue = v * 5;
            }
            for (int op = 0; op < 6; ++op) {
                s->setOPDetune(op, detuneValue);
            }
            s->setGain(2.0f);
            unisonSynths.push_back(s);
        }
    }
    // Setup CoreAudio output
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
    AURenderCallbackStruct cb = { audioRenderProc, &useSynth };
    AudioUnitSetProperty(audioUnit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof(cb));
    AudioUnitInitialize(audioUnit);
    AudioOutputUnitStart(audioUnit);
    // Setup CoreMIDI input
    MIDIClientCreate(CFSTR("Synth_Dexed"), nullptr, nullptr, &midiClient);
    midiIn = MIDIGetSource((midiDev >= 0 && midiDev < (int)midiSrcCount) ? midiDev : 0);
    MIDIPortRef inPort;
    MIDIInputPortCreate(midiClient, CFSTR("Input"), midiReadProc, nullptr, &inPort);
    MIDIPortConnectSource(inPort, midiIn, nullptr);
    std::cout << "[INFO] Synth_Dexed running. Press Ctrl+C to quit.\n";
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    running = false;
    AudioOutputUnitStop(audioUnit);
    AudioUnitUninitialize(audioUnit);
    AudioComponentInstanceDispose(audioUnit);
    MIDIClientDispose(midiClient);
    {
        std::lock_guard<std::mutex> lock(synthMutex);
        for (auto* s : unisonSynths) {
            delete s;
        }
        unisonSynths.clear();
        synth = nullptr;
    }
    return 0;
}

#endif