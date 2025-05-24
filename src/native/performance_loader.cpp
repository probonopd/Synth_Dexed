#include "performance_loader.h"
#include <sstream>
#include <vector>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <unordered_map>
#include "StereoDexed.h"

#define GAIN 3

// Helper to convert hex string to bytes (robust: removes all whitespace, parses every two hex digits)
static std::vector<uint8_t> hexStringToBytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    std::string clean;
    for (char c : hex) {
        if (!isspace((unsigned char)c)) clean += c;
    }
    for (size_t i = 0; i + 1 < clean.size(); i += 2) {
        std::string byteStr = clean.substr(i, 2);
        uint8_t byte = (uint8_t)std::stoul(byteStr, nullptr, 16);
        bytes.push_back(byte);
    }
    return bytes;
}

// Simple key/value parser for files with no section headers
static std::unordered_map<std::string, std::string> parse_simple_ini(const std::string& filename, bool debug) {
    std::unordered_map<std::string, std::string> kv;
    std::ifstream file(filename);
    if (!file.is_open()) {
        if (debug) std::cerr << "[ERROR] Could not open file: " << filename << std::endl;
        return kv;
    }
    std::string line;
    while (std::getline(file, line)) {
        // Remove comments
        auto comment = line.find_first_of(";#");
        if (comment != std::string::npos) line = line.substr(0, comment);
        // Trim whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        size_t end = line.find_last_not_of(" \t\r\n");
        if (start == std::string::npos || end == std::string::npos) continue;
        line = line.substr(start, end - start + 1);
        if (line.empty()) continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        // Trim key/value
        start = key.find_first_not_of(" \t");
        end = key.find_last_not_of(" \t");
        key = key.substr(start, end - start + 1);
        start = value.find_first_not_of(" \t");
        end = value.find_last_not_of(" \t");
        value = value.substr(start, end - start + 1);
        kv[key] = value;
        if (debug) std::cout << "[DEBUG] Parsed: '" << key << "' = '" << value << "'" << std::endl;
    }
    return kv;
}

