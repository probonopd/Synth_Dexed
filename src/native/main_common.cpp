#ifndef ARDUINO

#include "main_common.h"
#include "StereoDexed.h"
#include "midi_socket_server.h"
#include "performance_loader.h"
#include <iostream>
#include <csignal>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>
#include <string> // Add this line
#include <cstdlib>
#include <random>
#include <fstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "ws2_32.lib")
#endif

#if defined(__linux__)
#include "main_linux.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

#if defined(__APPLE__)
#include "main_mac.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int num_modules = 1; // Number of synth modules, each on its own MIDI channel
std::vector<uint8_t> customVoiceSysex; // Added for custom voice
int unisonVoices = 1;
float unisonSpread = 0.0f;
float unisonDetune = 0.0f;
std::vector<StereoDexed*> allSynths; // Changed from unisonSynths
unsigned int SAMPLE_RATE = 48000;
unsigned int BUFFER_FRAMES = 1024;
int numBuffers = 4;
std::atomic<bool> running{true};
StereoDexed* synth = nullptr;
std::mutex synthMutex;
std::vector<std::vector<short>> audioBuffers;
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
};

// Global phase for sine test (shared across all buffer fills)
double global_sine_phase = 0.0;

void signal_handler(int signal) {
    static std::atomic_flag handled = ATOMIC_FLAG_INIT;
    if (signal == SIGINT && !handled.test_and_set()) {
        running = false;
        std::cout << "\n[INFO] Caught Ctrl+C, exiting..." << std::endl;
        // Force exit after a short delay to ensure all threads terminate
        std::thread([](){
            std::this_thread::sleep_for(std::chrono::seconds(2));
            std::exit(0);
        }).detach();
    }
}

void setup_all_synths() { // Renamed from setup_unison_synths
    std::lock_guard<std::mutex> lock(synthMutex);
    allSynths.clear(); // Changed from unisonSynths
    
    std::cout << "[DEBUG] Setting up " << num_modules << " modules, each with " << unisonVoices << " unison synth voices" << std::endl;

    for (int module_idx = 0; module_idx < num_modules; ++module_idx) {
        int midi_channel_for_module = module_idx + 1; // Module 0 -> Channel 1, etc.
        std::cout << "[DEBUG] Setting up module " << module_idx << " for MIDI channel " << midi_channel_for_module << std::endl;

        for (int v = 0; v < unisonVoices; ++v) {
            StereoDexed* s = new StereoDexed(16, SAMPLE_RATE); // Max 16 notes polyphony per instance
            // Check for duplicate pointer (should never happen, but for safety)
            if (std::find(allSynths.begin(), allSynths.end(), s) != allSynths.end()) { // Changed from unisonSynths
                std::cout << "[ERROR] Attempt to add duplicate Dexed pointer to allSynths!" << std::endl;
                delete s;
                continue;
            }
            // s->setMidiChannel(midi_channel_for_module); // REMOVED: Dexed instances don't have a persistent MIDI channel property
            s->resetControllers();
            s->setVelocityScale(MIDI_VELOCITY_SCALING_OFF);
            s->loadVoiceParameters(fmpiano_sysex);
            s->setTranspose(36); // Match old working example
            
            // Explicitly activate the synthesizer
            s->activate();
            
            // Debug: Play a test note on the first voice of the first module
            if (module_idx == 0 && v == 0) {
                std::cout << "[DEBUG] Playing test note on module " << module_idx << ", voice " << v << std::endl;
                s->keydown(60, 100);  // Note on, middle C, velocity 100
                std::cout << "[DEBUG] After test note, notes playing: " << s->getNumNotesPlaying() << std::endl;
            }
            
            float detuneCents = 0.0f;
            if (unisonVoices > 1) {
                if (unisonVoices == 2) detuneCents = (v == 0 ? -1.0f : 1.0f) * (unisonDetune / 2.0f);
                else if (unisonVoices == 3) detuneCents = (v - 1) * (unisonDetune / 1.0f); // e.g., -1, 0, 1 scaled
                else if (unisonVoices == 4) detuneCents = (v - 1.5f) * (unisonDetune / 1.5f); // e.g., -1.5, -0.5, 0.5, 1.5 scaled
            }
            int8_t masterTune = static_cast<int8_t>(detuneCents);
            s->setMasterTune(masterTune);
            
            // Operator detune can also be used for unison effect if desired,
            // but masterTune is more direct for overall detuning of a voice.
            // Example for OP detune (can be kept or removed based on desired sound):
            // int detuneValue = 7; // Neutral
            // if (unisonVoices > 1) {
            //    if (unisonVoices == 2) detuneValue = (v == 0) ? 0 : 14;
            //    else if (unisonVoices == 3) detuneValue = v * 7; // 0, 7, 14
            //    else if (unisonVoices == 4) detuneValue = v * 5; // 0, 5, 10, 15 (approx)
            // }
            // for (int op = 0; op < 6; ++op) s->setOPDetune(op, detuneValue);
            
            // Always apply global GAIN multiplier when setting gain
            #ifdef GAIN
            s->setGain(1.0f * GAIN);
            #else
            s->setGain(1.0f);
            #endif

            s->setPitchbendRange(2);
            s->setModWheelRange(99);
            s->setModWheelTarget(1);

            allSynths.push_back(s); // Changed from unisonSynths
        }
    }
    
    // Make the global synth pointer point to the first synth instance if any
    if (!allSynths.empty()) { // Changed from unisonSynths
        synth = allSynths[0]; // Changed from unisonSynths
        std::cout << "[DEBUG] Global synth pointer set to " << synth << " (first synth of first module)" << std::endl;
    } else {
        synth = nullptr;
    }
}

