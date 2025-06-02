#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../../src/FMRack/Rack.h"
#include "FileBrowserDialog.h"

// Forward declaration
class AudioPluginAudioProcessorEditor;
class ModuleTabComponent;

class RackAccordionComponent : public juce::Component
{
public:
    RackAccordionComponent(FMRack::Rack* rackPtr);
    void resized() override;
    void paint(juce::Graphics&) override;
    void updatePanels();
    
    // Controls to move from PluginEditor
    juce::Slider numModulesSlider;
    juce::Label numModulesLabel;
    
    AudioPluginAudioProcessorEditor* getEditor() const { return editor; }
    void setEditor(AudioPluginAudioProcessorEditor* ed) { editor = ed; }
    
private:
    FMRack::Rack* rack;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    std::vector<std::unique_ptr<ModuleTabComponent>> moduleTabs;
    AudioPluginAudioProcessorEditor* editor = nullptr;
};

class ModuleTabComponent : public juce::Component {
public:
    ModuleTabComponent(FMRack::Module* modulePtr, int idx, RackAccordionComponent* parent);
    void resized() override;
    void updateFromModule(); // NEW: update sliders from module state
    
    // Per-tab controls
    juce::Slider unisonVoicesSlider;
    juce::Label unisonVoicesLabel;
    juce::Slider unisonDetuneSlider;
    juce::Label unisonDetuneLabel;
    juce::Slider unisonPanSlider;
    juce::Label unisonPanLabel;
    juce::TextButton loadVoiceButton;
    juce::Slider midiChannelSlider;
    juce::Label midiChannelLabel;
    juce::TextButton openVoiceEditorButton { "Voice Editor" };

    bool isFileDialogOpen() const { return false; } // No longer needed

private:
    FMRack::Module* module;
    int moduleIndex;
    RackAccordionComponent* parentAccordion;
    
    void loadVoiceFile(const juce::File& file);
};
