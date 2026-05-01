#include "Rack.h"
#include "AudioEffectPlateReverb.h"
#include "Debug.h"
#include "VoiceData.h"
#include <iostream>
#include <algorithm>
#include <cstring>
#include <regex>
#include <future>
#include <mutex>

namespace FMRack {

// Rack class implementation
Rack::Rack(float sampleRate) : initialized(false), sampleRate_(sampleRate) {
    performance_ = std::make_unique<Performance>();
    reverb_ = std::make_unique<AudioEffectPlateReverb>(sampleRate);
    
    // Initialize audio buffers
    const int maxBufferSize = 1024;
    dryLeftBuffer_.resize(maxBufferSize);
    dryRightBuffer_.resize(maxBufferSize);
    reverbLeftBuffer_.resize(maxBufferSize);
    reverbRightBuffer_.resize(maxBufferSize);
    reverbOutLeftBuffer_.resize(maxBufferSize);
    reverbOutRightBuffer_.resize(maxBufferSize);
    finalLeftBuffer_.resize(maxBufferSize);
    finalRightBuffer_.resize(maxBufferSize);
    
    std::cout << "Rack initialized with sample rate: " << sampleRate << " Hz\n";
    initialized = true;
}

bool Rack::loadPerformance(const std::string& filename) {
    std::lock_guard<std::mutex> lock(modulesMutex);
    if (performance_->loadFromFile(filename)) {
        createModulesFromPerformance_Unlocked();
        // Configure effects from performance
        DEBUG_PRINT("[DEBUG] Performance loaded: " << filename);
        DEBUG_PRINT("[DEBUG] ReverbEnable: " << performance_->effects.reverbEnable);
        DEBUG_PRINT("[DEBUG] ReverbLevel: " << static_cast<int>(performance_->effects.reverbLevel));
        DEBUG_PRINT("[DEBUG] ReverbSize: " << static_cast<int>(performance_->effects.reverbSize));
        DEBUG_PRINT("[DEBUG] Number of modules: " << modules_.size());
        reverb_->setEnabled(performance_->effects.reverbEnable);
        reverb_->setSize(performance_->effects.reverbSize / 127.0f);
        reverb_->setLevel(performance_->effects.reverbLevel / 127.0f);
        return true;
    }
    return false;
}

int Rack::getNumModules() const {
    std::lock_guard<std::mutex> lock(modulesMutex);
    return static_cast<int>(modules_.size());
}

void Rack::setDefaultPerformance() {
    std::lock_guard<std::mutex> lock(modulesMutex);
    performance_->setDefaults(16, 1); // Added unisonVoices parameter
    createModulesFromPerformance_Unlocked();
    
    // Set default effects
    reverb_->setEnabled(true);
    reverb_->setSize(0.5f);
    reverb_->setLevel(0.25f);
}

void Rack::createModulesFromPerformance() {
    std::lock_guard<std::mutex> lock(modulesMutex);
    createModulesFromPerformance_Unlocked();
}

void Rack::createModulesFromPerformance_Unlocked() {
    // NOTE: Caller must hold modulesMutex!
    std::cout << "\n=== Creating modules from performance ===\n";
    modules_.clear();
    std::cout << "[DEBUG] performance_->parts.size(): " << performance_->parts.size() << std::endl;
    // Debug output for global reverb settings
    DEBUG_PRINT("[DEBUG] Global Reverb Settings:\n");
    DEBUG_PRINT("  ReverbEnable: " << performance_->effects.reverbEnable << "\n");
    DEBUG_PRINT("  ReverbLevel: " << static_cast<int>(performance_->effects.reverbLevel) << "/127\n");
    DEBUG_PRINT("  ReverbSize: " << static_cast<int>(performance_->effects.reverbSize) << "/127\n");
    DEBUG_PRINT("  ReverbHighDamp: " << static_cast<int>(performance_->effects.reverbHighDamp) << "/127\n");
    DEBUG_PRINT("  ReverbLowDamp: " << static_cast<int>(performance_->effects.reverbLowDamp) << "/127\n");
    DEBUG_PRINT("  ReverbLowPass: " << static_cast<int>(performance_->effects.reverbLowPass) << "/127\n");
    DEBUG_PRINT("  ReverbDiffusion: " << static_cast<int>(performance_->effects.reverbDiffusion) << "/127\n");

    // First pass: count modules per MIDI channel (1-16)
    int midiChannelCounts[17] = {0}; // 1-based, midiChannelCounts[0] unused
    for (int i = 0; i < 16; ++i) {
        auto config = performance_->getPartConfig(i);
        if (config.midiChannel > 0 && config.midiChannel <= 16) {
            midiChannelCounts[config.midiChannel]++;
        }
    }

    // Second pass: create modules and assign gain compensation
    for (int i = 0; i < 16; ++i) {
        auto config = performance_->getPartConfig(i);
        if (config.midiChannel == 0)
            continue; // Only create modules for enabled parts

        if (debugEnabled) {
            std::cout << "[DEBUG] Got part config for index: " << i << std::endl;
        }

        if (config.midiChannel > 0 ) {
            // Force each part to its own MIDI channel (1-16)
            // config.midiChannel = i + 1;

            if (debugEnabled) {
                std::string voiceName = VoiceData::extractDX7VoiceName(std::vector<uint8_t>(config.voiceData.begin(), config.voiceData.end()));
                std::cout << "Creating module " << (i + 1) << " on MIDI channel "
                        << static_cast<int>(config.midiChannel) << " with voice: \"" << voiceName << "\"\n";
                
                // Show detailed configuration
                std::cout << "  Voice parameters:\n";
                std::cout << "    Volume: " << static_cast<int>(config.volume) << "/127 (" 
                        << (config.volume / 127.0f * 100.0f) << "%)\n";
                std::cout << "    Pan: " << static_cast<int>(config.pan) << "/127 (" 
                        << (config.pan / 127.0f * 100.0f) << "%)\n";
                std::cout << "    Note range: " << static_cast<int>(config.noteLimitLow) 
                        << "-" << static_cast<int>(config.noteLimitHigh) << "\n";
                std::cout << "    Note shift: " << static_cast<int>(config.noteShift) << " semitones\n";
                std::cout << "    Detune: " << static_cast<int>(config.detune) << " cents\n";
                std::cout << "    Reverb send: " << static_cast<int>(config.reverbSend) << "/127\n";
                std::cout << "    Unison voices: " << static_cast<int>(config.unisonVoices) << "\n";
                if (config.unisonVoices > 1) {
                    std::cout << "    Unison detune: " << config.unisonDetune << " cents\n";
                    std::cout << "    Unison spread: " << (config.unisonSpread * 100.0f) << "%\n";
                }
                std::cout << "    Mono mode: " << (config.monoMode ? "ON" : "OFF") << "\n";
                
                // Show controller assignments
                if (config.modulationWheelRange > 0) {
                    std::cout << "    Mod wheel -> " << getControllerTargetName(config.modulationWheelTarget) 
                            << " (range: " << static_cast<int>(config.modulationWheelRange) << ")\n";
                }
                if (config.footControlRange > 0) {
                    std::cout << "    Foot control -> " << getControllerTargetName(config.footControlTarget) 
                            << " (range: " << static_cast<int>(config.footControlRange) << ")\n";
                }
                if (config.breathControlRange > 0) {
                    std::cout << "    Breath control -> " << getControllerTargetName(config.breathControlTarget) 
                            << " (range: " << static_cast<int>(config.breathControlRange) << ")\n";
                }
                if (config.aftertouchRange > 0) {
                    std::cout << "    Aftertouch -> " << getControllerTargetName(config.aftertouchTarget) 
                            << " (range: " << static_cast<int>(config.aftertouchRange) << ")\n";
                }
                
                // Show pitch bend range if applicable
                std::cout << "    Pitch bend range: " << static_cast<int>(config.pitchBendRange) << " semitones\n";
            }
            // Use new constructor to load voice at creation
            auto module = std::make_unique<Module>(sampleRate_, config);
            // Gain compensation: 1/sqrt(N) for N modules on this MIDI channel
            int count = midiChannelCounts[config.midiChannel];
            float gainComp = (count > 0) ? (1.0f / std::sqrt(static_cast<float>(count))) : 1.0f;
            module->setOutputGain(gainComp);
            modules_.push_back(std::move(module));
            std::string voiceName = VoiceData::extractDX7VoiceName(std::vector<uint8_t>(config.voiceData.begin(), config.voiceData.end()));
            std::cout << "Module " << (i + 1) << " created and configured with MIDI channel "
                      << static_cast<int>(config.midiChannel) << " and voice: \"" << voiceName << "\"\n";
            
            // Debug: print raw voice name bytes
            std::cout << "  Raw voice name bytes: ";
            for (int vi = 145; vi < 155; ++vi) {
                std::cout << std::hex << std::uppercase << (int)config.voiceData[vi] << " ";
            }
            std::cout << std::dec << std::endl;
        }
    }

    std::cout << "Total active modules: " << modules_.size() << "\n";
    std::cout << "=== Module creation complete ===\n\n";
}

uint8_t Rack::extractMidiChannel(uint8_t status) const {
    if ((status & 0xF0) >= 0x80 && (status & 0xF0) <= 0xE0) {
        return (status & 0x0F) + 1; // Convert to 1-16 range
    }
    return 0; // System messages
}

void Rack::routeMidiToModules(uint8_t status, uint8_t data1, uint8_t data2) {
    std::lock_guard<std::mutex> lock(modulesMutex);
    uint8_t channel = extractMidiChannel(status);
    DEBUG_PRINT("[DEBUG] routeMidiToModules: status=0x" << std::hex << (int)status
        << " channel=" << std::dec << (int)channel);

    if (channel == 0) return; // Skip system messages

    // Route to all modules with matching MIDI channel
    [[maybe_unused]] bool anyMatched = false;
    for (size_t i = 0; i < modules_.size(); ++i) {
        uint8_t moduleChan = modules_[i]->getMIDIChannel();
        DEBUG_PRINT("[DEBUG] Module " << i << " getMIDIChannel()=" << (int)moduleChan);
        if (moduleChan == channel) {
            DEBUG_PRINT("[DEBUG] Routing to module " << i);
            modules_[i]->processMidiMessage(status, data1, data2);
            anyMatched = true;
        }
    }

    // Channel 16 messages are also broadcast to all modules except those set to 16
    if (channel == 16) {
        for (size_t i = 0; i < modules_.size(); ++i) {
            uint8_t moduleChan = modules_[i]->getMIDIChannel();
            if (moduleChan != 16) {
                DEBUG_PRINT("[DEBUG] Channel 16 broadcast to module " << i);
                modules_[i]->processMidiMessage(status, data1, data2);
            }
        }
    }
}

void Rack::processMidiMessage(uint8_t status, uint8_t data1, uint8_t data2) {
    if (!initialized) return;
    
    // Print MIDI messages for debugging
    uint8_t msgType = status & 0xF0;
    uint8_t channel = extractMidiChannel(status);
    if (msgType == 0x90 && data2 > 0) {
        DEBUG_PRINT("[DEBUG] Note ON - Channel: " << static_cast<int>(channel) 
            << ", Note: " << static_cast<int>(data1) 
            << ", Velocity: " << static_cast<int>(data2));
    } else if (msgType == 0x80 || (msgType == 0x90 && data2 == 0)) {
        DEBUG_PRINT("[DEBUG] Note OFF - Channel: " << static_cast<int>(channel) 
            << ", Note: " << static_cast<int>(data1));
    }
    
    routeMidiToModules(status, data1, data2);
}

void Rack::processAudio(float* leftOut, float* rightOut, int numSamples) {
    std::lock_guard<std::mutex> lock(modulesMutex);
    if (!initialized || numSamples <= 0) {
        std::fill(leftOut, leftOut + numSamples, 0.0f);
        std::fill(rightOut, rightOut + numSamples, 0.0f);
        return;
    }
    
    // Safety check: don't exceed pre-allocated buffer size
    if (numSamples > kMaxBufferSize) {
        std::fill(leftOut, leftOut + numSamples, 0.0f);
        std::fill(rightOut, rightOut + numSamples, 0.0f);
        return;
    }
    
    // Clear accumulation buffers using memset for speed
    std::memset(dryLeftBuffer_.data(), 0, numSamples * sizeof(float));
    std::memset(dryRightBuffer_.data(), 0, numSamples * sizeof(float));
    std::memset(reverbLeftBuffer_.data(), 0, numSamples * sizeof(float));
    std::memset(reverbRightBuffer_.data(), 0, numSamples * sizeof(float));
    std::memset(reverbOutLeftBuffer_.data(), 0, numSamples * sizeof(float));
    std::memset(reverbOutRightBuffer_.data(), 0, numSamples * sizeof(float));
    std::memset(finalLeftBuffer_.data(), 0, numSamples * sizeof(float));
    std::memset(finalRightBuffer_.data(), 0, numSamples * sizeof(float));

    // Get module count (capped at max)
    const size_t moduleCount = std::min(modules_.size(), static_cast<size_t>(kMaxModules));

    // Only use multiprocessing if enabled
    if (multiprocessingEnabled && moduleCount > 1) {
        // Use pre-allocated per-module buffers - no heap allocation!
        // Clear the buffers we'll use
        for (size_t i = 0; i < moduleCount; ++i) {
            std::memset(moduleBuffers_[i].left.data(), 0, numSamples * sizeof(float));
            std::memset(moduleBuffers_[i].right.data(), 0, numSamples * sizeof(float));
            std::memset(moduleBuffers_[i].revLeft.data(), 0, numSamples * sizeof(float));
            std::memset(moduleBuffers_[i].revRight.data(), 0, numSamples * sizeof(float));
        }

        // Launch parallel processing for each module using pre-allocated futures array
        // Note: std::async still has internal allocation, but we avoid vector allocation
        for (size_t i = 0; i < moduleCount; ++i) {
            auto& mbuf = moduleBuffers_[i];
            auto* module = modules_[i].get();
            moduleFutures_[i] = std::async(std::launch::async, [module, &mbuf, numSamples]() {
                module->processAudio(
                    mbuf.left.data(), mbuf.right.data(),
                    mbuf.revLeft.data(), mbuf.revRight.data(),
                    numSamples
                );
            });
        }
        // Wait for all modules to finish
        for (size_t i = 0; i < moduleCount; ++i) {
            if (moduleFutures_[i].valid()) {
                moduleFutures_[i].get();
            }
        }
        // Accumulate results
        for (size_t m = 0; m < moduleCount; ++m) {
            const auto& mbuf = moduleBuffers_[m];
            for (int i = 0; i < numSamples; ++i) {
                dryLeftBuffer_[i] += mbuf.left[i];
                dryRightBuffer_[i] += mbuf.right[i];
                reverbLeftBuffer_[i] += mbuf.revLeft[i];
                reverbRightBuffer_[i] += mbuf.revRight[i];
            }
        }
    } else {
        // Serial processing: use pre-allocated per-module buffers
        for (size_t m = 0; m < moduleCount; ++m) {
            auto& mbuf = moduleBuffers_[m];
            // Clear per-module buffers before processing
            std::memset(mbuf.left.data(), 0, numSamples * sizeof(float));
            std::memset(mbuf.right.data(), 0, numSamples * sizeof(float));
            std::memset(mbuf.revLeft.data(), 0, numSamples * sizeof(float));
            std::memset(mbuf.revRight.data(), 0, numSamples * sizeof(float));
            
            modules_[m]->processAudio(
                mbuf.left.data(), mbuf.right.data(),
                mbuf.revLeft.data(), mbuf.revRight.data(),
                numSamples
            );
            
            // Accumulate results immediately (better cache locality)
            for (int i = 0; i < numSamples; ++i) {
                dryLeftBuffer_[i] += mbuf.left[i];
                dryRightBuffer_[i] += mbuf.right[i];
                reverbLeftBuffer_[i] += mbuf.revLeft[i];
                reverbRightBuffer_[i] += mbuf.revRight[i];
            }
        }
    }
    // Process reverb
    reverb_->process(
        reverbLeftBuffer_.data(), reverbRightBuffer_.data(),
        reverbOutLeftBuffer_.data(), reverbOutRightBuffer_.data(),
        numSamples
    );
    bool reverbEnabled = reverb_->get_bypass() == false;
    for (int i = 0; i < numSamples; ++i) {
        if (reverbEnabled) {
            finalLeftBuffer_[i] = dryLeftBuffer_[i] + reverbOutLeftBuffer_[i];
            finalRightBuffer_[i] = dryRightBuffer_[i] + reverbOutRightBuffer_[i];
        } else {
            finalLeftBuffer_[i] = dryLeftBuffer_[i];
            finalRightBuffer_[i] = dryRightBuffer_[i];
        }
    }
    for (int i = 0; i < numSamples; ++i) {
        leftOut[i] = finalLeftBuffer_[i];
        rightOut[i] = finalRightBuffer_[i];
    }
}

bool Rack::captureModuleOutput(int moduleIndex, float* leftOut, float* rightOut, int numSamples)
{
    // Capture the dry output from an already-processed module
    // This must be called immediately after processAudio() in the same audio callback
    // Does NOT re-process the module, just copies from the internal buffers
    
    if (moduleIndex < 0 || moduleIndex >= static_cast<int>(modules_.size())) {
        // Invalid module index
        if (leftOut) std::fill(leftOut, leftOut + numSamples, 0.0f);
        if (rightOut) std::fill(rightOut, rightOut + numSamples, 0.0f);
        return false;
    }
    
    if (numSamples <= 0 || numSamples > kMaxBufferSize) {
        // Invalid sample count
        if (leftOut) std::fill(leftOut, leftOut + numSamples, 0.0f);
        if (rightOut) std::fill(rightOut, rightOut + numSamples, 0.0f);
        return false;
    }
    
    // Copy from the module's pre-allocated buffer (no re-processing)
    const auto& mbuf = moduleBuffers_[moduleIndex];
    
    if (leftOut)
        std::copy(mbuf.left.begin(), mbuf.left.begin() + numSamples, leftOut);
    if (rightOut)
        std::copy(mbuf.right.begin(), mbuf.right.begin() + numSamples, rightOut);
    
    return true;
}

bool Rack::isInitialized() const { return initialized; }

int Rack::getActiveVoices() const {
    std::lock_guard<std::mutex> lock(modulesMutex);
    int total = 0;
    for (const auto& module : modules_) {
        if (module->isActive()) {
            total++;
        }
    }
    return total;
}

int Rack::getEnabledPartCount() const {
    if (!performance_) return 0;
    return performance_->getEnabledPartCount();
}

std::string Rack::getControllerTargetName(uint8_t target) const {
    switch (target) {
        case 0: return "Pitch";
        case 1: return "Amplitude";
        case 2: return "EG Bias";
        case 3: return "LFO Speed";
        case 4: return "LFO Delay";
        case 5: return "LFO Depth";
        case 6: return "Cutoff";
        case 7: return "Resonance";
        default: return "Unknown";
    }
}

void Rack::routeSysexToModules(const uint8_t* data, int len, uint8_t sysex_channel) {
    std::lock_guard<std::mutex> lock(modulesMutex);
    // NOTE: All std::cout logging removed - this function may be called from audio thread
    
    // Only handle MiniDexed (0x7D) SysEx in Performance
    if (len >= 3 && data[1] == 0x7D && performance_) {
        int responseLen = 0;
        if (performance_->handleSysex(data, len, sysexResponseBuffer_.data(), 
                                       kMaxSysexResponseSize, responseLen, -1)) {
            // TODO: Send response via MIDI output here (if responseLen > 0)
            return;
        }
    }
    // Forward all other SysEx to modules (Yamaha/Dexed etc)
    for (auto& module : modules_) {
        if (sysex_channel == 0 || module->getMIDIChannel() == sysex_channel) {
            module->processSysex(data, len);
        }
    }
}

// Add handleProgramChange implementation
void Rack::handleProgramChange(int programNum, const std::string& performanceDir) {
    // Special case: program 1 loads ../performance.ini
    if (programNum == 0) {
        std::filesystem::path perfPath = std::filesystem::path(performanceDir) / ".." / "performance.ini";
        DEBUG_PRINT("[DEBUG] Program 1: trying to load " << perfPath.string());
        if (std::filesystem::exists(perfPath)) {
            if (loadPerformance(perfPath.string())) {
                DEBUG_PRINT("[DEBUG] Performance loaded from: " << perfPath.string());
            } else {
                DEBUG_PRINT("[DEBUG] Failed to load performance: " << perfPath.string());
            }
        } else {
            DEBUG_PRINT("[DEBUG] Performance file does not exist: " << perfPath.string());
        }
        return;
    }
    // For other programs, match files with any number of leading zeros and any suffix
    std::regex pattern(R"((0*)" + std::to_string(programNum) + R"(_.*\.ini)$)", std::regex_constants::icase);
    std::filesystem::directory_iterator dirIter(performanceDir);
    std::string foundFile;
    for (const auto& entry : dirIter) {
        if (!entry.is_regular_file()) continue;
        std::string fname = entry.path().filename().string();
        if (std::regex_search(fname, pattern)) {
            foundFile = entry.path().string();
            break;
        }
    }
    if (!foundFile.empty()) {
        DEBUG_PRINT("[DEBUG] Program Change: found file " << foundFile);
        if (loadPerformance(foundFile)) {
            DEBUG_PRINT("[DEBUG] Performance loaded from: " << foundFile);
        } else {
            DEBUG_PRINT("[DEBUG] Failed to load performance: " << foundFile);
        }
    } else {
        DEBUG_PRINT("[DEBUG] No matching performance file found for program " << (programNum + 1));
    }
}

bool Rack::loadInitialPerformance(const std::string& performanceFile) {
    if (performanceFile.empty()) return false;
    if (loadPerformance(performanceFile)) {
        std::cout << "Performance loaded successfully!\n";
        std::cout << "Enabled parts: " << getEnabledPartCount() << "/16\n";
        return true;
    } else {
        std::cout << "Failed to load performance file.\n";
        // Exit the application if performance loading fails
        std::cerr << "Error: Could not load performance file: " << performanceFile << "\n";
        return false;
    }
}

// New: Directly set up modules from a list of voice data blobs
void Rack::setupModulesFromVoices(const std::vector<std::vector<uint8_t>>& voices, int unisonVoices, float unisonDetune, float unisonSpread) {
    std::lock_guard<std::mutex> lock(modulesMutex);
    std::cout << "\n=== Creating modules from provided voices ===\n";
    modules_.clear();
    int numModules = static_cast<int>(voices.size());
    for (int i = 0; i < numModules; ++i) {
        if (voices[i].size() < 156) {
            std::cerr << "[ERROR] Voice data for module " << (i+1) << " is too short (" << voices[i].size() << " bytes). Skipping.\n";
            continue;
        }
        // Only create module if the voice name is not empty or all zeros
        std::string voiceName = VoiceData::extractDX7VoiceName(std::vector<uint8_t>(voices[i].begin(), voices[i].end()));
        bool allZero = std::all_of(voices[i].begin() + 145, voices[i].begin() + 155, [](uint8_t c) { return c == 0; });
        if (voiceName.empty() || allZero) {
            std::cerr << "[ERROR] Voice data for module " << (i+1) << " has empty or invalid name. Skipping.\n";
            continue;
        }
        // Build a minimal PartConfig for this module
        Performance::PartConfig config;
        config.voiceData = {0};
        std::copy_n(voices[i].begin(), 156, config.voiceData.begin());
        config.midiChannel = 1; // Always use MIDI channel 1 for all modules loaded via --voice
        config.unisonVoices = static_cast<uint8_t>(unisonVoices);
        config.unisonDetune = unisonDetune;
        config.unisonSpread = unisonSpread;
        config.volume = 100;
        std::cout << "Creating module " << (i + 1) << " on MIDI channel " << (i + 1) << " with voice: \"" << voiceName << "\"\n";
        auto module = std::make_unique<Module>(sampleRate_, config);
        modules_.push_back(std::move(module));
        // Debug: print raw voice name bytes
        std::cout << "  Raw voice name bytes: ";
        for (int vi = 145; vi < 155; ++vi) {
            std::cout << std::hex << std::uppercase << (int)config.voiceData[vi] << " ";
        }
        std::cout << std::dec << std::endl;
    }
    std::cout << "Total active modules: " << modules_.size() << "\n";
    std::cout << "=== Module creation complete ===\n\n";
}

} // namespace FMRack