void cleanup_all_synths() { // Renamed from cleanup_unison_synths
    static bool already_cleaned = false;
    if (already_cleaned) {
        std::cout << "[DEBUG] cleanup_all_synths called more than once!" << std::endl;
        return;
    }
    already_cleaned = true;
    std::lock_guard<std::mutex> lock(synthMutex);
    std::cout << "[DEBUG] cleanup_all_synths: deleting " << allSynths.size() << " Dexed instances" << std::endl; // Changed from unisonSynths
    for (auto*& s : allSynths) { // Changed from unisonSynths
        if (s) {
            std::cout << "[DEBUG] deleting Dexed at " << s << std::endl;
            delete s;
            s = nullptr;
        } else {
            std::cout << "[DEBUG] Dexed pointer already null (possible double free attempt)" << std::endl;
        }
    }
    allSynths.clear(); // Changed from unisonSynths
    synth = nullptr;
}

bool DEBUG_ENABLED = false;

void parse_common_args(int argc, char* argv[], int& audioDev, int& midiDev, bool& useSynth) {
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
                      << "  --modules N          Set number of synth modules (1-16, default: 1), each on its own MIDI channel\n"
                      << "  --unison-voices N    Set number of unison voices per module (1-4, default: 1)\n"
                      << "  --unison-spread N    Set unison spread (0-99, default: 0)\n"
                      << "  --unison-detune N    Set unison detune (cents, default: 0)\n"
                      << "  --midi-data HEX_STRING   Load custom voice from sysex hex string\n"
                      << "  --debug              Enable debug output\n";
            exit(0);
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
        } else if (arg == "--modules" && i + 1 < argc) {
            num_modules = std::clamp(std::atoi(argv[++i]), 1, 16);
        } else if (arg == "--unison-voices" && i + 1 < argc) {
            unisonVoices = std::clamp(std::atoi(argv[++i]), 1, 4);
        } else if (arg == "--unison-spread" && i + 1 < argc) {
            unisonSpread = std::clamp((float)std::atof(argv[++i]), 0.0f, 99.0f);
        } else if (arg == "--unison-detune" && i + 1 < argc) {
            unisonDetune = (float)std::atof(argv[++i]);
        } else if (arg == "--midi-data" && i + 1 < argc) {
            std::string hexString = argv[++i];
            customVoiceSysex.clear();
            for (size_t j = 0; j < hexString.length(); j += 2) {
                if (hexString[j] == ' ') { j--; continue; }
                std::string byteString = hexString.substr(j, 2);
                if (byteString.length() < 2) break;
                try {
                    uint8_t byte = static_cast<uint8_t>(std::stoul(byteString, nullptr, 16));
                    customVoiceSysex.push_back(byte);
                } catch (...) {
                    customVoiceSysex.clear();
                    break;
                }
            }
        } else if (arg == "--debug") {
            DEBUG_ENABLED = true;
        }
    }
}

// Helper to find a free TCP port
int find_free_port() {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2,2), &wsaData);
#endif
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return 0;
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0; // Let OS pick a free port
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        return 0;
    }
    socklen_t len = sizeof(addr);
    getsockname(sock, (struct sockaddr*)&addr, &len);
    int port = ntohs(addr.sin_port);
#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif
    return port;
}

