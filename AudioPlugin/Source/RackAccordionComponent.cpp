#include "PluginEditor.h"
#include "RackAccordionComponent.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_core/juce_core.h>
#include <fstream>
#include "../../src/FMRack/VoiceData.h" // NEW: Include VoiceData for conversion function

// ================= RackAccordionComponent =================
RackAccordionComponent::RackAccordionComponent(FMRack::Rack* rackPtr)
    : rack(rackPtr)
{
    addAndMakeVisible(numModulesLabel);
    addAndMakeVisible(numModulesSlider);
    addAndMakeVisible(tabs);
    numModulesLabel.setText("Modules", juce::dontSendNotification);
    numModulesSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    numModulesSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    numModulesSlider.setRange(1, 8, 1);
    numModulesSlider.setValue(1);
    numModulesSlider.onValueChange = [this] {
        auto* editorPtr = getEditor();
        auto* processor = editorPtr ? editorPtr->getProcessor() : nullptr;
        auto* perf = processor ? processor->getPerformance() : nullptr;
        if (perf && processor) {
            perf->setDefaults((int)numModulesSlider.getValue(), 1);
            processor->getRack()->setPerformance(*perf);
            updatePanels();
        }
    };
    editor = nullptr; // must be set by the parent (PluginEditor)
    updatePanels();
}

void RackAccordionComponent::updatePanels()
{
    if (tabs.getNumTabs() > 0) {
        // Check if any ModuleTabComponent has an open file dialog
        for (const auto& tab : moduleTabs) {
            if (tab && tab->isFileDialogOpen()) {
                std::cout << "[WARNING] updatePanels() called while a Load Voice file dialog is open! This can cause a crash.\n";
            }
        }
    }

    tabs.clearTabs();
    moduleTabs.clear();
    if (!rack) return;
    const auto& modules = rack->getModules();
    int numModules = (int)modules.size();
    for (int i = 0; i < numModules; ++i)
    {
        auto tab = std::make_unique<ModuleTabComponent>(modules[i].get(), i, this);
        tab->updateFromModule(); // NEW: sync controls to module state
        tabs.addTab(juce::String(i + 1), juce::Colours::darkgrey, tab.get(), false);
        moduleTabs.push_back(std::move(tab));
    }
    resized();
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
ModuleTabComponent::ModuleTabComponent(FMRack::Module* modulePtr, int idx, RackAccordionComponent* parent)
    : module(modulePtr), moduleIndex(idx), parentAccordion(parent)
{
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

    // Wire up controls to update Performance, not module directly
    unisonVoicesSlider.onValueChange = [this] {
        auto* editor = parentAccordion ? parentAccordion->getEditor() : nullptr;
        auto* processor = editor ? editor->getProcessor() : nullptr;
        auto* perf = processor ? processor->getPerformance() : nullptr;
        if (perf) {
            auto& part = const_cast<FMRack::Performance::PartConfig&>(perf->getPartConfig(moduleIndex));
            part.unisonVoices = (uint8_t)unisonVoicesSlider.getValue();
            processor->getRack()->setPerformance(*perf);
            // parentAccordion->updatePanels(); // REMOVE to prevent endless loop
        }
    };
    unisonDetuneSlider.onValueChange = [this] {
        auto* editor = parentAccordion ? parentAccordion->getEditor() : nullptr;
        auto* processor = editor ? editor->getProcessor() : nullptr;
        auto* perf = processor ? processor->getPerformance() : nullptr;
        if (perf) {
            auto& part = const_cast<FMRack::Performance::PartConfig&>(perf->getPartConfig(moduleIndex));
            part.unisonDetune = (float)unisonDetuneSlider.getValue();
            processor->getRack()->setPerformance(*perf);
            // parentAccordion->updatePanels(); // REMOVE to prevent endless loop
        }
    };
    unisonPanSlider.onValueChange = [this] {
        auto* editor = parentAccordion ? parentAccordion->getEditor() : nullptr;
        auto* processor = editor ? editor->getProcessor() : nullptr;
        auto* perf = processor ? processor->getPerformance() : nullptr;
        if (perf) {
            auto& part = const_cast<FMRack::Performance::PartConfig&>(perf->getPartConfig(moduleIndex));
            part.unisonSpread = (float)unisonPanSlider.getValue();
            processor->getRack()->setPerformance(*perf);
            // parentAccordion->updatePanels(); // REMOVE to prevent endless loop
        }
    };
    // Load Voice button
    loadVoiceButton.setButtonText("Load Voice");
    loadVoiceButton.onClick = [this] {
        std::cout << "[Load Voice] File dialog about to open." << std::endl;
        fileChooser = std::make_unique<juce::FileChooser>(
            "Select a DX7 voice file", juce::File(), "*.syx;*.bin;*.dx7;*.dat;*.voice;*.vce;*.opm;*.ini;*");
        fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc) {
                std::cout << "[Load Voice] Entered launchAsync lambda." << std::endl;
                auto file = fc.getResult();
                if (!file.existsAsFile()) {
                    std::cout << "[Load Voice] No file selected or file does not exist." << std::endl;
                    fileChooser.reset();
                    return;
                }
                juce::FileInputStream stream(file);
                if (!stream.openedOk()) {
                    std::cout << "[Load Voice] Failed to open file: " << file.getFullPathName() << std::endl;
                    fileChooser.reset();
                    return;
                }
                std::vector<uint8_t> data;
                data.resize((size_t)stream.getTotalLength());
                auto bytesRead = stream.read(data.data(), (int)data.size());
                if (bytesRead != (int)data.size()) {
                    std::cout << "[Load Voice] Failed to read all bytes from file: " << file.getFullPathName() << std::endl;
                    fileChooser.reset();
                    return;
                }
                std::cout << "[Load Voice] Read " << data.size() << " bytes from file: " << file.getFullPathName() << std::endl;
                // Convert to Dexed format if needed
                if (data.size() == 128 || data.size() == 155) {
                    std::cout << "[Load Voice] Converting 128/155-byte voice to Dexed format (156 bytes)..." << std::endl;
                    data = VoiceData::convertDX7ToDexed(data);
                } else if (data.size() == 163 && data[0] == 0xF0 && data[1] == 0x43) {
                    std::cout << "[Load Voice] Detected 163-byte DX7 sysex, extracting voice data..." << std::endl;
                    // DX7 single-voice sysex: F0 43 00 00 01 1B ... 155 bytes ... F7
                    if (data.size() >= 163) {
                        std::vector<uint8_t> voiceData(data.begin() + 6, data.begin() + 161);
                        data = VoiceData::convertDX7ToDexed(voiceData);
                    }
                }
                if (data.size() < 156) {
                    std::cout << "[Load Voice] Error: Voice data is too short (" << data.size() << " bytes). Needs 156 bytes." << std::endl;
                    fileChooser.reset();
                    return;
                }
                auto* editor = parentAccordion ? parentAccordion->getEditor() : nullptr;
                auto* processor = editor ? editor->getProcessor() : nullptr;
                auto* perf = processor ? processor->getPerformance() : nullptr;
                if (perf) {
                    perf->setPartVoiceData(moduleIndex, data);
                    processor->getRack()->setPerformance(*perf);
                    std::cout << "[Load Voice] Voice loaded successfully for module " << (moduleIndex+1) << "." << std::endl;
                } else {
                    std::cout << "[Load Voice] Error: Could not get performance object." << std::endl;
                }
                fileChooser.reset(); // Release after use
            });
    };
    addAndMakeVisible(loadVoiceButton);
}

