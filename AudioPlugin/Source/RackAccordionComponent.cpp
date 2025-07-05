#include "OperatorSliderLookAndFeel.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "RackAccordionComponent.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <fstream>
#include "../../src/FMRack/VoiceData.h" // NEW: Include VoiceData for conversion function
#include <juce_gui_extra/juce_gui_extra.h>

static OperatorSliderLookAndFeel operatorSliderLookAndFeel;

// --- StereoVolumeMeter: simple L/R bar meter with pre-gain only, in color ---
class StereoVolumeMeter : public juce::Component, private juce::Timer {
public:
    StereoVolumeMeter() {
        startTimerHz(30); // 30 Hz update
        pre[0] = pre[1] = 0.0f;
    }
    void setLevels(float preL, float preR) {
        // Scale up the RMS for better visual feedback, clamp to 1.0
        pre[0] = juce::jlimit(0.0f, 1.0f, preL * 15.0f);
        pre[1] = juce::jlimit(0.0f, 1.0f, preR * 15.0f);
        repaint();
    }
    void paint(juce::Graphics& g) override {
        auto area = getLocalBounds().reduced(2);
        g.setColour(juce::Colours::black);
        g.fillRect(area); // Black background
        int w = area.getWidth() / 2 - 2;
        int h = area.getHeight();
        // Draw left (pre-gain, limegreen)
        int preLH = juce::jlimit(0, h, (int)(pre[0] * h));
        g.setColour(juce::Colours::limegreen);
        g.fillRect(area.getX(), area.getBottom() - preLH, w, preLH);
        // Draw right (pre-gain, aqua)
        int preRH = juce::jlimit(0, h, (int)(pre[1] * h));
        g.setColour(juce::Colours::aqua);
        g.fillRect(area.getX() + w + 4, area.getBottom() - preRH, w, preRH);
    }
    void timerCallback() override {
        if (onRequestLevels) {
            float preL = 0, preR = 0;
            onRequestLevels(preL, preR);
            setLevels(preL, preR);
        }
    }
    // Callback: (preL, preR)
    std::function<void(float&, float&)> onRequestLevels;
private:
    float pre[2];
};

// ================= RackAccordionComponent =================
RackAccordionComponent::RackAccordionComponent(AudioPluginAudioProcessor* processorPtr)
    : processor(processorPtr)
{
    try {
        // Remove slider and label
        // addAndMakeVisible(numModulesLabel);
        // addAndMakeVisible(numModulesSlider);
        addAndMakeVisible(tabs);        // Add + and - buttons
    } catch (const std::exception& e) {
        juce::Logger::writeToLog("[RackAccordionComponent] Exception in constructor: " + juce::String(e.what()));
    } catch (...) {
        juce::Logger::writeToLog("[RackAccordionComponent] Unknown exception in constructor");
    }
}

void RackAccordionComponent::setNumModulesVT(int num) {
    if (suppressSetNumModulesVT) {
        juce::Logger::writeToLog("[RackAccordionComponent] setNumModulesVT suppressed (suppressSetNumModulesVT=true)");
        return;
    }
    juce::Logger::writeToLog("[RackAccordionComponent] setNumModulesVT called with num=" + juce::String(num));
    // Clamp num to [1, 16]
    if (num < 1) num = 1;
    if (num > 16) num = 16;
    if (getNumModulesVT() != num) {
        valueTree.setProperty("numModules", num, nullptr);
    }
    auto* controller = processor ? processor->getController() : nullptr;
    if (controller) {
        auto* perf = controller->getPerformance();
        if (perf)
        {
            // Log MIDI channels before change
            juce::String midiChannelsBefore;
            for (int i = 0; i < 16; ++i) midiChannelsBefore += juce::String(perf->parts[i].midiChannel) + " ";
            juce::Logger::writeToLog("[RackAccordionComponent] setNumModulesVT: MIDI channels before: " + midiChannelsBefore);

            FMRack::Performance newPerf = *perf; // Make a copy

            // Enable parts up to 'num', disable parts after 'num'
            for (int i = 0; i < 16; ++i)
            {
                if (i < num)
                {
                    // This part should be active. If it's currently off, turn it on.
                    if (newPerf.parts[i].midiChannel == 0)
                    {
                        newPerf.parts[i] = FMRack::Performance::PartConfig(); // Reset to default
                        newPerf.parts[i].midiChannel = i + 1;                   // Assign a unique MIDI channel
                    }
                }
                else
                {
                    // This part should be inactive.
                    newPerf.parts[i].midiChannel = 0;
                }
            }

            // Log MIDI channels after change
            juce::String midiChannelsAfter;
            for (int i = 0; i < 16; ++i) midiChannelsAfter += juce::String(newPerf.parts[i].midiChannel) + " ";
            juce::Logger::writeToLog("[RackAccordionComponent] setNumModulesVT: MIDI channels after: " + midiChannelsAfter);
            controller->setPerformance(newPerf);
        }
    }
}

int RackAccordionComponent::getNumModulesVT() const {
    return valueTree.getProperty("numModules", 1);
}

void RackAccordionComponent::forceSync() {
    juce::Logger::writeToLog("[RackAccordionComponent] forceSync() called - forcing immediate sync");
    updatePanels();
}

void RackAccordionComponent::valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& property)
{
    if (property == juce::Identifier("numModules") && tree == valueTree)
    {
        int newNumModules = tree.getProperty("numModules", 1);
        juce::Logger::writeToLog("[RackAccordionComponent] valueTreePropertyChanged: numModules changed to " + juce::String(newNumModules));

        // The ValueTree is the source of truth.
        // 1. Update the backend.
        setNumModulesVT(newNumModules);

        // 2. Update the UI panels.
        // This is now safe because the backend has been updated based on our ValueTree.
        updatePanels();
    }
}