void fill_audio_buffers(bool useSynth) {
    static bool forceSine = (std::getenv("DEXED_TEST_SINE") != nullptr);
    std::vector<int16_t> monoBuffer(BUFFER_FRAMES); // This might be unused if direct stereo fill is done
    // std::vector<std::vector<int16_t>> unisonBuffers(unisonVoices, std::vector<int16_t>(BUFFER_FRAMES)); // This was per-module, handled in fill_audio_buffer now

    for (int i = 0; i < numBuffers; ++i) {
        if (forceSine) {
            double freq = 440.0;
            double phaseInc = 2.0 * M_PI * freq / SAMPLE_RATE;
            for (int j = 0; j < BUFFER_FRAMES; ++j) {
                int16_t sample = (int16_t)(std::sin(global_sine_phase) * 16000.0);
                global_sine_phase += phaseInc;
                if (global_sine_phase > 2.0 * M_PI) global_sine_phase -= 2.0 * M_PI;
                audioBuffers[i][2*j] = sample;
                audioBuffers[i][2*j+1] = sample;
                monoBuffer[j] = sample;
            }
        } else if (useSynth) {
            std::lock_guard<std::mutex> lock(synthMutex);
            if (allSynths.empty()) { // Early exit if no synths
                 for (int j = 0; j < BUFFER_FRAMES; ++j) {
                    audioBuffers[i][2*j] = 0;
                    audioBuffers[i][2*j+1] = 0;
                }
                continue;
            }

            std::vector<float> final_left(BUFFER_FRAMES, 0.0f);
            std::vector<float> final_right(BUFFER_FRAMES, 0.0f);
            std::vector<int16_t> single_synth_mono_output(BUFFER_FRAMES);

            for (int module_idx = 0; module_idx < num_modules; ++module_idx) {
                std::vector<float> module_left(BUFFER_FRAMES, 0.0f);
                std::vector<float> module_right(BUFFER_FRAMES, 0.0f);

                for (int voice_in_module_idx = 0; voice_in_module_idx < unisonVoices; ++voice_in_module_idx) {
                    int overall_synth_idx = module_idx * unisonVoices + voice_in_module_idx;
                    if (overall_synth_idx >= allSynths.size() || !allSynths[overall_synth_idx]) continue;

                    allSynths[overall_synth_idx]->getSamples(single_synth_mono_output.data(), BUFFER_FRAMES);
                    
                    float pan = 0.0f;
                    if (unisonVoices > 1) { // Ensure unisonVoices > 1 for panning logic
                        if (unisonVoices == 2) pan = (voice_in_module_idx == 0 ? -1.0f : 1.0f) * (unisonSpread / 99.0f);
                        else if (unisonVoices == 3) pan = (voice_in_module_idx - 1.0f) * (unisonSpread / 99.0f); // -1, 0, 1 scaled
                        else if (unisonVoices == 4) pan = (voice_in_module_idx - 1.5f) * (unisonSpread / 99.0f); // -1.5, -0.5, 0.5, 1.5 scaled
                        pan = std::clamp(pan, -1.0f, 1.0f); // Ensure pan is within [-1, 1]
                    }

                    float panL_factor = std::cos((pan * 0.5f + 0.5f) * M_PI / 2.0f);
                    float panR_factor = std::sin((pan * 0.5f + 0.5f) * M_PI / 2.0f);

                    for (int j = 0; j < BUFFER_FRAMES; ++j) {
                        float current_sample = static_cast<float>(single_synth_mono_output[j]);
                        module_left[j] += current_sample * panL_factor;
                        module_right[j] += current_sample * panR_factor;
                    }
                }
                for (int j = 0; j < BUFFER_FRAMES; ++j) {
                    final_left[j] += module_left[j];
                    final_right[j] += module_right[j];
                }
            }
            
            if (DEBUG_ENABLED) {
                std::cout << "[DEBUG] mixed final_left: ";
                for (int k = 0; k < 8; ++k) std::cout << final_left[k] << " ";
                std::cout << std::endl;
                std::cout << "[DEBUG] mixed final_right: ";
                for (int k = 0; k < 8; ++k) std::cout << final_right[k] << " ";
                std::cout << std::endl;
            }
            // Fill the stereo audioBuffers directly
            for (int j = 0; j < BUFFER_FRAMES; ++j) {
                audioBuffers[i][2*j]   = std::clamp(static_cast<int>(final_left[j]), -32768, 32767);
                audioBuffers[i][2*j+1] = std::clamp(static_cast<int>(final_right[j]), -32768, 32767);
            }

        } else {
            double freq = 440.0;
            double phaseInc = 2.0 * M_PI * freq / SAMPLE_RATE;
            for (int j = 0; j < BUFFER_FRAMES; ++j) {
                int16_t sample = (int16_t)(std::sin(global_sine_phase) * 16000.0);
                global_sine_phase += phaseInc;
                if (global_sine_phase > 2.0 * M_PI) global_sine_phase -= 2.0 * M_PI;
                audioBuffers[i][2*j] = sample;
                audioBuffers[i][2*j+1] = sample;
                monoBuffer[j] = sample;
            }
        }
    }
    if (DEBUG_ENABLED) std::cout << "[DEBUG] Exiting fill_audio_buffers" << std::endl;
}

