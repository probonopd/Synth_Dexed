/*
 * SPX90 Symphonic Effect - ESP32 Port
 *
 * Faithful emulation of the Yamaha SPX90 Symphonic algorithm.
 *
 * SPX90-specific traits:
 * - 3 fixed voices with specific delay offsets
 * - Extremely slow LFOs (sub-Hz, unsynced)
 * - Narrow modulation depth
 * - Phase-engineered stereo matrix
 * - Gentle wet-path HF rolloff (~10kHz)
 * - Dry always present
 * - No feedback, no tempo sync
 * - Mono-in / stereo-out topology
 *
 * Ported from AudioPlugin/SPX90Symphonic/Source/PluginProcessor.cpp
 */

#pragma once

#include <cstdint>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace FMRack {

class AudioEffectSymphonic {
public:
    AudioEffectSymphonic(float sampleRate);

    // Process mono input to stereo output (float, -1.0 to 1.0)
    void process(const float* monoIn, float* leftOut, float* rightOut, int numSamples);

    // Parameter setters
    void setEnabled(bool enabled) { enabled_ = enabled; }
    void setMix(float mix) { mix_ = std::max(0.0f, std::min(1.0f, mix)); }
    void setDepth(float depth) { depthPercent_ = std::max(0.0f, std::min(1.0f, depth)); }
    void setSpeed(float hz) { modFreqHz_ = std::max(0.1f, std::min(20.0f, hz)); }

    // Parameter getters
    bool isEnabled() const { return enabled_; }
    float getMix() const { return mix_; }
    float getDepth() const { return depthPercent_; }
    float getSpeed() const { return modFreqHz_; }

private:
    static constexpr int NUM_VOICES = 3;
    static constexpr int MAX_DELAY_MS = 30;
    static constexpr float TWO_PI = 6.283185307f;

    // Sine LFO for modulating delay times
    struct LFO {
        float phase;
        float rate; // Hz

        inline float tick(float sampleRate) {
            float v = sinf(phase);
            phase += TWO_PI * rate / sampleRate;
            if (phase > TWO_PI)
                phase -= TWO_PI;
            return v;
        }
    };

    // Delay line with linear interpolation for fractional delays
    struct DelayLine {
        float* buffer;
        int size;
        int writePos;

        void init(float* mem, int maxSamples) {
            buffer = mem;
            size = maxSamples + 2; // +2 for interpolation safety
            writePos = 0;
            memset(buffer, 0, size * sizeof(float));
        }

        void clear() {
            if (buffer) memset(buffer, 0, size * sizeof(float));
            writePos = 0;
        }

        inline float read(float delaySamples) const {
            int i0 = static_cast<int>(delaySamples);
            int i1 = i0 + 1;
            float frac = delaySamples - static_cast<float>(i0);

            int idx0 = (writePos - i0 + size) % size;
            int idx1 = (writePos - i1 + size) % size;

            return buffer[idx0] * (1.0f - frac) + buffer[idx1] * frac;
        }

        inline void write(float sample) {
            buffer[writePos] = sample;
            writePos = (writePos + 1) % size;
        }
    };

    float sampleRate_;
    bool enabled_ = false;

    // Parameters
    float mix_ = 1.0f;           // 0..1 wet/dry balance
    float depthPercent_ = 0.5f;  // 0..1 modulation depth
    float modFreqHz_ = 0.7f;     // LFO frequency in Hz

    // Voice components
    DelayLine delays_[NUM_VOICES];
    LFO lfos_[NUM_VOICES];

    // SPX90-derived constants
    static constexpr float baseDelayMs_[NUM_VOICES] = { 3.5f, 5.2f, 6.8f };
    static constexpr float baseLfoRates_[NUM_VOICES] = { 0.9f, 1.0f, 1.12f };
    static constexpr float lfoStartPhases_[NUM_VOICES] = { 0.0f, 2.1f, 4.0f };

    // One-pole LPF for wet-path HF rolloff (~10kHz)
    float lpfCoeff_;      // feedback coefficient
    float lpfGain_;       // input gain (1 - coeff)
    float lpfStateL_ = 0.0f;
    float lpfStateR_ = 0.0f;

    // Delay line memory (pre-allocated, sized for max delay at given sample rate)
    // At 48kHz, 30ms = 1440 samples + 2 padding = 1442 per line, 3 lines ≈ 17KB
    static constexpr int MAX_DELAY_SAMPLES = 1442 + 10; // generous padding
    float delayMem_[NUM_VOICES][MAX_DELAY_SAMPLES];
};

} // namespace FMRack