void RackAccordionComponent::syncNumModulesSliderWithRack()
{
    suppressSetNumModulesVT = true;
    auto* controller = processor ? processor->getController() : nullptr;
    if (controller) {
        int rackNum = controller->getNumModules();
        // if ((int)numModulesSlider.getValue() != rackNum) {
        //     numModulesSlider.setValue(rackNum, juce::dontSendNotification);
        // }
        if (getNumModulesVT() != rackNum) {
            valueTree.setProperty("numModules", rackNum, nullptr);
        }
    }
    suppressSetNumModulesVT = false;
}

void RackAccordionComponent::updatePanels()
{
    // Prevent recursive calls
    if (updatingPanels.exchange(true)) {
        juce::Logger::writeToLog("[RackAccordionComponent] updatePanels() called recursively - skipping to prevent infinite loop");
        return;
    }
    
    try {
        juce::Logger::writeToLog("[RackAccordionComponent] updatePanels() called");
        
        auto* controller = processor ? processor->getController() : nullptr;
        if (!controller) {
            juce::Logger::writeToLog("[RackAccordionComponent] No controller available!");
            updatingPanels = false;
            return;
        }
        
        // Save the currently selected tab index
        int selectedTabIndex = tabs.getCurrentTabIndex();        if (tabs.getNumTabs() > 0) {
            // Check if any ModuleTabComponent has an open file dialog
            for (const auto& tab : moduleTabs) {
                if (tab && tab->isFileDialogOpen()) {
                    juce::Logger::writeToLog("[WARNING] updatePanels() called while a Load Voice file dialog is open! This can cause a crash. Skipping updatePanels.");
                    updatingPanels = false;
                    return; // Prevent crash by not updating panels while dialog is open
                }
            }
        }tabs.clearTabs();
        moduleTabs.clear();        // Check if we need to sync ValueTree with actual rack modules
        int rackModules = controller->getNumModules();
        int vtModules = getNumModulesVT();
        
        juce::Logger::writeToLog("[RackAccordionComponent] Rack has " + juce::String(rackModules) + " modules, ValueTree has " + juce::String(vtModules));
        
        // If the rack and VT disagree, the rack is the source of truth during initialization/sync.
        int numModules = rackModules;
        
        // Update ValueTree to match rack if they differ. This should only happen on initial load
        // or if an external change (like loading a preset) happens.
        if (rackModules != vtModules && rackModules >= 1 && rackModules <= 16) {
            juce::Logger::writeToLog("[RackAccordionComponent] SYNCING ValueTree from " + juce::String(vtModules) + " to " + juce::String(rackModules));
            // Don't trigger the property listener here, as we are already in an update cycle.
            // Just set the property directly.
            valueTree.setProperty("numModules", rackModules, nullptr);
            numModules = rackModules; // Make sure we use the synced value
        }
        
        // Clamp to safe range
        if (numModules < 1) numModules = 1;
        if (numModules > 16) numModules = 16;
        
        juce::Logger::writeToLog("[RackAccordionComponent] Creating " + juce::String(numModules) + " tabs");for (int i = 0; i < numModules; ++i)
        {
            juce::Logger::writeToLog("[RackAccordionComponent] Creating tab " + juce::String(i));
            
            try {
                auto tab = std::make_unique<ModuleTabComponent>(i, this);
                tabs.addTab(juce::String(i + 1), juce::Colours::darkgrey, tab.get(), false);
                moduleTabs.push_back(std::move(tab));
                
            } catch (const std::exception& e) {
                juce::Logger::writeToLog("[RackAccordionComponent] Exception creating tab " + juce::String(i) + ": " + juce::String(e.what()));
                break; // Stop creating tabs if one fails
            }
        }
        tabs.resized(); // Ensure child tabs get resized after adding
        resized();
        syncNumModulesSliderWithRack(); // Always sync slider at the end
        // After updating tabs, ensure slider matches the number of tabs
        // numModulesSlider.setValue(tabs.getNumTabs(), juce::dontSendNotification);
        // Restore the previously selected tab index if possible
        if (selectedTabIndex >= 0 && selectedTabIndex < tabs.getNumTabs()) {
            tabs.setCurrentTabIndex(selectedTabIndex);
        }        juce::Logger::writeToLog("[RackAccordionComponent] updatePanels() completed successfully with " + juce::String(tabs.getNumTabs()) + " tabs");
        
    } catch (const std::exception& e) {
        juce::Logger::writeToLog("[RackAccordionComponent] Exception in updatePanels: " + juce::String(e.what()));
    } catch (...) {
        juce::Logger::writeToLog("[RackAccordionComponent] Unknown exception in updatePanels");
    }
    
    // Always reset the flag
    updatingPanels = false;
}

void RackAccordionComponent::resized()
{
    tabs.setBounds(0, 0, getWidth(), getHeight());
}

void RackAccordionComponent::paint(juce::Graphics& g)
{
    // Use the same background color as VoiceEditorPanel
    g.fillAll(juce::Colour(0xff23272e)); // dark gray-blue, adjust as needed to match VoiceEditorPanel
}