void fill_audio_buffer(int i, bool useSynth) {
    static bool forceSine = (std::getenv("DEXED_TEST_SINE") != nullptr);
    static bool noAutoNotes = (std::getenv("DEXED_NO_AUTO_NOTES") != nullptr);
    // std::vector<int16_t> monoBuffer(BUFFER_FRAMES); // No longer needed for final output if stereo
    // std::vector<std::vector<int16_t>> unisonBuffers(unisonVoices, std::vector<int16_t>(BUFFER_FRAMES)); // Replaced by single_synth_mono_output logic

    #ifdef DEBUG_ENABLED
    std::cout << "[AUDIO DEBUG] fill_audio_buffer: buffer=" << i 
              << ", useSynth=" << (useSynth ? "true" : "false") 
              << ", num_modules=" << num_modules
              << ", unisonVoices=" << unisonVoices << std::endl;
    #endif
    
    if (!useSynth || forceSine) {
        // Generate sine wave for testing
        static double phase = 0.0;
        double freq = 440.0; // A4 440 Hz
        double amplitude = 16000.0;
        #ifdef DEBUG_ENABLED
        std::cout << "[AUDIO] Generating sine wave with amplitude " << amplitude << std::endl;
        #endif
        for (int j = 0; j < BUFFER_FRAMES; ++j) {
            int16_t sample = (int16_t)(std::sin(phase) * amplitude);
            phase += 2.0 * M_PI * freq / SAMPLE_RATE;
            if (phase > 2.0 * M_PI) phase -= 2.0 * M_PI;
            audioBuffers[i][2*j] = sample;     // Left channel
            audioBuffers[i][2*j+1] = sample;   // Right channel (same as left for mono sine)
        }
    } else if (useSynth) {
        std::lock_guard<std::mutex> lock(synthMutex);
        if (allSynths.empty()) { // Early exit if no synths
             for (int j = 0; j < BUFFER_FRAMES; ++j) {
                audioBuffers[i][2*j] = 0;
                audioBuffers[i][2*j+1] = 0;
            }
            return; // Exit function early
        }

        std::vector<float> final_mixed_left(BUFFER_FRAMES, 0.0f);
        std::vector<float> final_mixed_right(BUFFER_FRAMES, 0.0f);
        std::vector<int16_t> single_synth_mono_output(BUFFER_FRAMES);

        for (int module_idx = 0; module_idx < num_modules; ++module_idx) {
            std::vector<float> current_module_left(BUFFER_FRAMES, 0.0f);
            std::vector<float> current_module_right(BUFFER_FRAMES, 0.0f);

            // Calculate gain compensation for unison voices only (per module)
            float unison_gain = 1.0f;
            if (unisonVoices > 1) {
                unison_gain = 1.0f / sqrtf(static_cast<float>(unisonVoices));
            }

            for (int voice_in_module_idx = 0; voice_in_module_idx < unisonVoices; ++voice_in_module_idx) {
                int overall_synth_idx = module_idx * unisonVoices + voice_in_module_idx;
                if (overall_synth_idx >= allSynths.size() || !allSynths[overall_synth_idx]) {
                    #ifdef DEBUG_ENABLED
                    std::cout << "[AUDIO DEBUG] Skipping synth index " << overall_synth_idx << " (out of bounds or null)" << std::endl;
                    #endif
                    continue;
                }
                StereoDexed* current_synth = allSynths[overall_synth_idx];
                #ifdef DEBUG_ENABLED
                std::cout << "[AUDIO DEBUG] Module " << module_idx << ", Voice " << voice_in_module_idx 
                          << " (Overall Idx " << overall_synth_idx << ", Ptr " << current_synth 
                          << "): Calling getSamples. Notes playing: " << current_synth->getNumNotesPlaying() << std::endl;
                #endif
                
                current_synth->getSamples(single_synth_mono_output.data(), BUFFER_FRAMES);
                
                #ifdef DEBUG_ENABLED
                // std::cout << "[AUDIO DEBUG] single_synth_mono_output first 8: ";
                // for (int k = 0; k < 8 && k < BUFFER_FRAMES; ++k) std::cout << single_synth_mono_output[k] << " ";
                // std::cout << std::endl;
                #endif
                
                float pan = 0.0f;
                 if (unisonVoices > 1) { // Panning only if multiple unison voices
                    if (unisonVoices == 2) pan = (voice_in_module_idx == 0 ? -1.0f : 1.0f) * (unisonSpread / 99.0f);
                    else if (unisonVoices == 3) pan = (voice_in_module_idx - 1.0f) * (unisonSpread / 99.0f); // Results in -1, 0, 1 scaled
                    else if (unisonVoices == 4) pan = (voice_in_module_idx - 1.5f) * (unisonSpread / 99.0f); // Results in -1.5, -0.5, 0.5, 1.5 scaled
                    pan = std::clamp(pan, -1.0f, 1.0f); // Ensure pan is within [-1, 1]
                }

                float panL_factor = std::cos((pan * 0.5f + 0.5f) * M_PI / 2.0f);
                float panR_factor = std::sin((pan * 0.5f + 0.5f) * M_PI / 2.0f);

                for (int j = 0; j < BUFFER_FRAMES; ++j) {
                    float current_sample_float = static_cast<float>(single_synth_mono_output[j]);
                    current_sample_float *= unison_gain; // Apply gain compensation only for unison voices
                    current_module_left[j] += current_sample_float * panL_factor;
                    current_module_right[j] += current_sample_float * panR_factor;
                }
            }
            
            // Add this module's stereo output to the final mix
            for (int j = 0; j < BUFFER_FRAMES; ++j) {
                final_mixed_left[j] += current_module_left[j];
                final_mixed_right[j] += current_module_right[j];
            }
        }
        
        #ifdef DEBUG_ENABLED
        // std::cout << "[AUDIO DEBUG] final_mixed_left first 8: ";
        // for (int k = 0; k < 8 && k < BUFFER_FRAMES; ++k) std::cout << final_mixed_left[k] << " ";
        // std::cout << std::endl;
        // std::cout << "[AUDIO DEBUG] final_mixed_right first 8: ";
        // for (int k = 0; k < 8 && k < BUFFER_FRAMES; ++k) std::cout << final_mixed_right[k] << " ";
        // std::cout << std::endl;
        #endif        

        // Fill the stereo audioBuffers directly
        for (int j = 0; j < BUFFER_FRAMES; ++j) {
            audioBuffers[i][2*j]   = std::clamp(static_cast<int>(final_mixed_left[j]), -32768, 32767);
            audioBuffers[i][2*j+1] = std::clamp(static_cast<int>(final_mixed_right[j]), -32768, 32767);
        }

    } else { // Fallback: silent buffer if not sine and not synth (should not happen with default useSynth=true)
        for (int j = 0; j < BUFFER_FRAMES; ++j) {
            audioBuffers[i][2*j] = 0;
            audioBuffers[i][2*j+1] = 0;
        }
    }
    
    // The old monoBuffer transfer is removed as we fill stereo directly.
    // for (int j = 0; j < BUFFER_FRAMES; ++j) {
    //    audioBuffers[i][2*j] = monoBuffer[j];
    //    audioBuffers[i][2*j+1] = monoBuffer[j];
    // }
    
    #ifdef DEBUG_ENABLED
    // std::cout << "[AUDIO OUTPUT] Buffer " << i << " first 8 stereo samples (L/R pairs): ";
    // for (int k = 0; k < 4 && k < BUFFER_FRAMES; ++k) std::cout << audioBuffers[i][2*k] << "/" << audioBuffers[i][2*k+1] << " ";
    // std::cout << std::endl;
    #endif
}

