
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "RackAccordionComponent.h"
#include "DX7LookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <fstream>
#include <initializer_list>
#include "../../src/FMRack/VoiceData.h" // NEW: Include VoiceData for conversion function
#include <juce_gui_extra/juce_gui_extra.h>
#include "FMRackVerticalSlider.h"
#include "FMRackSliderConstants.h"



namespace
{
    constexpr int kContentPadding = 12;
    constexpr int kGroupLabelOffset = 24;
    constexpr int kSliderLabelHeight = 18;
    constexpr int kSliderLabelGap = 4;
    constexpr int kMinSliderWidth = FMRackSliderConstants::kSliderWidth;  // Use global constant

    struct SliderLabelPair
    {
        FMRackVerticalSlider* slider = nullptr;
        juce::Label* label = nullptr;
    };

    juce::Rectangle<int> makeGroupContentBounds(const juce::Rectangle<int>& groupBounds, int contentHeight)
    {
        return juce::Rectangle<int>(
            groupBounds.getX() + kContentPadding,
            groupBounds.getY() + kContentPadding + kGroupLabelOffset,
            groupBounds.getWidth() - 2 * kContentPadding,
            contentHeight);
    }

    void layoutLabeledSliderRow(const juce::Rectangle<int>& area,
                                const std::initializer_list<FMRackLabeledVerticalSlider*>& controls,
                                int sliderHeight,
                                int controlGap)
    {
        if (controls.size() == 0)
            return;

        const int sliderCount = static_cast<int>(controls.size());
        const int sliderWidth = kMinSliderWidth;
        const int gapTotal = controlGap * (sliderCount - 1);
        const int totalWidth = sliderWidth * sliderCount + gapTotal;

        int startX = area.getX() + juce::jmax(0, (area.getWidth() - totalWidth) / 2);

        for (auto control : controls)
        {
            if (control != nullptr)
            {
                juce::Rectangle<int> sliderBounds(startX, area.getY(), sliderWidth, sliderHeight);
                control->setBounds(sliderBounds);
            }

            startX += sliderWidth + controlGap;
        }
    }

    void layoutToggleRow(juce::Label& label,
                         juce::ToggleButton& button,
                         const juce::Rectangle<int>& rowBounds,
                         int buttonWidth)
    {
        const int gap = 8;
        juce::Rectangle<int> buttonArea(rowBounds.getRight() - buttonWidth,
                                        rowBounds.getY(),
                                        buttonWidth,
                                        rowBounds.getHeight());
        button.setBounds(buttonArea);

        juce::Rectangle<int> labelArea(rowBounds.getX(),
                                       rowBounds.getY(),
                                       juce::jmax(0, rowBounds.getWidth() - buttonWidth - gap),
                                       rowBounds.getHeight());
        label.setJustificationType(juce::Justification::centredLeft);
        label.setBounds(labelArea);
    }

    // Concept:
    // The engine's Module::setupUnison() currently interprets its 'detune' argument as
    // "cents per step" and then multiplies it by (i - (voices-1)/2). This means:
    //   voices=2 => outer voices at +/- detune*0.5
    //   voices=3 => outer voices at +/- detune*1.0
    //   voices=4 => outer voices at +/- detune*1.5
    // So if we store 'detune' directly, the perceived spread changes with voice count.
    // For a TX816-style control, it's more intuitive if the parameter means:
    //   "maximum cents offset of the outermost voice" (i.e., widest voice is +/-X cents)
    // and that remains consistent regardless of 2/3/4 voices.
    //
    // We implement that by mapping the UI to a target max-outer-cents value, then converting
    // it into the engine's expected per-step detune before writing to Performance.

    constexpr float kUnisonDetuneMaxOuterCents = 7.0f;   // practical maximum outer detune
    constexpr float kUnisonDetuneCurve = 2.2f;      // >1 gives more resolution near 0

    static float perStepDetuneFromMaxOuter(float maxOuterCents, int voices)
    {
        voices = juce::jlimit(1, 4, voices);
        if (voices <= 1) return 0.0f;
        const float maxFactor = (voices - 1) * 0.5f;  // 0.5, 1.0, 1.5 for 2/3/4
        return (maxFactor > 0.0f) ? (maxOuterCents / maxFactor) : 0.0f;
    }

    static float maxOuterFromPerStepDetune(float perStepCents, int voices)
    {
        voices = juce::jlimit(1, 4, voices);
        if (voices <= 1) return 0.0f;
        const float maxFactor = (voices - 1) * 0.5f;
        return perStepCents * maxFactor;
    }

    static float uiNormToUnisonCents(float norm)
    {
        norm = juce::jlimit(0.0f, 1.0f, norm);
        return kUnisonDetuneMaxOuterCents * std::pow(norm, kUnisonDetuneCurve);
    }

