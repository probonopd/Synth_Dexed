/*
  ==============================================================================

    SPX90 Symphonic Effect - Audio Processor Implementation
    
    Faithful emulation of the Yamaha SPX90 Symphonic algorithm.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
SPX90SymphonicAudioProcessor::SPX90SymphonicAudioProcessor()
    : AudioProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, juce::Identifier("SPX90Symphonic"), createParameterLayout())
{
    // Cache parameter pointers for thread-safe access
    bypassParam = parameters.getRawParameterValue("bypass");
    mixParam = parameters.getRawParameterValue("mix");
    depthParam = parameters.getRawParameterValue("depth");
    speedParam = parameters.getRawParameterValue("speed");

    // Initialize LFOs with SPX90-derived phase offsets
    for (int i = 0; i < SPX90::NUM_VOICES; ++i)
    {
        lfos[i].rate = baseLfoRates[i];
        lfos[i].phase = lfoStartPhases[i];
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout SPX90SymphonicAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Bypass
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("bypass", 1),
        "Bypass",
        false
    ));

    // Balance: 0-100% (dry/wet balance) - default 100% wet
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("mix", 1),
        "Balance",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        100.0f,  // Default 100% wet
        juce::String(),
        juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; },
        [](const juce::String& text) { return text.getFloatValue(); }
    ));

    // Mod Depth: 0-100% modulation depth
    // This sets the amount of delay time variation, thus adjusting the "depth" of the effect.
    // At the maximum setting, the delay time is varied by +/-4 msec.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("depth", 1),
        "Mod Depth",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        50.0f,  // Default 50%
        juce::String(),
        juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + "%"; },
        [](const juce::String& text) { return text.getFloatValue(); }
    ));

    // Mod Freq: LFO frequency in Hz (0.1 - 20.0 Hz)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("speed", 1),
        "Mod Freq",
        juce::NormalisableRange<float>(0.1f, 20.0f, 0.01f),
        0.7f,  // Default 0.7 Hz
        juce::String(),
        juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 2) + " Hz"; },
        [](const juce::String& text) { return text.getFloatValue(); }
    ));

    return { params.begin(), params.end() };
}

//==============================================================================
void SPX90SymphonicAudioProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate;

    // Prepare delay lines
    int maxDelaySamples = static_cast<int>(sampleRate * SPX90::MAX_DELAY_MS / 1000.0) + 10;
    for (int i = 0; i < SPX90::NUM_VOICES; ++i)
    {
        delays[i].prepare(maxDelaySamples);
        lfos[i].rate = baseLfoRates[i];
        lfos[i].phase = lfoStartPhases[i];
    }

    // Prepare SPX90-style wet HF rolloff (~10kHz)
    lpfCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, 10000.0f);
    lpfL.coefficients = lpfCoeffs;
    lpfR.coefficients = lpfCoeffs;
    lpfL.reset();
    lpfR.reset();
}

void SPX90SymphonicAudioProcessor::releaseResources()
{
    for (int i = 0; i < SPX90::NUM_VOICES; ++i)
        delays[i].clear();
}

//==============================================================================
void SPX90SymphonicAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                                  juce::MidiBuffer& /*midiMessages*/)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numChannels == 0 || numSamples == 0)
        return;

    // Get current parameter values
    const bool bypassed = bypassParam->load() > 0.5f;
    const float mix = mixParam->load() / 100.0f;  // Convert 0-100 to 0-1
    const float depthPercent = depthParam->load() / 100.0f;  // Convert 0-100 to 0-1
    const float modFreq = speedParam->load();  // Direct Hz value

    // Convert depth percentage to ms (0-100% maps to 0-8ms modulation depth, +/-4ms variation)
    // At maximum setting, delay time varied by +/-4 msec
    const float depthMs = depthPercent * 2.0f;

    // Calculate wet/dry gains for crossfade
    // At 100% mix: full wet, no dry
    // At 0% mix: full dry, no wet
    const float dryGain = bypassed ? 1.0f : (1.0f - mix);
    const float wetGain = bypassed ? 0.0f : mix;

    auto* inL = buffer.getReadPointer(0);
    auto* inR = numChannels > 1 ? buffer.getReadPointer(1) : nullptr;
    auto* outL = buffer.getWritePointer(0);
    auto* outR = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

    for (int n = 0; n < numSamples; ++n)
    {
        // SPX90 is mono-in / stereo-out: sum input to mono
        float monoIn = inL[n];
        if (inR != nullptr)
            monoIn = (inL[n] + inR[n]) * 0.5f;

        float wetL = 0.0f;
        float wetR = 0.0f;

        for (int i = 0; i < SPX90::NUM_VOICES; ++i)
        {
            // Update LFO rate: user rate * per-voice multiplier
            lfos[i].rate = baseLfoRates[i] * modFreq;

            // Get modulated delay time
            float mod = lfos[i].tick(currentSampleRate);
            float delayMs = baseDelayMs[i] + mod * depthMs;
            float delaySamples = delayMs * static_cast<float>(currentSampleRate) / 1000.0f;

            // Clamp to valid range
            delaySamples = juce::jlimit(1.0f, static_cast<float>(SPX90::MAX_DELAY_MS * currentSampleRate / 1000.0f), delaySamples);

            // Read from delay and write input
            float delayed = delays[i].read(delaySamples);
            delays[i].write(monoIn);

            // Stereo matrix tuned for width and mono compatibility (tri-chorus style)
            switch (i)
            {
                case 0: wetL += delayed * 0.9f;  wetR += delayed * 0.1f; break; // left-leaning
                case 1: wetL += delayed * 0.1f;  wetR += delayed * 0.9f; break; // right-leaning
                case 2: wetL += delayed * 0.65f; wetR += delayed * 0.65f; break; // center body
            }
        }

        // Keep wet signal strong; gentle normalization
        wetL *= 0.8f;
        wetR *= 0.8f;

        // Apply gentle HF rolloff on wet path only (SPX90 characteristic)
        wetL = lpfL.processSample(wetL);
        wetR = lpfR.processSample(wetR);

        // Mix dry and wet
        outL[n] = monoIn * dryGain + wetL * wetGain;
        if (outR != nullptr)
            outR[n] = monoIn * dryGain + wetR * wetGain;
    }
}

//==============================================================================
juce::AudioProcessorEditor* SPX90SymphonicAudioProcessor::createEditor()
{
    return new SPX90SymphonicAudioProcessorEditor(*this);
}

//==============================================================================
void SPX90SymphonicAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void SPX90SymphonicAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName(parameters.state.getType()))
        parameters.replaceState(juce::ValueTree::fromXml(*xml));
}

//==============================================================================
// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SPX90SymphonicAudioProcessor();
}
