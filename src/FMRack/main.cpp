#include "Rack.h"
#include "UdpServer.h"
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <cmath>  // Add for M_PI and sin functions
#include <memory>

// Define M_PI if not available (common on Windows)
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifdef _WIN32
#define NOMINMAX  // Prevent Windows from defining min/max macros
#include <windows.h>
#include <mmeapi.h>
#pragma comment(lib, "winmm.lib")

// Windows audio output variables
static HWAVEOUT g_hWaveOut = nullptr;
static std::vector<WAVEHDR> g_waveHeaders;
static std::vector<std::vector<short>> g_audioBuffers;
static int g_currentBuffer = 0;
static const int AUDIO_BUFFER_SIZE = 1024; // samples per channel

#elif __APPLE__
#include <CoreAudio/CoreAudio.h>
#include <AudioUnit/AudioUnit.h>
#include <CoreMIDI/CoreMIDI.h>
#elif __linux__
#include <alsa/asoundlib.h>
#include <alsa/seq.h>
#endif

using namespace FMRack;

// Global state for audio/MIDI interface - updated to match native implementation
static std::unique_ptr<Rack> g_rack;
static std::atomic<bool> g_running{false};
static unsigned int SAMPLE_RATE = 48000;  // Match native default
static unsigned int BUFFER_FRAMES = 1024; // Match native default
static int numBuffers = 4;                // Match native default
static int audioDev = 0;                  // Audio device ID
static int midiDev = 0;                   // MIDI device ID
static bool useSine = false;              // Sine wave test mode

// Add global UDP server pointer
static std::unique_ptr<UdpServer> g_udpServer;

// Add UDP port definition
int udpPort = 50007; // Default UDP port, can be made configurable

// Global variables for command line options
static int numModules = 1; // Number of modules/parts
static int unisonVoices = 1; // Unison voices per module
static float unisonDetune = 0.0f; // Detune in cents
static float unisonSpread = 0.5f; // Stereo spread

#ifdef _WIN32
// Windows MIDI callback
void CALLBACK midiInProc(HMIDIIN hmi, UINT wMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    if (wMsg == MIM_DATA && g_rack) {
        DWORD midiMsg = static_cast<DWORD>(dwParam1);
        int status = midiMsg & 0xFF;
        int data1 = (midiMsg >> 8) & 0xFF;
        int data2 = (midiMsg >> 16) & 0xFF;
        
        g_rack->processMidiMessage(status, data1, data2);
    }
}

