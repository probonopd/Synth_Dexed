#include "OperatorSliderLookAndFeel.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "RackAccordionComponent.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <fstream>
#include "../../src/FMRack/VoiceData.h" // NEW: Include VoiceData for conversion function

static OperatorSliderLookAndFeel operatorSliderLookAndFeel;

// ================= RackAccordionComponent =================
RackAccordionComponent::RackAccordionComponent(AudioPluginAudioProcessor* processorPtr)
    : processor(processorPtr)
{
    try {
        addAndMakeVisible(numModulesLabel);
        addAndMakeVisible(numModulesSlider);
        addAndMakeVisible(tabs);
        numModulesLabel.setText("Modules", juce::dontSendNotification);
        numModulesSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        numModulesSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
        numModulesSlider.setRange(1, 8, 1);
        // Initialize ValueTree for UI sync
        valueTree = juce::ValueTree("RackUI");
        int initialModules = processor && processor->getController() ? processor->getController()->getNumModules() : 1;
        valueTree.setProperty("numModules", initialModules, nullptr);
        valueTree.addListener(this);
        numModulesSlider.setValue(getNumModulesVT());
        numModulesSlider.onValueChange = [this] {
            try {
                setNumModulesVT((int)numModulesSlider.getValue());
            } catch (const std::exception& e) {
                juce::Logger::writeToLog("[RackAccordionComponent] Exception in numModulesSlider.onValueChange: " + juce::String(e.what()));
            } catch (...) {
                juce::Logger::writeToLog("[RackAccordionComponent] Unknown exception in numModulesSlider.onValueChange");
            }
        };
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
        }
        juce::Logger::writeToLog("[RackAccordionComponent] Calling initial updatePanels()");
        updatePanels();
    } catch (const std::exception& e) {
        juce::Logger::writeToLog("[RackAccordionComponent] Exception in constructor: " + juce::String(e.what()));
    } catch (...) {
        juce::Logger::writeToLog("[RackAccordionComponent] Unknown exception in constructor");
    }
}

void RackAccordionComponent::setNumModulesVT(int num) {
    try {
        if (num < 1) num = 1;
        if (num > 8) num = 8;
        if (getNumModulesVT() != num) {
            valueTree.setProperty("numModules", num, nullptr);
        }
        auto* controller = processor ? processor->getController() : nullptr;
        if (controller) {
            auto* perf = controller->getPerformance();
            if (perf) {
                // Create a new performance with the correct number of parts
                FMRack::Performance newPerf;
                
                // Copy existing parts up to the minimum of current and target size
                int minSize = std::min((int)perf->parts.size(), num);
                for (int i = 0; i < minSize; ++i) {
                    newPerf.parts[i] = perf->parts[i];
                }
                
                // Initialize any additional parts if expanding
                for (int i = minSize; i < num && i < 16; ++i) {
                    newPerf.parts[i] = FMRack::Performance::PartConfig();
                    newPerf.parts[i].midiChannel = i + 1; // Set unique MIDI channels
                }
                
                // Copy effects settings
                newPerf.effects = perf->effects;
                
                controller->setPerformance(newPerf);
            }
            updatePanels();
            // Ensure slider matches the number of tabs (modules)
            numModulesSlider.setValue(controller->getNumModules(), juce::dontSendNotification);
        }
    } catch (const std::exception& e) {
        juce::Logger::writeToLog("[RackAccordionComponent] Exception in setNumModulesVT: " + juce::String(e.what()));
    } catch (...) {
        juce::Logger::writeToLog("[RackAccordionComponent] Unknown exception in setNumModulesVT");
    }
}

int RackAccordionComponent::getNumModulesVT() const {
    return valueTree.getProperty("numModules", 1);
}

void RackAccordionComponent::valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& property)
{
    if (property == juce::Identifier("numModules") && tree == valueTree)
    {
        int newNumModules = tree.getProperty("numModules", 1);
        if ((int)numModulesSlider.getValue() != newNumModules) {
            numModulesSlider.setValue(newNumModules, juce::dontSendNotification);
        }
        updatePanels();
    }
}

void RackAccordionComponent::syncNumModulesSliderWithRack()
{
    auto* controller = processor ? processor->getController() : nullptr;
    if (controller) {
        int rackNum = controller->getNumModules();
        if ((int)numModulesSlider.getValue() != rackNum) {
            numModulesSlider.setValue(rackNum, juce::dontSendNotification);
        }
        if (getNumModulesVT() != rackNum) {
            valueTree.setProperty("numModules", rackNum, nullptr);
        }
    }
}

