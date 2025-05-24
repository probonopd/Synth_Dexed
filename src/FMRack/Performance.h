#pragma once

#include <array>
#include <string>

namespace FMRack {

// Performance class - stores configuration for up to 8 parts
class Performance {
public:
    struct PartConfig {
        // Basic settings
        uint8_t bankNumber = 0;
        uint8_t voiceNumber = 0;
        uint8_t midiChannel = 0;  // 0=disabled, 1-16=enabled
        uint8_t volume = 100;
        uint8_t pan = 64;         // 0-127, 64=center
        int8_t detune = 0;        // -64 to +63 cents
        uint8_t cutoff = 127;
        uint8_t resonance = 0;
        uint8_t noteLimitLow = 0;
        uint8_t noteLimitHigh = 127;
        int8_t noteShift = 0;     // -24 to +24 semitones
        uint8_t reverbSend = 0;   // 0-127
        uint8_t pitchBendRange = 2;
        uint8_t pitchBendStep = 0;
        uint8_t portamentoMode = 0;
        uint8_t portamentoGlissando = 0;
        uint8_t portamentoTime = 0;
        uint8_t monoMode = 0;     // 0=poly, 1=mono
        
        // Controller assignments
        uint8_t modulationWheelRange = 0;
        uint8_t modulationWheelTarget = 0;
        uint8_t footControlRange = 0;
        uint8_t footControlTarget = 0;
        uint8_t breathControlRange = 0;
        uint8_t breathControlTarget = 0;
        uint8_t aftertouchRange = 0;
        uint8_t aftertouchTarget = 0;
        
        // Voice data - 155 bytes of DX7 patch data
        std::array<uint8_t, 155> voiceData;
        
        // Unison settings
        uint8_t unisonVoices = 1;  // 1-4 voices for unison
        float unisonDetune = 0.1f; // Detune amount between voices
        float unisonSpread = 0.5f; // Pan spread between voices
    };
    
    // Global effects settings
    struct EffectsConfig {
        bool compressorEnable = false;
        bool reverbEnable = true;
        uint8_t reverbSize = 64;
        uint8_t reverbHighDamp = 64;
        uint8_t reverbLowDamp = 64;
        uint8_t reverbLowPass = 127;
        uint8_t reverbDiffusion = 64;
        uint8_t reverbLevel = 32;
    };
    
    std::array<PartConfig, 8> parts;
    EffectsConfig effects;
    
    // Constructor and destructor
    Performance();
    ~Performance();
    
    // Methods
    bool loadFromFile(const std::string& filename);
    void setDefaults();
    const PartConfig& getPartConfig(int partIndex) const;
    int getEnabledPartCount() const;
};

} // namespace FMRack