// ================= ModuleTabComponent =================
ModuleTabComponent::ModuleTabComponent(int idx, RackAccordionComponent* parent)
    : moduleIndex(idx), parentAccordion(parent),
      voiceGroup("voiceGroup", "Voice"),
      unisonGroup("unisonGroup", "Unison"),
      midiPitchGroup("midiPitchGroup", "MIDI & Pitch"),
      noteRangeGroup("noteRangeGroup", "Note Range"),
      portaMonoGroup("portaMonoGroup", "Portamento & Mono"),
      filterGroup("filterGroup", "Filter"),
      mainPartGroup("mainPartGroup", "Main") // NEW
{
    // Add and make visible all group components
    addAndMakeVisible(voiceGroup);
    addAndMakeVisible(unisonGroup);
    addAndMakeVisible(midiPitchGroup);
    addAndMakeVisible(noteRangeGroup);
    addAndMakeVisible(portaMonoGroup);
    addAndMakeVisible(filterGroup);
    addAndMakeVisible(mainPartGroup); // NEW

    // Remove setSize() calls from all sliders, as their size is set in resized().
    unisonVoicesLabel.setText("Unison Voices", juce::dontSendNotification);
    addAndMakeVisible(unisonVoicesLabel);
    unisonVoicesSlider.setSliderStyle(juce::Slider::LinearVertical);
    unisonVoicesSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    unisonVoicesSlider.setRange(1, 4, 1);
    unisonVoicesSlider.setValue(1);
    addAndMakeVisible(unisonVoicesSlider);

    unisonDetuneLabel.setText("Detune", juce::dontSendNotification);
    addAndMakeVisible(unisonDetuneLabel);
    unisonDetuneSlider.setSliderStyle(juce::Slider::LinearVertical);
    unisonDetuneSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    unisonDetuneSlider.setRange(0.0, 50.0, 0.1);
    unisonDetuneSlider.setValue(7.0);
    addAndMakeVisible(unisonDetuneSlider);

    unisonPanLabel.setText("Unison Pan", juce::dontSendNotification);
    addAndMakeVisible(unisonPanLabel);
    unisonPanSlider.setSliderStyle(juce::Slider::LinearVertical);
    unisonPanSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    unisonPanSlider.setRange(0.0, 1.0, 0.01);
    unisonPanSlider.setValue(0.5);
    addAndMakeVisible(unisonPanSlider);

    midiChannelLabel.setText("Channel", juce::dontSendNotification);
    addAndMakeVisible(midiChannelLabel);
    midiChannelSlider.setSliderStyle(juce::Slider::LinearVertical);
    midiChannelSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    midiChannelSlider.setRange(1, 16, 1);
    midiChannelSlider.setValue(idx + 1); // Default to 1-based index
    addAndMakeVisible(midiChannelSlider);

    reverbSendLabel.setText("Reverb Send", juce::dontSendNotification);
    addAndMakeVisible(reverbSendLabel);
    reverbSendSlider.setSliderStyle(juce::Slider::LinearVertical);
    reverbSendSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    reverbSendSlider.setRange(0, 99, 1);
    addAndMakeVisible(reverbSendSlider);

    volumeLabel.setText("Volume", juce::dontSendNotification);
    addAndMakeVisible(volumeLabel);
    volumeSlider.setSliderStyle(juce::Slider::LinearVertical);
    volumeSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    volumeSlider.setRange(0, 127, 1);
    addAndMakeVisible(volumeSlider);

    panLabel.setText("Pan", juce::dontSendNotification);
    addAndMakeVisible(panLabel);
    panSlider.setSliderStyle(juce::Slider::LinearVertical);
    panSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    panSlider.setRange(0, 127, 1);
    addAndMakeVisible(panSlider);

    detuneLabel.setText("Detune", juce::dontSendNotification);
    addAndMakeVisible(detuneLabel);
    detuneSlider.setSliderStyle(juce::Slider::LinearVertical);
    detuneSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    detuneSlider.setRange(-99, 99, 1);
    addAndMakeVisible(detuneSlider);

    // Note Range controls
    noteLimitLowLabel.setText("Low", juce::dontSendNotification);
    addAndMakeVisible(noteLimitLowLabel);
    noteLimitLowSlider.setSliderStyle(juce::Slider::LinearVertical);
    noteLimitLowSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    noteLimitLowSlider.setRange(0, 127, 1);
    addAndMakeVisible(noteLimitLowSlider);

    noteLimitHighLabel.setText("High", juce::dontSendNotification);
    addAndMakeVisible(noteLimitHighLabel);
    noteLimitHighSlider.setSliderStyle(juce::Slider::LinearVertical);
    noteLimitHighSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    noteLimitHighSlider.setRange(0, 127, 1);
    addAndMakeVisible(noteLimitHighSlider);

    noteShiftLabel.setText("Shift", juce::dontSendNotification);
    addAndMakeVisible(noteShiftLabel);
    noteShiftSlider.setSliderStyle(juce::Slider::LinearVertical);
    noteShiftSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    noteShiftSlider.setRange(-24, 24, 1);
    addAndMakeVisible(noteShiftSlider);

    // Pitch Bend
    pitchBendRangeLabel.setText("PB Range", juce::dontSendNotification);
    addAndMakeVisible(pitchBendRangeLabel);
    pitchBendRangeSlider.setSliderStyle(juce::Slider::LinearVertical);
    pitchBendRangeSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    pitchBendRangeSlider.setRange(0, 12, 1);
    addAndMakeVisible(pitchBendRangeSlider);

    // Portamento
    portamentoModeLabel.setText("Portamento", juce::dontSendNotification);
    addAndMakeVisible(portamentoModeLabel);
    portamentoModeButton.setButtonText("On");
    addAndMakeVisible(portamentoModeButton);

    portamentoTimeLabel.setText("Time", juce::dontSendNotification);
    addAndMakeVisible(portamentoTimeLabel);
    portamentoTimeSlider.setSliderStyle(juce::Slider::LinearVertical);
    portamentoTimeSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    portamentoTimeSlider.setRange(0, 99, 1);
    addAndMakeVisible(portamentoTimeSlider);

    // Mono Mode
    monoModeLabel.setText("Mono Mode", juce::dontSendNotification);
    addAndMakeVisible(monoModeLabel);
    monoModeButton.setButtonText("On");
    addAndMakeVisible(monoModeButton);

    // Misc
    velocityScaleLabel.setText("Velocity", juce::dontSendNotification);
    addAndMakeVisible(velocityScaleLabel);
    velocityScaleSlider.setSliderStyle(juce::Slider::LinearVertical);
    velocityScaleSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    velocityScaleSlider.setRange(0, 127, 1);
    addAndMakeVisible(velocityScaleSlider);

    masterTuneLabel.setText("Tune", juce::dontSendNotification);
    addAndMakeVisible(masterTuneLabel);
    masterTuneSlider.setSliderStyle(juce::Slider::LinearVertical);
    masterTuneSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    masterTuneSlider.setRange(-100, 100, 1);
    addAndMakeVisible(masterTuneSlider);

    // Filter
    filterEnabledLabel.setText("Filter", juce::dontSendNotification);
    addAndMakeVisible(filterEnabledLabel);
    filterEnabledButton.setButtonText("On");
    addAndMakeVisible(filterEnabledButton);

    filterCutoffLabel.setText("Cutoff", juce::dontSendNotification);
    addAndMakeVisible(filterCutoffLabel);
    filterCutoffSlider.setSliderStyle(juce::Slider::LinearVertical);
    filterCutoffSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    filterCutoffSlider.setRange(0, 127, 1);
    addAndMakeVisible(filterCutoffSlider);

    filterResonanceLabel.setText("Resonance", juce::dontSendNotification);
    addAndMakeVisible(filterResonanceLabel);
    filterResonanceSlider.setSliderStyle(juce::Slider::LinearVertical);
    filterResonanceSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    filterResonanceSlider.setRange(0, 127, 1);
    addAndMakeVisible(filterResonanceSlider);

    // Wire up controls to update Performance, not module directly
    unisonVoicesSlider.onValueChange = [this] {
        auto* editor = parentAccordion ? parentAccordion->getEditor() : nullptr;
        auto* processor = editor ? editor->getProcessor() : nullptr;
        auto* controller = processor ? processor->getController() : nullptr;
        auto* perf = controller ? controller->getPerformance() : nullptr;
        if (perf) {
            auto& part = const_cast<FMRack::Performance::PartConfig&>(perf->getPartConfig(moduleIndex));
            uint8_t newValue = (uint8_t)unisonVoicesSlider.getValue();
            if (part.unisonVoices != newValue) {
                part.unisonVoices = newValue;
                controller->setPerformance(*perf);
            }
        }
    };
    unisonDetuneSlider.onValueChange = [this] {
        auto* editor = parentAccordion ? parentAccordion->getEditor() : nullptr;
        auto* processor = editor ? editor->getProcessor() : nullptr;
        auto* controller = processor ? processor->getController() : nullptr;
        auto* perf = controller ? controller->getPerformance() : nullptr;
        if (perf) {
            auto& part = const_cast<FMRack::Performance::PartConfig&>(perf->getPartConfig(moduleIndex));
            float newValue = (float)unisonDetuneSlider.getValue();
            if (part.unisonDetune != newValue) {
                part.unisonDetune = newValue;
                controller->setPerformance(*perf);
            }
        }
    };
    unisonPanSlider.onValueChange = [this] {
        auto* editor = parentAccordion ? parentAccordion->getEditor() : nullptr;
        auto* processor = editor ? editor->getProcessor() : nullptr;
        auto* controller = processor ? processor->getController() : nullptr;
        auto* perf = controller ? controller->getPerformance() : nullptr;
        if (perf) {
            auto& part = const_cast<FMRack::Performance::PartConfig&>(perf->getPartConfig(moduleIndex));
            float newValue = (float)unisonPanSlider.getValue();
            if (part.unisonSpread != newValue) {
                part.unisonSpread = newValue;
                controller->setPerformance(*perf);
            }
        }
    };
    midiChannelSlider.onValueChange = [this] {
        auto* editor = parentAccordion ? parentAccordion->getEditor() : nullptr;
        auto* processor = editor ? editor->getProcessor() : nullptr;
        auto* controller = processor ? processor->getController() : nullptr;
        auto* perf = controller ? controller->getPerformance() : nullptr;
        if (perf) {
            auto& part = const_cast<FMRack::Performance::PartConfig&>(perf->getPartConfig(moduleIndex));
            uint8_t newValue = (uint8_t)midiChannelSlider.getValue();
            if (part.midiChannel != newValue) {
                part.midiChannel = newValue;
                controller->setPerformance(*perf);
            }
        }
    };

    reverbSendSlider.onValueChange = [this] {
        auto* editor = parentAccordion ? parentAccordion->getEditor() : nullptr;
        auto* processor = editor ? editor->getProcessor() : nullptr;
        auto* controller = processor ? processor->getController() : nullptr;
        auto* perf = controller ? controller->getPerformance() : nullptr;
        if (perf) {
            auto& part = const_cast<FMRack::Performance::PartConfig&>(perf->getPartConfig(moduleIndex));
            uint8_t newValue = (uint8_t)reverbSendSlider.getValue();
            if (part.reverbSend != newValue) {
                part.reverbSend = newValue;
                controller->setPerformance(*perf);
            }
        }
    };

    volumeSlider.onValueChange = [this] {
        auto* editor = parentAccordion ? parentAccordion->getEditor() : nullptr;
        auto* processor = editor ? editor->getProcessor() : nullptr;
        auto* controller = processor ? processor->getController() : nullptr;
        auto* perf = controller ? controller->getPerformance() : nullptr;
        if (perf) {
            auto& part = const_cast<FMRack::Performance::PartConfig&>(perf->getPartConfig(moduleIndex));
            uint8_t newValue = (uint8_t)volumeSlider.getValue();
            if (part.volume != newValue) {
                part.volume = newValue;
                controller->setPerformance(*perf);
            }
        }
    };

    panSlider.onValueChange = [this] {
        auto* editor = parentAccordion ? parentAccordion->getEditor() : nullptr;
        auto* processor = editor ? editor->getProcessor() : nullptr;
        auto* controller = processor ? processor->getController() : nullptr;
        auto* perf = controller ? controller->getPerformance() : nullptr;
        if (perf) {
            auto& part = const_cast<FMRack::Performance::PartConfig&>(perf->getPartConfig(moduleIndex));
            uint8_t newValue = (uint8_t)panSlider.getValue();
            if (part.pan != newValue) {
                part.pan = newValue;
                controller->setPerformance(*perf);
            }
        }
    };

    detuneSlider.onValueChange = [this] {
        auto* editor = parentAccordion ? parentAccordion->getEditor() : nullptr;
        auto* processor = editor ? editor->getProcessor() : nullptr;
        auto* controller = processor ? processor->getController() : nullptr;
        auto* perf = controller ? controller->getPerformance() : nullptr;
        if (perf) {
            auto& part = const_cast<FMRack::Performance::PartConfig&>(perf->getPartConfig(moduleIndex));
            int8_t newValue = (int8_t)detuneSlider.getValue();
            if (part.detune != newValue) {
                part.detune = newValue;
                controller->setPerformance(*perf);
            }
        }
    };

    // Note Range handlers
    noteLimitLowSlider.onValueChange = [this] {
        auto* perf = getPerformance(); if (!perf) return;
        auto& part = const_cast<FMRack::Performance::PartConfig&>(perf->getPartConfig(moduleIndex));
        uint8_t newValue = (uint8_t)noteLimitLowSlider.getValue();
        if (part.noteLimitLow != newValue) {
            part.noteLimitLow = newValue;
            getController()->setPerformance(*perf);
        }
    };
    noteLimitHighSlider.onValueChange = [this] {
        auto* perf = getPerformance(); if (!perf) return;
        auto& part = const_cast<FMRack::Performance::PartConfig&>(perf->getPartConfig(moduleIndex));
        uint8_t newValue = (uint8_t)noteLimitHighSlider.getValue();
        if (part.noteLimitHigh != newValue) {
            part.noteLimitHigh = newValue;
            getController()->setPerformance(*perf);
        }
    };
    noteShiftSlider.onValueChange = [this] {
        auto* perf = getPerformance(); if (!perf) return;
        auto& part = const_cast<FMRack::Performance::PartConfig&>(perf->getPartConfig(moduleIndex));
        int8_t newValue = (int8_t)noteShiftSlider.getValue();
        if (part.noteShift != newValue) {
            part.noteShift = newValue;
            getController()->setPerformance(*perf);
        }
    };

    // Pitch Bend handler
    pitchBendRangeSlider.onValueChange = [this] {
        auto* perf = getPerformance(); if (!perf) return;
        auto& part = const_cast<FMRack::Performance::PartConfig&>(perf->getPartConfig(moduleIndex));
        uint8_t newValue = (uint8_t)pitchBendRangeSlider.getValue();
        if (part.pitchBendRange != newValue) {
            part.pitchBendRange = newValue;
            getController()->setPerformance(*perf);
        }
    };

    // Portamento handlers
    portamentoModeButton.onClick = [this] {
        auto* perf = getPerformance(); if (!perf) return;
        auto& part = const_cast<FMRack::Performance::PartConfig&>(perf->getPartConfig(moduleIndex));
        bool newValue = portamentoModeButton.getToggleState();
        if (part.portamentoMode != newValue) {
            part.portamentoMode = newValue;
            getController()->setPerformance(*perf);
        }
    };
    portamentoTimeSlider.onValueChange = [this] {
        auto* perf = getPerformance(); if (!perf) return;
        auto& part = const_cast<FMRack::Performance::PartConfig&>(perf->getPartConfig(moduleIndex));
        uint8_t newValue = (uint8_t)portamentoTimeSlider.getValue();
        if (part.portamentoTime != newValue) {
            part.portamentoTime = newValue;
            getController()->setPerformance(*perf);
        }
    };

    // Mono Mode handler
    monoModeButton.onClick = [this] {
        auto* perf = getPerformance(); if (!perf) return;
        auto& part = const_cast<FMRack::Performance::PartConfig&>(perf->getPartConfig(moduleIndex));
        bool newValue = monoModeButton.getToggleState();
        if (part.monoMode != newValue) {
            part.monoMode = newValue;
            getController()->setPerformance(*perf);
        }
    };

    // Misc handlers
    velocityScaleSlider.onValueChange = [this] {
        auto* perf = getPerformance(); if (!perf) return;
        auto& part = const_cast<FMRack::Performance::PartConfig&>(perf->getPartConfig(moduleIndex));
        uint8_t newValue = (uint8_t)velocityScaleSlider.getValue();
        if (part.velocityScale != newValue) {
            part.velocityScale = newValue;
            getController()->setPerformance(*perf);
        }
    };
    masterTuneSlider.onValueChange = [this] {
        auto* perf = getPerformance(); if (!perf) return;
        auto& part = const_cast<FMRack::Performance::PartConfig&>(perf->getPartConfig(moduleIndex));
        int16_t newValue = (int16_t)masterTuneSlider.getValue();
        if (part.masterTune != newValue) {
            part.masterTune = newValue;
            getController()->setPerformance(*perf);
        }
    };

    // Filter handlers
    filterEnabledButton.onClick = [this] {
        auto* perf = getPerformance(); if (!perf) return;
        auto& part = const_cast<FMRack::Performance::PartConfig&>(perf->getPartConfig(moduleIndex));
        bool newValue = filterEnabledButton.getToggleState();
        if (part.filterEnabled != newValue) {
            part.filterEnabled = newValue;
            getController()->setPerformance(*perf);
        }
    };
    filterCutoffSlider.onValueChange = [this] {
        auto* perf = getPerformance(); if (!perf) return;
        auto& part = const_cast<FMRack::Performance::PartConfig&>(perf->getPartConfig(moduleIndex));
        uint8_t newValue = (uint8_t)filterCutoffSlider.getValue();
        if (part.filterCutoff != newValue) {
            part.filterCutoff = newValue;
            getController()->setPerformance(*perf);
        }
    };
    filterResonanceSlider.onValueChange = [this] {
        auto* perf = getPerformance(); if (!perf) return;
        auto& part = const_cast<FMRack::Performance::PartConfig&>(perf->getPartConfig(moduleIndex));
        uint8_t newValue = (uint8_t)filterResonanceSlider.getValue();
        if (part.filterResonance != newValue) {
            part.filterResonance = newValue;
            getController()->setPerformance(*perf);
        }
    };

    // File dialog state flag
    fileDialogOpen = false;
    // Load Voice button
    loadVoiceButton.setButtonText("Load");
    loadVoiceButton.onClick = [this] {
        if (fileDialogOpen) {
            juce::Logger::writeToLog("[ModuleTabComponent] File dialog already open, ignoring.");
            return;
        }
        fileDialogOpen = true;
        auto dialog = std::make_unique<FileBrowserDialog>(
            "Select Voice File",
            "*.syx;*.bin;*.dx7;*.dat;*.voice;*.vce;*.opm;*.ini;*",
            juce::File(),
            FileBrowserDialog::DialogType::Voice);
        auto* dialogPtr = dialog.get();
        dialogPtr->showDialog(this,
            [this](const juce::File& file) {
                fileDialogOpen = false;
                loadVoiceFile(file);
            },
            [this]() {
                fileDialogOpen = false;
            });
        dialog.release();
    };
    loadVoiceButton.setButtonText("Open");
    addAndMakeVisible(loadVoiceButton);

    // Use the member variable, don't redeclare it
    openVoiceEditorButton.setButtonText("Edit");
    addAndMakeVisible(openVoiceEditorButton);
    openVoiceEditorButton.onClick = [this] {
        auto* editor = parentAccordion ? parentAccordion->getEditor() : nullptr;
        if (editor) {
            editor->showVoiceEditorPanel(moduleIndex);
        }
    };

    // Apply custom look and feel to sliders
    unisonVoicesSlider.setLookAndFeel(&operatorSliderLookAndFeel);
    unisonDetuneSlider.setLookAndFeel(&operatorSliderLookAndFeel);
    unisonPanSlider.setLookAndFeel(&operatorSliderLookAndFeel);
    midiChannelSlider.setLookAndFeel(&operatorSliderLookAndFeel);
    reverbSendSlider.setLookAndFeel(&operatorSliderLookAndFeel);
    volumeSlider.setLookAndFeel(&operatorSliderLookAndFeel);
    panSlider.setLookAndFeel(&operatorSliderLookAndFeel);
    detuneSlider.setLookAndFeel(&operatorSliderLookAndFeel);

    // Apply custom look and feel to new sliders
    noteLimitLowSlider.setLookAndFeel(&operatorSliderLookAndFeel);
    noteLimitHighSlider.setLookAndFeel(&operatorSliderLookAndFeel);
    noteShiftSlider.setLookAndFeel(&operatorSliderLookAndFeel);
    pitchBendRangeSlider.setLookAndFeel(&operatorSliderLookAndFeel);
    portamentoTimeSlider.setLookAndFeel(&operatorSliderLookAndFeel);
    velocityScaleSlider.setLookAndFeel(&operatorSliderLookAndFeel);
    masterTuneSlider.setLookAndFeel(&operatorSliderLookAndFeel);
    filterCutoffSlider.setLookAndFeel(&operatorSliderLookAndFeel);
    filterResonanceSlider.setLookAndFeel(&operatorSliderLookAndFeel);

    volumeMeter = std::make_unique<StereoVolumeMeter>();
    addAndMakeVisible(*volumeMeter);
    volumeMeter->onRequestLevels = [this](float& preL, float& preR) {
        auto* editor = parentAccordion ? parentAccordion->getEditor() : nullptr;
        auto* processor = editor ? editor->getProcessor() : nullptr;
        if (processor) {
            float dummyL, dummyR;
            processor->getModuleOutputLevels(moduleIndex, dummyL, dummyR, preL, preR);
        } else {
            preL = preR = 0.0f;
        }
    };

    updateFromModule(); // Initial sync
}