// Windows audio thread function
void audioThread() {
    // Set audio thread to high priority
    HANDLE hThread = GetCurrentThread();
    SetThreadPriority(hThread, THREAD_PRIORITY_HIGHEST);
    
    std::vector<float> leftBuffer(BUFFER_FRAMES);
    std::vector<float> rightBuffer(BUFFER_FRAMES);
    float outputGain = 1.0f; // Add this line to define outputGain, adjust as needed
    
    while (g_running) {
        if (g_rack && g_hWaveOut) {
            // Check if current buffer is done playing
            if (g_waveHeaders[g_currentBuffer].dwFlags & WHDR_DONE || 
                !(g_waveHeaders[g_currentBuffer].dwFlags & WHDR_INQUEUE)) {
                
                // Generate audio
                if (useSine) {
                    // Generate test sine wave
                    static double phase = 0.0;
                    double freq = 440.0;
                    double phaseInc = 2.0 * M_PI * freq / SAMPLE_RATE;
                    for (unsigned int i = 0; i < BUFFER_FRAMES; ++i) {
                        float sample = static_cast<float>(std::sin(phase) * 0.5);
                        leftBuffer[i] = rightBuffer[i] = sample;
                        phase += phaseInc;
                        if (phase > 2.0 * M_PI) phase -= 2.0 * M_PI;
                    }
                } else {
                    g_rack->processAudio(leftBuffer.data(), rightBuffer.data(), BUFFER_FRAMES);
                }
                
                // Convert float to 16-bit PCM and interleave
                for (unsigned int i = 0; i < BUFFER_FRAMES; ++i) {
                    // Clamp and convert to 16-bit
                    short leftSample = static_cast<short>(std::clamp(leftBuffer[i] * 32767.0f, -32768.0f, 32767.0f));
                    short rightSample = static_cast<short>(std::clamp(rightBuffer[i] * 32767.0f, -32768.0f, 32767.0f));
                    g_audioBuffers[g_currentBuffer][i * 2] = leftSample;   // Left channel
                    g_audioBuffers[g_currentBuffer][i * 2 + 1] = rightSample; // Right channel
                }
                
                // Send buffer to audio device
                waveOutWrite(g_hWaveOut, &g_waveHeaders[g_currentBuffer], sizeof(WAVEHDR));
                
                // Switch to next buffer
                g_currentBuffer = (g_currentBuffer + 1) % numBuffers;
            }
        }
        
        // Small sleep to prevent busy waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

bool initializeAudioMidi() {
    // Initialize MIDI input
    UINT numMidiDevices = midiInGetNumDevs();
    for (UINT i = 0; i < numMidiDevices; ++i) {
        MIDIINCAPS caps;
        if (midiInGetDevCaps(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
            std::wcout << "  " << i << ": " << caps.szPname << L"\n";
        }
    }
    
    if (numMidiDevices > 0 && midiDev < (int)numMidiDevices) {
        HMIDIIN hMidiIn;
        MMRESULT result = midiInOpen(&hMidiIn, midiDev, (DWORD_PTR)midiInProc, 0, CALLBACK_FUNCTION);
        if (result == MMSYSERR_NOERROR) {
            midiInStart(hMidiIn);
            std::cout << "MIDI input initialized on device " << midiDev << ".\n";
        } else {
            std::cout << "Failed to open MIDI input device " << midiDev << ".\n";
            return false;
        }
    }
    
    // Initialize audio output with configurable settings
    WAVEFORMATEX waveFormat;
    waveFormat.wFormatTag = WAVE_FORMAT_PCM;
    waveFormat.nChannels = 2;
    waveFormat.nSamplesPerSec = SAMPLE_RATE;
    waveFormat.wBitsPerSample = 16;
    waveFormat.nBlockAlign = (waveFormat.nChannels * waveFormat.wBitsPerSample) / 8;
    waveFormat.nAvgBytesPerSec = waveFormat.nSamplesPerSec * waveFormat.nBlockAlign;
    waveFormat.cbSize = 0;
    
    MMRESULT result = waveOutOpen(&g_hWaveOut, audioDev, &waveFormat, 0, 0, CALLBACK_NULL);
    if (result != MMSYSERR_NOERROR) {
        std::cout << "Failed to open wave output device " << audioDev << ".\n";
        return false;
    }
    
    // Prepare multiple wave headers like native implementation
    g_waveHeaders.resize(numBuffers);
    g_audioBuffers.resize(numBuffers);
    for (int i = 0; i < numBuffers; ++i) {
        g_audioBuffers[i].resize(BUFFER_FRAMES * 2); // stereo
        ZeroMemory(&g_waveHeaders[i], sizeof(WAVEHDR));
        g_waveHeaders[i].lpData = reinterpret_cast<LPSTR>(g_audioBuffers[i].data());
        g_waveHeaders[i].dwBufferLength = BUFFER_FRAMES * sizeof(short) * 2;
        waveOutPrepareHeader(g_hWaveOut, &g_waveHeaders[i], sizeof(WAVEHDR));
    }
    
    std::cout << "Audio interface initialized (Windows WaveOut).\n";
    std::cout << "  Sample Rate: " << SAMPLE_RATE << " Hz\n";
    std::cout << "  Buffer Frames: " << BUFFER_FRAMES << "\n";
    std::cout << "  Number of Buffers: " << numBuffers << "\n";
    return true;
}

#elif __APPLE__
// macOS Core Audio callback
OSStatus audioCallback(void* inRefCon, AudioUnitRenderActionFlags* ioActionFlags,
                      const AudioTimeStamp* inTimeStamp, UInt32 inBusNumber,
                      UInt32 inNumberFrames, AudioBufferList* ioData) {
    
    if (g_rack && ioData->mNumberBuffers >= 2) {
        float* leftOut = static_cast<float*>(ioData->mBuffers[0].mData);
        float* rightOut = static_cast<float*>(ioData->mBuffers[1].mData);
        
        g_rack->processAudio(leftOut, rightOut, inNumberFrames);
    }
    
    return noErr;
}

// macOS Core MIDI callback
void midiReadProc(const MIDIPacketList* pktlist, void* refCon, void* connRefCon) {
    if (!g_rack) return;
    
    const MIDIPacket* packet = &pktlist->packet[0];
    for (UInt32 i = 0; i < pktlist->numPackets; ++i) {
        if (packet->length >= 3) {
            g_rack->processMidiMessage(packet->data[0], packet->data[1], packet->data[2]);
        }
        packet = MIDIPacketNext(packet);
    }
}

bool initializeAudioMidi() {
    // Initialize Core Audio
    AudioUnit audioUnit;
    AudioComponentDescription desc = {};
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_DefaultOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    
    AudioComponent comp = AudioComponentFindNext(NULL, &desc);
    if (!comp) {
        std::cout << "Failed to find audio component.\n";
        return false;
    }
    
    OSStatus status = AudioComponentInstanceNew(comp, &audioUnit);
    if (status != noErr) {
        std::cout << "Failed to create audio unit instance.\n";
        return false;
    }
    
    AURenderCallbackStruct callbackStruct = {};
    callbackStruct.inputProc = audioCallback;
    callbackStruct.inputProcRefCon = nullptr;
    
    status = AudioUnitSetProperty(audioUnit, kAudioUnitProperty_SetRenderCallback,
                                 kAudioUnitScope_Input, 0, &callbackStruct, sizeof(callbackStruct));
    
    if (status == noErr) {
        AudioUnitInitialize(audioUnit);
        AudioOutputUnitStart(audioUnit);
        std::cout << "Core Audio initialized successfully.\n";
    }
    
    // Initialize Core MIDI
    MIDIClientRef midiClient;
    MIDIPortRef inputPort;
    
    status = MIDIClientCreate(CFSTR("FMRack"), NULL, NULL, &midiClient);
    if (status == noErr) {
        status = MIDIInputPortCreate(midiClient, CFSTR("Input"), midiReadProc, NULL, &inputPort);
        
        // Connect to all MIDI sources
        ItemCount sourceCount = MIDIGetNumberOfSources();
        for (ItemCount i = 0; i < sourceCount; ++i) {
            MIDIEndpointRef source = MIDIGetSource(i);
            MIDIPortConnectSource(inputPort, source, NULL);
        }
        
        std::cout << "Core MIDI initialized successfully.\n";
    }
    
    return true;
}

#elif __linux__
// Linux ALSA audio thread - updated to use BUFFER_FRAMES
void audioThread() {
    // Set audio thread to high priority
    struct sched_param sch_params;
    sch_params.sched_priority = 20;
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &sch_params);
    
    snd_pcm_t* pcm_handle;
    int err = snd_pcm_open(&pcm_handle, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        std::cout << "Failed to open PCM device: " << snd_strerror(err) << "\n";
        return;
    }
    
    // Configure PCM
    snd_pcm_hw_params_t* hw_params;
    snd_pcm_hw_params_alloca(&hw_params);
    snd_pcm_hw_params_any(pcm_handle, hw_params);
    snd_pcm_hw_params_set_access(pcm_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm_handle, hw_params, SND_PCM_FORMAT_FLOAT_LE);
    snd_pcm_hw_params_set_channels(pcm_handle, hw_params, 2);
    unsigned int rate = SAMPLE_RATE;
    snd_pcm_hw_params_set_rate_near(pcm_handle, hw_params, &rate, 0);
    snd_pcm_hw_params_set_period_size(pcm_handle, hw_params, BUFFER_FRAMES, 0);
    snd_pcm_hw_params(pcm_handle, hw_params);
    snd_pcm_prepare(pcm_handle);
    
    std::vector<float> audioBuffer(BUFFER_FRAMES * 2); // Interleaved stereo
    std::vector<float> leftBuffer(BUFFER_FRAMES);
    std::vector<float> rightBuffer(BUFFER_FRAMES);
    
    // Declare and initialize outputGain before use
    float outputGain = 1.0f; // Set to appropriate value or make configurable
    
    while (g_running) {
        if (g_rack) {
            if (useSine) {
                // Generate test sine wave
                static double phase = 0.0;
                double freq = 440.0;
                double phaseInc = 2.0 * M_PI * freq / SAMPLE_RATE;
                for (int i = 0; i < BUFFER_FRAMES; ++i) {
                    float sample = static_cast<float>(std::sin(phase) * 0.5);
                    leftBuffer[i] = rightBuffer[i] = sample;
                    phase += phaseInc;
                    if (phase > 2.0 * M_PI) phase -= 2.0 * M_PI;
                }
            } else {
                g_rack->processAudio(leftBuffer.data(), rightBuffer.data(), BUFFER_FRAMES);
            }
            
            // Interleave samples
            for (unsigned int i = 0; i < BUFFER_FRAMES; ++i) {
                audioBuffer[i * 2] = leftBuffer[i] * static_cast<float>(outputGain);
                audioBuffer[i * 2 + 1] = rightBuffer[i] * static_cast<float>(outputGain);
            }
            
            snd_pcm_writei(pcm_handle, audioBuffer.data(), BUFFER_FRAMES);
        }
    }
    
    snd_pcm_close(pcm_handle);
}

// Linux ALSA MIDI thread
void midiThread() {
    snd_seq_t* seq_handle;
    int err = snd_seq_open(&seq_handle, "default", SND_SEQ_OPEN_DUPLEX, 0);
    if (err < 0) {
        std::cout << "Failed to open sequencer: " << snd_strerror(err) << "\n";
        return;
    }
    
    snd_seq_set_client_name(seq_handle, "FMRack");
    snd_seq_create_simple_port(seq_handle, "Input",
        SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_APPLICATION);
    
    while (g_running) {
        snd_seq_event_t* ev;
        if (snd_seq_event_input(seq_handle, &ev) >= 0) {
            if (g_rack && ev->type == SND_SEQ_EVENT_NOTEON) {
                g_rack->processMidiMessage(0x90 | ev->data.note.channel,
                                         ev->data.note.note, ev->data.note.velocity);
            } else if (g_rack && ev->type == SND_SEQ_EVENT_NOTEOFF) {
                g_rack->processMidiMessage(0x80 | ev->data.note.channel,
                                         ev->data.note.note, ev->data.note.velocity);
            }
            snd_seq_free_event(ev);
        }
    }
    
    snd_seq_close(seq_handle);
}

bool initializeAudioMidi() {
    std::cout << "ALSA audio/MIDI interface initialized.\n";
    std::cout << "  Sample Rate: " << SAMPLE_RATE << " Hz\n";
    std::cout << "  Buffer Frames: " << BUFFER_FRAMES << "\n";
    return true;
}
#endif

// Parse command line arguments - matches native implementation
void parseCommandLineArgs(int argc, char* argv[], std::string& performanceFile) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--performance" && i + 1 < argc) {
            performanceFile = argv[i + 1];
            ++i;
        } else if (arg == "--sample-rate" && i + 1 < argc) {
            SAMPLE_RATE = std::atoi(argv[i + 1]);
            ++i;
        } else if (arg == "--buffer-frames" && i + 1 < argc) {
            BUFFER_FRAMES = std::atoi(argv[i + 1]);
            ++i;
        } else if (arg == "--num-buffers" && i + 1 < argc) {
            numBuffers = std::atoi(argv[i + 1]);
            ++i;
        } else if ((arg == "--audio-device" || arg == "-a") && i + 1 < argc) {
            audioDev = std::atoi(argv[i + 1]);
            ++i;
        } else if ((arg == "--midi-device" || arg == "-m") && i + 1 < argc) {
            midiDev = std::atoi(argv[i + 1]);
            ++i;
        } else if (arg == "--num-modules" && i + 1 < argc) {
            numModules = std::clamp(std::atoi(argv[i + 1]), 1, 8);
            ++i;
        } else if (arg == "--unison-voices" && i + 1 < argc) {
            unisonVoices = std::clamp(std::atoi(argv[i + 1]), 1, 4);
            ++i;
        } else if (arg == "--unison-detune" && i + 1 < argc) {
            unisonDetune = std::stof(argv[i + 1]);
            ++i;
        } else if (arg == "--unison-spread" && i + 1 < argc) {
            unisonSpread = std::clamp(std::stof(argv[i + 1]), 0.0f, 1.0f);
            ++i;
        } else if (arg == "--sine") {
            useSine = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --performance <file>     Load performance file at startup\n";
            std::cout << "  --sample-rate <rate>     Set sample rate (default: 48000)\n";
            std::cout << "  --buffer-frames <frames> Set buffer size (default: 1024)\n";
            std::cout << "  --num-buffers <count>    Set number of buffers (default: 4)\n";
            std::cout << "  --audio-device <id>      Audio device ID (default: 0)\n";
            std::cout << "  --midi-device <id>       MIDI device ID (default: 0)\n";
            std::cout << "  --num-modules <n>        Number of modules/parts (default: 1)\n";
            std::cout << "  --unison-voices <n>      Unison voices per module (default: 1)\n";
            std::cout << "  --unison-detune <cents>  Unison detune in cents (default: 0.0)\n";
            std::cout << "  --unison-spread <0-1>    Unison stereo spread (default: 0.5)\n";
            std::cout << "  --sine                   Generate test sine wave\n";
            std::cout << "  --help, -h               Show this help message\n";
            exit(0);
        }
    }
}

