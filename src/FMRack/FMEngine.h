#pragma once
#include "../dexed.h"
#include <memory>
#include <vector>

namespace FMRack {

class FMEngine {
public:
    FMEngine(float sampleRate);
    ~FMEngine();
    
    // Configuration
    void loadPatch(const std::vector<uint8_t>& patchData);
    void setNoteRange(int lowNote, int highNote);
    void setMonoMode(bool mono);
    void setDetune(float cents);
    void setPan(float pan); // -1.0 to 1.0
    
    // MIDI handling
    void noteOn(int note, int velocity);
    void noteOff(int note);
    void pitchBend(int value); // 0-16383, 8192 = center
    void controlChange(int controller, int value);
    void programChange(int program);
    void allNotesOff();
    
    // Audio processing
    void processAudio(float* leftOut, float* rightOut, int numSamples);
    
    // State
    bool isNoteInRange(int note) const;
    
private:
    std::unique_ptr<Dexed> dexed;
    float sampleRate;
    int noteLimitLow;
    int noteLimitHigh;
    float detuneCents;
    float panPosition;
    bool monoMode;
    
    void applyDetuneAndPan(float* leftOut, float* rightOut, int numSamples);
};

} // namespace FMRack
