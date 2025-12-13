#include "Performance.h"
#include "VoiceData.h"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <vector>
#include <cstdint>
#include <iomanip> // For std::setw, std::setfill

// Add global debug flag
extern bool debugEnabled;

namespace FMRack {

Performance::Performance() {
    // Initialize with default values
    setDefaults(1, 1); // Added unisonVoices parameter
}

Performance::~Performance() {
    // Default destructor
}

bool Performance::loadFromFile(const std::string& filename) {
    setDefaults(0, 1); // Reset all parts to an inactive default state before loading
    std::cout << "Attempting to load performance file: " << filename << std::endl;
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cout << "Could not open performance file: " << filename << std::endl;
        return false;
    }
    std::cout << "[Performance::loadFromFile] File opened successfully: " << filename << std::endl;
    std::string line;
    int lineNum = 0;
    int max_part_number_in_file = 0; // Track the highest part number (1-based) with settings in the file

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
        std::cout << "[Performance::loadFromFile] Parsed key: '" << key << "', value: '" << value << "' (line " << lineNum << ")" << std::endl;
        // Parse numbered parameters for parts 1-8
        for (int part_num = 1; part_num <= 8; ++part_num) { // part_num is 1-based
            bool setting_found_for_this_part_key = false;
            if (key == "BankNumber" + std::to_string(part_num)) {
                int v = std::stoi(value);
                parts[part_num-1].bankNumber = static_cast<uint8_t>(std::clamp(v, 0, 127));
                setting_found_for_this_part_key = true;
            } else if (key == "VoiceNumber" + std::to_string(part_num)) {
                int v = std::stoi(value);
                parts[part_num-1].voiceNumber = static_cast<uint8_t>(std::clamp(v, 1, 32));
                setting_found_for_this_part_key = true;
            } else if (key == "MIDIChannel" + std::to_string(part_num)) {
                int v = std::stoi(value);
                parts[part_num-1].midiChannel = (v < 0) ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); // allow 0, 1..16, >16 for omni
                setting_found_for_this_part_key = true;
            } else if (key == "Volume" + std::to_string(part_num)) {
                int v = std::stoi(value);
                parts[part_num-1].volume = static_cast<uint8_t>(std::clamp(v, 0, 127));
                setting_found_for_this_part_key = true;
            } else if (key == "Pan" + std::to_string(part_num)) {
                int v = std::stoi(value);
                parts[part_num-1].pan = static_cast<uint8_t>(std::clamp(v, 0, 127));
                setting_found_for_this_part_key = true;
            } else if (key == "Detune" + std::to_string(part_num)) {
                int v = std::stoi(value);
                parts[part_num-1].detune = static_cast<int8_t>(std::clamp(v, -99, 99));
                setting_found_for_this_part_key = true;
            } else if (key == "Cutoff" + std::to_string(part_num)) {
                int v = std::stoi(value);
                parts[part_num-1].cutoff = static_cast<uint8_t>(std::clamp(v, 0, 99));
                setting_found_for_this_part_key = true;
            } else if (key == "Resonance" + std::to_string(part_num)) {
                int v = std::stoi(value);
                parts[part_num-1].resonance = static_cast<uint8_t>(std::clamp(v, 0, 99));
                setting_found_for_this_part_key = true;
            } else if (key == "NoteLimitLow" + std::to_string(part_num)) {
                int v = std::stoi(value);
                parts[part_num-1].noteLimitLow = static_cast<uint8_t>(std::clamp(v, 0, 127));
                setting_found_for_this_part_key = true;
            } else if (key == "NoteLimitHigh" + std::to_string(part_num)) {
                int v = std::stoi(value);
                parts[part_num-1].noteLimitHigh = static_cast<uint8_t>(std::clamp(v, 0, 127));
                setting_found_for_this_part_key = true;
            } else if (key == "NoteShift" + std::to_string(part_num)) {
                int v = std::stoi(value);
                parts[part_num-1].noteShift = static_cast<int8_t>(std::clamp(v, -24, 24));
                setting_found_for_this_part_key = true;
            } else if (key == "ReverbSend" + std::to_string(part_num)) {
                int v = std::stoi(value);
                parts[part_num-1].reverbSend = static_cast<uint8_t>(std::clamp(v, 0, 99));
                setting_found_for_this_part_key = true;
            } else if (key == "PitchBendRange" + std::to_string(part_num)) {
                int v = std::stoi(value);
                parts[part_num-1].pitchBendRange = static_cast<uint8_t>(std::clamp(v, 0, 12));
                setting_found_for_this_part_key = true;
            } else if (key == "PitchBendStep" + std::to_string(part_num)) {
                int v = std::stoi(value);
                parts[part_num-1].pitchBendStep = static_cast<uint8_t>(std::clamp(v, 0, 12));
                setting_found_for_this_part_key = true;
            } else if (key == "PortamentoMode" + std::to_string(part_num)) {
                int v = std::stoi(value);
                parts[part_num-1].portamentoMode = static_cast<uint8_t>(std::clamp(v, 0, 1));
                setting_found_for_this_part_key = true;
            } else if (key == "PortamentoGlissando" + std::to_string(part_num)) {
                int v = std::stoi(value);
                parts[part_num-1].portamentoGlissando = static_cast<uint8_t>(std::clamp(v, 0, 1));
                setting_found_for_this_part_key = true;
            } else if (key == "PortamentoTime" + std::to_string(part_num)) {
                int v = std::stoi(value);
                parts[part_num-1].portamentoTime = static_cast<uint8_t>(std::clamp(v, 0, 99));
                setting_found_for_this_part_key = true;
            } else if (key == "MonoMode" + std::to_string(part_num)) {
                int v = std::stoi(value);
                parts[part_num-1].monoMode = static_cast<uint8_t>(std::clamp(v, 0, 1));
                setting_found_for_this_part_key = true;
            } else if (key == "ModulationWheelRange" + std::to_string(part_num)) {
                int v = std::stoi(value);
                parts[part_num-1].modulationWheelRange = static_cast<uint8_t>(std::clamp(v, 0, 99));
                setting_found_for_this_part_key = true;
            } else if (key == "ModulationWheelTarget" + std::to_string(part_num)) {
                int v = std::stoi(value);
                parts[part_num-1].modulationWheelTarget = static_cast<uint8_t>(std::clamp(v, 0, 7));
                setting_found_for_this_part_key = true;
            } else if (key == "FootControlRange" + std::to_string(part_num)) {
                int v = std::stoi(value);
                parts[part_num-1].footControlRange = static_cast<uint8_t>(std::clamp(v, 0, 99));
                setting_found_for_this_part_key = true;
            } else if (key == "FootControlTarget" + std::to_string(part_num)) {
                int v = std::stoi(value);
                parts[part_num-1].footControlTarget = static_cast<uint8_t>(std::clamp(v, 0, 7));
                setting_found_for_this_part_key = true;
            } else if (key == "BreathControlRange" + std::to_string(part_num)) {
                int v = std::stoi(value);
                parts[part_num-1].breathControlRange = static_cast<uint8_t>(std::clamp(v, 0, 99));
                setting_found_for_this_part_key = true;
            } else if (key == "BreathControlTarget" + std::to_string(part_num)) {
                int v = std::stoi(value);
                parts[part_num-1].breathControlTarget = static_cast<uint8_t>(std::clamp(v, 0, 7));
                setting_found_for_this_part_key = true;
            } else if (key == "AftertouchRange" + std::to_string(part_num)) {
                int v = std::stoi(value);
                parts[part_num-1].aftertouchRange = static_cast<uint8_t>(std::clamp(v, 0, 99));
                setting_found_for_this_part_key = true;
            } else if (key == "AftertouchTarget" + std::to_string(part_num)) {
                int v = std::stoi(value);
                parts[part_num-1].aftertouchTarget = static_cast<uint8_t>(std::clamp(v, 0, 7));
                setting_found_for_this_part_key = true;
            } else if (key == "VoiceData" + std::to_string(part_num)) {
                std::istringstream hexStream(value);
                std::string hexByte;
                int byteIndex = 0;
                while (hexStream >> hexByte && byteIndex < 156) {
                    parts[part_num-1].voiceData[byteIndex] = static_cast<uint8_t>(std::stoul(hexByte, nullptr, 16));
                    byteIndex++;
                }
                setting_found_for_this_part_key = true;
            }
            if (setting_found_for_this_part_key) {
                max_part_number_in_file = std::max(max_part_number_in_file, part_num);
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
    
    // Deactivate parts that are beyond the highest part number found in the INI file
    for (int i = 0; i < 16; ++i) {
        if ((i + 1) > max_part_number_in_file) {
            parts[i].midiChannel = 0; // Deactivate part
            parts[i].volume = 0;
            parts[i].unisonVoices = 1; // Set to single voice if inactive
            // Other parameters can retain their defaults from setDefaults, 
            // as they won't be used if midiChannel is 0.
        }
    }
    
    return true;
}

bool Performance::saveToFile(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cout << "[Performance::saveToFile] Could not open file for writing: " << filename << std::endl;
        return false;
    }
    // Write header
    file << "; FMRack Performance INI\n";
    file << "; Saved on: ";
    time_t now = time(nullptr);
    {
#ifdef _WIN32
    std::tm tmSnapshot{};
    localtime_s(&tmSnapshot, &now);
    file << std::put_time(&tmSnapshot, "%Y-%m-%d %H:%M:%S") << "\n\n";
#else
    std::tm tmSnapshot{};
    localtime_r(&now, &tmSnapshot);
    file << std::put_time(&tmSnapshot, "%Y-%m-%d %H:%M:%S") << "\n\n";
#endif
    }

