/*
  ==============================================================================

    SPX90 Symphonic Effect - Plugin Editor Header
    
    Minimal GUI matching the simplicity of the original SPX90.

  ==============================================================================
*/

#pragma once

#include "PluginProcessor.h"
#include "../../Source/FMRackVerticalSlider.h"

//==============================================================================
class SPX90SymphonicAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit SPX90SymphonicAudioProcessorEditor(SPX90SymphonicAudioProcessor&);
    ~SPX90SymphonicAudioProcessorEditor() override;

    //==============================================================================
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    SPX90SymphonicAudioProcessor& processorRef;

    // Bypass toggle
    juce::ToggleButton bypassButton{ "Bypass" };

  // Simple knob-style controls (use unified FMRackVerticalSlider)
  FMRackVerticalSlider mixSlider;
  FMRackVerticalSlider depthSlider;
  FMRackVerticalSlider speedSlider;

    juce::Label mixLabel;
    juce::Label depthLabel;
    juce::Label speedLabel;
    juce::Label titleLabel;

    // Parameter attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> depthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> speedAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SPX90SymphonicAudioProcessorEditor)
};
