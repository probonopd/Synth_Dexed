#include "Reverb.h"
#include <algorithm>
#include <cmath>

namespace FMRack {

Reverb::Reverb(float sampleRate) 
    : sampleRate_(sampleRate), size_(0.5f), highDamp_(0.5f), lowDamp_(0.5f),
      lowPass_(1.0f), diffusion_(0.5f), level_(0.25f), enabled_(true), writeIndex_(0) {
    
    // Initialize delay lines for simple reverb
    int maxDelay = static_cast<int>(sampleRate * 0.2f); // 200ms max delay
    delayBuffer_.resize(maxDelay * 8, 0.0f); // 8 delay lines
    delayIndices_.resize(8, 0);
    
    // Set up delay line lengths (prime numbers for minimal correlation)
    std::vector<int> delayLengths = {1117, 1559, 1913, 2269, 2617, 3061, 3491, 3923};
    for (size_t i = 0; i < delayIndices_.size(); ++i) {
        delayIndices_[i] = delayLengths[i] % maxDelay;
    }
}

void Reverb::setSize(float size) { size_ = std::clamp(size, 0.0f, 1.0f); }
void Reverb::setHighDamp(float highDamp) { highDamp_ = std::clamp(highDamp, 0.0f, 1.0f); }
void Reverb::setLowDamp(float lowDamp) { lowDamp_ = std::clamp(lowDamp, 0.0f, 1.0f); }
void Reverb::setLowPass(float lowPass) { lowPass_ = std::clamp(lowPass, 0.0f, 1.0f); }
void Reverb::setDiffusion(float diffusion) { diffusion_ = std::clamp(diffusion, 0.0f, 1.0f); }
void Reverb::setLevel(float level) { level_ = std::clamp(level, 0.0f, 1.0f); }
void Reverb::setEnabled(bool enabled) { enabled_ = enabled; }

void Reverb::process(float* leftIn, float* rightIn, float* leftOut, float* rightOut, int numSamples) {
    if (!enabled_) {
        std::fill(leftOut, leftOut + numSamples, 0.0f);
        std::fill(rightOut, rightOut + numSamples, 0.0f);
        return;
    }
    
    int delayLength = static_cast<int>(delayBuffer_.size() / 8);
    
    for (int i = 0; i < numSamples; ++i) {
        float input = (leftIn[i] + rightIn[i]) * 0.5f;
        float leftSum = 0.0f;
        float rightSum = 0.0f;
        
        // Process through delay lines
        for (int tap = 0; tap < 4; ++tap) {
            int readIndex = (writeIndex_ - delayIndices_[tap] + delayLength) % delayLength;
            int bufferIndex = tap * delayLength + readIndex;
            
            float delayed = delayBuffer_[bufferIndex];
            float feedback = delayed * 0.7f * size_;
            
            delayBuffer_[tap * delayLength + writeIndex_] = input + feedback;
            leftSum += delayed;
        }
        
        for (int tap = 4; tap < 8; ++tap) {
            int readIndex = (writeIndex_ - delayIndices_[tap] + delayLength) % delayLength;
            int bufferIndex = tap * delayLength + readIndex;
            
            float delayed = delayBuffer_[bufferIndex];
            float feedback = delayed * 0.7f * size_;
            
            delayBuffer_[tap * delayLength + writeIndex_] = input + feedback;
            rightSum += delayed;
        }
        
        leftOut[i] = leftSum * level_ * 0.25f;
        rightOut[i] = rightSum * level_ * 0.25f;
        
        writeIndex_ = (writeIndex_ + 1) % delayLength;
    }
}

void Reverb::reset() {
    std::fill(delayBuffer_.begin(), delayBuffer_.end(), 0.0f);
    writeIndex_ = 0;
}

} // namespace FMRack
