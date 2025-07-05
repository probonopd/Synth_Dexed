#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <atomic>
#include "../../src/FMRack/Rack.h"
#include "FileBrowserDialog.h"

// Forward declaration
class AudioPluginAudioProcessorEditor;
class ModuleTabComponent;

class RackAccordionComponent : public juce::Component, public juce::ValueTree::Listener
{
public:
    RackAccordionComponent(AudioPluginAudioProcessor* processorPtr);
    void resized() override;
    void paint(juce::Graphics&) override;    void updatePanels();
    void forceSync(); // Force sync between rack modules and UI tabs
    void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& property) override;

    // Controls to move from PluginEditor
    juce::TextButton addModuleButton { "+" };
    juce::TextButton removeModuleButton { "-" };

    // ValueTree for UI sync (AudioPlugin only)
    juce::ValueTree valueTree { "RackUI" };
    void setNumModulesVT(int num);
    int getNumModulesVT() const;

    AudioPluginAudioProcessorEditor* getEditor() const { return editor; }
    void setEditor(AudioPluginAudioProcessorEditor* ed) { editor = ed; }
    
    // Expose moduleTabs for safe read-only access (for crash prevention logic)
    const std::vector<std::unique_ptr<ModuleTabComponent>>& getModuleTabs() const { return moduleTabs; }

    void suppressNumModulesSync(bool shouldSuppress) { suppressSetNumModulesVT = shouldSuppress; }

private:
    void syncNumModulesSliderWithRack();
    AudioPluginAudioProcessor* processor;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    std::vector<std::unique_ptr<ModuleTabComponent>> moduleTabs;
    AudioPluginAudioProcessorEditor* editor = nullptr;    // Suppress setNumModulesVT during state/UI sync
    bool suppressSetNumModulesVT = false;
    // Prevent recursive updatePanels calls
    std::atomic<bool> updatingPanels = false;
};

class ModuleTabComponent : public juce::Component {
public:
    ModuleTabComponent(int idx, RackAccordionComponent* parent);
    ~ModuleTabComponent() override;
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
    juce::TextButton openVoiceEditorButton { "Edit Voice" };

    bool isFileDialogOpen() const;
    void closeFileDialog();

private:
    int moduleIndex;
    RackAccordionComponent* parentAccordion;
    std::atomic<bool> fileDialogOpen { false };
    std::unique_ptr<class StereoVolumeMeter> volumeMeter;

    void loadVoiceFile(const juce::File& file);
};