void ModuleTabComponent::updateFromModule()
{
    auto* editor = parentAccordion ? parentAccordion->getEditor() : nullptr;
    auto* processor = editor ? editor->getProcessor() : nullptr;
    auto* controller = processor ? processor->getController() : nullptr;
    auto* perf = controller ? controller->getPerformance() : nullptr;
    if (perf) {
        const auto& part = perf->getPartConfig(moduleIndex);
        unisonVoicesSlider.setValue(part.unisonVoices, juce::dontSendNotification);
        unisonDetuneSlider.setValue(part.unisonDetune, juce::dontSendNotification);
        unisonPanSlider.setValue(part.unisonSpread, juce::dontSendNotification);
        midiChannelSlider.setValue(part.midiChannel, juce::dontSendNotification); // Sync MIDI channel
        reverbSendSlider.setValue(part.reverbSend, juce::dontSendNotification);
        volumeSlider.setValue(part.volume, juce::dontSendNotification);
        panSlider.setValue(part.pan, juce::dontSendNotification);
        detuneSlider.setValue(part.detune, juce::dontSendNotification);

        // Update new controls
        noteLimitLowSlider.setValue(part.noteLimitLow, juce::dontSendNotification);
        noteLimitHighSlider.setValue(part.noteLimitHigh, juce::dontSendNotification);
        noteShiftSlider.setValue(part.noteShift, juce::dontSendNotification);
        pitchBendRangeSlider.setValue(part.pitchBendRange, juce::dontSendNotification);
        portamentoModeButton.setToggleState(part.portamentoMode, juce::dontSendNotification);
        portamentoTimeSlider.setValue(part.portamentoTime, juce::dontSendNotification);
        monoModeButton.setToggleState(part.monoMode, juce::dontSendNotification);
        velocityScaleSlider.setValue(part.velocityScale, juce::dontSendNotification);
        masterTuneSlider.setValue(part.masterTune, juce::dontSendNotification);
        filterEnabledButton.setToggleState(part.filterEnabled, juce::dontSendNotification);
        filterCutoffSlider.setValue(part.filterCutoff, juce::dontSendNotification);
        filterResonanceSlider.setValue(part.filterResonance, juce::dontSendNotification);
    }
}

