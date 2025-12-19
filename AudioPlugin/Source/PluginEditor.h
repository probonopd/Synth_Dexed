#pragma once

#include "PluginProcessor.h"
#include "RackAccordionComponent.h"
#include "FileBrowserDialog.h"
#include "VoiceEditorPanel.h" // Added to include the VoiceEditorPanel
#include "VoiceEditorWindow.h" // Include the custom VoiceEditorWindow class
#include "VoiceBrowserComponent.h"
#include "FMRackVerticalSlider.h" // Custom vertical slider class
#include "DX7LookAndFeel.h" // DX7-inspired visual theme
#include <juce_gui_basics/juce_gui_basics.h> // Added for GUI elements
#include <memory> // Added for std::unique_ptr
#include <map> // Added for help text map

//==============================================================================
class AudioPluginAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor&);
    ~AudioPluginAudioProcessorEditor() override;    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;
    
    // Mouse listener overrides for hover help
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

    void appendLogMessage(const juce::String& message); // Public method to append log

    AudioPluginAudioProcessor* getProcessor() const { return &processorRef; } // Added method to access processor
    void showVoiceEditorPanel(int moduleIndex); // Accepts module index
    VoiceEditorPanel* getVoiceEditorPanel() const
    {
        // The voice editor panel is owned by the VoiceEditorWindow. Keep this accessor working by
        // returning the current window content if it is a VoiceEditorPanel.
        if (voiceEditorWindow)
            return dynamic_cast<VoiceEditorPanel*>(voiceEditorWindow->getContentComponent());
        return nullptr;
    } // Added getter for voiceEditorPanel
    RackAccordionComponent* getRackAccordion() const { return rackAccordion.get(); } // Added getter for rackAccordion
    void showVoiceBrowser(int moduleIndex);

    void numModulesChanged();

private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    AudioPluginAudioProcessor& processorRef;

    FMRackVerticalSlider numModulesSlider;
    juce::Label numModulesLabel;
    FMRackVerticalSlider unisonVoicesSlider;
    juce::Label unisonVoicesLabel;
    FMRackVerticalSlider unisonDetuneSlider;
    juce::Label unisonDetuneLabel;
    FMRackVerticalSlider unisonPanSlider;
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
    juce::TextButton initButton{ "Init" };

    void loadPerformanceButtonClicked();
    void savePerformanceButtonClicked(); // NEW: Save handler
    void initButtonClicked();
    
    void unisonVoicesChanged();
    void unisonDetuneChanged();
    void unisonPanChanged();

    std::unique_ptr<RackAccordionComponent> rackAccordion; // Added for the rack GUI

    // The VoiceEditorPanel is owned by VoiceEditorWindow (DocumentWindow content ownership).
    // Keep this here only if other code expects it, but do not use it for ownership.
    std::unique_ptr<VoiceEditorPanel> voiceEditorPanel;
    std::unique_ptr<VoiceEditorWindow> voiceEditorWindow; // Added for the voice editor window
    std::unique_ptr<VoiceBrowserComponent> voiceBrowser;
    std::unique_ptr<juce::DialogWindow> voiceBrowserWindow;

    // Singleton dialogs: reuse the same instance and bring to front if already open
    std::unique_ptr<FileBrowserDialog> performanceFileDialog;

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

    // DX7-inspired look and feel
    DX7LookAndFeel dx7LookAndFeel;

    // Help panel for displaying context-sensitive help
    juce::Label helpPanel;
    juce::String defaultHelpText;
    juce::var helpJson;
    std::map<std::string, std::string> helpTextByKey;
    void loadMainWindowHelpJson();

public:
    void showHelpForKey(const juce::String& key);
    void restoreDefaultHelp();

private:
    void setupSlider(FMRackVerticalSlider& slider, juce::Label& label, const juce::String& labelText, const juce::String& paramID, std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment)
    {
        addAndMakeVisible(slider);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processorRef.treeState, paramID, slider);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessorEditor)
};
