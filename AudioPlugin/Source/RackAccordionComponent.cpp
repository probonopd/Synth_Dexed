#include "OperatorSliderLookAndFeel.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "RackAccordionComponent.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <fstream>
#include <initializer_list>
#include "../../src/FMRack/VoiceData.h" // NEW: Include VoiceData for conversion function
#include <juce_gui_extra/juce_gui_extra.h>

static OperatorSliderLookAndFeel operatorSliderLookAndFeel;

namespace
{
    constexpr int kContentPadding = 12;
    constexpr int kGroupLabelOffset = 24;
    constexpr int kSliderLabelHeight = 18;
    constexpr int kSliderLabelGap = 4;
    constexpr int kMinSliderWidth = 52;

    struct SliderLabelPair
    {
        juce::Slider* slider = nullptr;
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

    void layoutSliderRow(const juce::Rectangle<int>& area,
                         const std::initializer_list<SliderLabelPair>& controls,
                         int sliderHeight,
                         int controlGap)
    {
        if (controls.size() == 0)
            return;

        const int sliderCount = static_cast<int>(controls.size());
        const int gapTotal = controlGap * (sliderCount - 1);
        int availableWidth = juce::jmax(0, area.getWidth() - gapTotal);
        int sliderWidth = sliderCount > 0 ? availableWidth / sliderCount : availableWidth;
        sliderWidth = juce::jmax(kMinSliderWidth, sliderWidth);

        int totalWidth = sliderWidth * sliderCount + gapTotal;
        if (totalWidth > area.getWidth())
        {
            const int fittedWidth = juce::jmax(0, area.getWidth() - gapTotal);
            sliderWidth = sliderCount > 0 ? juce::jmax(32, fittedWidth / sliderCount) : fittedWidth;
            totalWidth = sliderWidth * sliderCount + gapTotal;
        }

        int startX = area.getX() + juce::jmax(0, (area.getWidth() - totalWidth) / 2);

        for (auto control : controls)
        {
            if (control.slider != nullptr)
            {
                juce::Rectangle<int> sliderBounds(startX, area.getY(), sliderWidth, sliderHeight);
                control.slider->setBounds(sliderBounds);

                if (control.label != nullptr)
                {
                    control.label->setJustificationType(juce::Justification::centred);
                    control.label->setBounds(sliderBounds.withY(sliderBounds.getBottom() + kSliderLabelGap)
                                                         .withHeight(kSliderLabelHeight));
                }
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

    // Add Browse button for voice browser
    browsePatchesButton.setButtonText("Browse");
    addAndMakeVisible(browsePatchesButton);    browsePatchesButton.onClick = [this] {
        auto* editor = parentAccordion ? parentAccordion->getEditor() : nullptr;
        if (editor) {
            editor->showVoiceBrowser(moduleIndex);
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
        const int outerMargin = 12;
        const int columnGap = 14;
        const int rowGap = 14;
        const int controlGap = 12;
        const int buttonHeight = 26;
        const int meterWidth = 36;

        juce::Rectangle<int> area = getLocalBounds().reduced(outerMargin);

        auto meterArea = area.removeFromRight(meterWidth);
        if (volumeMeter)
                volumeMeter->setBounds(meterArea);

        constexpr int columnCount = 3;
        const int minColumnWidth = 150;
        const int totalGapWidth = columnGap * (columnCount - 1);
        const int columnWidth = juce::jmax(minColumnWidth, (area.getWidth() - totalGapWidth) / columnCount);
        const int totalWidthUsed = columnWidth * columnCount + totalGapWidth;
        const int offsetX = juce::jmax(0, (area.getWidth() - totalWidthUsed) / 2);

        const int sliderHeight = juce::jlimit(100, 150, area.getHeight() / 4);
        const int sliderRowHeight = sliderHeight + kSliderLabelGap + kSliderLabelHeight;

        struct ColumnState { int x = 0; int width = 0; int y = 0; };
        ColumnState columns[columnCount]{};
        int currentX = area.getX() + offsetX;
        for (int i = 0; i < columnCount; ++i)
        {
                columns[i].x = currentX;
                columns[i].width = columnWidth;
                columns[i].y = area.getY();
                currentX += columnWidth + columnGap;
        }

        auto placeGroup = [&](int columnIndex, juce::GroupComponent& group, int contentHeight)
        {
                auto& column = columns[columnIndex];
                const int groupHeight = contentHeight + kGroupLabelOffset + kContentPadding * 2;
                juce::Rectangle<int> bounds(column.x, column.y, column.width, groupHeight);
                group.setBounds(bounds);
                column.y = bounds.getBottom() + rowGap;
                return makeGroupContentBounds(bounds, contentHeight);
        };

        auto layoutVoiceButtons = [&](const juce::Rectangle<int>& content)
        {
                const int buttonCount = 3;
                const int buttonSpacing = controlGap;
            const int availableWidth = content.getWidth() - buttonSpacing * (buttonCount - 1);
            const int buttonWidth = buttonCount > 0 ? availableWidth / buttonCount : availableWidth;
            const int totalWidth = buttonWidth * buttonCount + buttonSpacing * (buttonCount - 1);
            int startX = content.getX() + juce::jmax(0, (content.getWidth() - totalWidth) / 2);
            const int buttonY = content.getCentreY() - buttonHeight / 2;

            loadVoiceButton.setBounds(startX, buttonY, buttonWidth, buttonHeight);
            startX += buttonWidth + buttonSpacing;
            browsePatchesButton.setBounds(startX, buttonY, buttonWidth, buttonHeight);
            startX += buttonWidth + buttonSpacing;
            openVoiceEditorButton.setBounds(startX, buttonY, buttonWidth, buttonHeight);
        };

        auto voiceContent = placeGroup(0, voiceGroup, buttonHeight);
        layoutVoiceButtons(voiceContent);

        auto unisonContent = placeGroup(0, unisonGroup, sliderRowHeight);
        layoutSliderRow(unisonContent.withHeight(sliderHeight),
                                        { { &unisonVoicesSlider, &unisonVoicesLabel },
                                            { &unisonDetuneSlider, &unisonDetuneLabel },
                                            { &unisonPanSlider, &unisonPanLabel } },
                                        sliderHeight,
                                        controlGap);

        auto midiPitchContent = placeGroup(0, midiPitchGroup, sliderRowHeight);
        layoutSliderRow(midiPitchContent.withHeight(sliderHeight),
                                        { { &midiChannelSlider, &midiChannelLabel },
                                            { &pitchBendRangeSlider, &pitchBendRangeLabel },
                                            { &masterTuneSlider, &masterTuneLabel } },
                                        sliderHeight,
                                        controlGap);

        auto mainPartContent = placeGroup(1, mainPartGroup, sliderRowHeight);
        layoutSliderRow(mainPartContent.withHeight(sliderHeight),
                                        { { &volumeSlider, &volumeLabel },
                                            { &panSlider, &panLabel },
                                            { &detuneSlider, &detuneLabel },
                                            { &reverbSendSlider, &reverbSendLabel },
                                            { &velocityScaleSlider, &velocityScaleLabel } },
                                        sliderHeight,
                                        controlGap);

        auto noteRangeContent = placeGroup(1, noteRangeGroup, sliderRowHeight);
        layoutSliderRow(noteRangeContent.withHeight(sliderHeight),
                                        { { &noteLimitLowSlider, &noteLimitLowLabel },
                                            { &noteLimitHighSlider, &noteLimitHighLabel },
                                            { &noteShiftSlider, &noteShiftLabel } },
                                        sliderHeight,
                                        controlGap);

        const int filterContentHeight = buttonHeight + rowGap + sliderRowHeight;
        auto filterContent = placeGroup(2, filterGroup, filterContentHeight);
        juce::Rectangle<int> filterToggleRow(filterContent.getX(), filterContent.getY(), filterContent.getWidth(), buttonHeight);
        layoutToggleRow(filterEnabledLabel, filterEnabledButton, filterToggleRow, 80);
        juce::Rectangle<int> filterSliderRow(filterContent.getX(), filterToggleRow.getBottom() + rowGap, filterContent.getWidth(), sliderHeight);
        layoutSliderRow(filterSliderRow,
                                        { { &filterCutoffSlider, &filterCutoffLabel },
                                            { &filterResonanceSlider, &filterResonanceLabel } },
                                        sliderHeight,
                                        controlGap);

        const int portaContentHeight = buttonHeight + rowGap + sliderRowHeight + rowGap + buttonHeight;
        auto portaContent = placeGroup(2, portaMonoGroup, portaContentHeight);
        juce::Rectangle<int> portaRow(portaContent.getX(), portaContent.getY(), portaContent.getWidth(), buttonHeight);
        layoutToggleRow(portamentoModeLabel, portamentoModeButton, portaRow, 80);
        juce::Rectangle<int> portaSliderRow(portaContent.getX(), portaRow.getBottom() + rowGap, portaContent.getWidth(), sliderHeight);
        layoutSliderRow(portaSliderRow,
                                        { { &portamentoTimeSlider, &portamentoTimeLabel } },
                                        sliderHeight,
                                        controlGap);
        juce::Rectangle<int> monoRow(portaContent.getX(), portaSliderRow.getBottom() + rowGap, portaContent.getWidth(), buttonHeight);
        layoutToggleRow(monoModeLabel, monoModeButton, monoRow, 80);

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
