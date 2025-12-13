#pragma once

#include "PluginProcessor.h"
#include "RackAccordionComponent.h"
#include "FileBrowserDialog.h"
#include "VoiceEditorPanel.h" // Added to include the VoiceEditorPanel
#include "VoiceEditorWindow.h" // Include the custom VoiceEditorWindow class
#include "VoiceBrowserComponent.h"
#include "FMRackVerticalSlider.h" // Custom vertical slider class
#include <juce_gui_basics/juce_gui_basics.h> // Added for GUI elements
#include <memory> // Added for std::unique_ptr

//==============================================================================
class AudioPluginAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor&);
    ~AudioPluginAudioProcessorEditor() override;    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

    void appendLogMessage(const juce::String& message); // Public method to append log

    AudioPluginAudioProcessor* getProcessor() const { return &processorRef; } // Added method to access processor
    void showVoiceEditorPanel(int moduleIndex); // Accepts module index
    VoiceEditorPanel* getVoiceEditorPanel() const { return voiceEditorPanel.get(); } // Added getter for voiceEditorPanel
    RackAccordionComponent* getRackAccordion() const { return rackAccordion.get(); } // Added getter for rackAccordion
    void showVoiceBrowser(int moduleIndex);

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
    juce::TextButton savePerformanceButton{ "Save As..." };
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
    std::unique_ptr<VoiceBrowserComponent> voiceBrowser;

    // Track open file dialogs to ensure they are closed when the editor is destroyed
    std::vector<FileBrowserDialog*> openFileDialogs;

    juce::GroupComponent effectsGroup;
    juce::ToggleButton compressorEnableButton{ "Compressor" };
    juce::ToggleButton reverbEnableButton{ "Reverb" };
    FMRackVerticalSlider reverbSizeSlider;
    juce::Label reverbSizeLabel;
    FMRackVerticalSlider reverbHighDampSlider;
    juce::Label reverbHighDampLabel;
    FMRackVerticalSlider reverbLowDampSlider;
    juce::Label reverbLowDampLabel;
    FMRackVerticalSlider reverbLowPassSlider;
    juce::Label reverbLowPassLabel;
    FMRackVerticalSlider reverbDiffusionSlider;
    juce::Label reverbDiffusionLabel;
    FMRackVerticalSlider reverbLevelSlider;
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
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
        addAndMakeVisible(label);
        label.setText(labelText, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processorRef.treeState, paramID, slider);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessorEditor)
};
