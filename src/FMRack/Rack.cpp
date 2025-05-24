#include "Rack.h"
#include <iostream>
#include <algorithm>
#include <cstring>

namespace FMRack {

// Helper function to extract voice name from DX7 voice data
std::string extractVoiceName(const std::array<uint8_t, 155>& voiceData) {
    // Voice name is at offset 145-154 (10 characters)
    std::string voiceName;
    for (int i = 145; i < 155; ++i) {
        char c = static_cast<char>(voiceData[i]);
        // DX7 uses ASCII characters 32-127
        if (c >= 32 && c <= 127) {
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
    reverb_ = std::make_unique<Reverb>(sampleRate);
    
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
        reverb_->setEnabled(performance_->effects.reverbEnable);
        reverb_->setSize(performance_->effects.reverbSize / 127.0f);
        reverb_->setLevel(performance_->effects.reverbLevel / 127.0f);
        
        return true;
    }
    return false;
}

void Rack::setDefaultPerformance() {
    performance_->setDefaults();
    createModulesFromPerformance();
    
    // Set default effects
    reverb_->setEnabled(true);
    reverb_->setSize(0.5f);
    reverb_->setLevel(0.25f);
}

void Rack::createModulesFromPerformance() {
    std::cout << "\n=== Creating modules from performance ===\n";

    modules_.clear();

    for (int i = 0; i < 16; ++i) {
        auto config = performance_->getPartConfig(i);

        if (config.midiChannel > 0) {
            // Force each part to its own MIDI channel (1-16)
            // config.midiChannel = i + 1;
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
            auto module = std::make_unique<Module>(sampleRate_);
            module->configureFromPerformance(config);
            std::cout << "[DEBUG] Module " << (i + 1)
                      << " MIDI channel: " << (int)module->getMIDIChannel()
                      << " Note range: " << (int)config.noteLimitLow << "-" << (int)config.noteLimitHigh
                      << " Pan: " << (int)config.pan << std::endl;
            modules_.push_back(std::move(module));
            std::cout << "  Module " << (i + 1) << " created successfully\n\n";
        } else {
            std::cout << "Skipping module " << (i + 1) << " (MIDI channel 0 = disabled)\n";
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
    uint8_t channel = extractMidiChannel(status);
    std::cout << "[DEBUG] routeMidiToModules: status=0x" << std::hex << (int)status
              << " channel=" << std::dec << (int)channel << std::endl;

    if (channel == 0) return; // Skip system messages

    // Route to all modules with matching MIDI channel
    bool anyMatched = false;
    for (size_t i = 0; i < modules_.size(); ++i) {
        uint8_t moduleChan = modules_[i]->getMIDIChannel();
        std::cout << "[DEBUG] Module " << i << " getMIDIChannel()=" << (int)moduleChan << std::endl;
        if (moduleChan == channel) {
            std::cout << "[DEBUG] Routing to module " << i << std::endl;
            modules_[i]->processMidiMessage(status, data1, data2);
            anyMatched = true;
        }
    }

    // Channel 16 messages are also broadcast to all modules except those set to 16
    if (channel == 16) {
        for (size_t i = 0; i < modules_.size(); ++i) {
            uint8_t moduleChan = modules_[i]->getMIDIChannel();
            if (moduleChan != 16) {
                std::cout << "[DEBUG] Channel 16 broadcast to module " << i << std::endl;
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
        std::cout << "Note ON  - Channel: " << static_cast<int>(channel) 
                  << ", Note: " << static_cast<int>(data1) 
                  << ", Velocity: " << static_cast<int>(data2) << std::endl;
    } else if (msgType == 0x80 || (msgType == 0x90 && data2 == 0)) {
        std::cout << "Note OFF - Channel: " << static_cast<int>(channel) 
                  << ", Note: " << static_cast<int>(data1) << std::endl;
    }
    
    routeMidiToModules(status, data1, data2);
}

void Rack::processAudio(float* leftOut, float* rightOut, int numSamples) {
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

    // Temporary per-module buffers
    std::vector<float> moduleLeft(numSamples, 0.0f);   
    std::vector<float> moduleRight(numSamples, 0.0f);   
    std::vector<float> moduleRevLeft(numSamples, 0.0f);
    std::vector<float> moduleRevRight(numSamples, 0.0f);

    // Process each module and accumulate
    for (auto& module : modules_) {
        module->processAudio(
            moduleLeft.data(), moduleRight.data(),
            moduleRevLeft.data(), moduleRevRight.data(),
            numSamples
        );
        
        // Accumulate dry signal
        for (int i = 0; i < numSamples; ++i) {
            dryLeftBuffer_[i] += moduleLeft[i];
            dryRightBuffer_[i] += moduleRight[i];
            reverbLeftBuffer_[i] += moduleRevLeft[i];
            reverbRightBuffer_[i] += moduleRevRight[i];
        }
    }
    
    // Process reverb
    reverb_->process(
        reverbLeftBuffer_.data(), reverbRightBuffer_.data(),
        reverbOutLeftBuffer_.data(), reverbOutRightBuffer_.data(),
        numSamples
    );  

    // Mix dry and reverb
    for (int i = 0; i < numSamples; ++i) {
        finalLeftBuffer_[i] = dryLeftBuffer_[i] + reverbOutLeftBuffer_[i];
        finalRightBuffer_[i] = dryRightBuffer_[i] + reverbOutRightBuffer_[i];
    }

    // Copy final buffers to output
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

} // namespace FMRack
