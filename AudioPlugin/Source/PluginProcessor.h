#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h> // Added for AudioProcessorValueTreeState
#include <juce_core/juce_core.h> // Added for FileLogger
#include <vector> // Added for std::vector
#include <memory> // Added for std::unique_ptr
#include "../../src/FMRack/Module.h"
#include "../../src/FMRack/Performance.h"
#include "../../src/FMRack/Rack.h" // Add this include

//==============================================================================
class AudioPluginAudioProcessor final : public juce::AudioProcessor
{
public:
    //==============================================================================
    AudioPluginAudioProcessor();
    ~AudioPluginAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;

    template <typename FloatType>
    void process(juce::AudioBuffer<FloatType>& buffer, juce::MidiBuffer& midiMessages);

    using AudioProcessor::processBlock;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState treeState; // Changed from valueTreeState

private:
    //==============================================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout(); // Added
    
    std::unique_ptr<FMRack::Module> module; // Use FMRack::Module
    std::unique_ptr<FMRack::Rack> rack; // Use FMRack::Rack for multi-part support
    std::unique_ptr<FMRack::Performance> performance; // Store the loaded performance

    std::unique_ptr<juce::FileLogger> fileLogger; // Added for file logging

    bool wasMidiEmptyLastBlock = true; // Added to track MIDI state for logging

    // Pointer to the editor for GUI logging
    class AudioPluginAudioProcessorEditor* editorPtr = nullptr;

public:
    void setEditorPointer(class AudioPluginAudioProcessorEditor* editor);
    void logToGui(const juce::String& message);
    bool loadPerformanceFile(const juce::String& path); // Add this
    void setNumModules(int num);
    void setUnisonVoices(int num);
    void setUnisonDetune(float detune);
    void setUnisonPan(float pan);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessor)
};