    // Write part configs (1-based)
    for (int part_num = 1; part_num <= 8; ++part_num) {
        const auto& part = parts[part_num-1];
        // Only save enabled parts (midiChannel > 0)
        if (part.midiChannel == 0) continue;
        file << "BankNumber" << part_num << "=" << (int)part.bankNumber << "\n";
        file << "VoiceNumber" << part_num << "=" << (int)part.voiceNumber << "\n";
        file << "MIDIChannel" << part_num << "=" << (int)part.midiChannel << "\n";
        file << "Volume" << part_num << "=" << (int)part.volume << "\n";
        file << "Pan" << part_num << "=" << (int)part.pan << "\n";
        file << "Detune" << part_num << "=" << (int)part.detune << "\n";
        file << "Cutoff" << part_num << "=" << (int)part.cutoff << "\n";
        file << "Resonance" << part_num << "=" << (int)part.resonance << "\n";
        file << "NoteLimitLow" << part_num << "=" << (int)part.noteLimitLow << "\n";
        file << "NoteLimitHigh" << part_num << "=" << (int)part.noteLimitHigh << "\n";
        file << "NoteShift" << part_num << "=" << (int)part.noteShift << "\n";
        file << "ReverbSend" << part_num << "=" << (int)part.reverbSend << "\n";
        file << "PitchBendRange" << part_num << "=" << (int)part.pitchBendRange << "\n";
        file << "PitchBendStep" << part_num << "=" << (int)part.pitchBendStep << "\n";
        file << "PortamentoMode" << part_num << "=" << (int)part.portamentoMode << "\n";
        file << "PortamentoGlissando" << part_num << "=" << (int)part.portamentoGlissando << "\n";
        file << "PortamentoTime" << part_num << "=" << (int)part.portamentoTime << "\n";
        file << "MonoMode" << part_num << "=" << (int)part.monoMode << "\n";
        file << "ModulationWheelRange" << part_num << "=" << (int)part.modulationWheelRange << "\n";
        file << "ModulationWheelTarget" << part_num << "=" << (int)part.modulationWheelTarget << "\n";
        file << "FootControlRange" << part_num << "=" << (int)part.footControlRange << "\n";
        file << "FootControlTarget" << part_num << "=" << (int)part.footControlTarget << "\n";
        file << "BreathControlRange" << part_num << "=" << (int)part.breathControlRange << "\n";
        file << "BreathControlTarget" << part_num << "=" << (int)part.breathControlTarget << "\n";
        file << "AftertouchRange" << part_num << "=" << (int)part.aftertouchRange << "\n";
        file << "AftertouchTarget" << part_num << "=" << (int)part.aftertouchTarget << "\n";
        // Write voice data as hex bytes
        file << "VoiceData" << part_num << "=";
        for (size_t i = 0; i < part.voiceData.size(); ++i) {
            file << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << (int)part.voiceData[i];
            if (i < part.voiceData.size() - 1) file << " ";
        }
        file << std::dec << "\n";
    }
    // Write global effects
    file << "\nReverbEnable=" << (effects.reverbEnable ? 1 : 0) << "\n";
    file << "ReverbSize=" << (int)effects.reverbSize << "\n";
    file << "ReverbLevel=" << (int)effects.reverbLevel << "\n";
    // Optionally add more global effect parameters here if needed
    file.close();
    return true;
}

