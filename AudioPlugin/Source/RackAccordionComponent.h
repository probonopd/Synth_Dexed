#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <atomic>
#include "../../src/FMRack/Rack.h"
#include "FileBrowserDialog.h"
#include "FMRackLabeledVerticalSlider.h" // New labeled vertical slider class

// Forward declaration
class AudioPluginAudioProcessorEditor;
class ModuleTabComponent;
class StereoVolumeMeter; // Forward-declare StereoVolumeMeter

class RackAccordionComponent : public juce::Component, public juce::ValueTree::Listener
{
public:
    RackAccordionComponent(AudioPluginAudioProcessor* processorPtr);
    void resized() override;
    void paint(juce::Graphics&) override;    void updatePanels();
    void forceSync(); // Force sync between rack modules and UI tabs
    void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& property) override;

    // ValueTree for UI sync (AudioPlugin only)
    juce::ValueTree valueTree { "RackUI" };
    void setNumModulesVT(int num);
    int getNumModulesVT() const;

    AudioPluginAudioProcessorEditor* getEditor() const { return editor; }
    void setEditor(AudioPluginAudioProcessorEditor* ed) { editor = ed; }
    
    // Expose moduleTabs for safe read-only access (for crash prevention logic)
    const std::vector<std::unique_ptr<ModuleTabComponent>>& getModuleTabs() const { return moduleTabs; }

    // Get the currently selected tab index
    int getCurrentTabIndex() const { return tabs.getCurrentTabIndex(); }

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
    
    // Mouse listener overrides for hover help
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    
    // Per-tab controls
    FMRackLabeledVerticalSlider unisonVoicesSlider;
    FMRackLabeledVerticalSlider unisonDetuneSlider;
    FMRackLabeledVerticalSlider unisonPanSlider;
    FMRackLabeledVerticalSlider midiChannelSlider;

    FMRackLabeledVerticalSlider reverbSendSlider;
    FMRackLabeledVerticalSlider volumeSlider;
    FMRackLabeledVerticalSlider panSlider;
    FMRackLabeledVerticalSlider detuneSlider;

    // Note Range
    FMRackLabeledVerticalSlider noteLimitLowSlider;
    FMRackLabeledVerticalSlider noteLimitHighSlider;
    FMRackLabeledVerticalSlider noteShiftSlider;

    // Pitch Bend (TX816Perf: PBR, PBS)
    FMRackLabeledVerticalSlider pitchBendRangeSlider;    // PBR
    FMRackLabeledVerticalSlider pitchBendStepSlider;     // PBS

    // Portamento (TX816Perf: PRT, PGL, PMD)
    juce::ToggleButton portamentoGlissandoButton;        // PGL
    juce::Label portamentoGlissandoLabel;
    FMRackLabeledVerticalSlider portamentoTimeSlider;    // PRT
    juce::ToggleButton portamentoModeButton;             // PMD (Normal/Fingered)
    juce::Label portamentoModeLabel;

    // Mono Mode (TX816Perf: PMO)
    juce::ToggleButton monoModeButton;                   // PMO
    juce::Label monoModeLabel;
    
    // Controller Assignments (TX816Perf: MWS, MWA, FCS, FCA, ATS, ATA, BCS, BCA)
    FMRackLabeledVerticalSlider modWheelSensSlider;      // MWS
    FMRackLabeledVerticalSlider modWheelAssignSlider;    // MWA
    FMRackLabeledVerticalSlider footCtrlSensSlider;      // FCS
    FMRackLabeledVerticalSlider footCtrlAssignSlider;    // FCA
    FMRackLabeledVerticalSlider afterTouchSensSlider;    // ATS
    FMRackLabeledVerticalSlider afterTouchAssignSlider;  // ATA
    FMRackLabeledVerticalSlider breathCtrlSensSlider;    // BCS
    FMRackLabeledVerticalSlider breathCtrlAssignSlider;  // BCA

    // Misc (TX816Perf: ATT, MTU)
    FMRackLabeledVerticalSlider velocityScaleSlider;
    FMRackLabeledVerticalSlider audioAttenuatorSlider;   // ATT
    FMRackLabeledVerticalSlider masterTuneSlider;        // MTU

    // Filter
    juce::ToggleButton filterEnabledButton;
    juce::Label filterEnabledLabel;
    FMRackLabeledVerticalSlider filterCutoffSlider;
    FMRackLabeledVerticalSlider filterResonanceSlider;

    juce::TextButton loadVoiceButton;
    juce::TextButton openVoiceEditorButton;
    juce::TextButton browsePatchesButton;

    std::unique_ptr<StereoVolumeMeter> volumeMeter;

    // Group components for layout
    juce::GroupComponent voiceGroup;
    juce::GroupComponent unisonGroup;
    juce::GroupComponent midiPitchGroup;
    juce::GroupComponent noteRangeGroup;
    juce::GroupComponent portaMonoGroup;
    juce::GroupComponent filterGroup;
    juce::GroupComponent mainPartGroup; // NEW

    bool isFileDialogOpen() const;
    void closeFileDialog();

private:
    int moduleIndex;
    RackAccordionComponent* parentAccordion;
    std::atomic<bool> fileDialogOpen { false };
    std::unique_ptr<FileBrowserDialog> openVoiceFileDialog; // Singleton voice file dialog for this tab

    void loadVoiceFile(const juce::File& file);
    void loadVoiceFileIntoModule(const juce::File& file, int targetModuleIndex);

    // Helper methods to get controller and performance
    FMRackController* getController();
    FMRack::Performance* getPerformance();
};