void handle_midi_channel_print(uint8_t channel) {
    std::cout << (int)channel;
}

// Helper: Extract DX7 single voice data from sysex
bool extract_dx7_voice_data(const std::vector<uint8_t>& sysex, uint8_t* out, size_t out_size) {
    // Typical DX7 single voice dump: F0 43 00 09 20 ... 155 bytes ... F7
    // Or: F0 43 10 00 01 1B ... 155 bytes ... F7
    // Find F0 at start, F7 at end, and at least 32 or 155 bytes in between
    if (sysex.size() < 34) return false;
    if (sysex.front() != 0xF0 || sysex.back() != 0xF7) return false;
    // Try to find the start of the actual voice data (skip header)
    size_t data_start = 0;
    // Yamaha usually: F0 43 00 09 20 ... or F0 43 10 00 01 1B ...
    // Find first byte after header (usually 5 or 6 bytes header)
    // We'll look for the first byte after the header that could be the start of the 32/155 bytes
    // For now, try offsets 5 and 6
    size_t possible_starts[] = {5, 6};
    for (size_t off : possible_starts) {
        if (sysex.size() > off + out_size && sysex.size() - off - 1 >= out_size) {
            // Looks like enough data
            std::memcpy(out, &sysex[off], out_size);
            return true;
        }
    }
    // Fallback: try to find a 32/155-byte region before F7
    if (sysex.size() - 1 >= out_size) {
        std::memcpy(out, &sysex[sysex.size() - 1 - out_size], out_size);
        return true;
    }
    return false;
}