void ModuleTabComponent::updateFromModule()
{
    auto* editor = parentAccordion ? parentAccordion->getEditor() : nullptr;
    auto* processor = editor ? editor->getProcessor() : nullptr;
    auto* perf = processor ? processor->getPerformance() : nullptr;
    if (perf) {
        const auto& part = perf->getPartConfig(moduleIndex);
        unisonVoicesSlider.setValue(part.unisonVoices, juce::dontSendNotification);
        unisonDetuneSlider.setValue(part.unisonDetune, juce::dontSendNotification);
        unisonPanSlider.setValue(part.unisonSpread, juce::dontSendNotification);
    }
}

void ModuleTabComponent::resized()
{
    int x = 10, y = 10, w = 120, h = 24, gap = 6;
    unisonVoicesLabel.setBounds(x, y, w, h);
    unisonVoicesSlider.setBounds(x + w + 4, y, 80, h);
    y += h + gap;
    unisonDetuneLabel.setBounds(x, y, w, h);
    unisonDetuneSlider.setBounds(x + w + 4, y, 80, h);
    y += h + gap;
    unisonPanLabel.setBounds(x, y, w, h);
    unisonPanSlider.setBounds(x + w + 4, y, 80, h);
    y += h + gap;
    loadVoiceButton.setBounds(x, y, 120, h);
}
