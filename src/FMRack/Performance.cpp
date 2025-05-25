#include "Performance.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <vector>
#include <cstdint>

namespace FMRack {

Performance::Performance() {
    // Initialize with default values
    setDefaults(16, 1); // Added unisonVoices parameter
}

Performance::~Performance() {
    // Default destructor
}

bool Performance::loadFromFile(const std::string& filename) {
    std::cout << "[DEBUG] Attempting to load performance file: " << filename << std::endl;
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cout << "[DEBUG] Could not open performance file: " << filename << std::endl;
        return false;
    }
    std::string line;
    int lineNum = 0;
    while (std::getline(file, line)) {
        ++lineNum;
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        // Parse key=value pairs
        size_t equalPos = line.find('=');
        if (equalPos == std::string::npos) {
            std::cout << "[DEBUG] Skipping malformed line " << lineNum << ": " << line << std::endl;
            continue;
        }
        std::string key = line.substr(0, equalPos);
        std::string value = line.substr(equalPos + 1);
        // Trim whitespace
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        std::cout << "[DEBUG] Parsed key: '" << key << "', value: '" << value << "' (line " << lineNum << ")" << std::endl;
        // Parse numbered parameters for parts 1-8
        for (int part = 1; part <= 8; ++part) {
            if (key == "BankNumber" + std::to_string(part)) {
                int v = std::stoi(value);
                parts[part-1].bankNumber = std::clamp(v, 0, 127);
            } else if (key == "VoiceNumber" + std::to_string(part)) {
                int v = std::stoi(value);
                parts[part-1].voiceNumber = std::clamp(v, 1, 32);
            } else if (key == "MIDIChannel" + std::to_string(part)) {
                int v = std::stoi(value);
                parts[part-1].midiChannel = (v < 0) ? 0 : (v > 255 ? 255 : v); // allow 0, 1..16, >16 for omni
            } else if (key == "Volume" + std::to_string(part)) {
                int v = std::stoi(value);
                parts[part-1].volume = std::clamp(v, 0, 127);
            } else if (key == "Pan" + std::to_string(part)) {
                int v = std::stoi(value);
                parts[part-1].pan = std::clamp(v, 0, 127);
            } else if (key == "Detune" + std::to_string(part)) {
                int v = std::stoi(value);
                parts[part-1].detune = std::clamp(v, -99, 99);
            } else if (key == "Cutoff" + std::to_string(part)) {
                int v = std::stoi(value);
                parts[part-1].cutoff = std::clamp(v, 0, 99);
            } else if (key == "Resonance" + std::to_string(part)) {
                int v = std::stoi(value);
                parts[part-1].resonance = std::clamp(v, 0, 99);
            } else if (key == "NoteLimitLow" + std::to_string(part)) {
                int v = std::stoi(value);
                parts[part-1].noteLimitLow = std::clamp(v, 0, 127);
            } else if (key == "NoteLimitHigh" + std::to_string(part)) {
                int v = std::stoi(value);
                parts[part-1].noteLimitHigh = std::clamp(v, 0, 127);
            } else if (key == "NoteShift" + std::to_string(part)) {
                int v = std::stoi(value);
                parts[part-1].noteShift = std::clamp(v, -24, 24);
            } else if (key == "ReverbSend" + std::to_string(part)) {
                int v = std::stoi(value);
                parts[part-1].reverbSend = std::clamp(v, 0, 99);
            } else if (key == "PitchBendRange" + std::to_string(part)) {
                int v = std::stoi(value);
                parts[part-1].pitchBendRange = std::clamp(v, 0, 12);
            } else if (key == "PitchBendStep" + std::to_string(part)) {
                int v = std::stoi(value);
                parts[part-1].pitchBendStep = std::clamp(v, 0, 12);
            } else if (key == "PortamentoMode" + std::to_string(part)) {
                int v = std::stoi(value);
                parts[part-1].portamentoMode = std::clamp(v, 0, 1);
            } else if (key == "PortamentoGlissando" + std::to_string(part)) {
                int v = std::stoi(value);
                parts[part-1].portamentoGlissando = std::clamp(v, 0, 1);
            } else if (key == "PortamentoTime" + std::to_string(part)) {
                int v = std::stoi(value);
                parts[part-1].portamentoTime = std::clamp(v, 0, 99);
            } else if (key == "MonoMode" + std::to_string(part)) {
                int v = std::stoi(value);
                parts[part-1].monoMode = std::clamp(v, 0, 1);
            } else if (key == "ModulationWheelRange" + std::to_string(part)) {
                int v = std::stoi(value);
                parts[part-1].modulationWheelRange = std::clamp(v, 0, 99);
            } else if (key == "ModulationWheelTarget" + std::to_string(part)) {
                int v = std::stoi(value);
                parts[part-1].modulationWheelTarget = std::clamp(v, 0, 7);
            } else if (key == "FootControlRange" + std::to_string(part)) {
                int v = std::stoi(value);
                parts[part-1].footControlRange = std::clamp(v, 0, 99);
            } else if (key == "FootControlTarget" + std::to_string(part)) {
                int v = std::stoi(value);
                parts[part-1].footControlTarget = std::clamp(v, 0, 7);
            } else if (key == "BreathControlRange" + std::to_string(part)) {
                int v = std::stoi(value);
                parts[part-1].breathControlRange = std::clamp(v, 0, 99);
            } else if (key == "BreathControlTarget" + std::to_string(part)) {
                int v = std::stoi(value);
                parts[part-1].breathControlTarget = std::clamp(v, 0, 7);
            } else if (key == "AftertouchRange" + std::to_string(part)) {
                int v = std::stoi(value);
                parts[part-1].aftertouchRange = std::clamp(v, 0, 99);
            } else if (key == "AftertouchTarget" + std::to_string(part)) {
                int v = std::stoi(value);
                parts[part-1].aftertouchTarget = std::clamp(v, 0, 7);
            } else if (key == "VoiceData" + std::to_string(part)) {
                std::istringstream hexStream(value);
                std::string hexByte;
                int byteIndex = 0;
                while (hexStream >> hexByte && byteIndex < 156) {
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

void Performance::setDefaults(int numParts, int unisonVoices) {
    // Initialize with a basic FM voice (INIT VOICE)
    std::array<uint8_t, 156> initVoice = {{
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
        73, 78, 73, 84, 32, 86, 79, 73, 67, 69,                                             // 10 * char for name ("INIT VOICE")
        0                                                                                   // pad to 156 bytes
    }};
    
    // Set default values for all parts
    for (int i = 0; i < 16; ++i) {
        this->parts[i] = PartConfig{};  // Use default constructor values
        this->parts[i].voiceData = initVoice;
        if (i < numParts) {
            this->parts[i].midiChannel = i + 1; // Assign unique MIDI channels
            this->parts[i].volume = 100;
            this->parts[i].unisonVoices = unisonVoices; // Use the provided unison voices value
        } else {
            this->parts[i].midiChannel = 0; // Disable unused parts
        }
    }

    // Set default effects
    this->effects = EffectsConfig{};
}

const Performance::PartConfig& Performance::getPartConfig(int partIndex) const {
    if (partIndex >= 0 && partIndex < 16) {
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

// Helper: encode signed int8_t as 2 MIDI 7-bit bytes (2's complement, 14-bit)
static void encode_signed_14bit(int8_t val, uint8_t& msb, uint8_t& lsb) {
    int16_t v = static_cast<int16_t>(val);
    uint16_t midi14 = static_cast<uint16_t>(v & 0x3FFF);
    msb = (midi14 >> 7) & 0x7F;
    lsb = midi14 & 0x7F;
}
// Helper: decode signed 14-bit from 2 MIDI 7-bit bytes
static int8_t decode_signed_14bit(uint8_t msb, uint8_t lsb) {
    int16_t midi14 = ((msb & 0x7F) << 7) | (lsb & 0x7F);
    if (midi14 & 0x2000) midi14 |= 0xC000; // sign extend
    return static_cast<int8_t>(midi14);
}

bool Performance::handleSysex(const uint8_t* data, int len, std::vector<uint8_t>& response, int partIndex) {
    std::cout << "[PERFORMANCE] MiniDexed SysEx received, length: " << len << " bytes, partIndex: " << partIndex << "\n";

    if (len < 5 || data[0] != 0xF0 || data[1] != 0x7D || data[len-1] != 0xF7) return false;
    uint8_t cmd = data[2];
    std::cout << "[PERFORMANCE] MiniDexed command: " << std::hex << static_cast<int>(cmd) << " (partIndex: " << partIndex << ")\n";
    // Global GET
    if (cmd == 0x10 && partIndex == -1) {
        std::cout << "[PERFORMANCE] Global GET command received\n";
        response = {0xF0, 0x7D, 0x20};
        auto& eff = effects;
        auto add = [&](uint16_t p, uint16_t v) {
            response.push_back((p >> 8) & 0x7F); response.push_back(p & 0x7F);
            response.push_back((v >> 8) & 0x7F); response.push_back(v & 0x7F);
        };
        add(0x0000, eff.compressorEnable ? 1 : 0);
        add(0x0001, eff.reverbEnable ? 1 : 0);
        add(0x0002, eff.reverbSize);
        add(0x0003, eff.reverbHighDamp);
        add(0x0004, eff.reverbLowDamp);
        add(0x0005, eff.reverbLowPass);
        add(0x0006, eff.reverbDiffusion);
        add(0x0007, eff.reverbLevel);
        response.push_back(0xF7);
        return true;
    }
    // TG GET
    if (cmd == 0x11 && len >= 5 && partIndex >= 0 && partIndex < (int)parts.size()) {
        std::cout << "[PERFORMANCE] TG GET command received for part " << partIndex << "\n";
        response = {0xF0, 0x7D, 0x21, static_cast<uint8_t>(partIndex)};
        const auto& p = parts[partIndex];
        auto add = [&](uint16_t param, uint16_t value) {
            response.push_back((param >> 8) & 0x7F); response.push_back(param & 0x7F);
            response.push_back((value >> 8) & 0x7F); response.push_back(value & 0x7F);
        };
        add(0x0000, p.bankNumber);
        add(0x0001, p.voiceNumber);
        add(0x0002, p.midiChannel);
        add(0x0003, p.volume);
        add(0x0004, p.pan);
        // Detune (signed)
        uint8_t msb, lsb;
        encode_signed_14bit(p.detune, msb, lsb);
        add(0x0005, (msb << 8) | lsb);
        add(0x0006, p.cutoff);
        add(0x0007, p.resonance);
        add(0x0008, p.noteLimitLow);
        add(0x0009, p.noteLimitHigh);
        // NoteShift (signed)
        encode_signed_14bit(p.noteShift, msb, lsb);
        add(0x000A, (msb << 8) | lsb);
        add(0x000B, p.reverbSend);
        add(0x000C, p.pitchBendRange);
        add(0x000D, p.pitchBendStep);
        add(0x000E, p.portamentoMode);
        add(0x000F, p.portamentoGlissando);
        add(0x0010, p.portamentoTime);
        add(0x0011, p.monoMode);
        add(0x0012, p.modulationWheelRange);
        add(0x0013, p.modulationWheelTarget);
        add(0x0014, p.footControlRange);
        add(0x0015, p.footControlTarget);
        add(0x0016, p.breathControlRange);
        add(0x0017, p.breathControlTarget);
        add(0x0018, p.aftertouchRange);
        add(0x0019, p.aftertouchTarget);
        response.push_back(0xF7);
        return true;
    }
    // Global SET
    if (cmd == 0x20 /* && partIndex == -1 */) { // Ignore partIndex for global
        std::cout << "[PERFORMANCE] Global SET command received\n";
        int offset = 3;
        while (offset + 3 < len - 1) {
            uint16_t param = (data[offset] << 8) | data[offset+1];
            uint16_t value = (data[offset+2] << 8) | data[offset+3];
            offset += 4;
            switch (param) {
                case 0x0000:
                    std::cout << "Setting CompressorEnable to " << (value != 0) << std::endl;
                    effects.compressorEnable = (value != 0); break;
                case 0x0001:
                    std::cout << "Setting ReverbEnable to " << (value != 0) << std::endl;
                    effects.reverbEnable = (value != 0); break;
                case 0x0002:
                    std::cout << "Setting ReverbSize to " << (value & 0x7F) << std::endl;
                    effects.reverbSize = value & 0x7F; break;
                case 0x0003:
                    std::cout << "Setting ReverbHighDamp to " << (value & 0x7F) << std::endl;
                    effects.reverbHighDamp = value & 0x7F; break;
                case 0x0004:
                    std::cout << "Setting ReverbLowDamp to " << (value & 0x7F) << std::endl;
                    effects.reverbLowDamp = value & 0x7F; break;
                case 0x0005:
                    std::cout << "Setting ReverbLowPass to " << (value & 0x7F) << std::endl;
                    effects.reverbLowPass = value & 0x7F; break;
                case 0x0006:
                    std::cout << "Setting ReverbDiffusion to " << (value & 0x7F) << std::endl;
                    effects.reverbDiffusion = value & 0x7F; break;
                case 0x0007:
                    std::cout << "Setting ReverbLevel to " << (value & 0x7F) << std::endl;
                    effects.reverbLevel = value & 0x7F; break;
                default: break;
            }
        }
        return true;
    }
    // TG SET
    if (cmd == 0x21 && len >= 8 && partIndex >= 0 && partIndex < (int)parts.size()) {
        std::cout << "[PERFORMANCE] TG SET command received for part " << partIndex << "\n";
        int offset = 4;
        auto& p = parts[partIndex];
        while (offset + 3 < len - 1) {
            uint16_t param = (data[offset] << 8) | data[offset+1];
            uint16_t value = (data[offset+2] << 8) | data[offset+3];
            offset += 4;
            switch (param) {
                case 0x0000:
                    std::cout << "Setting BankNumber to " << (value & 0x7F) << " (part " << partIndex << ")" << std::endl;
                    p.bankNumber = value & 0x7F; break;
                case 0x0001:
                    std::cout << "Setting VoiceNumber to " << (value & 0x1F) << " (part " << partIndex << ")" << std::endl;
                    p.voiceNumber = value & 0x1F; break;
                case 0x0002:
                    std::cout << "Setting MIDIChannel to " << (value & 0x7F) << " (part " << partIndex << ")" << std::endl;
                    p.midiChannel = value & 0x7F; break;
                case 0x0003:
                    std::cout << "Setting Volume to " << (value & 0x7F) << " (part " << partIndex << ")" << std::endl;
                    p.volume = value & 0x7F; break;
                case 0x0004:
                    std::cout << "Setting Pan to " << (value & 0x7F) << " (part " << partIndex << ")" << std::endl;
                    p.pan = value & 0x7F; break;
                case 0x0005: {
                    int8_t detune = decode_signed_14bit((value >> 8) & 0x7F, value & 0x7F);
                    std::cout << "Setting Detune to " << static_cast<int>(detune) << " (part " << partIndex << ")" << std::endl;
                    p.detune = detune; break;
                }
                case 0x0006:
                    std::cout << "Setting Cutoff to " << (value & 0x7F) << " (part " << partIndex << ")" << std::endl;
                    p.cutoff = value & 0x7F; break;
                case 0x0007:
                    std::cout << "Setting Resonance to " << (value & 0x7F) << " (part " << partIndex << ")" << std::endl;
                    p.resonance = value & 0x7F; break;
                case 0x0008:
                    std::cout << "Setting NoteLimitLow to " << (value & 0x7F) << " (part " << partIndex << ")" << std::endl;
                    p.noteLimitLow = value & 0x7F; break;
                case 0x0009:
                    std::cout << "Setting NoteLimitHigh to " << (value & 0x7F) << " (part " << partIndex << ")" << std::endl;
                    p.noteLimitHigh = value & 0x7F; break;
                case 0x000A: {
                    int8_t noteShift = decode_signed_14bit((value >> 8) & 0x7F, value & 0x7F);
                    std::cout << "Setting NoteShift to " << static_cast<int>(noteShift) << " (part " << partIndex << ")" << std::endl;
                    p.noteShift = noteShift; break;
                }
                case 0x000B:
                    std::cout << "Setting ReverbSend to " << (value & 0x7F) << " (part " << partIndex << ")" << std::endl;
                    p.reverbSend = value & 0x7F; break;
                case 0x000C:
                    std::cout << "Setting PitchBendRange to " << (value & 0x7F) << " (part " << partIndex << ")" << std::endl;
                    p.pitchBendRange = value & 0x7F; break;
                case 0x000D:
                    std::cout << "Setting PitchBendStep to " << (value & 0x7F) << " (part " << partIndex << ")" << std::endl;
                    p.pitchBendStep = value & 0x7F; break;
                case 0x000E:
                    std::cout << "Setting PortamentoMode to " << (value & 0x7F) << " (part " << partIndex << ")" << std::endl;
                    p.portamentoMode = value & 0x7F; break;
                case 0x000F:
                    std::cout << "Setting PortamentoGlissando to " << (value & 0x7F) << " (part " << partIndex << ")" << std::endl;
                    p.portamentoGlissando = value & 0x7F; break;
                case 0x0010:
                    std::cout << "Setting PortamentoTime to " << (value & 0x7F) << " (part " << partIndex << ")" << std::endl;
                    p.portamentoTime = value & 0x7F; break;
                case 0x0011:
                    std::cout << "Setting MonoMode to " << (value & 0x7F) << " (part " << partIndex << ")" << std::endl;
                    p.monoMode = value & 0x7F; break;
                case 0x0012:
                    std::cout << "Setting ModulationWheelRange to " << (value & 0x7F) << " (part " << partIndex << ")" << std::endl;
                    p.modulationWheelRange = value & 0x7F; break;
                case 0x0013:
                    std::cout << "Setting ModulationWheelTarget to " << (value & 0x7F) << " (part " << partIndex << ")" << std::endl;
                    p.modulationWheelTarget = value & 0x7F; break;
                case 0x0014:
                    std::cout << "Setting FootControlRange to " << (value & 0x7F) << " (part " << partIndex << ")" << std::endl;
                    p.footControlRange = value & 0x7F; break;
                case 0x0015:
                    std::cout << "Setting FootControlTarget to " << (value & 0x7F) << " (part " << partIndex << ")" << std::endl;
                    p.footControlTarget = value & 0x7F; break;
                case 0x0016:
                    std::cout << "Setting BreathControlRange to " << (value & 0x7F) << " (part " << partIndex << ")" << std::endl;
                    p.breathControlRange = value & 0x7F; break;
                case 0x0017:
                    std::cout << "Setting BreathControlTarget to " << (value & 0x7F) << " (part " << partIndex << ")" << std::endl;
                    p.breathControlTarget = value & 0x7F; break;
                case 0x0018:
                    std::cout << "Setting AftertouchRange to " << (value & 0x7F) << " (part " << partIndex << ")" << std::endl;
                    p.aftertouchRange = value & 0x7F; break;
                case 0x0019:
                    std::cout << "Setting AftertouchTarget to " << (value & 0x7F) << " (part " << partIndex << ")" << std::endl;
                    p.aftertouchTarget = value & 0x7F; break;
                default: break;
            }
        }
        return true;
    }
    return false;
}

} // namespace FMRack