void stop_socket_server(MidiSocketServer& server, int port) {
#ifdef _WIN32
    // Connect to the socket to unblock accept()
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s != INVALID_SOCKET) {
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port);
        connect(s, (sockaddr*)&addr, sizeof(addr));
        closesocket(s);
    }
#else
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s >= 0) {
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port);
        connect(s, (sockaddr*)&addr, sizeof(addr));
        close(s);
    }
#endif
    server.stop();
}

int main_common_entry(int argc, char* argv[], PlatformHooks hooks) {
    if (DEBUG_ENABLED) std::cout << "[DEBUG] Entered main_common_entry" << std::endl;
    int audioDev = 0;
    int midiDev = 0;
    bool useSynth = true;
    std::signal(SIGINT, signal_handler);
    if (DEBUG_ENABLED) std::cout << "[DEBUG] Before parse_common_args" << std::endl;
    parse_common_args(argc, argv, audioDev, midiDev, useSynth);
    if (DEBUG_ENABLED) std::cout << "[DEBUG] After parse_common_args" << std::endl;
    std::cout << "[INFO] Number of modules: " << num_modules << std::endl;
    std::cout << "[INFO] Unison voices per module: " << unisonVoices << std::endl;


    // Load custom voice if provided
    if (!customVoiceSysex.empty()) {
        size_t n = (customVoiceSysex.size() < sizeof(fmpiano_sysex)) ? customVoiceSysex.size() : sizeof(fmpiano_sysex);
        std::memcpy(fmpiano_sysex, customVoiceSysex.data(), n);
        std::cout << "[INFO] Loaded custom voice: copied " << n << " bytes directly from --voice argument." << std::endl;
        // Optional: print first 8 bytes for debug
        std::cout << "[VOICE DEBUG] First 8 bytes: ";
        for (size_t i = 0; i < 8 && i < n; ++i) std::cout << (int)fmpiano_sysex[i] << " ";
        std::cout << std::endl;
    }

    std::string iniFilePath;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.size() > 4 && arg.substr(arg.size() - 4) == ".ini") {
            iniFilePath = arg;
            break;
        }
    }
    if (!iniFilePath.empty()) {
        // Count VoiceDataN entries to determine number of modules
        std::ifstream iniFile(iniFilePath);
        int detectedModules = 0;
        std::string line;
        while (std::getline(iniFile, line)) {
            for (int n = 1; n <= 8; ++n) {
                std::string key = "VoiceData" + std::to_string(n) + "=";
                if (line.find(key) == 0) {
                    detectedModules = n;
                }
            }
        }
        if (detectedModules > 0) {
            num_modules = detectedModules;
            std::cout << "[INFO] Detected " << num_modules << " modules from INI file." << std::endl;
        }
        std::lock_guard<std::mutex> lock(synthMutex);
        allSynths.clear();
        for (int module_idx = 0; module_idx < num_modules; ++module_idx) {
            for (int v = 0; v < unisonVoices; ++v) {
                StereoDexed* s = new StereoDexed(16, SAMPLE_RATE);
                load_performance_ini(iniFilePath, s, module_idx + 1, DEBUG_ENABLED);
                s->activate();
                allSynths.push_back(s);
            }
        }
        if (!allSynths.empty()) {
            synth = allSynths[0];
        } else {
            synth = nullptr;
        }
    } else {
        setup_all_synths();
    }
    // Allocate audio buffers ONCE at startup
    audioBuffers.resize(numBuffers);
    for (int i = 0; i < numBuffers; ++i) {
        audioBuffers[i].resize(BUFFER_FRAMES * 2);
        if (DEBUG_ENABLED) std::cout << "[DEBUG] audioBuffers[" << i << "].size() = " << audioBuffers[i].size() << std::endl;
    }
    // Always open audio and MIDI devices at startup
    hooks.open_audio(audioDev);
    // Pre-fill all audio buffers before starting the audio thread (Windows)
#if defined(_WIN32)
    extern void win_prefill_audio_buffers(bool useSynth);
    win_prefill_audio_buffers(useSynth);
#elif defined(__APPLE__)
    mac_prefill_audio_buffers(useSynth, hooks);
#elif defined(__linux__)
    linux_prefill_audio_buffers(useSynth, hooks);
