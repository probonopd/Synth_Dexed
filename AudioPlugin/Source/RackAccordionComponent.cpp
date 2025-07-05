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
        addAndMakeVisible(addModuleButton);
        addAndMakeVisible(removeModuleButton);
        addModuleButton.onClick = [this] {
            juce::Logger::writeToLog("[RackAccordionComponent] '+' button clicked");
            int num = getNumModulesVT();
            juce::Logger::writeToLog("[RackAccordionComponent] '+' button: current ValueTree numModules=" + juce::String(num));
            if (num < 16) {
                // ONLY update the ValueTree. The property listener will handle the rest.
                valueTree.setProperty("numModules", num + 1, nullptr);
            } else {
                juce::Logger::writeToLog("[RackAccordionComponent] '+' button pressed, but already at 16 modules. No action.");
            }
        };
        removeModuleButton.onClick = [this] {
            int num = getNumModulesVT();
            if (num > 1) {
                // ONLY update the ValueTree. The property listener will handle the rest.
                valueTree.setProperty("numModules", num - 1, nullptr);
            } else {
                juce::Logger::writeToLog("[RackAccordionComponent] '-' button pressed, but only one module left. No action.");
            }
        };        // Initialize ValueTree for UI sync
        valueTree = juce::ValueTree("RackUI");
        int initialModules = 1; // Start with default, will sync in updatePanels()
        if (processor && processor->getController()) {
            int rackModules = processor->getController()->getNumModules();
            juce::Logger::writeToLog("[RackAccordionComponent] Constructor: Rack reports " + juce::String(rackModules) + " modules");
            if (rackModules >= 1 && rackModules <= 16) {
                initialModules = rackModules;
            }
        }
        juce::Logger::writeToLog("[RackAccordionComponent] Constructor: Setting initial ValueTree to " + juce::String(initialModules) + " modules");
        valueTree.setProperty("numModules", initialModules, nullptr);
        valueTree.addListener(this);
        // numModulesSlider.setValue(getNumModulesVT());
        // numModulesSlider.onValueChange = ...
        editor = nullptr; // must be set by the parent (PluginEditor)
        // Register UI update callback
        if (processor && processor->getController()) {
            juce::Logger::writeToLog("[RackAccordionComponent] Registering onModulesChanged callback");
            processor->getController()->onModulesChanged = [this]() {
                juce::MessageManager::callAsync([this] {
                    try {
                        updatePanels();
                    } catch (const std::exception& e) {
                        juce::Logger::writeToLog("[RackAccordionComponent] Exception in onModulesChanged callback: " + juce::String(e.what()));
                    } catch (...) {
                        juce::Logger::writeToLog("[RackAccordionComponent] Unknown exception in onModulesChanged callback");
                    }
                });
            };
        } else {
            juce::Logger::writeToLog("[RackAccordionComponent] Warning: processor or controller is null, cannot register callback");
        }        juce::Logger::writeToLog("[RackAccordionComponent] Calling initial updatePanels()");
        updatePanels();
        
        // Schedule a single delayed update to ensure sync after constructor completes
        juce::Timer::callAfterDelay(100, [this]() {
            juce::MessageManager::callAsync([this] {
                juce::Logger::writeToLog("[RackAccordionComponent] Delayed sync check");
                updatePanels();
            });
        });
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
    int x = 10, y = 10, buttonW = 32, buttonH = 28, gap = 6;
    // Place + and - buttons horizontally at the top left
    addModuleButton.setBounds(x, y, buttonW, buttonH);
    removeModuleButton.setBounds(x + buttonW + gap, y, buttonW, buttonH);
    int tabY = y + buttonH + gap;
    tabs.setBounds(0, tabY, getWidth(), getHeight() - tabY);
}

void RackAccordionComponent::paint(juce::Graphics& g)
{
    // Use the same background color as VoiceEditorPanel
    g.fillAll(juce::Colour(0xff23272e)); // dark gray-blue, adjust as needed to match VoiceEditorPanel
}

// ================= ModuleTabComponent =================
ModuleTabComponent::ModuleTabComponent(int idx, RackAccordionComponent* parent)
    : moduleIndex(idx), parentAccordion(parent)
{
    // Remove setSize() calls from all sliders, as their size is set in resized().
    unisonVoicesLabel.setText("Unison Voices", juce::dontSendNotification);
    addAndMakeVisible(unisonVoicesLabel);
    unisonVoicesSlider.setSliderStyle(juce::Slider::LinearVertical);
    unisonVoicesSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    unisonVoicesSlider.setRange(1, 4, 1);
    unisonVoicesSlider.setValue(1);
    addAndMakeVisible(unisonVoicesSlider);

    unisonDetuneLabel.setText("Unison Detune", juce::dontSendNotification);
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

    midiChannelLabel.setText("MIDI Channel", juce::dontSendNotification);
    addAndMakeVisible(midiChannelLabel);
    midiChannelSlider.setSliderStyle(juce::Slider::LinearVertical);
    midiChannelSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    midiChannelSlider.setRange(1, 16, 1);
    midiChannelSlider.setValue(idx + 1); // Default to 1-based index
    addAndMakeVisible(midiChannelSlider);

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
    // File dialog state flag
    fileDialogOpen = false;
    // Load Voice button
    loadVoiceButton.setButtonText("Load Voice");
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
    addAndMakeVisible(loadVoiceButton);

    // Use the member variable, don't redeclare it
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
    }
}

void ModuleTabComponent::resized()
{
    int x = 20, y = 20, sliderW = 60, sliderH = 120, gap = 24;
    // Arrange labels above each slider, sliders horizontally
    unisonVoicesLabel.setBounds(x, y, sliderW, 20);
    unisonVoicesSlider.setBounds(x, y + 22, sliderW, sliderH);
    x += sliderW + gap;
    unisonDetuneLabel.setBounds(x, y, sliderW, 20);
    unisonDetuneSlider.setBounds(x, y + 22, sliderW, sliderH);
    x += sliderW + gap;
    unisonPanLabel.setBounds(x, y, sliderW, 20);
    unisonPanSlider.setBounds(x, y + 22, sliderW, sliderH);
    x += sliderW + gap;
    midiChannelLabel.setBounds(x, y, sliderW, 20);
    midiChannelSlider.setBounds(x, y + 22, sliderW, sliderH);
    // Place buttons below the sliders
    int buttonY = y + sliderH + 32;
    x = 20;
    loadVoiceButton.setBounds(x, buttonY, 120, 28);
    openVoiceEditorButton.setBounds(x + 140, buttonY, 140, 28);
    // Place volume meter to the right of sliders, 1/5 of tab height
    int meterW = 32;
    int tabH = getHeight();
    int meterH = std::max(24, tabH / 5);
    int meterX = getWidth() - meterW - 20;
    int meterY = 20;
    if (volumeMeter)
        volumeMeter->setBounds(meterX, meterY, meterW, meterH);

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