// Loads a Soundplantage-style .ini performance file and applies it to the given Dexed synth instance.
bool load_performance_ini(const std::string& filename, StereoDexed* synth, int partIndex, bool debug) {
    if (debug) std::cout << "[DEBUG] Loading INI file: " << filename << " for module " << partIndex << std::endl;
    auto kv = parse_simple_ini(filename, debug);
    std::string idx = std::to_string(partIndex);
    // Check MIDI channel (1-indexed in ini, 0 means do not create)
    int midiCh = kv.count("MIDIChannel" + idx) ? std::stoi(kv["MIDIChannel" + idx]) : -1;
    if (midiCh == 0) {
        if (debug) std::cout << "[DEBUG] [Module " << partIndex << "] MIDIChannel is 0, skipping module creation." << std::endl;
        return false;
    }
    // VoiceData
    std::string voiceDataKey = "VoiceData" + idx;
    std::string voiceDataHex = kv.count(voiceDataKey) ? kv[voiceDataKey] : "";
    if (!voiceDataHex.empty()) {
        if (debug) std::cout << "[DEBUG] [Module " << partIndex << "] Loading VoiceData: " << voiceDataHex << std::endl;
        std::vector<uint8_t> voiceData = hexStringToBytes(voiceDataHex);
        if (voiceData.size() == 156) {
            // DX7 single voice: 155 bytes voice data, name at bytes 145-154 (0-based)
            // If 156 bytes, assume first byte is not part of voice data (status or padding)
            uint8_t* voicePtr = voiceData.data();
            if (voiceData[0] == 0xF0 || voiceData[0] == 0x00) {
                // If first byte is a status/padding, skip it
                voicePtr = voiceData.data() + 1;
            }
            // Pass only the 155 bytes of voice data to Dexed
            synth->loadVoiceParameters(voicePtr);
            if (debug) {
                std::string voiceName;
                for (int i = 145; i < 155; ++i) {
                    char c = static_cast<char>(voicePtr[i]);
                    if (c >= 32 && c <= 126) voiceName += c;
                    else voiceName += ' ';
                }
                std::cout << "[DEBUG] [Module " << partIndex << "] Voice Name: '" << voiceName << "'" << std::endl;
                std::ostringstream oss;
                oss << std::hex << std::uppercase << std::setfill('0');
                for (int i = 0; i < 155; ++i) {
                    oss << std::setw(2) << static_cast<int>(voicePtr[i]);
                    if (i + 1 < 155) oss << " ";
                }
                std::cout << "[DEBUG] [Module " << partIndex << "] VoiceData (155 bytes): " << oss.str() << std::endl;
            }
            if (debug) std::cout << "[DEBUG] [Module " << partIndex << "] VoiceData (155 bytes) loaded directly" << std::endl;
        } else if (voiceData.size() == 163) {
            // Extract 155 bytes from SysEx (offset 5)
            synth->loadVoiceParameters(voiceData.data() + 5);
            if (debug) {
                std::string voiceName;
                for (int i = 150; i < 160; ++i) {
                    char c = static_cast<char>(voiceData[i]);
                    if (c >= 32 && c <= 126) voiceName += c;
                    else voiceName += ' ';
                }
                std::cout << "[DEBUG] [Module " << partIndex << "] Voice Name: '" << voiceName << "'" << std::endl;
                std::ostringstream oss;
                oss << std::hex << std::uppercase << std::setfill('0');
                for (int i = 5; i < 160; ++i) {
                    oss << std::setw(2) << static_cast<int>(voiceData[i]);
                    if (i + 1 < 160) oss << " ";
                }
                std::cout << "[DEBUG] [Module " << partIndex << "] VoiceData (155 bytes from SysEx): " << oss.str() << std::endl;
            }
            if (debug) std::cout << "[DEBUG] [Module " << partIndex << "] VoiceData (155 bytes from SysEx) loaded" << std::endl;
        } else {
            std::cerr << "[WARN] [Module " << partIndex << "] VoiceData has unexpected size: " << voiceData.size() << std::endl;
        }
    } else if (debug) {
        std::cout << "[DEBUG] [Module " << partIndex << "] No VoiceData found" << std::endl;
    }
    // Note limits (keyboard range)
    int noteLow = kv.count("NoteLimitLow" + idx) ? std::stoi(kv["NoteLimitLow" + idx]) : 0;
    int noteHigh = kv.count("NoteLimitHigh" + idx) ? std::stoi(kv["NoteLimitHigh" + idx]) : 127;
    if (debug) std::cout << "[DEBUG] [Module " << partIndex << "] NoteLimitLow: " << noteLow << ", NoteLimitHigh: " << noteHigh << std::endl;
    if (synth) {
        synth->setNoteLimitLow(noteLow);
        synth->setNoteLimitHigh(noteHigh);
    }
    // Stereo shift (pan)
    int pan = kv.count("Pan" + idx) ? std::stoi(kv["Pan" + idx]) : -1;
    if (debug) std::cout << "[DEBUG] [Module " << partIndex << "] Pan: " << pan << std::endl;
    if (synth && pan >= 0 && pan <= 127) {
        // Map MIDI pan (0-127) to stereo position (-1.0 to 1.0)
        float stereoPos = (pan / 63.5f) - 1.0f;
        synth->setStereoPosition(stereoPos);
    }
    // Detune
    int detune = kv.count("Detune" + idx) ? std::stoi(kv["Detune" + idx]) : 0;
    if (debug) std::cout << "[DEBUG] [Module " << partIndex << "] Detune: " << detune << std::endl;
    synth->setMasterTune(detune);
    // Note shift
    int noteShift = kv.count("NoteShift" + idx) ? std::stoi(kv["NoteShift" + idx]) : 0;
    if (debug) std::cout << "[DEBUG] [Module " << partIndex << "] NoteShift: " << noteShift << std::endl;
    synth->setTranspose(noteShift);
    // Volume
    int volume = kv.count("Volume" + idx) ? std::stoi(kv["Volume" + idx]) : 70;
    if (debug) std::cout << "[DEBUG] [Module " << partIndex << "] Volume: " << volume << std::endl;
    // Apply global GAIN multiplier if defined
    #ifdef GAIN
    synth->setGain((volume / 127.0f) * GAIN);
    #else
    synth->setGain(volume / 127.0f);
    #endif
    // Cutoff/Resonance
    int cutoff = kv.count("Cutoff" + idx) ? std::stoi(kv["Cutoff" + idx]) : 99;
    int resonance = kv.count("Resonance" + idx) ? std::stoi(kv["Resonance" + idx]) : 0;
    if (debug) std::cout << "[DEBUG] [Module " << partIndex << "] Cutoff: " << cutoff << ", Resonance: " << resonance << std::endl;
    synth->setFilterCutoff(cutoff / 127.0f);
    synth->setFilterResonance(resonance / 127.0f);
    // Pitch bend
    int pbRange = kv.count("PitchBendRange" + idx) ? std::stoi(kv["PitchBendRange" + idx]) : 2;
    int pbStep = kv.count("PitchBendStep" + idx) ? std::stoi(kv["PitchBendStep" + idx]) : 0;
    if (debug) std::cout << "[DEBUG] [Module " << partIndex << "] PitchBendRange: " << pbRange << ", PitchBendStep: " << pbStep << std::endl;
    synth->setPBController(pbRange, pbStep);
    // Portamento
    int portMode = kv.count("PortamentoMode" + idx) ? std::stoi(kv["PortamentoMode" + idx]) : 0;
    int portGliss = kv.count("PortamentoGlissando" + idx) ? std::stoi(kv["PortamentoGlissando" + idx]) : 0;
    int portTime = kv.count("PortamentoTime" + idx) ? std::stoi(kv["PortamentoTime" + idx]) : 0;
    if (debug) std::cout << "[DEBUG] [Module " << partIndex << "] PortamentoMode: " << portMode << ", PortamentoGlissando: " << portGliss << ", PortamentoTime: " << portTime << std::endl;
    synth->setPortamento(portMode, portGliss, portTime);
    // Modulation wheel
    int mwRange = kv.count("ModulationWheelRange" + idx) ? std::stoi(kv["ModulationWheelRange" + idx]) : 99;
    int mwTarget = kv.count("ModulationWheelTarget" + idx) ? std::stoi(kv["ModulationWheelTarget" + idx]) : 1;
    if (debug) std::cout << "[DEBUG] [Module " << partIndex << "] ModulationWheelRange: " << mwRange << ", ModulationWheelTarget: " << mwTarget << std::endl;
    synth->setMWController(mwRange, mwTarget, 0);
    // Foot control
    int fcRange = kv.count("FootControlRange" + idx) ? std::stoi(kv["FootControlRange" + idx]) : 99;
    int fcTarget = kv.count("FootControlTarget" + idx) ? std::stoi(kv["FootControlTarget" + idx]) : 0;
    if (debug) std::cout << "[DEBUG] [Module " << partIndex << "] FootControlRange: " << fcRange << ", FootControlTarget: " << fcTarget << std::endl;
    synth->setFCController(fcRange, fcTarget, 0);
    // Breath control
    int bcRange = kv.count("BreathControlRange" + idx) ? std::stoi(kv["BreathControlRange" + idx]) : 99;
    int bcTarget = kv.count("BreathControlTarget" + idx) ? std::stoi(kv["BreathControlTarget" + idx]) : 0;
    if (debug) std::cout << "[DEBUG] [Module " << partIndex << "] BreathControlRange: " << bcRange << ", BreathControlTarget: " << bcTarget << std::endl;
    synth->setBCController(bcRange, bcTarget, 0);
    // Aftertouch
    int atRange = kv.count("AftertouchRange" + idx) ? std::stoi(kv["AftertouchRange" + idx]) : 99;
    int atTarget = kv.count("AftertouchTarget" + idx) ? std::stoi(kv["AftertouchTarget" + idx]) : 0;
    if (debug) std::cout << "[DEBUG] [Module " << partIndex << "] AftertouchRange: " << atRange << ", AftertouchTarget: " << atTarget << std::endl;
    synth->setATController(atRange, atTarget, 0);
    // Mono mode
    int mono = kv.count("MonoMode" + idx) ? std::stoi(kv["MonoMode" + idx]) : 0;
    if (debug) std::cout << "[DEBUG] [Module " << partIndex << "] MonoMode: " << mono << std::endl;
    synth->setMonoMode(mono != 0);
    // Bank/Voice/MIDI channel (for reference, not directly settable in Dexed)
    int bank = kv.count("BankNumber" + idx) ? std::stoi(kv["BankNumber" + idx]) : -1;
    int voice = kv.count("VoiceNumber" + idx) ? std::stoi(kv["VoiceNumber" + idx]) : -1;
    if (debug) std::cout << "[DEBUG] [Module " << partIndex << "] BankNumber: " << bank << ", VoiceNumber: " << voice << ", MIDIChannel: " << midiCh << std::endl;
    return true;
}
