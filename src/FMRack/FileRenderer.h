#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace FMRack {
class Rack;

class FileRenderer {
public:
    // Render a MIDI file to a stereo 16-bit 48kHz WAV file using an existing Rack
    // Returns true on success, false on failure
    static bool renderMidiToWav(Rack* rack,
                                const std::string& midiFile, const std::string& wavFile,
                                int sampleRate = 48000, int bufferFrames = 1024);
};
}