void RackAccordionComponent::updatePanels()
{
    try {
        juce::Logger::writeToLog("[RackAccordionComponent] updatePanels() called");
        // Save the currently selected tab index
        int selectedTabIndex = tabs.getCurrentTabIndex();
        if (tabs.getNumTabs() > 0) {
            // Check if any ModuleTabComponent has an open file dialog
            for (const auto& tab : moduleTabs) {
                if (tab && tab->isFileDialogOpen()) {
                    juce::Logger::writeToLog("[WARNING] updatePanels() called while a Load Voice file dialog is open! This can cause a crash. Skipping updatePanels.");
                    return; // Prevent crash by not updating panels while dialog is open
                }
            }
        }
        tabs.clearTabs();
        moduleTabs.clear();
        auto* controller = processor ? processor->getController() : nullptr;
        if (!controller) {
            juce::Logger::writeToLog("[RackAccordionComponent] No controller available!");
            return;
        }
        const auto& modules = controller->getModules();
        int numModules = (int)modules.size();
        juce::Logger::writeToLog("[RackAccordionComponent] Number of modules: " + juce::String(numModules));
        for (int i = 0; i < numModules; ++i)
        {
            juce::Logger::writeToLog("[RackAccordionComponent] Creating tab " + juce::String(i));
            auto tab = std::make_unique<ModuleTabComponent>(i, this);
            tab->updateFromModule(); // NEW: sync controls to module state
            tabs.addTab(juce::String(i + 1), juce::Colours::darkgrey, tab.get(), false);
            moduleTabs.push_back(std::move(tab));
        }
        tabs.resized(); // Ensure child tabs get resized after adding
        resized();
        syncNumModulesSliderWithRack(); // Always sync slider at the end
        // After updating tabs, ensure slider matches the number of tabs
        numModulesSlider.setValue(tabs.getNumTabs(), juce::dontSendNotification);
        // Restore the previously selected tab index if possible
        if (selectedTabIndex >= 0 && selectedTabIndex < tabs.getNumTabs()) {
            tabs.setCurrentTabIndex(selectedTabIndex);
        }
        juce::Logger::writeToLog("[RackAccordionComponent] updatePanels() completed successfully with " + juce::String(tabs.getNumTabs()) + " tabs");
    } catch (const std::exception& e) {
        juce::Logger::writeToLog("[RackAccordionComponent] Exception in updatePanels: " + juce::String(e.what()));
    } catch (...) {
        juce::Logger::writeToLog("[RackAccordionComponent] Unknown exception in updatePanels");
    }
}

void RackAccordionComponent::resized()
{
    int x = 10, y = 10, w = 120, h = 24, gap = 6;
    numModulesLabel.setBounds(x, y, w, h);
    numModulesSlider.setBounds(x + w + 4, y, 80, h);
    y += h + gap;
    int tabY = y;
    tabs.setBounds(0, tabY, getWidth(), getHeight() - tabY);
}

void RackAccordionComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::darkgrey);
}

// ================= ModuleTabComponent =================
ModuleTabComponent::ModuleTabComponent(int idx, RackAccordionComponent* parent)
    : moduleIndex(idx), parentAccordion(parent)
{
    // Remove setSize() calls from all sliders, as their size is set in resized().
    unisonVoicesLabel.setText("Unison Voices", juce::dontSendNotification);
    addAndMakeVisible(unisonVoicesLabel);
    unisonVoicesSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    unisonVoicesSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    unisonVoicesSlider.setRange(1, 4, 1);
    unisonVoicesSlider.setValue(1);
    addAndMakeVisible(unisonVoicesSlider);

    unisonDetuneLabel.setText("Unison Detune", juce::dontSendNotification);
    addAndMakeVisible(unisonDetuneLabel);
    unisonDetuneSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    unisonDetuneSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    unisonDetuneSlider.setRange(0.0, 50.0, 0.1);
    unisonDetuneSlider.setValue(7.0);
    addAndMakeVisible(unisonDetuneSlider);

    unisonPanLabel.setText("Unison Pan", juce::dontSendNotification);
    addAndMakeVisible(unisonPanLabel);
    unisonPanSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    unisonPanSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    unisonPanSlider.setRange(0.0, 1.0, 0.01);
    unisonPanSlider.setValue(0.5);
    addAndMakeVisible(unisonPanSlider);

    midiChannelLabel.setText("MIDI Channel", juce::dontSendNotification);
    addAndMakeVisible(midiChannelLabel);
    midiChannelSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
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
