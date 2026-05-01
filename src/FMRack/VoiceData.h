#pragma once
#include <vector>
#include <string>
#include <cstdint>

// VoiceData class for loading DX7 voice data from various file formats
class VoiceData {
public:
    // Holds up to 128 voices, each 128 bytes (raw) or 155/156 bytes (sysex)
    std::vector<std::vector<uint8_t>> voices;

    // Load voice data from a file (auto-detects format)
    bool loadFromFile(const std::string& filename);

    // Helper: Extract DX7 voice name from voice data
    static std::string extractDX7VoiceName(const std::vector<uint8_t>& voice);

    // Convert a 128-byte DX7 voice to a 156-byte Dexed-compatible voice
    static std::vector<uint8_t> convertDX7ToDexed(const std::vector<uint8_t>& dx7voice, const std::string& nameHint = "");

private:
    bool loadBulkSysex(const std::vector<uint8_t>& data);
    bool loadSingleSysexDX7(const std::vector<uint8_t>& data);
    bool loadSingleSysexTX7(const std::vector<uint8_t>& data);
    bool loadRawPacked(const std::vector<uint8_t>& data, const std::string& filename);
    bool loadFromMidi(const std::vector<uint8_t>& data);
    static uint8_t calcChecksum(const std::vector<uint8_t>& data, size_t offset, size_t length);
    static bool endsWith(const std::string& str, const std::string& suffix);
};
