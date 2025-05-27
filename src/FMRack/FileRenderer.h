#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace FMRack {
class Rack;

class FileRenderer {
public:
    // Render a MIDI file to a stereo 16-bit 48kHz WAV file
    // Returns true on success, false on failure
    static bool renderMidiToWav(const std::string& midiFile, const std::string& wavFile,
                                int sampleRate = 48000, int bufferFrames = 1024,
                                int numModules = 16, int unisonVoices = 1, float unisonDetune = 7.0f, float unisonSpread = 0.5f);
};
}
