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

    juce::TextButton loadPerformanceButton { "Load Performance .ini" };
    juce::TextButton savePerformanceButton { "Save Performance .ini" }; // NEW: Save button
    void loadPerformanceButtonClicked();
    void savePerformanceButtonClicked(); // NEW: Save handler
    
    void numModulesChanged();
    void unisonVoicesChanged();
    void unisonDetuneChanged();
    void unisonPanChanged();

    std::unique_ptr<RackAccordionComponent> rackAccordion; // Added for the rack GUI

    std::unique_ptr<VoiceEditorPanel> voiceEditorPanel; // Added to manage the new panel
    std::unique_ptr<VoiceEditorWindow> voiceEditorWindow; // Added for the voice editor window

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessorEditor)
};
