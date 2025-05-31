#include "Rack.h"
#include "AudioEffectPlateReverb.h"
#include "Debug.h"
#include <iostream>
#include <algorithm>
#include <cstring>
#include <regex>
#include <future>
#include <mutex>

namespace FMRack {

// Helper function to extract voice name from DX7 voice data
std::string extractVoiceName(const std::array<uint8_t, 156>& voiceData) {
    // Voice name is at offset 145-154 (10 characters)
    std::string voiceName;
    for (int i = 145; i < 155; ++i) {
        char c = static_cast<char>(voiceData[i]);
        // DX7 uses ASCII characters 32-127
        if (c >= 32) {
            voiceName += c;
        } else {
            voiceName += ' ';
        }
    }
    
    // Trim trailing spaces
    while (!voiceName.empty() && voiceName.back() == ' ') {
        voiceName.pop_back();
    }
    
    return voiceName.empty() ? "Unnamed Voice" : voiceName;
}

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
    if (performance_->loadFromFile(filename)) {
        createModulesFromPerformance();
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

void Rack::setDefaultPerformance() {
    performance_->setDefaults(16, 1); // Added unisonVoices parameter
    createModulesFromPerformance();
    
    // Set default effects
    reverb_->setEnabled(true);
    reverb_->setSize(0.5f);
    reverb_->setLevel(0.25f);
}

void Rack::createModulesFromPerformance() {
    std::lock_guard<std::mutex> lock(modulesMutex);
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
    for (int i = 0; i < 16; ++i) {
        if (i >= static_cast<int>(performance_->parts.size())) {
            std::cout << "[DEBUG] Index " << i << " out of bounds for parts.size() " << performance_->parts.size() << std::endl;
            break;
        }
        auto config = performance_->getPartConfig(i);
        if (debugEnabled) {
            std::cout << "[DEBUG] Got part config for index: " << i << std::endl;
        }

        if (config.midiChannel > 0 ) {
            // Force each part to its own MIDI channel (1-16)
            // config.midiChannel = i + 1;

            if (debugEnabled) {
                std::string voiceName = extractVoiceName(config.voiceData);
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
            auto module = std::make_unique<Module>(sampleRate_);
            module->configureFromPerformance(config);
            DEBUG_PRINT("[DEBUG] Module " << (i + 1)
                << " MIDI channel: " << (int)module->getMIDIChannel()
                << " Note range: " << (int)config.noteLimitLow << "-" << (int)config.noteLimitHigh
                << " Pan: " << (int)config.pan);
            modules_.push_back(std::move(module));
            std::string voiceName = extractVoiceName(config.voiceData);
            std::cout << "Module " << (i + 1) << " created and configured with MIDI channel "
                      << static_cast<int>(config.midiChannel) << " and voice: \"" << voiceName << "\"\n";
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
    // Clear accumulation buffers
    std::fill(dryLeftBuffer_.begin(), dryLeftBuffer_.begin() + numSamples, 0.0f);
    std::fill(dryRightBuffer_.begin(), dryRightBuffer_.begin() + numSamples, 0.0f);
    std::fill(reverbLeftBuffer_.begin(), reverbLeftBuffer_.begin() + numSamples, 0.0f);
    std::fill(reverbRightBuffer_.begin(), reverbRightBuffer_.begin() + numSamples, 0.0f);
    std::fill(reverbOutLeftBuffer_.begin(), reverbOutLeftBuffer_.begin() + numSamples, 0.0f);
    std::fill(reverbOutRightBuffer_.begin(), reverbOutRightBuffer_.begin() + numSamples, 0.0f);
    std::fill(finalLeftBuffer_.begin(), finalLeftBuffer_.begin() + numSamples, 0.0f);
    std::fill(finalRightBuffer_.begin(), finalRightBuffer_.begin() + numSamples, 0.0f);

    // Only use multiprocessing if enabled
    if (multiprocessingEnabled) {
        // Prepare per-module buffers and futures
        struct ModuleBuffers {
            std::vector<float> left, right, revLeft, revRight;
            ModuleBuffers(int n) : left(n, 0.0f), right(n, 0.0f), revLeft(n, 0.0f), revRight(n, 0.0f) {}
        };
        std::vector<ModuleBuffers> moduleBuffers;
        std::vector<std::future<void>> futures;
        moduleBuffers.reserve(modules_.size());
        futures.reserve(modules_.size());

        // Launch parallel processing for each module
        for (size_t i = 0; i < modules_.size(); ++i) {
            moduleBuffers.emplace_back(numSamples);
            auto& mbuf = moduleBuffers.back();
            auto* module = modules_[i].get();
            futures.push_back(std::async(std::launch::async, [module, &mbuf, numSamples]() {
                module->processAudio(
                    mbuf.left.data(), mbuf.right.data(),
                    mbuf.revLeft.data(), mbuf.revRight.data(),
                    numSamples
                );
            }));
        }
        // Wait for all modules to finish
        for (auto& fut : futures) {
            fut.get();
        }
        // Accumulate results
        for (const auto& mbuf : moduleBuffers) {
            for (int i = 0; i < numSamples; ++i) {
                dryLeftBuffer_[i] += mbuf.left[i];
                dryRightBuffer_[i] += mbuf.right[i];
                reverbLeftBuffer_[i] += mbuf.revLeft[i];
                reverbRightBuffer_[i] += mbuf.revRight[i];
            }
        }
    } else {
        // Serial processing: use per-module buffers and sum
        struct ModuleBuffers {
            std::vector<float> left, right, revLeft, revRight;
            ModuleBuffers(int n) : left(n, 0.0f), right(n, 0.0f), revLeft(n, 0.0f), revRight(n, 0.0f) {}
        };
        std::vector<ModuleBuffers> moduleBuffers;
        moduleBuffers.reserve(modules_.size());
        for (auto& module : modules_) {
            moduleBuffers.emplace_back(numSamples);
            auto& mbuf = moduleBuffers.back();
            // Clear per-module buffers before processing
            std::fill(mbuf.left.begin(), mbuf.left.end(), 0.0f);
            std::fill(mbuf.right.begin(), mbuf.right.end(), 0.0f);
            std::fill(mbuf.revLeft.begin(), mbuf.revLeft.end(), 0.0f);
            std::fill(mbuf.revRight.begin(), mbuf.revRight.end(), 0.0f);
            module->processAudio(
                mbuf.left.data(), mbuf.right.data(),
                mbuf.revLeft.data(), mbuf.revRight.data(),
                numSamples
            );
        }
        // Accumulate results
        for (const auto& mbuf : moduleBuffers) {
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

bool Rack::isInitialized() const { return initialized; }

int Rack::getActiveVoices() const {
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
    // Only handle MiniDexed (0x7D) SysEx in Performance
    if (len >= 3 && data[1] == 0x7D && performance_) {
        std::cout << "[SYSEX] MiniDexed SysEx received, length: " << len << " bytes\n";
        std::vector<uint8_t> response;
        if (performance_->handleSysex(data, len, response)) {
            if (!response.empty()) {
                // TODO: Send response via MIDI output here
                std::cout << "[SYSEX] TODO: Send performance parameter response (" << response.size() << " bytes)\n";
            }
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

} // namespace FMRack
