#ifndef ARDUINO

#include "main_common.h"
#include "dexed.h"
#include <iostream>
#include <csignal>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>
#include <string> // Add this line

#if defined(_WIN32)
#include <windows.h>
#include <mmsystem.h>
#endif

#if defined(__linux__)
#include "main_linux.h"
#endif

#if defined(__APPLE__)
#include "main_mac.h"
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

std::vector<uint8_t> customVoiceSysex; // Added for custom voice
int unisonVoices = 1;
float unisonSpread = 0.0f;
float unisonDetune = 0.0f;
std::vector<Dexed*> unisonSynths;
unsigned int SAMPLE_RATE = 48000;
unsigned int BUFFER_FRAMES = 1024;
int numBuffers = 4;
std::atomic<bool> running{true};
Dexed* synth = nullptr;
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
    if (signal == SIGINT) {
        running = false;
        std::cout << "\n[INFO] Caught Ctrl+C, exiting..." << std::endl;
    }
}

void setup_unison_synths() {
    std::lock_guard<std::mutex> lock(synthMutex);
    unisonSynths.clear();
    int voices_to_create = (unisonVoices > 1) ? unisonVoices : 1;
    
    std::cout << "[DEBUG] Setting up " << voices_to_create << " unison synth voices" << std::endl;
    
    for (int v = 0; v < voices_to_create; ++v) {
        Dexed* s = new Dexed(16, SAMPLE_RATE);
        s->resetControllers();
        s->setVelocityScale(MIDI_VELOCITY_SCALING_OFF);
        s->loadVoiceParameters(fmpiano_sysex);
        s->setTranspose(36); // Match old working example
        
        // Explicitly activate the synthesizer
        s->activate();
        
        // Debug: Play a test note to ensure the synth is producing sound
        if (v == 0) {
            std::cout << "[DEBUG] Playing test note on synth " << v << std::endl;
            s->keydown(60, 100);  // Note on, middle C, velocity 100
            std::cout << "[DEBUG] After test note, notes playing: " << s->getNumNotesPlaying() << std::endl;
        }
        
        float detuneCents = 0.0f;
        if (unisonVoices > 1) {
            if (unisonVoices == 2) detuneCents = (v == 0 ? -1.0f : 1.0f) * (unisonDetune / 2.0f);
            else if (unisonVoices == 3) detuneCents = (v - 1) * (unisonDetune / 1.0f);
            else if (unisonVoices == 4) detuneCents = (v - 1.5f) * (unisonDetune / 1.5f);
        }
        int8_t masterTune = static_cast<int8_t>(detuneCents);
        s->setMasterTune(masterTune);
        int detuneValue = 7;
        if (unisonVoices > 1) {
            if (unisonVoices == 2) detuneValue = (v == 0) ? 0 : 14;
            else if (unisonVoices == 3) detuneValue = v * 7;
            else if (unisonVoices == 4) detuneValue = v * 5;
        }
        for (int op = 0; op < 6; ++op) s->setOPDetune(op, detuneValue);
        
        // Ensure gain is properly set to a value that will produce audible output
        s->setGain(2.0f);
        
        unisonSynths.push_back(s);
    }
    
    // Make the global synth pointer point to the first unison voice
    if (!unisonSynths.empty()) {
        synth = unisonSynths[0];
        std::cout << "[DEBUG] Global synth pointer set to " << synth << std::endl;
    }
}

void cleanup_unison_synths() {
    std::lock_guard<std::mutex> lock(synthMutex);
    for (auto* s : unisonSynths) delete s;
    unisonSynths.clear();
    synth = nullptr;
}

bool DEBUG_ENABLED = false;

void parse_common_args(int argc, char* argv[], int& audioDev, int& midiDev, bool& useSynth) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\\n"
                      << "  -h, --help           Show this help message and exit\\n"
                      << "  -a, --audio-device N Select audio device index (default: 0)\\n"
                      << "  -m, --midi-device N  Select MIDI input device index (default: 0)\\n"
                      << "  --sample-rate N      Set sample rate (default: 48000)\\n"
                      << "  --buffer-frames N    Set audio buffer size in frames (default: 1024)\\n"
                      << "  --num-buffers N      Set number of audio buffers (default: 4)\\n"
                      << "  --sine               Output test sine wave instead of synth\\n"
                      << "  --synth              Use synth (default)\\n"
                      << "  --unison-voices N    Set number of unison voices (1-4, default: 1)\\n"
                      << "  --unison-spread N    Set unison spread (0-99, default: 0)\\n"
                      << "  --unison-detune N    Set unison detune (cents, default: 0)\\n"
                      << "  --midi-data HEX_STRING   Load custom voice from sysex hex string\\n"
                      << "  --debug              Enable debug output\\n";
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

