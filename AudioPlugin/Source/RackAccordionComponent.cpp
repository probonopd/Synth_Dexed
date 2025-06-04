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
    tabs.resized(); // Ensure child tabs get resized after adding
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
        auto* perf = processor ? processor->getPerformance() : nullptr;
        if (perf) {
            auto& part = const_cast<FMRack::Performance::PartConfig&>(perf->getPartConfig(moduleIndex));
            uint8_t newValue = (uint8_t)unisonVoicesSlider.getValue();
            if (part.unisonVoices != newValue) {
                part.unisonVoices = newValue;
                processor->getRack()->setPerformance(*perf);
            }
        }
    };
    unisonDetuneSlider.onValueChange = [this] {
        auto* editor = parentAccordion ? parentAccordion->getEditor() : nullptr;
        auto* processor = editor ? editor->getProcessor() : nullptr;
        auto* perf = processor ? processor->getPerformance() : nullptr;
        if (perf) {
            auto& part = const_cast<FMRack::Performance::PartConfig&>(perf->getPartConfig(moduleIndex));
            float newValue = (float)unisonDetuneSlider.getValue();
            if (part.unisonDetune != newValue) {
                part.unisonDetune = newValue;
                processor->getRack()->setPerformance(*perf);
            }
        }
    };
    unisonPanSlider.onValueChange = [this] {
        auto* editor = parentAccordion ? parentAccordion->getEditor() : nullptr;
        auto* processor = editor ? editor->getProcessor() : nullptr;
        auto* perf = processor ? processor->getPerformance() : nullptr;
        if (perf) {
            auto& part = const_cast<FMRack::Performance::PartConfig&>(perf->getPartConfig(moduleIndex));
            float newValue = (float)unisonPanSlider.getValue();
            if (part.unisonSpread != newValue) {
                part.unisonSpread = newValue;
                processor->getRack()->setPerformance(*perf);
            }
        }
    };
    midiChannelSlider.onValueChange = [this] {
        auto* editor = parentAccordion ? parentAccordion->getEditor() : nullptr;
        auto* processor = editor ? editor->getProcessor() : nullptr;
        auto* perf = processor ? processor->getPerformance() : nullptr;
        if (perf) {
            auto& part = const_cast<FMRack::Performance::PartConfig&>(perf->getPartConfig(moduleIndex));
            uint8_t newValue = (uint8_t)midiChannelSlider.getValue();
            if (part.midiChannel != newValue) {
                part.midiChannel = newValue;
                processor->getRack()->setPerformance(*perf);
            }
        }
    };
    // Load Voice button
    loadVoiceButton.setButtonText("Load Voice");
    loadVoiceButton.onClick = [this] {
        auto dialog = std::make_unique<FileBrowserDialog>(
            "Select Voice File",
            "*.syx;*.bin;*.dx7;*.dat;*.voice;*.vce;*.opm;*.ini;*",
            juce::File(),
            FileBrowserDialog::DialogType::Voice);
        auto* dialogPtr = dialog.get();
        dialogPtr->showDialog(this,
            [this](const juce::File& file) {
                loadVoiceFile(file);
            },
            []() {
                // Cancel callback - nothing needed
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
    std::cout << "[Load Voice] Loading file: " << file.getFullPathName() << std::endl;
    
    juce::FileInputStream stream(file);
    if (!stream.openedOk()) {
        std::cout << "[Load Voice] Failed to open file: " << file.getFullPathName() << std::endl;
        return;
    }
    
    std::vector<uint8_t> data;
    data.resize((size_t)stream.getTotalLength());
    auto bytesRead = stream.read(data.data(), (int)data.size());
    if (bytesRead != (int)data.size()) {
        std::cout << "[Load Voice] Failed to read all bytes from file: " << file.getFullPathName() << std::endl;
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
}
