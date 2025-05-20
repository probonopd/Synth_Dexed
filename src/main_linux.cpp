#ifndef ARDUINO

// Linux-specific main implementation using ALSA for audio and MIDI

#include <alsa/asoundlib.h>
#include <alsa/seq.h>
#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <csignal>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include "dexed.h"
#include <map>
#include <poll.h>
unsigned int SAMPLE_RATE = 48000;
unsigned int BUFFER_FRAMES = 1024;
unsigned int NUM_BUFFERS = 4;
unsigned int ALSA_LATENCY = 10000; // microseconds
std::atomic<bool> running{true};
void signal_handler(int signal) {
    if (signal == SIGINT) {
        running = false;
        std::cout << "\n[INFO] Caught Ctrl+C, exiting..." << std::endl;
    }
}
Dexed* synth = nullptr;
std::mutex synthMutex;
std::vector<short> audioBuffers[NUM_BUFFERS];
snd_pcm_t* pcm_handle = nullptr;
snd_seq_t* seq_handle = nullptr;
int midi_in_port = -1;
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

void midi_thread_func() {
    snd_seq_event_t* ev = nullptr;
    while (running) {
        // Use poll to avoid blocking forever
        int npfd = snd_seq_poll_descriptors_count(seq_handle, POLLIN);
        struct pollfd* pfd = (struct pollfd*)alloca(npfd * sizeof(struct pollfd));
        snd_seq_poll_descriptors(seq_handle, pfd, npfd, POLLIN);
        int poll_result = poll(pfd, npfd, 100); // 100ms timeout
        if (!running) break;
        if (poll_result > 0) {
            int events = snd_seq_event_input(seq_handle, &ev);
            if (events > 0 && ev) {
                std::cout << "[MIDI] Event type: " << ev->type << std::endl;
                if (ev->type == SND_SEQ_EVENT_NOTEON || ev->type == SND_SEQ_EVENT_NOTEOFF) {
                    std::lock_guard<std::mutex> lock(synthMutex);
                    if (!synth) continue;
                    uint8_t note = ev->data.note.note;
                    uint8_t velocity = ev->data.note.velocity;
                    std::cout << "[MIDI] Note " << (int)note << (ev->type == SND_SEQ_EVENT_NOTEON ? " ON " : " OFF ") << (int)velocity << std::endl;
                    if (ev->type == SND_SEQ_EVENT_NOTEON && velocity > 0) {
                        synth->keydown(note, velocity);
                    } else {
                        synth->keyup(note);
                    }
                } else if (ev->type == SND_SEQ_EVENT_CONTROLLER) {
                    std::lock_guard<std::mutex> lock(synthMutex);
                    if (!synth) continue;
                    uint8_t ctrl = ev->data.control.param;
                    uint8_t value = ev->data.control.value;
                    uint8_t midi[3] = { static_cast<uint8_t>(0xB0 | (ev->data.control.channel & 0x0F)), ctrl, value };
                    std::cout << "[MIDI] Controller " << (int)ctrl << " value " << (int)value << std::endl;
                    synth->midiDataHandler(1, midi, 3);
                }
            }
        }
    }
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
                      << "  --alsa-latency N     Set ALSA latency in microseconds (default: 10000)\n"
                      << "  --sine               Output test sine wave instead of synth\n"
                      << "  --synth              Use synth (default)\n";
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
            NUM_BUFFERS = std::atoi(argv[++i]);
        } else if (arg == "--alsa-latency" && i + 1 < argc) {
            ALSA_LATENCY = std::atoi(argv[++i]);
        } else if (arg == "--sine") {
            useSynth = false;
        } else if (arg == "--synth") {
            useSynth = true;
        }
    }
    // Enumerate and print available ALSA PCM audio output devices
    void **hints;
    int numAudioDevices = 0;
    std::vector<std::string> deviceNames;
    std::vector<std::string> deviceDescs;
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
        numAudioDevices = idx;
        snd_device_name_free_hint(hints);
    }
    if (deviceNames.empty()) {
        std::cerr << "[WARN] No ALSA audio output devices found. Defaulting to 'default'." << std::endl;
        deviceNames.push_back("default");
    }
    if (audioDev < 0 || audioDev >= (int)deviceNames.size()) {
        std::cerr << "[WARN] Invalid audio device index, using device 0." << std::endl;
        audioDev = 0;
    }
    std::cout << "[INFO] Using audio device: [" << audioDev << "] " << deviceDescs[audioDev] << " (" << deviceNames[audioDev] << ")" << std::endl;
    // Open ALSA PCM device for playback
    int err = snd_pcm_open(&pcm_handle, deviceNames[audioDev].c_str(), SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        std::cerr << "[ERROR] Failed to open ALSA PCM device: " << snd_strerror(err) << std::endl;
        return 1;
    }
    snd_pcm_set_params(pcm_handle, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 2, SAMPLE_RATE, 1, ALSA_LATENCY); // microseconds
    std::cout << "[INFO] ALSA PCM device opened successfully." << std::endl;
    for (int i = 0; i < NUM_BUFFERS; ++i) {
        audioBuffers[i].resize(BUFFER_FRAMES * 2);
    }
    // ALSA MIDI setup
    if (snd_seq_open(&seq_handle, "default", SND_SEQ_OPEN_INPUT, 0) < 0) {
        std::cerr << "[ERROR] Failed to open ALSA sequencer." << std::endl;
        return 1;
    }
    snd_seq_set_client_name(seq_handle, "Synth_Dexed");
    midi_in_port = snd_seq_create_simple_port(seq_handle, "Input",
        SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_APPLICATION);
    if (midi_in_port < 0) {
        std::cerr << "[ERROR] Failed to create ALSA MIDI input port." << std::endl;
        return 1;
    }
    std::cout << "[INFO] ALSA MIDI input port created." << std::endl;
    // List available MIDI input ports
    std::map<int, std::string> midi_ports;
    int port_count = 0;
    snd_seq_client_info_t* cinfo;
    snd_seq_port_info_t* pinfo;
    snd_seq_client_info_alloca(&cinfo);
    snd_seq_port_info_alloca(&pinfo);
    snd_seq_client_info_set_client(cinfo, -1);
    std::cout << "Available MIDI input devices:" << std::endl;
    while (snd_seq_query_next_client(seq_handle, cinfo) >= 0) {
        int client = snd_seq_client_info_get_client(cinfo);
        snd_seq_port_info_set_client(pinfo, client);
        snd_seq_port_info_set_port(pinfo, -1);
        while (snd_seq_query_next_port(seq_handle, pinfo) >= 0) {
            if ((snd_seq_port_info_get_capability(pinfo) & SND_SEQ_PORT_CAP_READ) &&
                (snd_seq_port_info_get_type(pinfo) & SND_SEQ_PORT_TYPE_MIDI_GENERIC)) {
                std::string name = std::string(snd_seq_client_info_get_name(cinfo)) + ": " + std::string(snd_seq_port_info_get_name(pinfo));
                std::cout << "  [" << port_count << "] " << name << std::endl;
                midi_ports[port_count] = std::to_string(client) + "," + std::to_string(snd_seq_port_info_get_port(pinfo));
                ++port_count;
            }
        }
    }
    std::cout << "[INFO] Using MIDI input device: " << midiDev << std::endl;
    // Connect to selected MIDI port
    int selected_port = midiDev;
    if (selected_port >= 0 && selected_port < port_count) {
        int client, port;
        sscanf(midi_ports[selected_port].c_str(), "%d,%d", &client, &port);
        snd_seq_connect_from(seq_handle, midi_in_port, client, port);
    } else if (port_count > 0) {
        int client, port;
        sscanf(midi_ports[0].c_str(), "%d,%d", &client, &port);
        snd_seq_connect_from(seq_handle, midi_in_port, client, port);
    } else {
        std::cerr << "[WARN] No MIDI input devices found. MIDI will be disabled." << std::endl;
    }
    // Synth setup
    if (useSynth) {
        std::lock_guard<std::mutex> lock(synthMutex);
        synth = new Dexed(MAX_NOTES, SAMPLE_RATE);
        synth->resetControllers();
        synth->setVelocityScale(MIDI_VELOCITY_SCALING_OFF);
        synth->loadVoiceParameters(fmpiano_sysex);
        synth->setGain(2.0f);
    }
    // Pre-fill all audio buffers
    std::vector<int16_t> monoBuffer(BUFFER_FRAMES);
    for (int i = 0; i < NUM_BUFFERS; ++i) {
        if (useSynth) {
            std::lock_guard<std::mutex> lock(synthMutex);
            if (synth) synth->getSamples(monoBuffer.data(), BUFFER_FRAMES);
            for (int j = 0; j < BUFFER_FRAMES; ++j) {
                audioBuffers[i][2*j] = monoBuffer[j];
                audioBuffers[i][2*j+1] = monoBuffer[j];
            }
        } else {
            static double phase = 0.0;
            double freq = 440.0;
            double phaseInc = 2.0 * M_PI * freq / SAMPLE_RATE;
            for (int j = 0; j < BUFFER_FRAMES; ++j) {
                int16_t sample = (int16_t)(std::sin(phase) * 16000.0);
                phase += phaseInc;
                if (phase > 2.0 * M_PI) phase -= 2.0 * M_PI;
                audioBuffers[i][2*j] = sample;
                audioBuffers[i][2*j+1] = sample;
                monoBuffer[j] = sample;
            }
        }
    }
    // Start MIDI thread
    std::thread midi_thread(midi_thread_func);
    // Start audio thread
    std::thread audio([&]() {
        int bufferIndex = 0;
        std::vector<int16_t> monoBuffer(BUFFER_FRAMES);
        while (running) {
            if (useSynth) {
                std::lock_guard<std::mutex> lock(synthMutex);
                if (synth) synth->getSamples(monoBuffer.data(), BUFFER_FRAMES);
                for (int i = 0; i < BUFFER_FRAMES; ++i) {
                    audioBuffers[bufferIndex][2*i] = monoBuffer[i];
                    audioBuffers[bufferIndex][2*i+1] = monoBuffer[i];
                }
            } else {
                static double phase = 0.0;
                double freq = 440.0;
                double phaseInc = 2.0 * M_PI * freq / SAMPLE_RATE;
                for (int i = 0; i < BUFFER_FRAMES; ++i) {
                    int16_t sample = (int16_t)(std::sin(phase) * 16000.0);
                    phase += phaseInc;
                    if (phase > 2.0 * M_PI) phase -= 2.0 * M_PI;
                    audioBuffers[bufferIndex][2*i] = sample;
                    audioBuffers[bufferIndex][2*i+1] = sample;
                    monoBuffer[i] = sample;
                }
            }
            int err = snd_pcm_writei(pcm_handle, audioBuffers[bufferIndex].data(), BUFFER_FRAMES);
            if (err < 0) {
                snd_pcm_prepare(pcm_handle);
            }
            bufferIndex = (bufferIndex + 1) % NUM_BUFFERS;
        }
    });
    std::cout << "[INFO] Synth_Dexed running. Press Ctrl+C to quit.\n";
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    running = false;
    audio.join();
    midi_thread.join();
    snd_pcm_drain(pcm_handle);
    snd_pcm_close(pcm_handle);
    snd_seq_close(seq_handle);
    {
        std::lock_guard<std::mutex> lock(synthMutex);
        if (synth) {
            delete synth;
            synth = nullptr;
        }
    }
    return 0;
}

#endif