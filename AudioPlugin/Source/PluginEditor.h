#pragma once

#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h> // Added for GUI elements

//==============================================================================
class AudioPluginAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor&);
    ~AudioPluginAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

    void appendLogMessage(const juce::String& message); // Public method to append log

private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    AudioPluginAudioProcessor& processorRef;

    juce::Slider unisonVoicesSlider; // Added
    juce::Label unisonVoicesLabel; // Added
    juce::Slider unisonDetuneSlider; // Added
    juce::Label unisonDetuneLabel; // Added

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> unisonVoicesAttachment; // Added
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> unisonDetuneAttachment; // Added

    juce::TextEditor logTextBox; // For displaying log messages

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessorEditor)
};
