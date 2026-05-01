/*
  ==============================================================================

    SPX90 Symphonic Effect
    
    A faithful emulation of the Yamaha SPX90 Symphonic algorithm.
    
    SPX90-specific traits:
    - 3 fixed voices with specific delay offsets
    - Extremely slow LFOs (sub-Hz, unsynced)
    - Narrow modulation depth
    - Phase-engineered stereo matrix
    - Gentle wet-path HF rolloff (~10kHz)
    - Dry always present
    - No feedback, no tempo sync
    - Mono-in / stereo-out topology

  ==============================================================================
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

namespace SPX90
{

constexpr int NUM_VOICES = 3;
constexpr int MAX_DELAY_MS = 30;

//==============================================================================
/** Simple sine LFO for modulating delay times */
struct LFO
{
    float phase = 0.0f;
    float rate = 0.0f;  // Hz

    inline float tick(double sampleRate) noexcept
    {
        float v = std::sin(phase);
        phase += juce::MathConstants<float>::twoPi * rate / static_cast<float>(sampleRate);
        if (phase > juce::MathConstants<float>::twoPi)
            phase -= juce::MathConstants<float>::twoPi;
        return v;
    }

    void reset() noexcept { phase = 0.0f; }
};

//==============================================================================
/** Simple delay line with linear interpolation for fractional delays */
struct DelayLine
{
    juce::AudioBuffer<float> buffer;
    int writePos = 0;

    void prepare(int maxSamples)
    {
        buffer.setSize(1, maxSamples + 2);  // +2 for interpolation safety
        buffer.clear();
        writePos = 0;
    }

    void clear()
    {
        buffer.clear();
        writePos = 0;
    }

    inline float read(float delaySamples) const noexcept
    {
        int size = buffer.getNumSamples();
        
        int i0 = static_cast<int>(delaySamples);
        int i1 = i0 + 1;
        float frac = delaySamples - static_cast<float>(i0);

        int idx0 = (writePos - i0 + size) % size;
        int idx1 = (writePos - i1 + size) % size;

        const float* data = buffer.getReadPointer(0);
        return data[idx0] * (1.0f - frac) + data[idx1] * frac;
    }

    inline void write(float sample) noexcept
    {
        buffer.setSample(0, writePos, sample);
        writePos = (writePos + 1) % buffer.getNumSamples();
    }
};

}  // namespace SPX90

//==============================================================================
class SPX90SymphonicAudioProcessor : public juce::AudioProcessor
{
public:
    SPX90SymphonicAudioProcessor();
    ~SPX90SymphonicAudioProcessor() override = default;

    //==============================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //==============================================================================
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.03; }  // Max delay ~30ms

    //==============================================================================
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    //==============================================================================
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState& getParameters() { return parameters; }

private:
    //==============================================================================
    juce::AudioProcessorValueTreeState parameters;
    
    // Atomic parameter values for thread-safe access
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* depthParam = nullptr;
    std::atomic<float>* speedParam = nullptr;

    double currentSampleRate = 48000.0;

    // Voice components
    SPX90::DelayLine delays[SPX90::NUM_VOICES];
    SPX90::LFO lfos[SPX90::NUM_VOICES];

    // SPX90-derived constants
    // Shorter delays and gentle offsets for a denser tri-chorus feel
    float baseDelayMs[SPX90::NUM_VOICES] = { 3.5f, 5.2f, 6.8f };
    // Per-voice multipliers around the user rate
    float baseLfoRates[SPX90::NUM_VOICES] = { 0.9f, 1.0f, 1.12f };
    float lfoStartPhases[SPX90::NUM_VOICES] = { 0.0f, 2.1f, 4.0f };

    // Wet HF rolloff filter (~10kHz, SPX90-style)
    juce::dsp::IIR::Filter<float> lpfL, lpfR;
    juce::dsp::IIR::Coefficients<float>::Ptr lpfCoeffs;

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SPX90SymphonicAudioProcessor)
};