void fill_audio_buffers(bool useSynth) {
    static bool forceSine = (std::getenv("DEXED_TEST_SINE") != nullptr);
    std::vector<int16_t> monoBuffer(BUFFER_FRAMES);
    std::vector<std::vector<int16_t>> unisonBuffers(unisonVoices, std::vector<int16_t>(BUFFER_FRAMES));
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
            std::vector<float> left(BUFFER_FRAMES, 0.0f), right(BUFFER_FRAMES, 0.0f);
            for (int v = 0; v < unisonVoices; ++v) {
                if (DEBUG_ENABLED) std::cout << "[DEBUG] Calling getSamples for unison voice " << v << std::endl;
                unisonSynths[v]->getSamples(unisonBuffers[v].data(), BUFFER_FRAMES);
                if (DEBUG_ENABLED) {
                    std::cout << "[DEBUG] unisonBuffers[" << v << "]: ";
                    for (int k = 0; k < 8; ++k) std::cout << unisonBuffers[v][k] << " ";
                    std::cout << std::endl;
                }
                float pan = 0.0f;
                if (unisonVoices == 2) pan = (v == 0 ? -1.0f : 1.0f) * (unisonSpread / 99.0f);
                else if (unisonVoices == 3) pan = (v - 1) * (unisonSpread / 99.0f);
                else if (unisonVoices == 4) pan = (v - 1.5f) * (unisonSpread / 99.0f);
                float panL = std::cos((pan * 0.5f + 0.5f) * M_PI / 2.0f);
                float panR = std::sin((pan * 0.5f + 0.5f) * M_PI / 2.0f);
                for (int j = 0; j < BUFFER_FRAMES; ++j) {
                    left[j] += unisonBuffers[v][j] * panL;
                    right[j] += unisonBuffers[v][j] * panR;
                }
            }
            if (DEBUG_ENABLED) {
                std::cout << "[DEBUG] mixed left: ";
                for (int k = 0; k < 8; ++k) std::cout << left[k] << " ";
                std::cout << std::endl;
                std::cout << "[DEBUG] mixed right: ";
                for (int k = 0; k < 8; ++k) std::cout << right[k] << " ";
                std::cout << std::endl;
            }
            for (int j = 0; j < BUFFER_FRAMES; ++j) {
                audioBuffers[i][2*j] = std::clamp((int)left[j], -32768, 32767);
                audioBuffers[i][2*j+1] = std::clamp((int)right[j], -32768, 32767);
            }
            if (DEBUG_ENABLED) {
                std::cout << "[DEBUG] audioBuffers[" << i << "]: ";
                for (int k = 0; k < 16; ++k) std::cout << audioBuffers[i][k] << " ";
                std::cout << std::endl;
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
    std::vector<int16_t> monoBuffer(BUFFER_FRAMES);
    std::vector<std::vector<int16_t>> unisonBuffers(unisonVoices, std::vector<int16_t>(BUFFER_FRAMES));
    
    #ifdef DEBUG_ENABLED
    std::cout << "[AUDIO DEBUG] fill_audio_buffer: buffer=" << i 
              << ", useSynth=" << (useSynth ? "true" : "false") 
              << ", synth=" << synth 
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
            monoBuffer[j] = sample;
        }
    } else if (useSynth) {
        std::lock_guard<std::mutex> lock(synthMutex);
        std::vector<float> left(BUFFER_FRAMES, 0.0f), right(BUFFER_FRAMES, 0.0f);
        
        // Get samples from all unison voices and mix them
        for (int v = 0; v < unisonVoices; ++v) {
            #ifdef DEBUG_ENABLED
            std::cout << "[AUDIO DEBUG] Calling getSamples for unison voice " << v
                     << ", synth pointer=" << unisonSynths[v] 
                     << ", notes playing=" << unisonSynths[v]->getNumNotesPlaying() 
                     << std::endl;
            #endif
            
            // Get audio samples from the synth
            unisonSynths[v]->getSamples(unisonBuffers[v].data(), BUFFER_FRAMES);
            
            #ifdef DEBUG_ENABLED
            std::cout << "[AUDIO DEBUG] unisonBuffers[" << v << "] first 8 samples: ";
            for (int k = 0; k < 8 && k < BUFFER_FRAMES; ++k) std::cout << unisonBuffers[v][k] << " ";
            std::cout << std::endl;
            #endif
            
            float pan = 0.0f;
            if (unisonVoices == 2) pan = (v == 0 ? -1.0f : 1.0f) * (unisonSpread / 99.0f);
            else if (unisonVoices == 3) pan = (v - 1) * (unisonSpread / 99.0f);
            else if (unisonVoices == 4) pan = (v - 1.5f) * (unisonSpread / 99.0f);
            float panL = std::cos((pan * 0.5f + 0.5f) * M_PI / 2.0f);
            float panR = std::sin((pan * 0.5f + 0.5f) * M_PI / 2.0f);
            for (int j = 0; j < BUFFER_FRAMES; ++j) {
                left[j] += unisonBuffers[v][j] * panL;
                right[j] += unisonBuffers[v][j] * panR;
            }
        }
        
        #ifdef DEBUG_ENABLED
        std::cout << "[AUDIO DEBUG] mixed left channel first 8 samples: ";
        for (int k = 0; k < 8 && k < BUFFER_FRAMES; ++k) std::cout << left[k] << " ";
        std::cout << std::endl;
        
        std::cout << "[AUDIO DEBUG] mixed right channel first 8 samples: ";
        for (int k = 0; k < 8 && k < BUFFER_FRAMES; ++k) std::cout << right[k] << " ";
        std::cout << std::endl;
        #endif        

        for (int j = 0; j < BUFFER_FRAMES; ++j) {
            monoBuffer[j] = std::clamp((int)left[j], -32768, 32767);
        }
    } else {
        // Default fallback case
        static double phase = 0.0;
        double freq = 440.0;
        double phaseInc = 2.0 * M_PI * freq / SAMPLE_RATE;
        for (int j = 0; j < BUFFER_FRAMES; ++j) {
            int16_t sample = (int16_t)(std::sin(phase) * 16000.0);
            phase += phaseInc;
            if (phase > 2.0 * M_PI) phase -= 2.0 * M_PI;
            monoBuffer[j] = sample;
        }
    }
    
    // Transfer from mono buffer to final stereo output buffer
    for (int j = 0; j < BUFFER_FRAMES; ++j) {
        audioBuffers[i][2*j] = monoBuffer[j];
        audioBuffers[i][2*j+1] = monoBuffer[j];
    }
    
    #ifdef DEBUG_ENABLED
    std::cout << "[AUDIO OUTPUT] Buffer " << i << " first 8 samples: ";
    for (int k = 0; k < 8; ++k) std::cout << audioBuffers[i][2*k] << " ";
    std::cout << std::endl;
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

