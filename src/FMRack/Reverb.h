#pragma once

#include <vector>

namespace FMRack {

// Reverb effect class
class Reverb {
public:
    Reverb(float sampleRate);
    ~Reverb() = default;
    
    void setSize(float size);
    void setHighDamp(float highDamp);
    void setLowDamp(float lowDamp);
    void setLowPass(float lowPass);
    void setDiffusion(float diffusion);
    void setLevel(float level);
    void setEnabled(bool enabled);
    
    void process(float* leftIn, float* rightIn, float* leftOut, float* rightOut, int numSamples);
    void reset();
    
private:
    float sampleRate_;
    float size_;
    float highDamp_;
    float lowDamp_;
    float lowPass_;
    float diffusion_;
    float level_;
    bool enabled_;
    
    // Simple reverb implementation with delay lines
    std::vector<float> delayBuffer_;
    std::vector<int> delayIndices_;
    int writeIndex_;
};

} // namespace FMRack