    static float unisonCentsToUiNorm(float cents)
    {
        const float norm = juce::jlimit(0.0f, 1.0f, cents / kUnisonDetuneMaxOuterCents);
        return std::pow(norm, 1.0f / kUnisonDetuneCurve);
    }
}

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
            // Find the MIDI channel of the last active part to use for new parts
            uint8_t lastActiveChannel = 1; // Default to 1
            for (int j = 0; j < 16; ++j)
            {
                if (newPerf.parts[j].midiChannel != 0)
                {
                    lastActiveChannel = newPerf.parts[j].midiChannel;
                }
            }
            
            for (int i = 0; i < 16; ++i)
            {
                if (i < num)
                {
                    // This part should be active. If it's currently off, turn it on.
                    if (newPerf.parts[i].midiChannel == 0)
                    {
                        newPerf.parts[i] = FMRack::Performance::PartConfig(); // Reset to default
                        newPerf.parts[i].midiChannel = lastActiveChannel; // Use the same channel as the last active module
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
    // Use dark charcoal background matching the main panel
    g.fillAll(DX7LookAndFeel::getPanelDarkColour());
}

// ================= ModuleTabComponent =================
ModuleTabComponent::ModuleTabComponent(int idx, RackAccordionComponent* parent)
    : moduleIndex(idx), parentAccordion(parent),
      voiceGroup("voiceGroup", "Voice"),
      unisonGroup("unisonGroup", "Unison"),
      midiPitchGroup("midiPitchGroup", "Controllers"),
      noteRangeGroup("noteRangeGroup", "Note Range"),
      portaMonoGroup("portaMonoGroup", "Portamento & Mono"),
      filterGroup("filterGroup", "Filter"),
      mainPartGroup("mainPartGroup", "Main") // NEW
{
    // Add and make visible all group components
    juce::GroupComponent* groups[] = { &voiceGroup, &unisonGroup, &midiPitchGroup, 
                                       &noteRangeGroup, &portaMonoGroup, &filterGroup, &mainPartGroup };
    for (auto* group : groups)
        addAndMakeVisible(*group);

    // Helper to setup sliders with label, visibility, and range
    auto setupSlider = [this](FMRackLabeledVerticalSlider& slider, const char* label, 
                              double min, double max, double step, double defaultVal = 0) {
        slider.setLabelText(label);
        addAndMakeVisible(slider);
        slider.getSlider().setRange(min, max, step);
        if (defaultVal != 0) slider.getSlider().setValue(defaultVal);
    };
    
    auto setupLabelAndButton = [this](juce::Label& label, juce::ToggleButton& button, const char* text) {
        label.setText(text, juce::dontSendNotification);
        addAndMakeVisible(label);
        button.setButtonText("");
        addAndMakeVisible(button);
    };

    // Setup all sliders
    setupSlider(unisonVoicesSlider, "Voices", 1, 4, 1, 1);
    setupSlider(unisonDetuneSlider, "Detune", 0.0, 1.0, 0.001, unisonCentsToUiNorm(0.0f));
    setupSlider(unisonPanSlider, "Spread", 0.0, 1.0, 0.01, 0.5);
    setupSlider(reverbSendSlider, "Rvb", 0, 99, 1);
    setupSlider(volumeSlider, "Vol", 0, 127, 1);
    setupSlider(panSlider, "Pan", 0, 127, 1);
    setupSlider(detuneSlider, "Fine", -64, 63, 1);
    setupSlider(noteLimitLowSlider, "Low", 0, 127, 1);
    setupSlider(noteLimitHighSlider, "High", 0, 127, 1);
    setupSlider(noteShiftSlider, "Shift", -24, 24, 1);
    setupSlider(pitchBendRangeSlider, "PBR", 0, 12, 1);
    setupSlider(pitchBendStepSlider, "PBS", 0, 12, 1);
    setupSlider(portamentoTimeSlider, "PRT", 0, 99, 1);
    setupSlider(modWheelSensSlider, "MWS", 0, 15, 1);
    setupSlider(modWheelAssignSlider, "MWA", 0, 7, 1);
    setupSlider(footCtrlSensSlider, "FCS", 0, 15, 1);
    setupSlider(footCtrlAssignSlider, "FCA", 0, 7, 1);
    setupSlider(afterTouchSensSlider, "ATS", 0, 15, 1);
    setupSlider(afterTouchAssignSlider, "ATA", 0, 7, 1);
    setupSlider(breathCtrlSensSlider, "BCS", 0, 15, 1);
    setupSlider(breathCtrlAssignSlider, "BCA", 0, 7, 1);
    setupSlider(velocityScaleSlider, "Vel", 0, 127, 1);
    setupSlider(audioAttenuatorSlider, "ATT", 0, 7, 1);
    setupSlider(masterTuneSlider, "MTU", 0, 127, 1);
    setupSlider(filterCutoffSlider, "Cut", 0, 127, 1);
    setupSlider(filterResonanceSlider, "Res", 0, 127, 1);
    
    // MIDI Channel slider with custom text functions
    setupSlider(midiChannelSlider, "Ch", 0, 16, 1, idx + 1);
    midiChannelSlider.getSlider().textFromValueFunction = [](double value) {
        int ch = (int)value;
        return ch == 0 ? "Omni" : juce::String(ch);
    };
    midiChannelSlider.getSlider().valueFromTextFunction = [](const juce::String& text) {
        if (text.equalsIgnoreCase("Omni")) return 0.0;
        return text.getDoubleValue();
    };
    
    // Labels and buttons
    setupLabelAndButton(portamentoGlissandoLabel, portamentoGlissandoButton, "Gliss");
    setupLabelAndButton(portamentoModeLabel, portamentoModeButton, "Porta");
    setupLabelAndButton(monoModeLabel, monoModeButton, "Mono");
    setupLabelAndButton(filterEnabledLabel, filterEnabledButton, "Filter");

    // Helper lambdas for setting up value change handlers (reduces code duplication)
    auto setupUint8SliderHandler = [this](FMRackLabeledVerticalSlider& slider, uint8_t FMRack::Performance::PartConfig::*member) {
        slider.onValueChange = [this, &slider, member] {
            auto* perf = getPerformance(); if (!perf) return;
            auto& part = const_cast<FMRack::Performance::PartConfig&>(perf->getPartConfig(moduleIndex));
            const auto newValue = static_cast<uint8_t>(slider.getSlider().getValue());
            if (part.*member != newValue) {
                part.*member = newValue;
                getController()->setPerformance(*perf);
            }
        };
    };
    
    auto setupInt8SliderHandler = [this](FMRackLabeledVerticalSlider& slider, int8_t FMRack::Performance::PartConfig::*member) {
        slider.onValueChange = [this, &slider, member] {
            auto* perf = getPerformance(); if (!perf) return;
            auto& part = const_cast<FMRack::Performance::PartConfig&>(perf->getPartConfig(moduleIndex));
            const auto newValue = static_cast<int8_t>(slider.getSlider().getValue());
            if (part.*member != newValue) {
                part.*member = newValue;
                getController()->setPerformance(*perf);
            }
        };
    };
    
    auto setupInt16SliderHandler = [this](FMRackLabeledVerticalSlider& slider, int16_t FMRack::Performance::PartConfig::*member) {
        slider.onValueChange = [this, &slider, member] {
            auto* perf = getPerformance(); if (!perf) return;
            auto& part = const_cast<FMRack::Performance::PartConfig&>(perf->getPartConfig(moduleIndex));
            const auto newValue = static_cast<int16_t>(slider.getSlider().getValue());
            if (part.*member != newValue) {
                part.*member = newValue;
                getController()->setPerformance(*perf);
            }
        };
    };
    
    auto setupFloatSliderHandler = [this](FMRackLabeledVerticalSlider& slider, float FMRack::Performance::PartConfig::*member) {
        slider.onValueChange = [this, &slider, member] {
            auto* perf = getPerformance(); if (!perf) return;
            auto& part = const_cast<FMRack::Performance::PartConfig&>(perf->getPartConfig(moduleIndex));
            const float newValue = static_cast<float>(slider.getSlider().getValue());
            if (part.*member != newValue) {
                part.*member = newValue;
                getController()->setPerformance(*perf);
            }
        };
    };
    
    auto setupToggleButtonHandler = [this](juce::ToggleButton& button, uint8_t FMRack::Performance::PartConfig::*member) {
        button.onClick = [this, &button, member] {
            auto* perf = getPerformance(); if (!perf) return;
            auto& part = const_cast<FMRack::Performance::PartConfig&>(perf->getPartConfig(moduleIndex));
            const bool newValue = button.getToggleState();
            const bool currentValue = part.*member != 0;
            if (currentValue != newValue) {
                part.*member = static_cast<uint8_t>(newValue);
                getController()->setPerformance(*perf);
            }
        };
    };
    
    auto setupBoolToggleButtonHandler = [this](juce::ToggleButton& button, bool FMRack::Performance::PartConfig::*member) {
        button.onClick = [this, &button, member] {
            auto* perf = getPerformance(); if (!perf) return;
            auto& part = const_cast<FMRack::Performance::PartConfig&>(perf->getPartConfig(moduleIndex));
            const bool newValue = button.getToggleState();
            if (part.*member != newValue) {
                part.*member = newValue;
                getController()->setPerformance(*perf);
            }
        };
    };

    // Wire up controls to update Performance, not module directly
    
    // Special handlers for unison (with detune conversion logic)
    unisonVoicesSlider.onValueChange = [this] {
        auto* perf = getPerformance(); if (!perf) return;
        auto& part = const_cast<FMRack::Performance::PartConfig&>(perf->getPartConfig(moduleIndex));
        uint8_t newValue = (uint8_t)unisonVoicesSlider.getSlider().getValue();
        if (part.unisonVoices != newValue) {
            const float currentMaxOuter = uiNormToUnisonCents((float)unisonDetuneSlider.getSlider().getValue());
            part.unisonVoices = newValue;
            part.unisonDetune = perStepDetuneFromMaxOuter(currentMaxOuter, (int)part.unisonVoices);
            getController()->setPerformance(*perf);
        }
    };
    unisonDetuneSlider.onValueChange = [this] {
        auto* perf = getPerformance(); if (!perf) return;
        auto& part = const_cast<FMRack::Performance::PartConfig&>(perf->getPartConfig(moduleIndex));
        const float uiNorm = (float)unisonDetuneSlider.getSlider().getValue();
        const float maxOuterCents = uiNormToUnisonCents(uiNorm);
        const int voices = (int)part.unisonVoices;
        const float perStepCents = perStepDetuneFromMaxOuter(maxOuterCents, voices);
        if (part.unisonDetune != perStepCents) {
            part.unisonDetune = perStepCents;
            getController()->setPerformance(*perf);
        }
    };
    
    // Standard handlers using helper functions
    setupFloatSliderHandler(unisonPanSlider, &FMRack::Performance::PartConfig::unisonSpread);
    setupUint8SliderHandler(midiChannelSlider, &FMRack::Performance::PartConfig::midiChannel);
    setupUint8SliderHandler(reverbSendSlider, &FMRack::Performance::PartConfig::reverbSend);
    setupUint8SliderHandler(volumeSlider, &FMRack::Performance::PartConfig::volume);
    setupUint8SliderHandler(panSlider, &FMRack::Performance::PartConfig::pan);
    setupInt8SliderHandler(detuneSlider, &FMRack::Performance::PartConfig::detune);
    setupUint8SliderHandler(noteLimitLowSlider, &FMRack::Performance::PartConfig::noteLimitLow);
    setupUint8SliderHandler(noteLimitHighSlider, &FMRack::Performance::PartConfig::noteLimitHigh);
    setupInt8SliderHandler(noteShiftSlider, &FMRack::Performance::PartConfig::noteShift);
    setupUint8SliderHandler(pitchBendRangeSlider, &FMRack::Performance::PartConfig::pitchBendRange);
    setupUint8SliderHandler(pitchBendStepSlider, &FMRack::Performance::PartConfig::pitchBendStep);
    setupUint8SliderHandler(portamentoTimeSlider, &FMRack::Performance::PartConfig::portamentoTime);
    setupUint8SliderHandler(velocityScaleSlider, &FMRack::Performance::PartConfig::velocityScale);
    setupUint8SliderHandler(audioAttenuatorSlider, &FMRack::Performance::PartConfig::cutoff);
    setupInt16SliderHandler(masterTuneSlider, &FMRack::Performance::PartConfig::masterTune);
    setupUint8SliderHandler(filterCutoffSlider, &FMRack::Performance::PartConfig::filterCutoff);
    setupUint8SliderHandler(filterResonanceSlider, &FMRack::Performance::PartConfig::filterResonance);
    
    // Controller assignments
    setupUint8SliderHandler(modWheelSensSlider, &FMRack::Performance::PartConfig::modulationWheelRange);
    setupUint8SliderHandler(modWheelAssignSlider, &FMRack::Performance::PartConfig::modulationWheelTarget);
    setupUint8SliderHandler(footCtrlSensSlider, &FMRack::Performance::PartConfig::footControlRange);
    setupUint8SliderHandler(footCtrlAssignSlider, &FMRack::Performance::PartConfig::footControlTarget);
    setupUint8SliderHandler(afterTouchSensSlider, &FMRack::Performance::PartConfig::aftertouchRange);
    setupUint8SliderHandler(afterTouchAssignSlider, &FMRack::Performance::PartConfig::aftertouchTarget);
    setupUint8SliderHandler(breathCtrlSensSlider, &FMRack::Performance::PartConfig::breathControlRange);
    setupUint8SliderHandler(breathCtrlAssignSlider, &FMRack::Performance::PartConfig::breathControlTarget);
    
    // Toggle button handlers
    setupToggleButtonHandler(portamentoModeButton, &FMRack::Performance::PartConfig::portamentoMode);
    setupToggleButtonHandler(portamentoGlissandoButton, &FMRack::Performance::PartConfig::portamentoGlissando);
    setupToggleButtonHandler(monoModeButton, &FMRack::Performance::PartConfig::monoMode);
    setupBoolToggleButtonHandler(filterEnabledButton, &FMRack::Performance::PartConfig::filterEnabled);

    // File dialog state flag
    fileDialogOpen = false;
    // Load Voice button
    loadVoiceButton.setButtonText("Load");
    loadVoiceButton.onClick = [this] {
        if (!openVoiceFileDialog)
        {
            openVoiceFileDialog = std::make_unique<FileBrowserDialog>(
                "Select Voice File",
                "*.syx;*.bin;*.dx7;*.dat;*.voice;*.vce;*.opm;*.ini;*",
                juce::File(),
                FileBrowserDialog::DialogType::Voice);
        }

        fileDialogOpen = true;

        auto* dialogPtr = openVoiceFileDialog.get();
        dialogPtr->showDialog(this,
            [this](const juce::File& file) {
                fileDialogOpen = false;
                // Get the currently active tab index instead of using this tab's moduleIndex
                int currentTabIndex = 0;
                if (parentAccordion) {
                    currentTabIndex = parentAccordion->getCurrentTabIndex();
                    if (currentTabIndex < 0 || currentTabIndex >= 16) {
                        currentTabIndex = 0; // Fallback
                    }
                }
                loadVoiceFileIntoModule(file, currentTabIndex);
            },
            [this]() {
                fileDialogOpen = false;
            });
    };
    loadVoiceButton.setButtonText("Open");
    addAndMakeVisible(loadVoiceButton);

    // Use the member variable, don't redeclare it
    openVoiceEditorButton.setButtonText("Edit");
    addAndMakeVisible(openVoiceEditorButton);
    openVoiceEditorButton.onClick = [this] {
        auto* editor = parentAccordion ? parentAccordion->getEditor() : nullptr;
        if (editor) {
            // Always target the module represented by the currently active tab.
            int currentTabIndex = 0;
            if (parentAccordion) {
                currentTabIndex = parentAccordion->getCurrentTabIndex();
                if (currentTabIndex < 0 || currentTabIndex >= 16)
                    currentTabIndex = 0;
            }
            editor->showVoiceEditorPanel(currentTabIndex);
        }
    };

    // Add Browse button for voice browser
    browsePatchesButton.setButtonText("Browse");
    addAndMakeVisible(browsePatchesButton);    browsePatchesButton.onClick = [this] {
        auto* editor = parentAccordion ? parentAccordion->getEditor() : nullptr;
        if (editor) {
            editor->showVoiceBrowser(moduleIndex);
        }
    };

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

    // Add mouse listeners for hover help on all controls
    juce::Component* mouseListenerComponents[] = {
        &loadVoiceButton, &browsePatchesButton, &openVoiceEditorButton,
        &volumeSlider.getSlider(), &panSlider.getSlider(), &detuneSlider.getSlider(), &reverbSendSlider.getSlider(),
        &unisonVoicesSlider.getSlider(), &unisonDetuneSlider.getSlider(), &unisonPanSlider.getSlider(),
        &midiChannelSlider.getSlider(), &noteLimitLowSlider.getSlider(), &noteLimitHighSlider.getSlider(),
        &noteShiftSlider.getSlider(), &pitchBendRangeSlider.getSlider(), &pitchBendStepSlider.getSlider(),
        &portamentoGlissandoButton, &portamentoModeButton, &portamentoTimeSlider.getSlider(), &monoModeButton,
        &modWheelSensSlider.getSlider(), &modWheelAssignSlider.getSlider(), &footCtrlSensSlider.getSlider(),
        &footCtrlAssignSlider.getSlider(), &afterTouchSensSlider.getSlider(), &afterTouchAssignSlider.getSlider(),
        &breathCtrlSensSlider.getSlider(), &breathCtrlAssignSlider.getSlider(), &audioAttenuatorSlider.getSlider(),
        &velocityScaleSlider.getSlider(), &masterTuneSlider.getSlider(), &filterEnabledButton,
        &filterCutoffSlider.getSlider(), &filterResonanceSlider.getSlider()
    };
    for (auto* comp : mouseListenerComponents)
        comp->addMouseListener(this, false);

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
        unisonVoicesSlider.getSlider().setValue(part.unisonVoices, juce::dontSendNotification);
        const float maxOuterCents = maxOuterFromPerStepDetune(part.unisonDetune, (int)part.unisonVoices);
        unisonDetuneSlider.getSlider().setValue(unisonCentsToUiNorm(maxOuterCents), juce::dontSendNotification);
        unisonPanSlider.getSlider().setValue(part.unisonSpread, juce::dontSendNotification);
        midiChannelSlider.getSlider().setValue(part.midiChannel, juce::dontSendNotification); // Sync MIDI channel
        reverbSendSlider.getSlider().setValue(part.reverbSend, juce::dontSendNotification);
        volumeSlider.getSlider().setValue(part.volume, juce::dontSendNotification);
        panSlider.getSlider().setValue(part.pan, juce::dontSendNotification);
        detuneSlider.getSlider().setValue(part.detune, juce::dontSendNotification);

        // Update new controls
        noteLimitLowSlider.getSlider().setValue(part.noteLimitLow, juce::dontSendNotification);
        noteLimitHighSlider.getSlider().setValue(part.noteLimitHigh, juce::dontSendNotification);
        noteShiftSlider.getSlider().setValue(part.noteShift, juce::dontSendNotification);
        pitchBendRangeSlider.getSlider().setValue(part.pitchBendRange, juce::dontSendNotification);
        pitchBendStepSlider.getSlider().setValue(part.pitchBendStep, juce::dontSendNotification);
        portamentoTimeSlider.getSlider().setValue(part.portamentoTime, juce::dontSendNotification);
        velocityScaleSlider.getSlider().setValue(part.velocityScale, juce::dontSendNotification);
        audioAttenuatorSlider.getSlider().setValue(part.cutoff, juce::dontSendNotification);
        masterTuneSlider.getSlider().setValue(part.masterTune, juce::dontSendNotification);
        filterCutoffSlider.getSlider().setValue(part.filterCutoff, juce::dontSendNotification);
        filterResonanceSlider.getSlider().setValue(part.filterResonance, juce::dontSendNotification);
        
        // Controller assignments
        modWheelSensSlider.getSlider().setValue(part.modulationWheelRange, juce::dontSendNotification);
        modWheelAssignSlider.getSlider().setValue(part.modulationWheelTarget, juce::dontSendNotification);
        footCtrlSensSlider.getSlider().setValue(part.footControlRange, juce::dontSendNotification);
        footCtrlAssignSlider.getSlider().setValue(part.footControlTarget, juce::dontSendNotification);
        afterTouchSensSlider.getSlider().setValue(part.aftertouchRange, juce::dontSendNotification);
        afterTouchAssignSlider.getSlider().setValue(part.aftertouchTarget, juce::dontSendNotification);
        breathCtrlSensSlider.getSlider().setValue(part.breathControlRange, juce::dontSendNotification);
        breathCtrlAssignSlider.getSlider().setValue(part.breathControlTarget, juce::dontSendNotification);
        
        // Toggle buttons
        portamentoModeButton.setToggleState(part.portamentoMode != 0, juce::dontSendNotification);
        portamentoGlissandoButton.setToggleState(part.portamentoGlissando != 0, juce::dontSendNotification);
        monoModeButton.setToggleState(part.monoMode != 0, juce::dontSendNotification);
        filterEnabledButton.setToggleState(part.filterEnabled, juce::dontSendNotification);
    }
}

void ModuleTabComponent::resized()
{
    const int outerMargin = 4;
    const int groupGap = 4;
    const int rowGap = 4;
    const int controlGap = FMRackSliderConstants::kSliderGap;
    const int buttonHeight = 20;
    const int meterWidth = 24;

    juce::Rectangle<int> area = getLocalBounds().reduced(outerMargin);

    // Volume meter on the right
    auto meterArea = area.removeFromRight(meterWidth);
    if (volumeMeter)
        volumeMeter->setBounds(meterArea);
    area.removeFromRight(groupGap);

    // Calculate slider dimensions based on available height
    // Use global constant for consistent slider heights
    const int availableHeight = area.getHeight();
    const int rowHeight = (availableHeight - rowGap) / 2;
    const int sliderHeight = FMRackSliderConstants::kMinSliderHeight;
    const int sliderRowHeight = sliderHeight + kSliderLabelHeight;

    // === ROW 1: Voice | Main | Filter | Portamento & Mono ===
    auto row1 = area.removeFromTop(rowHeight);
    
    // Voice group (buttons) - stack vertically for narrower width
    const int voiceButtonWidth = 60; // narrow button width
    const int voiceButtonSpacing = 2;
    const int voiceWidth = voiceButtonWidth + 2 * kContentPadding;
    auto voiceArea = row1.removeFromLeft(voiceWidth);
    voiceGroup.setBounds(voiceArea);
    // Calculate vertical layout for 3 buttons
    {
        const int buttonCount = 3;
        const int totalButtonHeight = buttonCount * buttonHeight + (buttonCount - 1) * voiceButtonSpacing;
        auto voiceContent = voiceArea.reduced(kContentPadding).withTrimmedTop(kGroupLabelOffset);
        int x = voiceContent.getCentreX() - voiceButtonWidth / 2;
        int y = voiceContent.getY() + (voiceContent.getHeight() - totalButtonHeight) / 2;
        
        juce::TextButton* voiceButtons[] = { &loadVoiceButton, &browsePatchesButton, &openVoiceEditorButton };
        for (auto* button : voiceButtons) {
            button->setBounds(x, y, voiceButtonWidth, buttonHeight);
            y += buttonHeight + voiceButtonSpacing;
        }
    }
    row1.removeFromLeft(groupGap);

    // Calculate minimal widths for Main, Filter, PortaMono based on contained controls
    auto computeSliderGroupMin = [&](int sliderCount, int gap)->int {
        const int sliderW = kMinSliderWidth; // matches layoutLabeledSliderRow
        const int contentW = sliderCount * sliderW + gap * juce::jmax(0, sliderCount - 1);
        return contentW + 2 * kContentPadding;
    };
    
    // Helper to distribute available width among groups
    auto distributeWidth = [](int available, int* widths, int count, const int* minWidths, const int* priorities) {
        const int minFloor = 24; // Minimum acceptable width
        int sum = 0;
        for (int i = 0; i < count; ++i) {
            widths[i] = minWidths[i];
            sum += minWidths[i];
        }
        
        if (sum <= available) {
            int extra = available - sum;
            int totalPriority = 0;
            for (int i = 0; i < count; ++i) totalPriority += priorities[i];
            for (int i = 0; i < count; ++i) {
                int give = (i == count - 1) ? extra : (extra * priorities[i] / totalPriority);
                widths[i] += give;
                extra -= give;
            }
        } else {
            float scale = (float)available / (float)sum;
            for (int i = 0; i < count; ++i)
                widths[i] = juce::jmax(minFloor, (int)std::floor(widths[i] * scale));
            int adjusted = 0;
            for (int i = 0; i < count; ++i) adjusted += widths[i];
            while (adjusted > available) {
                for (int i = 0; i < count && adjusted > available; ++i) {
                    if (widths[i] > minFloor) { widths[i]--; adjusted--; }
                }
            }
        }
    };

    const int mainMin = computeSliderGroupMin(5, controlGap); // Ch, Vol, Pan, Fine, Rvb
    const int filterMin = computeSliderGroupMin(2, controlGap); // Cut, Res
    const int portaMonoMin = juce::jmax(computeSliderGroupMin(1, controlGap), 110); // Time + toggles

    // Distribute width for row 1
    int row1Widths[3];
    const int row1Mins[] = { mainMin, filterMin, portaMonoMin };
    const int row1Priorities[] = { 0, 70, 30 }; // Main gets no extra, Filter gets 70%, Porta gets 30%
    distributeWidth(row1.getWidth() - groupGap * 2, row1Widths, 3, row1Mins, row1Priorities);

    // Main group (Ch, Vol, Pan, Fine, Reverb)
    auto mainArea = row1.removeFromLeft(row1Widths[0]);
    mainPartGroup.setBounds(mainArea);
    auto mainContent = makeGroupContentBounds(mainArea, sliderRowHeight);
    layoutLabeledSliderRow(mainContent.withHeight(sliderHeight),
                          { &midiChannelSlider, &volumeSlider, &panSlider, &detuneSlider, &reverbSendSlider },
                          sliderHeight, controlGap);
    row1.removeFromLeft(groupGap);

    // Filter group
    auto filterArea = row1.removeFromLeft(row1Widths[1]);
    filterGroup.setBounds(filterArea);
    auto filterContent = makeGroupContentBounds(filterArea, sliderRowHeight);
    
    const int filterCheckboxWidth = 18;
    const int filterLabelGap = 2;
    const int filterSliderColumnGap = 4;
    const int filterToggleRowH = 16;
    
    // Split content: sliders on right (full height), checkbox on left
    auto filterSliderColumn = filterContent.removeFromRight(kMinSliderWidth * 2 + controlGap);
    filterContent.removeFromRight(filterSliderColumnGap);
    
    // Checkbox and label on left side at top
    juce::Rectangle<int> filterToggleRow(filterContent.getX(), filterContent.getY(), 
                                          filterContent.getWidth(), filterToggleRowH);
    filterEnabledButton.setBounds(filterToggleRow.removeFromLeft(filterCheckboxWidth));
    filterToggleRow.removeFromLeft(filterLabelGap);
    filterEnabledLabel.setBounds(filterToggleRow);
    filterEnabledLabel.setJustificationType(juce::Justification::centredLeft);
    
    // Sliders on right - use full available height
    layoutLabeledSliderRow(filterSliderColumn, { &filterCutoffSlider, &filterResonanceSlider }, sliderHeight, controlGap);
    row1.removeFromLeft(groupGap);

    // Portamento & Mono group (moved to row 1)
    auto portaMonoArea = row1;
    portaMonoGroup.setBounds(portaMonoArea);
    auto portaMonoContent = makeGroupContentBounds(portaMonoArea, sliderRowHeight);
    
    const int toggleRowH = 16;
    const int innerGap = 1;
    const int portaCheckboxWidth = 18;  // Checkbox width
    const int portaLabelGap = 2;        // Gap between checkbox and label
    
    // Split content: slider on right (full height), checkboxes on left
    const int sliderColumnGap = 4;
    auto sliderColumn = portaMonoContent.removeFromRight(kMinSliderWidth);
    portaMonoContent.removeFromRight(sliderColumnGap);
    
    // PRT slider on right - use full available height
    layoutLabeledSliderRow(sliderColumn, { &portamentoTimeSlider }, sliderHeight, controlGap);
    
    // Checkboxes stacked on left side
    // Portamento Mode (PMD), Glissando (PGL), Mono (PMO) - all with checkbox on left, label on right
    struct CheckboxRow { juce::ToggleButton* button; juce::Label* label; };
    CheckboxRow checkboxRows[] = {
        { &portamentoModeButton, &portamentoModeLabel },
        { &portamentoGlissandoButton, &portamentoGlissandoLabel },
        { &monoModeButton, &monoModeLabel }
    };
    
    int yPos = portaMonoContent.getY();
    for (auto& row : checkboxRows) {
        juce::Rectangle<int> rowRect(portaMonoContent.getX(), yPos, portaMonoContent.getWidth(), toggleRowH);
        row.button->setBounds(rowRect.removeFromLeft(portaCheckboxWidth));
        rowRect.removeFromLeft(portaLabelGap);
        row.label->setBounds(rowRect);
        row.label->setJustificationType(juce::Justification::centredLeft);
        yPos += toggleRowH + innerGap;
    }

    area.removeFromTop(rowGap);


    // === ROW 2: Unison | Note Range | MIDI & Pitch (3 sections now) ===
    auto row2 = area;

    // Distribute width for row 2
    const int unisonMin = computeSliderGroupMin(3, controlGap);
    const int noteRangeMin = computeSliderGroupMin(3, controlGap);
    const int midiPitchMin = computeSliderGroupMin(13, controlGap);
    
    int row2Widths[3];
    const int row2Mins[] = { unisonMin, noteRangeMin, midiPitchMin };
    const int row2Priorities[] = { 20, 20, 60 }; // MIDI gets most extra
    distributeWidth(row2.getWidth() - groupGap * 2, row2Widths, 3, row2Mins, row2Priorities);

    // Place the three groups
    auto unisonArea = row2.removeFromLeft(row2Widths[0]);
    unisonGroup.setBounds(unisonArea);
    auto unisonContent = makeGroupContentBounds(unisonArea, sliderRowHeight);
    layoutLabeledSliderRow(unisonContent.withHeight(sliderHeight),
                          { &unisonVoicesSlider, &unisonDetuneSlider, &unisonPanSlider },
                          sliderHeight, controlGap);
    row2.removeFromLeft(groupGap);

    auto noteRangeArea = row2.removeFromLeft(row2Widths[1]);
    noteRangeGroup.setBounds(noteRangeArea);
    auto noteRangeContent = makeGroupContentBounds(noteRangeArea, sliderRowHeight);
    layoutLabeledSliderRow(noteRangeContent.withHeight(sliderHeight),
                          { &noteLimitLowSlider, &noteLimitHighSlider, &noteShiftSlider },
                          sliderHeight, controlGap);
    row2.removeFromLeft(groupGap);

    auto midiPitchArea = row2.removeFromLeft(row2Widths[2]);
    midiPitchGroup.setBounds(midiPitchArea);
    auto midiPitchContent = makeGroupContentBounds(midiPitchArea, sliderRowHeight);
    layoutLabeledSliderRow(midiPitchContent.withHeight(sliderHeight),
                          { &pitchBendRangeSlider, &pitchBendStepSlider, &masterTuneSlider, &velocityScaleSlider, &audioAttenuatorSlider,
                            &modWheelSensSlider, &modWheelAssignSlider, &footCtrlSensSlider, &footCtrlAssignSlider,
                            &afterTouchSensSlider, &afterTouchAssignSlider, &breathCtrlSensSlider, &breathCtrlAssignSlider },
                          sliderHeight, controlGap);

    updateFromModule();
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
                    editor->appendLogMessage("Voice loaded: " + voiceName + " into module " + juce::String(moduleIndex + 1));
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

void ModuleTabComponent::loadVoiceFileIntoModule(const juce::File& file, int targetModuleIndex)
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
            if (perf && targetModuleIndex >= 0 && targetModuleIndex < 16) {
                controller->setPartVoiceData(targetModuleIndex, dexedVoice);
                juce::String voiceName = VoiceData::extractDX7VoiceName(dexedVoice);
                juce::Logger::writeToLog("[ModuleTabComponent] Loaded voice into module " + juce::String(targetModuleIndex + 1) + ": " + voiceName);
                if (editor) {
                    editor->appendLogMessage("Voice loaded: " + voiceName + " into module " + juce::String(targetModuleIndex + 1));
                    // --- Force update of voice editor panel if open ---
                    editor->showVoiceEditorPanel(targetModuleIndex);
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
    // Close any open file dialog
    if (openVoiceFileDialog) {
        openVoiceFileDialog->closeDialog();
    }
    fileDialogOpen = false;
}

bool ModuleTabComponent::isFileDialogOpen() const {
    return fileDialogOpen;
}

void ModuleTabComponent::closeFileDialog() {
    if (openVoiceFileDialog)
        openVoiceFileDialog->closeDialog();
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

void ModuleTabComponent::mouseEnter(const juce::MouseEvent& e) {
    auto* editor = parentAccordion ? parentAccordion->getEditor() : nullptr;
    if (!editor) return;
    
    // Map controls to help keys using a lookup table
    struct HelpMapping { juce::Component* comp; const char* helpKey; };
    const HelpMapping helpMappings[] = {
        { &loadVoiceButton, "loadVoice" }, { &browsePatchesButton, "browsePatches" }, { &openVoiceEditorButton, "editVoice" },
        { &volumeSlider.getSlider(), "volume" }, { &panSlider.getSlider(), "pan" }, { &detuneSlider.getSlider(), "detune" },
        { &reverbSendSlider.getSlider(), "reverbSend" }, { &unisonVoicesSlider.getSlider(), "unisonVoices" },
        { &unisonDetuneSlider.getSlider(), "unisonDetune" }, { &unisonPanSlider.getSlider(), "unisonPan" },
        { &midiChannelSlider.getSlider(), "midiChannel" }, { &noteLimitLowSlider.getSlider(), "noteLimitLow" },
        { &noteLimitHighSlider.getSlider(), "noteLimitHigh" }, { &noteShiftSlider.getSlider(), "noteShift" },
        { &pitchBendRangeSlider.getSlider(), "pitchBendRange" }, { &pitchBendStepSlider.getSlider(), "PBS" },
        { &portamentoGlissandoButton, "PGL" }, { &portamentoModeButton, "PMD" },
        { &portamentoTimeSlider.getSlider(), "PRT" }, { &monoModeButton, "PMO" },
        { &modWheelSensSlider.getSlider(), "MWS" }, { &modWheelAssignSlider.getSlider(), "MWA" },
        { &footCtrlSensSlider.getSlider(), "FCS" }, { &footCtrlAssignSlider.getSlider(), "FCA" },
        { &afterTouchSensSlider.getSlider(), "ATS" }, { &afterTouchAssignSlider.getSlider(), "ATA" },
        { &breathCtrlSensSlider.getSlider(), "BCS" }, { &breathCtrlAssignSlider.getSlider(), "BCA" },
        { &audioAttenuatorSlider.getSlider(), "ATT" }, { &velocityScaleSlider.getSlider(), "velocityScale" },
        { &masterTuneSlider.getSlider(), "MTU" }, { &filterEnabledButton, "filterEnabled" },
        { &filterCutoffSlider.getSlider(), "filterCutoff" }, { &filterResonanceSlider.getSlider(), "filterResonance" }
    };
    
    for (const auto& mapping : helpMappings) {
        if (e.eventComponent == mapping.comp) {
            editor->showHelpForKey(mapping.helpKey);
            break;
        }
    }
}

void ModuleTabComponent::mouseExit(const juce::MouseEvent& e) {
    auto* editor = parentAccordion ? parentAccordion->getEditor() : nullptr;
    if (editor) {
        editor->restoreDefaultHelp();
    }
}

