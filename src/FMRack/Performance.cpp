#include "Performance.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>

namespace FMRack {

Performance::Performance() {
    // Initialize with default values
    setDefaults();
}

Performance::~Performance() {
    // Default destructor
}

bool Performance::loadFromFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return false;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        
        // Parse key=value pairs
        size_t equalPos = line.find('=');
        if (equalPos == std::string::npos) {
            continue;
        }
        
        std::string key = line.substr(0, equalPos);
        std::string value = line.substr(equalPos + 1);
        
        // Trim whitespace
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        
        // Parse numbered parameters for parts 1-8
        for (int part = 1; part <= 8; ++part) {
            if (key == "MIDIChannel" + std::to_string(part)) {
                parts[part-1].midiChannel = static_cast<uint8_t>(std::stoi(value));
            } else if (key == "Volume" + std::to_string(part)) {
                parts[part-1].volume = static_cast<uint8_t>(std::stoi(value));
            } else if (key == "Pan" + std::to_string(part)) {
                parts[part-1].pan = static_cast<uint8_t>(std::stoi(value));
            } else if (key == "Detune" + std::to_string(part)) {
                parts[part-1].detune = static_cast<int8_t>(std::stoi(value));
            } else if (key == "NoteShift" + std::to_string(part)) {
                parts[part-1].noteShift = static_cast<int8_t>(std::stoi(value));
            } else if (key == "NoteLimitLow" + std::to_string(part)) {
                parts[part-1].noteLimitLow = static_cast<uint8_t>(std::stoi(value));
            } else if (key == "NoteLimitHigh" + std::to_string(part)) {
                parts[part-1].noteLimitHigh = static_cast<uint8_t>(std::stoi(value));
            } else if (key == "ReverbSend" + std::to_string(part)) {
                parts[part-1].reverbSend = static_cast<uint8_t>(std::stoi(value));
            } else if (key == "MonoMode" + std::to_string(part)) {
                parts[part-1].monoMode = static_cast<uint8_t>(std::stoi(value));
            } else if (key == "VoiceData" + std::to_string(part)) {
                // Parse hex bytes separated by spaces
                std::istringstream hexStream(value);
                std::string hexByte;
                int byteIndex = 0;
                while (hexStream >> hexByte && byteIndex < 155) {
                    parts[part-1].voiceData[byteIndex] = static_cast<uint8_t>(std::stoul(hexByte, nullptr, 16));
                    byteIndex++;
                }
            }
        }
        
        // Parse global effects settings
        if (key == "ReverbEnable") {
            effects.reverbEnable = (std::stoi(value) != 0);
        } else if (key == "ReverbSize") {
            effects.reverbSize = static_cast<uint8_t>(std::stoi(value));
        } else if (key == "ReverbLevel") {
            effects.reverbLevel = static_cast<uint8_t>(std::stoi(value));
        }
    }
    
    return true;
}

void Performance::setDefaults() {
    // Initialize with a basic FM patch (INIT VOICE)
    std::array<uint8_t, 155> initVoice = {{
        99, 99, 99, 99, 99, 99, 99, 00, 33, 00, 00, 00, 00, 00, 00, 00, 00, 00, 01, 00, 00, // OP6
        99, 99, 99, 99, 99, 99, 99, 00, 33, 00, 00, 00, 00, 00, 00, 00, 00, 00, 01, 00, 00, // OP5
        99, 99, 99, 99, 99, 99, 99, 00, 33, 00, 00, 00, 00, 00, 00, 00, 00, 00, 01, 00, 00, // OP4
        99, 99, 99, 99, 99, 99, 99, 00, 33, 00, 00, 00, 00, 00, 00, 00, 00, 00, 01, 00, 00, // OP3
        99, 99, 99, 99, 99, 99, 99, 00, 33, 00, 00, 00, 00, 00, 00, 00, 00, 00, 01, 00, 00, // OP2
        99, 99, 99, 99, 99, 99, 99, 00, 33, 00, 00, 00, 00, 00, 00, 00, 00, 00, 01, 00, 00, // OP1
        99, 99, 99, 99, 50, 50, 50, 50,                                                     // 4 * pitch EG rates, 4 * pitch EG level
        01, 00, 01,                                                                         // algorithm, feedback, osc sync
        35, 00, 00, 00, 01, 00,                                                             // lfo speed, lfo delay, lfo pitch_mod_depth, lfo_amp_mod_depth, lfo_sync, lfo_waveform
        03, 48,                                                                             // pitch_mod_sensitivity, transpose
        73, 78, 73, 84, 32, 86, 79, 73, 67, 69                                              // 10 * char for name ("INIT VOICE")
    }};
    
    // Set default values for all parts
    for (int i = 0; i < 8; ++i) {
        parts[i] = PartConfig{};  // Use default constructor values
        parts[i].voiceData = initVoice;
    }
    
    // Enable first part by default
    parts[0].midiChannel = 1;
    parts[0].volume = 100;
    
    // Set default effects
    effects = EffectsConfig{};
}

const Performance::PartConfig& Performance::getPartConfig(int partIndex) const {
    if (partIndex >= 0 && partIndex < 8) {
        return parts[partIndex];
    }
    static PartConfig defaultConfig;
    return defaultConfig;
}

int Performance::getEnabledPartCount() const {
    int count = 0;
    for (const auto& part : parts) {
        if (part.midiChannel > 0) count++;
    }
    return count;
}

} // namespace FMRack
