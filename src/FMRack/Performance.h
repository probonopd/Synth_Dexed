#pragma once

#include <array>
#include <string>
#include <vector>

namespace FMRack {

// Performance class - stores configuration for up to 8 parts
class Performance {
public:
    struct PartConfig {
        // Basic settings
        uint8_t bankNumber = 0;        // 0 .. 127
        uint8_t voiceNumber = 1;       // 1 .. 32
        uint8_t midiChannel = 0;       // 1 .. 16, 0: off, >16: omni mode
        uint8_t volume = 100;          // 0 .. 127
        uint8_t pan = 64;              // 0 .. 127
        int8_t detune = 0;             // -99 .. 99
        uint8_t cutoff = 99;           // 0 .. 99
        uint8_t resonance = 0;         // 0 .. 99
        uint8_t noteLimitLow = 0;      // 0 .. 127, C-2 .. G8
        uint8_t noteLimitHigh = 127;   // 0 .. 127, C-2 .. G8
        int8_t noteShift = 0;          // -24 .. 24
        uint8_t reverbSend = 0;        // 0 .. 99
        uint8_t pitchBendRange = 2;    // 0 .. 12
        uint8_t pitchBendStep = 0;     // 0 .. 12
        uint8_t portamentoMode = 0;    // 0 .. 1
        uint8_t portamentoGlissando = 0; // 0 .. 1
        uint8_t portamentoTime = 0;    // 0 .. 99
        uint8_t monoMode = 0;          // 0-off .. 1-On
        
        // Controller assignments
        uint8_t modulationWheelRange = 99;   // 0 .. 99
        uint8_t modulationWheelTarget = 1;   // 0 .. 7
        uint8_t footControlRange = 99;       // 0 .. 99
        uint8_t footControlTarget = 0;       // 0 .. 7
        uint8_t breathControlRange = 99;     // 0 .. 99
        uint8_t breathControlTarget = 0;     // 0 .. 7
        uint8_t aftertouchRange = 99;        // 0 .. 99
        uint8_t aftertouchTarget = 0;        // 0 .. 7
        
        // Voice data - 156 bytes of DX7 patch data
        std::array<uint8_t, 156> voiceData;  // 156 bytes
        
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
    
    // Handle MiniDexed SysEx (global or TG)
    // partIndex: -1 for global, 0..7 for TG
    bool handleSysex(const uint8_t* data, int len, std::vector<uint8_t>& response, int partIndex = -1);
};

} // namespace FMRack