int main_common_entry(int argc, char* argv[], PlatformHooks hooks) {
    if (DEBUG_ENABLED) std::cout << "[DEBUG] Entered main_common_entry" << std::endl;
    int audioDev = 0;
    int midiDev = 0;
    bool useSynth = true;
    std::signal(SIGINT, signal_handler);
    if (DEBUG_ENABLED) std::cout << "[DEBUG] Before parse_common_args" << std::endl;
    parse_common_args(argc, argv, audioDev, midiDev, useSynth);
    if (DEBUG_ENABLED) std::cout << "[DEBUG] After parse_common_args" << std::endl;

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

    if (DEBUG_ENABLED) std::cout << "[DEBUG] Before setup_unison_synths" << std::endl;
    setup_unison_synths();
    if (DEBUG_ENABLED) std::cout << "[DEBUG] After setup_unison_synths" << std::endl;
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
    std::thread audio([&]() {
        try {
            int bufferIndex = 0;
            if (DEBUG_ENABLED) std::cout << "[AUDIO THREAD] Starting audio thread loop." << std::endl;
            while (running) {
                if (DEBUG_ENABLED) std::cout << "[AUDIO THREAD] Top of loop, bufferIndex=" << bufferIndex << std::endl;
#if defined(_WIN32)
                extern std::vector<WAVEHDR> waveHeaders;
                while (waveHeaders[bufferIndex].dwFlags & WHDR_INQUEUE) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    if (!running) return;
                }
#endif
                fill_audio_buffer(bufferIndex, useSynth);
                if (DEBUG_ENABLED) std::cout << "[AUDIO THREAD] After fill_audio_buffer." << std::endl;
                hooks.submit_audio_buffer(bufferIndex);
                if (DEBUG_ENABLED) std::cout << "[AUDIO THREAD] After submit_audio_buffer." << std::endl;
                bufferIndex = (bufferIndex + 1) % numBuffers;
            }
            if (DEBUG_ENABLED) std::cout << "[AUDIO THREAD] Exiting audio thread loop." << std::endl;
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
        // Send to all unison synths (or just the first if you prefer)
        for (auto* s : unisonSynths) {
            if (s) s->midiDataHandler(0, customVoiceSysex.data(), (int)customVoiceSysex.size());
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
    cleanup_unison_synths();
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