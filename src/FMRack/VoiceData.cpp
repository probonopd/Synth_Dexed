#include <fstream>
#include <vector>
#include <string>
#include <iostream>
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <iomanip>
#include <cstring> // for memcmp
#include "VoiceData.h"

// You may need to adjust this according to your Dexed implementation
constexpr size_t NUM_VOICE_PARAMETERS = 155; // Dexed expects 155 bytes per voice

bool VoiceData::endsWith(const std::string& str, const std::string& suffix) {
    return str.size() >= suffix.size() &&
           str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

uint8_t VoiceData::calcChecksum(const std::vector<uint8_t>& data, size_t offset, size_t length) {
    uint8_t sum = 0;
    for (size_t i = 0; i < length; ++i)
        sum += data[offset + i];
    sum &= 0x7F;
    return (128 - sum) & 0x7F;
}

bool VoiceData::loadFromFile(const std::string& filename) {
    std::cout << "[VoiceData] Loading file: " << filename << std::endl;
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "[VoiceData] Failed to open file: " << filename << std::endl;
        return false;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::cout << "[VoiceData] File size: " << data.size() << " bytes" << std::endl;

    // Print HEX for debugging
    std::cout << "[VoiceData] HEX:";
    for (size_t i = 0; i < std::min<size_t>(data.size(), 64); ++i)
        std::cout << " " << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << int(data[i]);
    if (data.size() > 64) std::cout << " ...";
    std::cout << std::dec << std::endl;

    voices.clear();
    bool ok = false;

    // 1. MIDI file detection
    if (data.size() > 4 && ::memcmp(data.data(), "MThd", 4) == 0) {
        std::cout << "[VoiceData] Detected MIDI file format (header 'MThd')." << std::endl;
        ok = loadFromMidi(data);
    }
    // 2. Bulk Sysex: F0 43 00 09 20 00
    else if (data.size() == 4104 && data[0] == 0xF0) {
        if (data[1] == 0x43 && data[2] == 0x00 && data[3] == 0x09 && data[4] == 0x20 && data[5] == 0x00) {
            std::cout << "[VoiceData] Detected TX7/DX7II bulk sysex format." << std::endl;
            uint8_t expected = calcChecksum(data, 6, 128*32);
            if (data[4102] == expected) {
                std::cout << "[VoiceData] Bulk sysex checksum is correct." << std::endl;
            } else {
                std::cerr << "[VoiceData] Bulk sysex checksum incorrect (expected " << int(expected) << ", got " << int(data[4102]) << ")." << std::endl;
            }
            if (data[4103] == 0xF7) {
                std::cout << "[VoiceData] End of sysex (F7) is correct." << std::endl;
                ok = loadBulkSysex(data);
            } else {
                std::cerr << "[VoiceData] End of sysex (F7) missing or incorrect." << std::endl;
            }
        } else {
            std::cerr << "[VoiceData] Bulk sysex header incorrect." << std::endl;
        }
    }
    // 3. Single-voice Sysex: Original DX7 (F0 43 00 00 01 1B)
    else if (data.size() == 163 && data[0] == 0xF0 && data[1] == 0x43) {
        if (data[2] == 0x00 && data[3] == 0x00 && data[4] == 0x01 && data[5] == 0x1B) {
            std::cout << "[VoiceData] Detected original DX7 single-voice sysex format (F0 43 00 00 01 1B)." << std::endl;
            uint8_t expected = calcChecksum(data, 6, 155);
            if (data[161] == expected) {
                std::cout << "[VoiceData] Checksum is correct." << std::endl;
            } else {
                std::cerr << "[VoiceData] Checksum incorrect (expected " << int(expected) << ", got " << int(data[161]) << ")." << std::endl;
            }
            if (data[162] == 0xF7) {
                std::cout << "[VoiceData] End of sysex (F7) is correct." << std::endl;
                ok = loadSingleSysexDX7(data);
            } else {
                std::cerr << "[VoiceData] End of sysex (F7) missing or incorrect." << std::endl;
            }
        }
        // 4. Single-voice Sysex: TX7/DX7II (F0 43 00 09 ...)
        else if (data[3] == 0x09) {
            std::cout << "[VoiceData] Detected TX7/DX7II single-voice sysex format (F0 43 00 09 ...)." << std::endl;
            uint8_t expected = calcChecksum(data, 6, 128);
            if (data[134] == expected) {
                std::cout << "[VoiceData] Checksum is correct." << std::endl;
            } else {
                std::cerr << "[VoiceData] Checksum incorrect (expected " << int(expected) << ", got " << int(data[134]) << ")." << std::endl;
            }
            if (data[135] == 0xF7) {
                std::cout << "[VoiceData] End of sysex (F7) is correct." << std::endl;
                ok = loadSingleSysexTX7(data);
            } else {
                std::cerr << "[VoiceData] End of sysex (F7) missing or incorrect." << std::endl;
            }
        } else {
            std::cerr << "[VoiceData] Single-voice sysex header not recognized." << std::endl;
        }
    }
    // 5. Raw packed (128*N bytes)
    else if (data.size() % 128 == 0) {
        std::cout << "[VoiceData] Detected raw packed format (" << data.size() / 128 << " voices)." << std::endl;
        ok = loadRawPacked(data, filename);
    }
    // 6. .TX7/.SND special case
    else if ((endsWith(filename, ".tx7") || endsWith(filename, ".TX7") ||
              endsWith(filename, ".snd") || endsWith(filename, ".SND")) && data.size() == 8192) {
        std::cout << "[VoiceData] Detected .TX7/.SND format." << std::endl;
        std::cout << "[VoiceData] Using first 4096 bytes for 32 voices." << std::endl;
        ok = loadRawPacked(std::vector<uint8_t>(data.begin(), data.begin() + 4096), filename);
    }
    // 7. Embedded sysex in other files
    else {
        for (size_t i = 0; i + 163 <= data.size(); ++i) {
            if (data[i] == 0xF0 && data[i+1] == 0x43 && data[i+2] == 0x00) {
                if (data[i+3] == 0x00 && data[i+4] == 0x01 && data[i+5] == 0x1B) {
                    std::cout << "[VoiceData] Detected embedded original DX7 single-voice sysex at offset " << i << "." << std::endl;
                    uint8_t expected = calcChecksum(data, i+6, 155);
                    if (data[i+161] == expected && data[i+162] == 0xF7) {
                        std::cout << "[VoiceData] Embedded checksum and EOX are correct." << std::endl;
                        ok |= loadSingleSysexDX7(std::vector<uint8_t>(data.begin() + i, data.begin() + i + 163));
                    }
                } else if (data[i+3] == 0x09) {
                    std::cout << "[VoiceData] Detected embedded TX7/DX7II single-voice sysex at offset " << i << "." << std::endl;
                    uint8_t expected = calcChecksum(data, i+6, 128);
                    if (data[i+134] == expected && data[i+135] == 0xF7) {
                        std::cout << "[VoiceData] Embedded checksum and EOX are correct." << std::endl;
                        ok |= loadSingleSysexTX7(std::vector<uint8_t>(data.begin() + i, data.begin() + i + 136));
                    }
                }
            }
        }
    }

    std::cout << "[VoiceData] Loaded " << voices.size() << " voice(s) before conversion." << std::endl;

    // Convert to Dexed format (155 bytes: 128 packed + 27 Dexed params)
    for (auto& v : voices) {
        if (v.size() == 128 || v.size() == 155) {
            std::string name = VoiceData::extractDX7VoiceName(v);
            v = convertDX7ToDexed(v, name);
            std::cout << "[VoiceData] Converted voice to Dexed format." << std::endl;
        }
    }

    std::cout << "[VoiceData] Loaded " << voices.size() << " voice(s) after conversion." << std::endl;
    for (size_t i = 0; i < voices.size(); ++i) {
        std::string name = VoiceData::extractDX7VoiceName(voices[i]);
        std::cout << "[VoiceData] Voice " << i+1 << " name: '" << name << "', size: " << voices[i].size() << std::endl;
    }
    return ok && !voices.empty();
}

// --- Format-specific loaders ---

// Bulk Sysex: F0 43 00 09 20 00 ... [32*128 bytes] ... checksum F7
bool VoiceData::loadBulkSysex(const std::vector<uint8_t>& data) {
    if (data.size() != 4104 || data[0] != 0xF0) return false;
    size_t offset = 6; // header: F0 43 00 09 20 00
    for (int i = 0; i < 32; ++i) {
        if (offset + 128 > data.size() - 2) break; // -2: checksum and F7
        voices.emplace_back(data.begin() + offset, data.begin() + offset + 128);
        offset += 128;
    }
    return voices.size() == 32;
}

// Original DX7 single-voice sysex: F0 43 00 00 01 1B ... [155 bytes] ... checksum F7
bool VoiceData::loadSingleSysexDX7(const std::vector<uint8_t>& data) {
    if (data.size() != 163 || data[0] != 0xF0) return false;
    voices.emplace_back(data.begin() + 6, data.begin() + 161); // 155 bytes
    return true;
}

// TX7/DX7II single-voice sysex: F0 43 00 09 ... [128 bytes] ... checksum F7
bool VoiceData::loadSingleSysexTX7(const std::vector<uint8_t>& data) {
    if (data.size() < 136 || data[0] != 0xF0) return false;
    voices.emplace_back(data.begin() + 6, data.begin() + 6 + 128);
    return true;
}

// Raw packed: 128*N bytes
bool VoiceData::loadRawPacked(const std::vector<uint8_t>& data, const std::string& /*filename*/) {
    size_t count = data.size() / 128;
    for (size_t i = 0; i < count; ++i) {
        voices.emplace_back(data.begin() + i*128, data.begin() + (i+1)*128);
    }
    return !voices.empty();
}

// MIDI: look for sysex events
bool VoiceData::loadFromMidi(const std::vector<uint8_t>& data) {
    size_t pos = 0;
    bool found = false;
    while (pos + 6 < data.size()) {
        if (data[pos] == 0xF0 && data[pos+1] == 0x43 && data[pos+2] == 0x00) {
            // Bulk
            if (pos + 4104 <= data.size() &&
                data[pos+3] == 0x09 && data[pos+4] == 0x20 && data[pos+5] == 0x00) {
                uint8_t expected = calcChecksum(data, pos+6, 128*32);
                if (data[pos+4102] == expected && data[pos+4103] == 0xF7) {
                    std::cout << "[VoiceData] Found valid bulk sysex in MIDI at offset " << pos << "." << std::endl;
                    found |= loadBulkSysex(std::vector<uint8_t>(data.begin() + pos, data.begin() + pos + 4104));
                }
                pos += 4104;
                continue;
            }
            // Single DX7
            if (pos + 163 <= data.size() &&
                data[pos+3] == 0x00 && data[pos+4] == 0x01 && data[pos+5] == 0x1B) {
                uint8_t expected = calcChecksum(data, pos+6, 155);
                if (data[pos+161] == expected && data[pos+162] == 0xF7) {
                    std::cout << "[VoiceData] Found valid DX7 single-voice sysex in MIDI at offset " << pos << "." << std::endl;
                    found |= loadSingleSysexDX7(std::vector<uint8_t>(data.begin() + pos, data.begin() + pos + 163));
                }
                pos += 163;
                continue;
            }
            // Single TX7
            if (pos + 136 <= data.size() && data[pos+3] == 0x09) {
                uint8_t expected = calcChecksum(data, pos+6, 128);
                if (data[pos+134] == expected && data[pos+135] == 0xF7) {
                    std::cout << "[VoiceData] Found valid TX7/DX7II single-voice sysex in MIDI at offset " << pos << "." << std::endl;
                    found |= loadSingleSysexTX7(std::vector<uint8_t>(data.begin() + pos, data.begin() + pos + 136));
                }
                pos += 136;
                continue;
            }
        }
        ++pos;
    }
    return found;
}

// Extract voice name (Dexed: bytes 145–154, DX7: 118–127)
std::string VoiceData::extractDX7VoiceName(const std::vector<uint8_t>& voice) {
    // Handle 156-byte (Dexed), 155-byte (DX7), and 128-byte (TX7) voices
    // This function is now static.
    // The implementation remains the same, but it's now a static member.
    if (voice.size() >= 156) { // Dexed internal representation might be 156
        return std::string(voice.begin() + 145, voice.begin() + 155);
    } else if (voice.size() == 155) { // Original DX7 sysex data part or Dexed after initial load
        return std::string(voice.begin() + 145, voice.begin() + 155);
    } else if (voice.size() == 128) { // TX7 or packed format
        return std::string(voice.begin() + 118, voice.begin() + 128);
    }
    return "(unknown)";
}

// Convert 128/155-byte DX7 voice to Dexed format (155 bytes)
std::vector<uint8_t> VoiceData::convertDX7ToDexed(const std::vector<uint8_t>& dx7voice, const std::string& nameHint) {
    std::vector<uint8_t> dexedVoice(NUM_VOICE_PARAMETERS, 0);
    if (dx7voice.size() == 128) {
        std::copy_n(dx7voice.begin(), 128, dexedVoice.begin());
    } else if (dx7voice.size() == 155) {
        std::copy_n(dx7voice.begin(), 155, dexedVoice.begin());
    }
    std::string name = nameHint;
    if (name.empty() && dx7voice.size() >= 128) {
        name = std::string(dx7voice.begin() + 118, dx7voice.begin() + 128);
    }
    name.erase(std::remove_if(name.begin(), name.end(), [](char c) { return c < 32 || c > 126; }), name.end());
    while (!name.empty() && name.back() == ' ') name.pop_back();
    if (name.empty()) name = "DX7VOICE";
    for (size_t i = 0; i < 10; ++i) {
        dexedVoice[145 + i] = (i < name.size()) ? static_cast<uint8_t>(name[i]) : ' ';
    }
    // Ensure output is 156 bytes (pad with 0 if needed)
    if (dexedVoice.size() < 156) {
        dexedVoice.resize(156, 0);
    }
    return dexedVoice;
}