void ModuleTabComponent::resized()
{
    // Define layout constants
    const int margin = 10;
    const int groupGap = 8;
    const int controlGap = 10;
    const int sliderW = 55;
    const int sliderH = 100;
    const int labelH = 20;
    const int labelGap = 4;
    const int buttonH = 24;

    juce::Rectangle<int> area = getLocalBounds().reduced(margin);

    // Volume Meter (far right)
    int meterW = 32;
    int meterX = area.getRight() - meterW;
    int meterY = area.getY();
    int meterH = area.getHeight();
    if (volumeMeter)
        volumeMeter->setBounds(meterX, meterY, meterW, meterH);

    // Main content area (excluding meter)
    juce::Rectangle<int> mainArea = area.withRight(meterX - groupGap);

    // Column 1
    int col1X = mainArea.getX();
    int col1W = 200;

    // Part Group
    voiceGroup.setBounds(col1X, mainArea.getY(), col1W, 80);
    auto partArea = voiceGroup.getBounds().reduced(margin, 20);
    loadVoiceButton.setBounds(partArea.getX(), partArea.getCentreY() - buttonH / 2, 80, buttonH);
    openVoiceEditorButton.setBounds(partArea.getRight() - 90, partArea.getCentreY() - buttonH / 2, 90, buttonH);

    // Unison Group
    unisonGroup.setBounds(col1X, voiceGroup.getBottom() + groupGap, col1W, 160);
    auto unisonArea = unisonGroup.getBounds().reduced(margin, 20);
    int sliderX = unisonArea.getX();
    unisonVoicesSlider.setBounds(sliderX, unisonArea.getY(), sliderW, sliderH);
    unisonVoicesLabel.setBounds(sliderX, unisonArea.getY() + sliderH + labelGap, sliderW, labelH);
    sliderX += sliderW + controlGap;
    unisonDetuneSlider.setBounds(sliderX, unisonArea.getY(), sliderW, sliderH);
    unisonDetuneLabel.setBounds(sliderX, unisonArea.getY() + sliderH + labelGap, sliderW, labelH);
    sliderX += sliderW + controlGap;
    unisonPanSlider.setBounds(sliderX, unisonArea.getY(), sliderW, sliderH);
    unisonPanLabel.setBounds(sliderX, unisonArea.getY() + sliderH + labelGap, sliderW, labelH);

    // MIDI & Pitch Group
    midiPitchGroup.setBounds(col1X, unisonGroup.getBottom() + groupGap, col1W, 160);
    auto midiPitchArea = midiPitchGroup.getBounds().reduced(margin, 20);
    sliderX = midiPitchArea.getX();
    midiChannelSlider.setBounds(sliderX, midiPitchArea.getY(), sliderW, sliderH);
    midiChannelLabel.setBounds(sliderX, midiPitchArea.getY() + sliderH + labelGap, sliderW, labelH);
    sliderX += sliderW + controlGap;
    pitchBendRangeSlider.setBounds(sliderX, midiPitchArea.getY(), sliderW, sliderH);
    pitchBendRangeLabel.setBounds(sliderX, midiPitchArea.getY() + sliderH + labelGap, sliderW, labelH);
    sliderX += sliderW + controlGap;
    masterTuneSlider.setBounds(sliderX, midiPitchArea.getY(), sliderW, sliderH);
    masterTuneLabel.setBounds(sliderX, midiPitchArea.getY() + sliderH + labelGap, sliderW, labelH);

    // Column 2
    int col2X = col1X + col1W + groupGap;
    int col2W = 340;

    // Main Part Group
    mainPartGroup.setBounds(col2X, mainArea.getY(), col2W, 160);
    auto mainPartArea = mainPartGroup.getBounds().reduced(margin, 20);
    sliderX = mainPartArea.getX();
    volumeSlider.setBounds(sliderX, mainPartArea.getY(), sliderW, sliderH);
    volumeLabel.setBounds(sliderX, mainPartArea.getY() + sliderH + labelGap, sliderW, labelH);
    sliderX += sliderW + controlGap;
    panSlider.setBounds(sliderX, mainPartArea.getY(), sliderW, sliderH);
    panLabel.setBounds(sliderX, mainPartArea.getY() + sliderH + labelGap, sliderW, labelH);
    sliderX += sliderW + controlGap;
    detuneSlider.setBounds(sliderX, mainPartArea.getY(), sliderW, sliderH);
    detuneLabel.setBounds(sliderX, mainPartArea.getY() + sliderH + labelGap, sliderW, labelH);
    sliderX += sliderW + controlGap;
    reverbSendSlider.setBounds(sliderX, mainPartArea.getY(), sliderW, sliderH);
    reverbSendLabel.setBounds(sliderX, mainPartArea.getY() + sliderH + labelGap, sliderW, labelH);
    sliderX += sliderW + controlGap;
    velocityScaleSlider.setBounds(sliderX, mainPartArea.getY(), sliderW, sliderH);
    velocityScaleLabel.setBounds(sliderX, mainPartArea.getY() + sliderH + labelGap, sliderW, labelH);

    // Note Range Group
    noteRangeGroup.setBounds(col2X, mainPartGroup.getBottom() + groupGap, col2W, 160);
    auto noteRangeArea = noteRangeGroup.getBounds().reduced(margin, 20);
    sliderX = noteRangeArea.getX();
    noteLimitLowSlider.setBounds(sliderX, noteRangeArea.getY(), sliderW, sliderH);
    noteLimitLowLabel.setBounds(sliderX, noteRangeArea.getY() + sliderH + labelGap, sliderW, labelH);
    sliderX += sliderW + controlGap;
    noteLimitHighSlider.setBounds(sliderX, noteRangeArea.getY(), sliderW, sliderH);
    noteLimitHighLabel.setBounds(sliderX, noteRangeArea.getY() + sliderH + labelGap, sliderW, labelH);
    sliderX += sliderW + controlGap;
    noteShiftSlider.setBounds(sliderX, noteRangeArea.getY(), sliderW, sliderH);
    noteShiftLabel.setBounds(sliderX, noteRangeArea.getY() + sliderH + labelGap, sliderW, labelH);

    // Column 3
    int col3X = col2X + col2W + groupGap;
    int col3W = 220;

    // Filter Group
    filterGroup.setBounds(col3X, mainArea.getY(), col3W, 180);
    auto filterArea = filterGroup.getBounds().reduced(margin, 20);
    filterEnabledButton.setBounds(filterArea.getX(), filterArea.getY(), 60, buttonH);
    int sliderY = filterArea.getY() + buttonH + controlGap;
    sliderX = filterArea.getX();
    filterCutoffSlider.setBounds(sliderX, sliderY, sliderW, sliderH);
    filterCutoffLabel.setBounds(sliderX, sliderY + sliderH + labelGap, sliderW, labelH);
    sliderX += sliderW + controlGap;
    filterResonanceSlider.setBounds(sliderX, sliderY, sliderW, sliderH);
    filterResonanceLabel.setBounds(sliderX, sliderY + sliderH + labelGap, sliderW, labelH);

    // Porta & Mono Group
    portaMonoGroup.setBounds(col3X, filterGroup.getBottom() + groupGap, col3W, 200);
    auto portaMonoArea = portaMonoGroup.getBounds().reduced(margin, 20);
    int controlX = portaMonoArea.getX();
    int controlY = portaMonoArea.getY();
    portamentoModeLabel.setBounds(controlX, controlY, 80, buttonH);
    portamentoModeButton.setBounds(controlX + 80 + controlGap, controlY, 60, buttonH);
    controlY += buttonH + labelGap;
    portamentoTimeSlider.setBounds(controlX, controlY, sliderW, sliderH);
    portamentoTimeLabel.setBounds(controlX, controlY + sliderH + labelGap, sliderW, labelH);

    controlY += sliderH + labelGap + labelH + groupGap; // Add space between porta and mono

    monoModeLabel.setBounds(controlX, controlY, 80, buttonH);
    monoModeButton.setBounds(controlX + 80 + controlGap, controlY, 60, buttonH);

    updateFromModule(); // Initial sync
}