int main(int argc, char* argv[]) {
    std::cout << "FMRack Multi-Timbral FM Synthesizer\n";
    std::cout << "===================================\n";
    std::cout << "Build configuration: " << 
#ifdef DEBUG
        "Debug"
#else
        "Release"
#endif
        << "\n";
    std::cout << "Platform: " << 
#ifdef _WIN32
        "Windows"
#elif __APPLE__
        "macOS"
#elif __linux__
        "Linux"
#else
        "Unknown"
#endif
        << "\n\n";

    // List audio and MIDI devices for each platform
#ifdef _WIN32
    // List audio output devices
    UINT numAudioDevs = waveOutGetNumDevs();
    std::cout << "Available audio output devices:\n";
    for (UINT i = 0; i < numAudioDevs; ++i) {
        WAVEOUTCAPS caps;
        if (waveOutGetDevCaps(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
            std::wcout << "  " << i << ": " << caps.szPname << L"\n";
        }
    }
    if (numAudioDevs == 0) std::cout << "  (none found)\n";

    // List MIDI input devices
    UINT numMidiDevices = midiInGetNumDevs();
    std::cout << "Available MIDI input devices:\n";
    for (UINT i = 0; i < numMidiDevices; ++i) {
        MIDIINCAPS caps;
        if (midiInGetDevCaps(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
            std::wcout << "  " << i << ": " << caps.szPname << L"\n";
        }
    }
    if (numMidiDevices == 0) std::cout << "  (none found)\n";
#elif __APPLE__
    // List audio devices (CoreAudio)
    std::cout << "Available audio output devices:\n";
    UInt32 size = 0;
    AudioObjectPropertyAddress addr = {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMaster
    };
    AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &addr, 0, nullptr, &size);
    int numDevices = size / sizeof(AudioDeviceID);
    std::vector<AudioDeviceID> devices(numDevices);
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &size, devices.data());
    for (int i = 0; i < numDevices; ++i) {
        char name[256] = {0};
        size = sizeof(name);
        AudioObjectPropertyAddress nameAddr = {
            kAudioObjectPropertyName,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMaster
        };
        AudioObjectGetPropertyData(devices[i], &nameAddr, 0, nullptr, &size, name);
        std::cout << "  " << i << ": " << name << "\n";
    }
    if (numDevices == 0) std::cout << "  (none found)\n";

    // List MIDI input devices
    ItemCount midiSources = MIDIGetNumberOfSources();
    std::cout << "Available MIDI input devices:\n";
    for (ItemCount i = 0; i < midiSources; ++i) {
        MIDIEndpointRef src = MIDIGetSource(i);
        if (src) {
            CFStringRef pname = nullptr;
            MIDIObjectGetStringProperty(src, kMIDIPropertyName, &pname);
            char name[256] = {0};
            if (pname) {
                CFStringGetCString(pname, name, sizeof(name), kCFStringEncodingUTF8);
                CFRelease(pname);
            }
            std::cout << "  " << i << ": " << name << "\n";
        }
    }
    if (midiSources == 0) std::cout << "  (none found)\n";
#elif __linux__
    // List ALSA audio devices
    std::cout << "Available ALSA audio output devices:\n";
    void **hints;
    if (snd_device_name_hint(-1, "pcm", &hints) == 0) {
        void **n = hints;
        int idx = 0;
        while (*n != nullptr) {
            char *name = snd_device_name_get_hint(*n, "NAME");
            char *desc = snd_device_name_get_hint(*n, "DESC");
            if (name) {
                std::cout << "  " << idx << ": " << name;
                if (desc) std::cout << " (" << desc << ")";
                std::cout << "\n";
                free(name);
            }
            if (desc) free(desc);
            ++n;
            ++idx;
        }
        snd_device_name_free_hint(hints);
        if (idx == 0) std::cout << "  (none found)\n";
    } else {
        std::cout << "  (error listing devices)\n";
    }

    // List ALSA MIDI devices
    std::cout << "Available ALSA MIDI input devices:\n";
    snd_seq_t* seq;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_INPUT, 0) == 0) {
        snd_seq_client_info_t *cinfo;
        snd_seq_port_info_t *pinfo;
        snd_seq_client_info_alloca(&cinfo);
        snd_seq_port_info_alloca(&pinfo);
        int idx = 0;
        snd_seq_client_info_set_client(cinfo, -1);
        while (snd_seq_query_next_client(seq, cinfo) >= 0) {
            int client = snd_seq_client_info_get_client(cinfo);
            snd_seq_port_info_set_client(pinfo, client);
            snd_seq_port_info_set_port(pinfo, -1);
            while (snd_seq_query_next_port(seq, pinfo) >= 0) {
                unsigned int caps = snd_seq_port_info_get_capability(pinfo);
                if ((caps & SND_SEQ_PORT_CAP_WRITE) && (caps & SND_SEQ_PORT_CAP_SUBS_WRITE)) {
                    std::cout << "  " << idx << ": "
                              << snd_seq_client_info_get_name(cinfo) << " - "
                              << snd_seq_port_info_get_name(pinfo) << "\n";
                    ++idx;
                }
            }
        }
        if (idx == 0) std::cout << "  (none found)\n";
        snd_seq_close(seq);
    } else {
        std::cout << "  (error listing MIDI devices)\n";
    }
