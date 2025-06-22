#pragma once

#include <vector>
#include <memory>
#include <cstdint>
#include <functional>
#include <iostream>
#include "../dexed.h"
#include "Performance.h"

namespace FMRack {

// Module class - represents one synthesizer instance
class Module {
public:
    Module(float sampleRate);
    Module(float sampleRate, const Performance::PartConfig& config); // New constructor to allow direct voice loading
    ~Module() = default;
    
    // Configuration
    void configureFromPerformance(const Performance::PartConfig& config);
    void setMIDIChannel(uint8_t channel);
    uint8_t getMIDIChannel() const;
    
    // MIDI processing
    void processMidiMessage(uint8_t status, uint8_t data1, uint8_t data2);
    
    // Audio processing
    void processAudio(float* leftOut, float* rightOut, float* reverbSendLeft, float* reverbSendRight, int numSamples);
    
    // Voice management
    bool isActive() const;
    void panic();
    
    void setOutputGain(float gain);
    
    // Add SysEx handler
    void processSysex(const uint8_t* data, int len);

    // Print any MIDI data received from the synth (Dexed) in FF FF FF... format
    void onMidiFromDexed(const uint8_t* data, int len);

    // Add public getter for Dexed engine(s)
    Dexed* getDexedEngine(int idx = 0) {
        if (idx >= 0 && idx < static_cast<int>(fmEngines_.size()))
            return fmEngines_[idx].get();
        return nullptr;
    }
    
    // Register a callback for outgoing DX7 single voice dump SysEx
    void setSingleVoiceDumpHandler(std::function<void(const std::vector<uint8_t>&)> cb) { 
        std::cout << "[Module::setSingleVoiceDumpHandler] Handler being set, cb=" << (cb ? "VALID" : "NULL") << std::endl;
        singleVoiceDumpHandler_ = std::move(cb); 
        std::cout << "[Module::setSingleVoiceDumpHandler] Handler set, singleVoiceDumpHandler_=" << (singleVoiceDumpHandler_ ? "SET" : "NULL") << std::endl;
    }
    
private:
    float sampleRate_;
    uint8_t midiChannel_;
    bool enabled_;
    
    // Module-level parameters
    float volume_;
    float pan_;
    int8_t detune_;
    int8_t noteShift_;
    uint8_t noteLimitLow_;
    uint8_t noteLimitHigh_;
    float reverbSend_;
    bool monoMode_;
    
    // FM engines for unison
    std::vector<std::unique_ptr<Dexed>> fmEngines_;
    std::vector<float> unisonDetune_;
    std::vector<float> unisonPan_;
    
    // Internal buffers
    std::vector<int16_t> tempBuffer_;
    
    // Gain staging
    float outputGain_ = 1.0f;
    float moduleGain_ = 1.0f;
    
    // Pointer to live PartConfig for real-time updates
    Performance::PartConfig* partConfig_ = nullptr;
    
    // Callback for single voice dump SysEx
    std::function<void(const std::vector<uint8_t>&)> singleVoiceDumpHandler_;
    
    // Helper methods
    bool isNoteInRange(uint8_t note) const;
    void applyNoteShift(uint8_t& note) const;
    void setupUnison(uint8_t voices, float detune, float spread);
};

} // namespace FMRack
