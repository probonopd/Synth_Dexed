/*
 * SPX90 Symphonic Effect - ESP32 Port
 *
 * Ported from AudioPlugin/SPX90Symphonic/Source/PluginProcessor.cpp
 * Stripped of JUCE dependencies for bare-metal ESP32-S3.
 */

#include "AudioEffectSymphonic.h"

namespace FMRack {

// Static constexpr member definitions (required pre-C++17 inline)
constexpr float AudioEffectSymphonic::baseDelayMs_[];
constexpr float AudioEffectSymphonic::baseLfoRates_[];
constexpr float AudioEffectSymphonic::lfoStartPhases_[];

AudioEffectSymphonic::AudioEffectSymphonic(float sampleRate)
    : sampleRate_(sampleRate)
{
    // Initialize delay lines with pre-allocated memory
    int maxDelaySamples = static_cast<int>(sampleRate * MAX_DELAY_MS / 1000.0f) + 10;
    if (maxDelaySamples > MAX_DELAY_SAMPLES)
        maxDelaySamples = MAX_DELAY_SAMPLES;

    for (int i = 0; i < NUM_VOICES; ++i) {
        delays_[i].init(delayMem_[i], maxDelaySamples);
        lfos_[i].rate = baseLfoRates_[i];
        lfos_[i].phase = lfoStartPhases_[i];
    }

    // One-pole LPF coefficient for ~10kHz rolloff
    // y[n] = gain * x[n] + coeff * y[n-1]
    // coeff = exp(-2*pi*fc/fs)
    lpfCoeff_ = expf(-TWO_PI * 10000.0f / sampleRate);
    lpfGain_ = 1.0f - lpfCoeff_;
}

void AudioEffectSymphonic::process(const float* monoIn, float* leftOut, float* rightOut, int numSamples)
{
    if (!enabled_ || numSamples <= 0) {
        // Bypass: copy mono to both channels
        if (monoIn != leftOut)
            memcpy(leftOut, monoIn, numSamples * sizeof(float));
        if (monoIn != rightOut)
            memcpy(rightOut, monoIn, numSamples * sizeof(float));
        return;
    }

    // Convert depth percentage to ms (0-100% maps to 0-8ms modulation depth, +/-4ms)
    const float depthMs = depthPercent_ * 2.0f;

    const float dryGain = 1.0f - mix_;
    const float wetGain = mix_;

    const float maxDelaySamples = static_cast<float>(MAX_DELAY_MS) * sampleRate_ / 1000.0f;

    for (int n = 0; n < numSamples; ++n) {
        float in = monoIn[n];

        float wetL = 0.0f;
        float wetR = 0.0f;

        for (int i = 0; i < NUM_VOICES; ++i) {
            // Update LFO rate: user rate * per-voice multiplier
            lfos_[i].rate = baseLfoRates_[i] * modFreqHz_;

            // Get modulated delay time
            float mod = lfos_[i].tick(sampleRate_);
            float delayMs = baseDelayMs_[i] + mod * depthMs;
            float delaySamp = delayMs * sampleRate_ / 1000.0f;

            // Clamp to valid range
            if (delaySamp < 1.0f) delaySamp = 1.0f;
            if (delaySamp > maxDelaySamples) delaySamp = maxDelaySamples;

            // Read from delay and write input
            float delayed = delays_[i].read(delaySamp);
            delays_[i].write(in);

            // Stereo matrix (tri-chorus style, tuned for width + mono compatibility)
            switch (i) {
                case 0: wetL += delayed * 0.9f;  wetR += delayed * 0.1f;  break;
                case 1: wetL += delayed * 0.1f;  wetR += delayed * 0.9f;  break;
                case 2: wetL += delayed * 0.65f; wetR += delayed * 0.65f; break;
            }
        }

        // Gentle normalization
        wetL *= 0.8f;
        wetR *= 0.8f;

        // Apply one-pole LPF on wet path (SPX90 characteristic ~10kHz rolloff)
        lpfStateL_ = lpfGain_ * wetL + lpfCoeff_ * lpfStateL_;
        lpfStateR_ = lpfGain_ * wetR + lpfCoeff_ * lpfStateR_;
        wetL = lpfStateL_;
        wetR = lpfStateR_;

        // Mix dry and wet
        leftOut[n]  = in * dryGain + wetL * wetGain;
        rightOut[n] = in * dryGain + wetR * wetGain;
    }
}

} // namespace FMRack