void ModuleTabComponent::loadVoiceFile(const juce::File& file)
{
    auto* editor = parentAccordion ? parentAccordion->getEditor() : nullptr;
    auto* processor = editor ? editor->getProcessor() : nullptr;
    auto* controller = processor ? processor->getController() : nullptr;
    if (controller) {
        // Use VoiceData to load the voice file
        VoiceData voiceData;
        bool loaded = voiceData.loadFromFile(file.getFullPathName().toStdString());
        
        if (loaded && !voiceData.voices.empty()) {
            // Get the first voice from the file
            const auto& voiceBytes = voiceData.voices[0];
            
            // Convert to Dexed format if needed
            std::vector<uint8_t> dexedVoice;
            if (voiceBytes.size() == 128) {
                // Convert DX7 voice to Dexed format
                dexedVoice = VoiceData::convertDX7ToDexed(voiceBytes);
            } else if (voiceBytes.size() == 156) {
                // Already in Dexed format
                dexedVoice = voiceBytes;
            } else {
                juce::Logger::writeToLog("[ModuleTabComponent] Unsupported voice data size: " + juce::String((int)voiceBytes.size()));
                if (editor) {
                    editor->appendLogMessage("Unsupported voice data format");
                }
                return;
            }
            
            // Set the voice data in the performance
            auto* perf = controller->getPerformance();
            if (perf && moduleIndex < 16) {
                controller->setPartVoiceData(moduleIndex, dexedVoice);
                juce::String voiceName = VoiceData::extractDX7VoiceName(dexedVoice);
                juce::Logger::writeToLog("[ModuleTabComponent] Loaded voice: " + voiceName);
                if (editor) {
                    editor->appendLogMessage("Voice loaded: " + voiceName);
                    // --- Force update of voice editor panel if open ---
                    editor->showVoiceEditorPanel(moduleIndex);
                }
            }
        } else {
            juce::Logger::writeToLog("[ModuleTabComponent] Failed to load voice file: " + file.getFullPathName());
            if (editor) {
                editor->appendLogMessage("Failed to load voice file");
            }
        }
    }
}

ModuleTabComponent::~ModuleTabComponent() {
    fileDialogOpen = false;
}

bool ModuleTabComponent::isFileDialogOpen() const {
    return fileDialogOpen;
}

void ModuleTabComponent::closeFileDialog() {
    // Defensive: try to find any FileBrowserDialog child and close it
    for (int i = 0; i < getNumChildComponents(); ++i) {
        if (auto* dialog = dynamic_cast<FileBrowserDialog*>(getChildComponent(i))) {
            dialog->closeDialog();
        }
    }
    fileDialogOpen = false;
}

FMRackController* ModuleTabComponent::getController() {
    auto* editor = parentAccordion ? parentAccordion->getEditor() : nullptr;
    auto* processor = editor ? editor->getProcessor() : nullptr;
    return processor ? processor->getController() : nullptr;
}

FMRack::Performance* ModuleTabComponent::getPerformance() {
    auto* controller = getController();
    return controller ? controller->getPerformance() : nullptr;
}