#endif

    // Parse command line arguments
    std::string performanceFile;
    parseCommandLineArgs(argc, argv, performanceFile);
    
    try {
        // Initialize the rack with configured sample rate
        g_rack = std::make_unique<Rack>(SAMPLE_RATE);
        
        std::cout << "Rack initialized successfully.\n";
        
        // Initialize audio/MIDI hardware
        if (!initializeAudioMidi()) {
            std::cerr << "Failed to initialize audio/MIDI interface.\n";
            return 1;
        }
        
        // Load performance file if specified, otherwise use default
        if (!performanceFile.empty()) {
            std::cout << "Loading performance file: " << performanceFile << "\n";
            if (g_rack->loadPerformance(performanceFile)) {
                std::cout << "Performance loaded successfully!\n";
                std::cout << "Enabled parts: " << g_rack->getEnabledPartCount() << "/8\n";
            } else {
                std::cout << "Failed to load performance file. Using default configuration.\n";
                g_rack->setDefaultPerformance();
            }
        } else {
            // Custom default setup using command line options
            FMRack::Performance perf;
            perf.setDefaults();
            for (int i = 0; i < 8; ++i) {
                if (i < numModules) {
                    perf.parts[i].midiChannel = i + 1;
                    perf.parts[i].unisonVoices = unisonVoices;
                    perf.parts[i].unisonDetune = unisonDetune;
                    perf.parts[i].unisonSpread = unisonSpread;
                    perf.parts[i].volume = 100;
                } else {
                    perf.parts[i].midiChannel = 0;
                }
            }
            g_rack->setPerformance(perf);
            std::cout << "Default configuration: " << numModules << " modules, "
                      << unisonVoices << " unison voices, detune " << unisonDetune
                      << ", spread " << unisonSpread << std::endl;
            if (useSine) {
                std::cout << "Running in sine wave test mode.\n";
            } else {
                std::cout << "Running with custom FM synthesizer configuration.\n";
            }
        }
        
        // Start audio processing
        g_running = true;
        
#if defined(_WIN32) || defined(__linux__)
        std::thread audio_thread(audioThread);
#ifdef __linux__
        std::thread midi_thread(midiThread);
#endif
#endif
        
        std::cout << "[DEBUG] About to start UDP server on port " << udpPort << std::endl;
        // Start UDP server for raw UDP handling
        g_udpServer = std::make_unique<UdpServer>(udpPort, [](const uint8_t* data, int len) {
            if (!g_rack) return;
            // Handle standard 3-byte MIDI messages
            if (len == 3) {
                uint8_t status = data[0];
                uint8_t data1 = data[1];
                uint8_t data2 = data[2];
                g_rack->processMidiMessage(status, data1, data2);
            } else if (len > 0) {
                int i = 0;
                while (i < len) {
                    uint8_t status = data[i];
                    if (status == 0xF0) { // SysEx start
                        int sysex_end = i + 1;
                        while (sysex_end < len && data[sysex_end] != 0xF7) sysex_end++;
                        if (sysex_end < len && data[sysex_end] == 0xF7) sysex_end++;
                        int sysex_len = sysex_end - i;
                        if (sysex_len >= 2) {
                            // Yamaha format: F0 43 cc ... F7, where cc = channel (0x00 = ch1, 0x0F = ch16)
                            uint8_t sysex_channel = 0; // Default: OMNI
                            if (sysex_len > 2 && data[i+1] == 0x43) {
                                sysex_channel = (data[i+2] & 0x0F) + 1; // 1-16
                            }
                            g_rack->routeSysexToModules(&data[i], sysex_len, sysex_channel);
                        }
                        i = sysex_end;
                    } else if ((status & 0xF0) >= 0x80 && (status & 0xF0) <= 0xE0 && (i + 2) < len) {
                        g_rack->processMidiMessage(data[i], data[i+1], data[i+2]);
                        i += 3;
                    } else if (status >= 0xF8 && status <= 0xFF) {
                        g_rack->processMidiMessage(status, 0, 0);
                        i += 1;
                    } else {
                        i += 1;
                    }
                }
            }
        });
        g_udpServer->start();
        
        std::cout << "Audio/MIDI interface started.\n";
        std::cout << "Current settings:\n";
        std::cout << "  SAMPLE_RATE: " << SAMPLE_RATE << "\n";
        std::cout << "  BUFFER_FRAMES: " << BUFFER_FRAMES << "\n";
        std::cout << "  NUM_BUFFERS: " << numBuffers << "\n";
        std::cout << "  AUDIO_DEVICE: " << audioDev << "\n";
        std::cout << "  MIDI_DEVICE: " << midiDev << "\n";
        std::cout << "Commands:\n";
        std::cout << "  t - Send test MIDI note\n";
        std::cout << "  q - Quit\n";
        std::cout << "> ";
        std::cout << "FIXME: Send a single voice dump for sound to start working.\n";
        
        // Interactive command loop
        char command;
        while (std::cin >> command && command != 'q') {
            switch (command) {
                case 't':
                    if (useSine) {
                        std::cout << "Test note disabled in sine wave mode.\n";
                    } else {
                        std::cout << "Sending test MIDI note (C4)...\n";
                        g_rack->processMidiMessage(0x90, 60, 100); // Note on C4
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                        g_rack->processMidiMessage(0x80, 60, 0);   // Note off C4
                    }
                    break;
                default:
                    std::cout << "Unknown command. Use 't' for test note, 'q' to quit.\n";
                    break;
            }
            std::cout << "> ";
        }
        
        // Cleanup
        g_running = false;
        if (g_udpServer) g_udpServer->stop();
        
#ifdef _WIN32
        // Cleanup Windows audio
        if (g_hWaveOut) {
            waveOutReset(g_hWaveOut);
            for (auto& hdr : g_waveHeaders) {
                waveOutUnprepareHeader(g_hWaveOut, &hdr, sizeof(WAVEHDR));
            }
            waveOutClose(g_hWaveOut);
            g_hWaveOut = nullptr;
        }
#endif
        
#if defined(_WIN32) || defined(__linux__)
        if (audio_thread.joinable()) {
            audio_thread.join();
        }
#ifdef __linux__
        if (midi_thread.joinable()) {
            midi_thread.join();
        }
#endif
#endif
        
        std::cout << "Audio processing stopped.\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        g_running = false;
        return 1;
    }
    
    std::cout << "FMRack shutdown completed successfully.\n";
    return 0;
}