void Performance::setDefaults(int numParts, int unisonVoices) {
    // Initialize with a basic FM voice (voice.)

    std::array<uint8_t, 156> initVoice = {{
        99, 99, 99, 99, 99, 99, 99, 0, 39, 0, 0, 0, 0, 0, 0, 0,  0, 0, 1, 0, 7, // OP6
        99, 99, 99, 99, 99, 99, 99, 0, 39, 0, 0, 0, 0, 0, 0, 0,  0, 0, 1, 0, 7, // OP5
        99, 99, 99, 99, 99, 99, 99, 0, 39, 0, 0, 0, 0, 0, 0, 0,  0, 0, 1, 0, 7, // OP4
        99, 99, 99, 99, 99, 99, 99, 0, 39, 0, 0, 0, 0, 0, 0, 0,  0, 0, 1, 0, 7, // OP3
        99, 99, 99, 99, 99, 99, 99, 0, 39, 0, 0, 0, 0, 0, 0, 0,  0, 0, 1, 0, 7, // OP2
        99, 99, 99, 99, 99, 99, 99, 0, 39, 0, 0, 0, 0, 0, 0, 0, 99, 0, 1, 0, 7, // OP1
        99, 99, 99, 99, 50, 50, 50, 50, // 4 * pitch EG rates, 4 * pitch EG level
        0, 0, 1, // algorithm, feedback, osc sync
        35, 0, 0, 0, 1, 0, // lfo speed, lfo delay, lfo pitch_mod_depth, lfo_amp_mod_depth, lfo_sync, lfo_waveform
        3, 24, // pitch_mod_sensitivity, transpose
        73, 78, 73, 84, 32, 86, 79, 73, 67, 69, // 10 * char for name ("voice.")
        0 // pad to 156 bytes
    }};

    // Set default values for all parts
    for (int i = 0; i < 16; ++i) {
        this->parts[i] = PartConfig{};  // Start with value-initialized (mostly zeros)
        this->parts[i].voiceData = initVoice;

        // Explicit defaults for parameters
        this->parts[i].bankNumber = 0;
        this->parts[i].voiceNumber = 1; // Typically 1-indexed in UIs
        this->parts[i].pan = 64;        // Center pan
        this->parts[i].detune = 0;
        this->parts[i].cutoff = 99;     // Filter fully open
        this->parts[i].resonance = 0;
        this->parts[i].noteLimitLow = 0;    // No lower note limit
        this->parts[i].noteLimitHigh = 127; // No upper note limit
        this->parts[i].noteShift = 0;
        this->parts[i].reverbSend = 0;
        this->parts[i].pitchBendRange = 2;  // Standard +/- 2 semitones
        this->parts[i].pitchBendStep = 0;   // Smooth pitch bend (0 often means step is off/smooth)
        this->parts[i].portamentoMode = 0;  // Portamento off
        this->parts[i].portamentoGlissando = 0; // Glissando off
        this->parts[i].portamentoTime = 0;
        this->parts[i].monoMode = 0;        // Polyphonic mode
        
        this->parts[i].modulationWheelRange = 0; // No effect
        this->parts[i].modulationWheelTarget = 0; // No target
        this->parts[i].footControlRange = 0;
        this->parts[i].footControlTarget = 0;
        this->parts[i].breathControlRange = 0;
        this->parts[i].breathControlTarget = 0;
        this->parts[i].aftertouchRange = 0;
        this->parts[i].aftertouchTarget = 0;

        // Unison settings defaults (can be overridden by command line for active parts)
        this->parts[i].unisonDetune = 7.0f; // Default detune in cents
        this->parts[i].unisonSpread = 0.5f; // Default stereo spread

        if (i < numParts) {
            this->parts[i].midiChannel = static_cast<uint8_t>(i + 1); // Assign unique MIDI channels for active parts
            this->parts[i].volume = 100;        // Active parts default volume
            this->parts[i].unisonVoices = static_cast<uint8_t>(unisonVoices); // Use the provided unison voices value for active parts
        } else {
            this->parts[i].midiChannel = 0; // Disable unused parts
            this->parts[i].volume = 0;      // Inactive parts have no volume
            this->parts[i].unisonVoices = 1; // Inactive parts are single voice (no unison)
        }
    }

    // Set default effects (assuming EffectsConfig default constructor sets neutral values)
    this->effects = EffectsConfig{};
}

