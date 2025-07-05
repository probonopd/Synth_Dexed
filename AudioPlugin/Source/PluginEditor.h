#pragma once

#include "PluginProcessor.h"
#include "RackAccordionComponent.h"
#include "FileBrowserDialog.h"
#include "VoiceEditorPanel.h" // Added to include the VoiceEditorPanel
#include "VoiceEditorWindow.h" // Include the custom VoiceEditorWindow class
#include <juce_gui_basics/juce_gui_basics.h> // Added for GUI elements
#include <memory> // Added for std::unique_ptr

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

    AudioPluginAudioProcessor* getProcessor() const { return &processorRef; } // Added method to access processor
    void showVoiceEditorPanel(int moduleIndex); // Accepts module index
    VoiceEditorPanel* getVoiceEditorPanel() const { return voiceEditorPanel.get(); } // Added getter for voiceEditorPanel
    RackAccordionComponent* getRackAccordion() const { return rackAccordion.get(); } // Added getter for rackAccordion

    void numModulesChanged();

private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    AudioPluginAudioProcessor& processorRef;

    juce::Slider numModulesSlider;
    juce::Label numModulesLabel;
    juce::Slider unisonVoicesSlider;
    juce::Label unisonVoicesLabel;
    juce::Slider unisonDetuneSlider;
    juce::Label unisonDetuneLabel;
    juce::Slider unisonPanSlider;
    juce::Label unisonPanLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> numModulesAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> unisonVoicesAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> unisonDetuneAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> unisonPanAttachment;

    juce::TextEditor logTextBox; // For displaying log messages

    juce::TextButton loadPerformanceButton{ "Load Performance..." };
    juce::TextButton savePerformanceButton{ "Save Performance As..." };
    juce::TextButton addModuleButton{ "+" };
    juce::TextButton removeModuleButton{ "-" };

    void loadPerformanceButtonClicked();
    void savePerformanceButtonClicked(); // NEW: Save handler
    
    void unisonVoicesChanged();
    void unisonDetuneChanged();
    void unisonPanChanged();

    std::unique_ptr<RackAccordionComponent> rackAccordion; // Added for the rack GUI

    std::unique_ptr<VoiceEditorPanel> voiceEditorPanel; // Added to manage the new panel
    std::unique_ptr<VoiceEditorWindow> voiceEditorWindow; // Added for the voice editor window

    juce::GroupComponent effectsGroup;
    juce::ToggleButton compressorEnableButton{ "Compressor" };
    juce::ToggleButton reverbEnableButton{ "Reverb" };
    juce::Slider reverbSizeSlider;
    juce::Label reverbSizeLabel;
    juce::Slider reverbHighDampSlider;
    juce::Label reverbHighDampLabel;
    juce::Slider reverbLowDampSlider;
    juce::Label reverbLowDampLabel;
    juce::Slider reverbLowPassSlider;
    juce::Label reverbLowPassLabel;
    juce::Slider reverbDiffusionSlider;
    juce::Label reverbDiffusionLabel;
    juce::Slider reverbLevelSlider;
    juce::Label reverbLevelLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> compressorEnableAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> reverbEnableAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> reverbSizeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> reverbHighDampAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> reverbLowDampAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> reverbLowPassAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> reverbDiffusionAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> reverbLevelAttachment;

    juce::ResizableCornerComponent resizer;
    juce::ComponentBoundsConstrainer constrainer;

    void setupSlider(juce::Slider& slider, juce::Label& label, const juce::String& labelText, const juce::String& paramID, std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment)
    {
        addAndMakeVisible(slider);
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
        addAndMakeVisible(label);
        label.setText(labelText, juce::dontSendNotification);
        label.attachToComponent(&slider, true);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processorRef.treeState, paramID, slider);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessorEditor)
};
