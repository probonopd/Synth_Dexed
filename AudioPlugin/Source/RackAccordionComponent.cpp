#include "OperatorSliderLookAndFeel.h"
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
                std::cout << "[RackAccordionComponent] Exception in numModulesSlider.onValueChange: " << e.what() << std::endl;
            } catch (...) {
                std::cout << "[RackAccordionComponent] Unknown exception in numModulesSlider.onValueChange" << std::endl;
            }
        };
        editor = nullptr; // must be set by the parent (PluginEditor)
        // Register UI update callback
        if (processor && processor->getController()) {
            processor->getController()->onModulesChanged = [this]() {
                juce::MessageManager::callAsync([this] {
                    try {
                        updatePanels();
                    } catch (const std::exception& e) {
                        std::cout << "[RackAccordionComponent] Exception in onModulesChanged callback: " << e.what() << std::endl;
                    } catch (...) {
                        std::cout << "[RackAccordionComponent] Unknown exception in onModulesChanged callback" << std::endl;
                    }
                });
            };
        }
        updatePanels();
    } catch (const std::exception& e) {
        std::cout << "[RackAccordionComponent] Exception in constructor: " << e.what() << std::endl;
    } catch (...) {
        std::cout << "[RackAccordionComponent] Unknown exception in constructor" << std::endl;
    }
}

void RackAccordionComponent::setNumModulesVT(int num) {
    try {
        valueTree.setProperty("numModules", num, nullptr);
        auto* editor = getEditor();
        auto* processor = this->processor;
        auto* controller = processor ? processor->getController() : nullptr;
        auto* perf = controller ? controller->getPerformance() : nullptr;
        if (controller && perf) {
            int current = controller->getNumModules();
            for (int i = num; i < 16; ++i) {
                perf->parts[i] = FMRack::Performance::PartConfig{};
            }
            if (num > current) {
                for (int i = current; i < num; ++i) {
                    perf->parts[i].midiChannel = i + 1;
                    perf->parts[i].unisonVoices = 1;
                    perf->parts[i].unisonDetune = 7.0f;
                    perf->parts[i].unisonSpread = 0.5f;
                    perf->parts[i].volume = 100;
                }
            }
            controller->setPerformance(*perf);
            updatePanels();
            // Ensure slider matches the number of tabs (modules)
            numModulesSlider.setValue(controller->getNumModules(), juce::dontSendNotification);
        }
    } catch (const std::exception& e) {
        std::cout << "[RackAccordionComponent] Exception in setNumModulesVT: " << e.what() << std::endl;
    } catch (...) {
        std::cout << "[RackAccordionComponent] Unknown exception in setNumModulesVT" << std::endl;
    }
}

int RackAccordionComponent::getNumModulesVT() const {
    return (int)valueTree.getProperty("numModules", 1);
}

void RackAccordionComponent::valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& property)
{
    if (property == juce::Identifier("numModules")) {
        int num = getNumModulesVT();
        auto* controller = processor ? processor->getController() : nullptr;
        if (controller && controller->getNumModules() != num) {
            controller->setNumModules(num);
        }
        syncNumModulesSliderWithRack();
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
        if (!controller) return;
        const auto& modules = controller->getModules();
        int numModules = (int)modules.size();
        for (int i = 0; i < numModules; ++i)
        {
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
    } catch (const std::exception& e) {
        std::cout << "[RackAccordionComponent] Exception in updatePanels: " << e.what() << std::endl;
    } catch (...) {
        std::cout << "[RackAccordionComponent] Unknown exception in updatePanels" << std::endl;
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
            editor->showVoiceEditorPanel();
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
    juce::Logger::writeToLog("[Load Voice] Loading file: " + file.getFullPathName());
    juce::FileInputStream stream(file);
    if (!stream.openedOk()) {
        juce::Logger::writeToLog("[Load Voice] Failed to open file: " + file.getFullPathName());
        return;
    }
    std::vector<uint8_t> data;
    data.resize((size_t)stream.getTotalLength());
    auto bytesRead = stream.read(data.data(), (int)data.size());
    if (bytesRead != (int)data.size()) {
        juce::Logger::writeToLog("[Load Voice] Failed to read all bytes from file: " + file.getFullPathName());
        return;
    }
    juce::Logger::writeToLog("[Load Voice] Read " + juce::String((int)data.size()) + " bytes from file: " + file.getFullPathName());
    // Convert to Dexed format if needed
    if (data.size() == 128 || data.size() == 155) {
        juce::Logger::writeToLog("[Load Voice] Converting 128/155-byte voice to Dexed format (156 bytes)...");
        data = VoiceData::convertDX7ToDexed(data);
    } else if (data.size() == 163 && data[0] == 0xF0 && data[1] == 0x43) {
        juce::Logger::writeToLog("[Load Voice] Detected 163-byte DX7 sysex, extracting voice data...");
        if (data.size() >= 163) {
            std::vector<uint8_t> voiceData(data.begin() + 6, data.begin() + 161);
            data = VoiceData::convertDX7ToDexed(voiceData);
        }
    }
    if (data.size() < 156) {
        juce::Logger::writeToLog("[Load Voice] Error: Voice data is too short (" + juce::String((int)data.size()) + " bytes). Needs 156 bytes.");
        return;
    }
    auto* editor = parentAccordion ? parentAccordion->getEditor() : nullptr;
    auto* processor = editor ? editor->getProcessor() : nullptr;
    auto* controller = processor ? processor->getController() : nullptr;
    auto* perf = controller ? controller->getPerformance() : nullptr;
    if (perf) {
        perf->setPartVoiceData(moduleIndex, data);
        controller->setPerformance(*perf);
        juce::Logger::writeToLog("[Load Voice] Voice loaded successfully for module " + juce::String(moduleIndex+1) + ".");
        // --- PATCH: After loading a voice, update the UI safely ---
        if (auto* editor = parentAccordion ? parentAccordion->getEditor() : nullptr) {
            if (auto* panel = editor->getVoiceEditorPanel()) {
                // Defensive: update controller pointer and sync sliders
                panel->setController(controller);
                try {
                    panel->syncAllOperatorSlidersWithDexed();
                } catch (const std::exception& e) {
                    juce::Logger::writeToLog("[Load Voice] Exception syncing operator sliders: " + juce::String(e.what()));
                } catch (...) {
                    juce::Logger::writeToLog("[Load Voice] Unknown exception syncing operator sliders");
                }
            }
        }
    } else {
        juce::Logger::writeToLog("[Load Voice] Error: Could not get performance object.");
    }
}

ModuleTabComponent::~ModuleTabComponent() {
    fileDialogOpen = false;
}

bool ModuleTabComponent::isFileDialogOpen() const {
    return fileDialogOpen;
}