void Performance::setPartVoiceData(int partIndex, const std::vector<uint8_t>& voiceData) {
    if (partIndex < 0 || partIndex >= static_cast<int>(parts.size())) return;
    auto& part = parts[partIndex];
    size_t sz = std::min<size_t>(part.voiceData.size(), voiceData.size());
    std::copy_n(voiceData.begin(), sz, part.voiceData.begin());
    part.voiceName = VoiceData::extractDX7VoiceName(voiceData);
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

// Fixed-buffer version to avoid heap allocation in audio thread
bool Performance::handleSysex(const uint8_t* data, int len, uint8_t* response, int maxResponseLen, int& responseLen, int partIndex) {
    responseLen = 0;
    
    if (len < 5 || data[0] != 0xF0 || data[1] != 0x7D || data[len-1] != 0xF7) return false;
    uint8_t cmd = data[2];
    
    // Helper to add bytes to fixed buffer
    auto addByte = [&](uint8_t b) -> bool {
        if (responseLen < maxResponseLen) {
            response[responseLen++] = b;
            return true;
        }
        return false; // Buffer overflow
    };
    
    auto addParam = [&](uint16_t p, uint16_t v) -> bool {
        return addByte((p >> 8) & 0x7F) && addByte(p & 0x7F) &&
               addByte((v >> 8) & 0x7F) && addByte(v & 0x7F);
    };
    
    // Global GET
    if (cmd == 0x10 && partIndex == -1) {
        addByte(0xF0); addByte(0x7D); addByte(0x20);
        auto& eff = effects;
        addParam(0x0000, eff.compressorEnable ? 1 : 0);
        addParam(0x0001, eff.reverbEnable ? 1 : 0);
        addParam(0x0002, eff.reverbSize);
        addParam(0x0003, eff.reverbHighDamp);
        addParam(0x0004, eff.reverbLowDamp);
        addParam(0x0005, eff.reverbLowPass);
        addParam(0x0006, eff.reverbDiffusion);
        addParam(0x0007, eff.reverbLevel);
        addByte(0xF7);
        return true;
    }
    
    // TG GET
    if (cmd == 0x11 && len >= 5 && partIndex >= 0 && partIndex < (int)parts.size()) {
        addByte(0xF0); addByte(0x7D); addByte(0x21); addByte(static_cast<uint8_t>(partIndex));
        const auto& p = parts[partIndex];
        addParam(0x0000, p.bankNumber);
        addParam(0x0001, p.voiceNumber);
        addParam(0x0002, p.midiChannel);
        addParam(0x0003, p.volume);
        addParam(0x0004, p.pan);
        uint8_t msb, lsb;
        encode_signed_14bit(p.detune, msb, lsb);
        addParam(0x0005, (msb << 8) | lsb);
        addParam(0x0006, p.cutoff);
        addParam(0x0007, p.resonance);
        addParam(0x0008, p.noteLimitLow);
        addParam(0x0009, p.noteLimitHigh);
        encode_signed_14bit(p.noteShift, msb, lsb);
        addParam(0x000A, (msb << 8) | lsb);
        addParam(0x000B, p.reverbSend);
        addParam(0x000C, p.pitchBendRange);
        addParam(0x000D, p.pitchBendStep);
        addParam(0x000E, p.portamentoMode);
        addParam(0x000F, p.portamentoGlissando);
        addParam(0x0010, p.portamentoTime);
        addParam(0x0011, p.monoMode);
        addParam(0x0012, p.modulationWheelRange);
        addParam(0x0013, p.modulationWheelTarget);
        addParam(0x0014, p.footControlRange);
        addParam(0x0015, p.footControlTarget);
        addParam(0x0016, p.breathControlRange);
        addParam(0x0017, p.breathControlTarget);
        addParam(0x0018, p.aftertouchRange);
        addParam(0x0019, p.aftertouchTarget);
        addByte(0xF7);
        return true;
    }
    
    // Global SET (no response needed)
    if (cmd == 0x20) {
        int offset = 3;
        while (offset + 3 < len - 1) {
            uint16_t param = (data[offset] << 8) | data[offset+1];
            uint16_t value = (data[offset+2] << 8) | data[offset+3];
            offset += 4;
            switch (param) {
                case 0x0000: effects.compressorEnable = (value != 0); break;
                case 0x0001: effects.reverbEnable = (value != 0); break;
                case 0x0002: effects.reverbSize = value & 0x7F; break;
                case 0x0003: effects.reverbHighDamp = value & 0x7F; break;
                case 0x0004: effects.reverbLowDamp = value & 0x7F; break;
                case 0x0005: effects.reverbLowPass = value & 0x7F; break;
                case 0x0006: effects.reverbDiffusion = value & 0x7F; break;
                case 0x0007: effects.reverbLevel = value & 0x7F; break;
                default: break;
            }
        }
        return true;
    }
    
    // TG SET (no response needed)
    if (cmd == 0x21 && len >= 8 && partIndex >= 0 && partIndex < (int)parts.size()) {
        int offset = 4;
        auto& p = parts[partIndex];
        while (offset + 3 < len - 1) {
            uint16_t param = (data[offset] << 8) | data[offset+1];
            uint16_t value = (data[offset+2] << 8) | data[offset+3];
            offset += 4;
            switch (param) {
                case 0x0000: p.bankNumber = value & 0x7F; break;
                case 0x0001: p.voiceNumber = value & 0x1F; break;
                case 0x0002: p.midiChannel = value & 0x7F; break;
                case 0x0003: p.volume = value & 0x7F; break;
                case 0x0004: p.pan = value & 0x7F; break;
                case 0x0005: p.detune = decode_signed_14bit((value >> 8) & 0x7F, value & 0x7F); break;
                case 0x0006: p.cutoff = value & 0x7F; break;
                case 0x0007: p.resonance = value & 0x7F; break;
                case 0x0008: p.noteLimitLow = value & 0x7F; break;
                case 0x0009: p.noteLimitHigh = value & 0x7F; break;
                case 0x000A: p.noteShift = decode_signed_14bit((value >> 8) & 0x7F, value & 0x7F); break;
                case 0x000B: p.reverbSend = value & 0x7F; break;
                case 0x000C: p.pitchBendRange = value & 0x7F; break;
                case 0x000D: p.pitchBendStep = value & 0x7F; break;
                case 0x000E: p.portamentoMode = value & 0x7F; break;
                case 0x000F: p.portamentoGlissando = value & 0x7F; break;
                case 0x0010: p.portamentoTime = value & 0x7F; break;
                case 0x0011: p.monoMode = value & 0x7F; break;
                case 0x0012: p.modulationWheelRange = value & 0x7F; break;
                case 0x0013: p.modulationWheelTarget = value & 0x7F; break;
                case 0x0014: p.footControlRange = value & 0x7F; break;
                case 0x0015: p.footControlTarget = value & 0x7F; break;
                case 0x0016: p.breathControlRange = value & 0x7F; break;
                case 0x0017: p.breathControlTarget = value & 0x7F; break;
                case 0x0018: p.aftertouchRange = value & 0x7F; break;
                case 0x0019: p.aftertouchTarget = value & 0x7F; break;
                default: break;
            }
        }
        return true;
    }
    
    return false;
}

} // namespace FMRack
