#pragma once

#include <vector>
#include <memory>
#include <string>
#include "Performance.h"
#include "Reverb.h"
#include "Module.h"

namespace FMRack {

// Rack class - main container for the multi-timbral synthesizer
class Rack {
public:
    Rack(float sampleRate);
    ~Rack() = default;

    // Performance management
    bool loadPerformance(const std::string& filename);
    void setDefaultPerformance();

    // MIDI processing
    void processMidiMessage(uint8_t status, uint8_t data1, uint8_t data2);

    // Audio processing
    void processAudio(float* leftOut, float* rightOut, int numSamples);

    // Status
    bool isInitialized() const;
    int getActiveVoices() const;
    int getEnabledPartCount() const;
    std::string getControllerTargetName(uint8_t target) const;

private:
    bool initialized;
    float sampleRate_;
    
    // Performance configuration
    std::unique_ptr<Performance> performance_;
    
    // Modules (up to 8)
    std::vector<std::unique_ptr<Module>> modules_;
    
    // Effects
    std::unique_ptr<Reverb> reverb_;
    
    // Internal audio buffers
    std::vector<float> dryLeftBuffer_;
    std::vector<float> dryRightBuffer_;
    std::vector<float> reverbLeftBuffer_;
    std::vector<float> reverbRightBuffer_;
    std::vector<float> reverbOutLeftBuffer_;
    std::vector<float> reverbOutRightBuffer_;
    std::vector<float> finalLeftBuffer_;
    std::vector<float> finalRightBuffer_;
    
    // Helper methods
    void createModulesFromPerformance();
    void routeMidiToModules(uint8_t status, uint8_t data1, uint8_t data2);
    uint8_t extractMidiChannel(uint8_t status) const;
};

} // namespace FMRack