#endif
    hooks.open_midi(midiDev);
    // Parse --midi-socket-port N from args
    int midiSocketPort = 50007; // Default port
    // Parse --midi-socket-port N from args
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--midi-socket-port" || arg == "--midi-port") && i + 1 < argc) {
            midiSocketPort = std::atoi(argv[++i]);
        }
    }
    std::cout << "[INFO] MIDI UDP server will listen on 127.0.0.1:" << midiSocketPort << std::endl;
    // Start MIDI UDP server (UDP, localhost:midiSocketPort)
    MidiUdpServer midiServer(midiSocketPort, [](const uint8_t* data, size_t len) {
        printf("[DEBUG] UDP callback triggered, len=%zu\n", len);
        if (len > 0) {
            printf("[DEBUG] First byte: 0x%02X\n", data[0]);
        }
        if (len > 0) {
            std::lock_guard<std::mutex> lock(synthMutex);
            if (allSynths.empty() || num_modules == 0) return;

            uint8_t status_byte = data[0];
            if (status_byte == 0xF0) { // SysEx Start
                if (len > 1 && data[len - 1] == 0xF7) // Check for EOX (End of SysEx)
                {
                    int target_channel_one_based = 1; // Default to channel 1 for SysEx messages
                    if (len >= 3 && data[1] == 0x43) { // Yamaha Manufacturer ID
                        uint8_t yamaha_substatus_byte = data[2];
                        if ((yamaha_substatus_byte & 0xF0) == 0x10) { // Channel is specified in sub-status
                            target_channel_one_based = (yamaha_substatus_byte & 0x0F) + 1;
                        }
                    }

                    if (target_channel_one_based >= 1 && target_channel_one_based <= num_modules) {
                        int target_module_idx = target_channel_one_based - 1;
                        for (int voice_idx = 0; voice_idx < unisonVoices; ++voice_idx) {
                            int synth_idx = target_module_idx * unisonVoices + voice_idx;
                            if (synth_idx < allSynths.size() && allSynths[synth_idx]) {
                                allSynths[synth_idx]->midiDataHandler(target_channel_one_based, const_cast<uint8_t*>(data), static_cast<int>(len));
                            }
                        }
                    }
                }
            } else if (status_byte >= 0xF1 && status_byte <= 0xFF && status_byte != 0xF7) { 
                for (int module_idx = 0; module_idx < num_modules; ++module_idx) {
                    int module_listen_channel = module_idx + 1;
                    for (int voice_idx = 0; voice_idx < unisonVoices; ++voice_idx) {
                        int synth_idx = module_idx * unisonVoices + voice_idx;
                        if (synth_idx < allSynths.size() && allSynths[synth_idx]) {
                            allSynths[synth_idx]->midiDataHandler(module_listen_channel, const_cast<uint8_t*>(data), static_cast<int>(len));
                        }
                    }
                }
            } else { // Channel messages (0x8n to 0xEn)
                for (size_t i = 0; i < len; ) {
                    if (i + 1 > len) break; 
                    uint8_t current_msg_status_byte = data[i];
                    uint8_t msg_type_nibble = current_msg_status_byte & 0xF0;
                    int current_msg_len = 0;
                    if (msg_type_nibble >= 0x80 && msg_type_nibble <= 0xE0) {
                        if (msg_type_nibble == 0xC0 || msg_type_nibble == 0xD0) {
                            current_msg_len = 2;
                        } else { 
                            current_msg_len = 3;
                        }
                        if (i + current_msg_len > len) break;
                        int msg_midi_channel_zero_based = current_msg_status_byte & 0x0F;
                        int incoming_channel_one_based = msg_midi_channel_zero_based + 1;
                        printf("[DEBUG] Channel message: status=0x%02X, channel(one-based)=%d\n", current_msg_status_byte, incoming_channel_one_based);

                        if (incoming_channel_one_based == 16) { // Omni Channel (16)
                            uint8_t original_msg_type = current_msg_status_byte & 0xF0;
                            for (int module_idx = 0; module_idx < num_modules; ++module_idx) {
                                int module_channel_zero_based = module_idx;
                                uint8_t modified_midi_message[3];
                                memcpy(modified_midi_message, data + i, current_msg_len);
                                modified_midi_message[0] = original_msg_type | module_channel_zero_based;
                                printf("[DEBUG] OMNI: module %d, midiDataHandler ch=%d, status=0x%02X, d1=%d, d2=%d, len=%d\n",
                                    module_idx + 1, module_idx + 1, modified_midi_message[0],
                                    current_msg_len > 1 ? modified_midi_message[1] : -1,
                                    current_msg_len > 2 ? modified_midi_message[2] : -1,
                                    current_msg_len);
                                for (int voice_idx = 0; voice_idx < unisonVoices; ++voice_idx) {
                                    int synth_idx = module_idx * unisonVoices + voice_idx;
                                    if (synth_idx < allSynths.size() && allSynths[synth_idx]) {
                                        allSynths[synth_idx]->midiDataHandler(module_idx + 1, modified_midi_message, current_msg_len);
                                    }
                                }
                            }
                        } else { // Specific Channel (1-15)
                            if (incoming_channel_one_based >= 1 && incoming_channel_one_based <= num_modules) {
                                int target_module_idx = incoming_channel_one_based - 1;
                                for (int voice_idx = 0; voice_idx < unisonVoices; ++voice_idx) {
                                    int synth_idx = target_module_idx * unisonVoices + voice_idx;
                                    if (synth_idx < allSynths.size() && allSynths[synth_idx]) {
                                        uint8_t* temp_midi_data_ptr = const_cast<uint8_t*>(data + i);
                                        allSynths[synth_idx]->midiDataHandler(incoming_channel_one_based, temp_midi_data_ptr, current_msg_len);
                                    }
                                }
                            }
                        }
                        i += current_msg_len;
                    } else {
                        i++;
                        continue;
                    }
                }
            }
        }
    });
    midiServer.start();
    std::thread audio([&]() {
        try {
            int bufferIndex = 0;
            // std::cout << "[AUDIO THREAD] Starting audio thread loop." << std::endl;
            while (running) {
                // std::cout << "[AUDIO THREAD] Top of loop, bufferIndex=" << bufferIndex << std::endl;
#if defined(_WIN32)
                extern std::vector<WAVEHDR> waveHeaders;
                while (waveHeaders[bufferIndex].dwFlags & WHDR_INQUEUE) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    if (!running) return;
                }
#endif
                fill_audio_buffer(bufferIndex, useSynth);
                // std::cout << "[AUDIO THREAD] After fill_audio_buffer." << std::endl;
                hooks.submit_audio_buffer(bufferIndex);
                // std::cout << "[AUDIO THREAD] After submit_audio_buffer." << std::endl;
                bufferIndex = (bufferIndex + 1) % numBuffers;
            }
            // std::cout << "[AUDIO THREAD] Exiting audio thread loop." << std::endl;
        } catch (const std::exception& ex) {
            std::cerr << "[AUDIO THREAD EXCEPTION] " << ex.what() << std::endl;
            running = false;
        } catch (...) {
            std::cerr << "[AUDIO THREAD EXCEPTION] Unknown exception." << std::endl;
            running = false;
        }
    });
    std::cout << "[INFO] Synth_Dexed running. Press Ctrl+C to quit.\n";
    std::cout << "[INFO] Current settings:" << std::endl;
    std::cout << "  SAMPLE_RATE: " << SAMPLE_RATE << std::endl;
    std::cout << "  BUFFER_FRAMES: " << BUFFER_FRAMES << std::endl;
    std::cout << "  numBuffers: " << numBuffers << std::endl;
    std::cout << "  unisonVoices: " << unisonVoices << std::endl;
    std::cout << "  unisonSpread: " << unisonSpread << std::endl;
    std::cout << "  unisonDetune: " << unisonDetune << std::endl;
    // Send custom MIDI data if provided
    if (!customVoiceSysex.empty()) {
        std::cout << "[INFO] Sending custom MIDI data from --midi-data argument (" << customVoiceSysex.size() << " bytes) to synth engine." << std::endl;
        // Send to all voices of the first module (channel 1) by default for custom voice loading.
        // Or, this could be broadcast if the SysEx is general.
        // For now, let's send it to module 0 (channel 1).
        if (num_modules > 0) {
            for (int voice_idx = 0; voice_idx < unisonVoices; ++voice_idx) {
                 int synth_idx = 0 * unisonVoices + voice_idx; // First module
                 if (synth_idx < allSynths.size() && allSynths[synth_idx]) {
                    allSynths[synth_idx]->midiDataHandler(1, customVoiceSysex.data(), (int)customVoiceSysex.size());
                 }
            }
        }
    }
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "[MAIN THREAD] Exiting main loop." << std::endl;
    running = false;
    audio.join();
    hooks.close_audio();
    hooks.close_midi();
    midiServer.stop();
    cleanup_all_synths(); // Changed from cleanup_unison_synths
    std::cout << "[MAIN THREAD] Program exit." << std::endl;
    if (DEBUG_ENABLED) std::cout << "[DEBUG] main_common_entry: about to return from main_common_entry" << std::endl;
    return 0;
}

void debug_audio_buffers(bool useSynth) {
    // Define bufferIndex locally - using a default value of 0
    static int bufferIndex = 0;
    
    // Debug code only - this function isn't called in the actual program
    std::cout << "[DEBUG] debug_audio_buffers: synth=" << synth << std::endl;
    if (synth) {
        synth->getSamples(audioBuffers[bufferIndex].data(), BUFFER_FRAMES);
        std::cout << "[DEBUG] getSamples called, first sample: " << audioBuffers[bufferIndex][0] << std::endl;
        
        // Increment bufferIndex for next call, wrapping around if needed
        bufferIndex = (bufferIndex + 1) % numBuffers;
    } else {
        std::cout << "[DEBUG] synth is nullptr!" << std::endl;
    }
}

#endif