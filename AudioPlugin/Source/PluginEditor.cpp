#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "VoiceEditorPanel.h"
#include "VoiceEditorWindow.h"
#include "VoiceBrowserComponent.h"
#include <iostream> // For logging

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p), resizer(this, &constrainer)
{
    try {
        // Use JUCE logging system for VST3 compatibility
        juce::Logger::writeToLog("[PluginEditor] Constructor. this=" + juce::String::toHexString((juce::pointer_sized_int)this));
        processorRef.setEditorPointer(this); // Register this editor with the processor

        juce::ignoreUnused (processorRef);

        // Make the editor resizable
        setResizable(true, true);
        constrainer.setMinimumSize(800, 500);
        constrainer.setMaximumSize(1600, 1200);
        addAndMakeVisible(resizer);

        setSize (1000, 600);

        // Set up log text box
        logTextBox.setMultiLine(true);
        logTextBox.setReadOnly(true);
        logTextBox.setScrollbarsShown(true);
        logTextBox.setColour(juce::TextEditor::backgroundColourId, juce::Colours::black);
        logTextBox.setColour(juce::TextEditor::textColourId, juce::Colours::white);
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4996)
#endif
        logTextBox.setFont(juce::Font(12.0f));
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        addAndMakeVisible(logTextBox);
        juce::Logger::writeToLog("[PluginEditor] After addAndMakeVisible(logTextBox)");
        logTextBox.insertTextAtCaret("[PluginEditor] Constructor started\n");

        // Add the rack accordion GUI
        rackAccordion = std::make_unique<RackAccordionComponent>(&processorRef);
        rackAccordion->setEditor(this); // Set the editor pointer for child access
        addAndMakeVisible(rackAccordion.get());
        juce::Logger::writeToLog("[PluginEditor] After addAndMakeVisible(rackAccordion)");
        logTextBox.insertTextAtCaret("[PluginEditor] RackAccordion created and added\n");

        // Add the performance button back to the UI (keep in editor for now)
        addAndMakeVisible(loadPerformanceButton);
        loadPerformanceButton.onClick = [this]() {
            try {
                loadPerformanceButtonClicked();
            } catch (const std::exception& e) {
                juce::Logger::writeToLog("[PluginEditor] Exception in loadPerformanceButtonClicked: " + juce::String(e.what()));
                logTextBox.insertTextAtCaret("[PluginEditor] Exception: " + juce::String(e.what()) + "\n");
            } catch (...) {
                juce::Logger::writeToLog("[PluginEditor] Unknown exception in loadPerformanceButtonClicked");
                logTextBox.insertTextAtCaret("[PluginEditor] Unknown exception in loadPerformanceButtonClicked\n");
            }
        };        addAndMakeVisible(savePerformanceButton);
        savePerformanceButton.onClick = [this] { savePerformanceButtonClicked(); };

        addAndMakeVisible(addModuleButton);
        addModuleButton.onClick = [this]
        {
            int currentVal = 0;
            int minVal = 1;
            int maxVal = 16;
            if (rackAccordion)
                currentVal = rackAccordion->getNumModulesVT();
            else if (auto* param = processorRef.treeState.getParameter("numModules"))
                currentVal = static_cast<int>(param->getValue() * (param->getNormalisableRange().end - param->getNormalisableRange().start) + param->getNormalisableRange().start);
            if (auto* param = processorRef.treeState.getParameter("numModules"))
            {
                minVal = static_cast<int>(param->getNormalisableRange().start);
                maxVal = static_cast<int>(param->getNormalisableRange().end);
                if (currentVal < maxVal)
                {
                    param->setValueNotifyingHost(param->convertTo0to1(currentVal + 1));
                    numModulesChanged();
                }
            }
        };

        addAndMakeVisible(removeModuleButton);
        removeModuleButton.onClick = [this]
        {
            int currentVal = 0;
            int minVal = 1;
            if (rackAccordion)
                currentVal = rackAccordion->getNumModulesVT();
            else if (auto* param = processorRef.treeState.getParameter("numModules"))
                currentVal = static_cast<int>(param->getValue() * (param->getNormalisableRange().end - param->getNormalisableRange().start) + param->getNormalisableRange().start);
            if (auto* param = processorRef.treeState.getParameter("numModules"))
            {
                minVal = static_cast<int>(param->getNormalisableRange().start);
                if (currentVal > minVal)
                {
                    param->setValueNotifyingHost(param->convertTo0to1(currentVal - 1));
                    numModulesChanged();
                }
            }
        };

        addAndMakeVisible(effectsGroup);
        effectsGroup.setText("Global Effects");

        addAndMakeVisible(compressorEnableButton);
        compressorEnableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processorRef.treeState, "compressorEnable", compressorEnableButton);

        addAndMakeVisible(reverbEnableButton);
        reverbEnableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processorRef.treeState, "reverbEnable", reverbEnableButton);

        auto setupSlider = [this](juce::Slider& slider, juce::Label& label, const juce::String& labelText, const juce::String& paramID, std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment) {
            addAndMakeVisible(slider);
            slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
            addAndMakeVisible(label);
            label.setText(labelText, juce::dontSendNotification);
            label.attachToComponent(&slider, true);
            attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processorRef.treeState, paramID, slider);
        };

        setupSlider(reverbSizeSlider, reverbSizeLabel, "Size", "reverbSize", reverbSizeAttachment);
        setupSlider(reverbHighDampSlider, reverbHighDampLabel, "High Damp", "reverbHighDamp", reverbHighDampAttachment);
        setupSlider(reverbLowDampSlider, reverbLowDampLabel, "Low Damp", "reverbLowDamp", reverbLowDampAttachment);
        setupSlider(reverbLowPassSlider, reverbLowPassLabel, "Low Pass", "reverbLowPass", reverbLowPassAttachment);
        setupSlider(reverbDiffusionSlider, reverbDiffusionLabel, "Diffusion", "reverbDiffusion", reverbDiffusionAttachment);
        setupSlider(reverbLevelSlider, reverbLevelLabel, "Level", "reverbLevel", reverbLevelAttachment);

        resized(); // Force layout after construction
        juce::Logger::writeToLog("[PluginEditor] End of constructor");
        logTextBox.insertTextAtCaret("[PluginEditor] Constructor completed\n");

        // Force UI sync from processor's current performance after construction
        if (processorRef.getController() && processorRef.getController()->getPerformance()) {
            rackAccordion->updatePanels();
        }
    } catch (const std::exception& e) {
        juce::Logger::writeToLog("[PluginEditor] Exception in constructor: " + juce::String(e.what()));
        logTextBox.insertTextAtCaret("[PluginEditor] Exception: " + juce::String(e.what()) + "\n");
    } catch (...) {
        juce::Logger::writeToLog("[PluginEditor] Unknown exception in constructor");
        logTextBox.insertTextAtCaret("[PluginEditor] Unknown exception in constructor\n");
    }
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
    try {
        processorRef.setEditorPointer(nullptr);
        // --- CRASH PREVENTION: Ensure all child windows are closed before main editor is destroyed ---
        if (voiceEditorWindow) {
            voiceEditorWindow->setVisible(false);
            voiceEditorWindow->setContentOwned(nullptr, false);
            voiceEditorWindow.reset();
        }
        // Close any open FileBrowserDialog windows in all module tabs
        if (rackAccordion) {
            for (const auto& tab : rackAccordion->getModuleTabs()) {
                if (tab && tab->isFileDialogOpen()) {
                    tab->closeFileDialog();
                }
            }
            rackAccordion.reset();
        }
    } catch (const std::exception& e) {
        juce::Logger::writeToLog("[PluginEditor] Exception in destructor: " + juce::String(e.what()));
    } catch (...) {
        juce::Logger::writeToLog("[PluginEditor] Unknown exception in destructor");
    }
}

//==============================================================================
void AudioPluginAudioProcessorEditor::paint (juce::Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void AudioPluginAudioProcessorEditor::resized()
{
    juce::Logger::writeToLog("[PluginEditor] resized() called");
    auto bounds = getLocalBounds();
    resizer.setBounds (bounds.getRight() - 16, bounds.getBottom() - 16, 16, 16);    auto topArea = bounds.removeFromTop(40);
    loadPerformanceButton.setBounds(topArea.removeFromLeft(150).reduced(5));
    savePerformanceButton.setBounds(topArea.removeFromLeft(150).reduced(5));
    removeModuleButton.setBounds(topArea.removeFromRight(30).reduced(2));
    addModuleButton.setBounds(topArea.removeFromRight(30).reduced(2));

    auto rackWidth = bounds.getWidth() * 2 / 3;
    auto effectsWidth = bounds.getWidth() - rackWidth;

    if (rackAccordion)
        rackAccordion->setBounds(bounds.removeFromLeft(rackWidth));

    effectsGroup.setBounds(bounds.reduced(5));
    auto effectsArea = effectsGroup.getBounds().reduced(15);
    effectsArea.removeFromTop(10); // Space for group title

    // Arrange compressor and reverb enable buttons in a row
    auto buttonRow = effectsArea.removeFromTop(30);
    compressorEnableButton.setBounds(buttonRow.removeFromLeft(100).reduced(5));
    reverbEnableButton.setBounds(buttonRow.removeFromLeft(100).reduced(5));

    // Arrange the effect sliders in two rows, three per row, matching the style of the other sliders
    int sliderW = 55;
    int sliderH = 100;
    int labelH = 20;
    int labelGap = 4;
    int sliderSpacingX = 20;
    int sliderSpacingY = 10;
    int startX = effectsArea.getX() + 10;
    int startY = effectsArea.getY();

    // Use setupSlider for all global effect sliders (ensures consistent style)
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dummy;
    setupSlider(reverbSizeSlider, reverbSizeLabel, "Size", "reverbSize", dummy);
    setupSlider(reverbHighDampSlider, reverbHighDampLabel, "High Damp", "reverbHighDamp", dummy);
    setupSlider(reverbLowDampSlider, reverbLowDampLabel, "Low Damp", "reverbLowDamp", dummy);
    setupSlider(reverbLowPassSlider, reverbLowPassLabel, "Low Pass", "reverbLowPass", dummy);
    setupSlider(reverbDiffusionSlider, reverbDiffusionLabel, "Diffusion", "reverbDiffusion", dummy);
    setupSlider(reverbLevelSlider, reverbLevelLabel, "Level", "reverbLevel", dummy);

    // First row
    int x = startX;
    int y = startY;
    reverbSizeSlider.setBounds(x, y, sliderW, sliderH);
    reverbSizeLabel.setBounds(x, y + sliderH + labelGap, sliderW, labelH);
    x += sliderW + sliderSpacingX;
    reverbHighDampSlider.setBounds(x, y, sliderW, sliderH);
    reverbHighDampLabel.setBounds(x, y + sliderH + labelGap, sliderW, labelH);
    x += sliderW + sliderSpacingX;
    reverbLowDampSlider.setBounds(x, y, sliderW, sliderH);
    reverbLowDampLabel.setBounds(x, y + sliderH + labelGap, sliderW, labelH);

    // Second row
    x = startX;
    y += sliderH + labelH + sliderSpacingY + labelGap;
    reverbLowPassSlider.setBounds(x, y, sliderW, sliderH);
    reverbLowPassLabel.setBounds(x, y + sliderH + labelGap, sliderW, labelH);
    x += sliderW + sliderSpacingX;
    reverbDiffusionSlider.setBounds(x, y, sliderW, sliderH);
    reverbDiffusionLabel.setBounds(x, y + sliderH + labelGap, sliderW, labelH);
    x += sliderW + sliderSpacingX;
    reverbLevelSlider.setBounds(x, y, sliderW, sliderH);
    reverbLevelLabel.setBounds(x, y + sliderH + labelGap, sliderW, labelH);

    if (voiceEditorWindow)
    {
        voiceEditorWindow->setBounds(100, 100, 800, 600);
    }

    juce::Logger::writeToLog("[PluginEditor] resized() end");
}

void AudioPluginAudioProcessorEditor::appendLogMessage(const juce::String& message)
{
    logTextBox.moveCaretToEnd();
    logTextBox.insertTextAtCaret(message + "\n");
}

void AudioPluginAudioProcessorEditor::numModulesChanged() {
    if (rackAccordion)
    {
        if (auto* param = processorRef.treeState.getParameter("numModules"))
        {
            int numModules = static_cast<int>(param->getValue() * (param->getNormalisableRange().end - param->getNormalisableRange().start) + param->getNormalisableRange().start);
            rackAccordion->setNumModulesVT(numModules);
            rackAccordion->updatePanels(); // Ensure UI updates to reflect the new number of modules/tabs
        }
    }
}

void AudioPluginAudioProcessorEditor::loadPerformanceButtonClicked()
{
    auto dialog = std::make_unique<FileBrowserDialog>(
        "Select Performance File",
        "*.ini",
        juce::File(),
        FileBrowserDialog::DialogType::Performance);
    auto* dialogPtr = dialog.get();
    dialogPtr->showDialog(this,
        [this](const juce::File& file) {
            if (file.getFileExtension().equalsIgnoreCase(".ini"))
            {
                appendLogMessage("Loading performance: " + file.getFullPathName());
                bool loaded = false;
                try {
                    loaded = processorRef.loadPerformanceFile(file.getFullPathName());
                } catch (const std::exception& e) {
                    appendLogMessage("Exception loading performance: " + juce::String(e.what()));
                    juce::Logger::writeToLog("[PluginEditor] Exception in loadPerformanceFile: " + juce::String(e.what()));
                } catch (...) {
                    appendLogMessage("Unknown exception loading performance file.");
                    juce::Logger::writeToLog("[PluginEditor] Unknown exception in loadPerformanceFile");
                }
                if (loaded) {
                    appendLogMessage("Performance loaded successfully.");
                } else {
                    appendLogMessage("Failed to load performance file.");
                }
                // Defensive: updatePanels only if rackAccordion is valid and no file dialogs are open
                if (rackAccordion) {
                    bool canUpdate = true;
                    try {
                        // Check for open file dialogs in module tabs (use public getter)
                        for (const auto& tab : rackAccordion->getModuleTabs()) {
                            if (tab && tab->isFileDialogOpen()) {
                                appendLogMessage("[WARNING] Skipping updatePanels: a Load Voice file dialog is open.");
                                canUpdate = false;
                                break;
                            }
                        }
                    } catch (...) {
                        appendLogMessage("[PluginEditor] Exception checking file dialogs in moduleTabs.");
                        canUpdate = false;
                    }
                    if (canUpdate) {
                        try {
                            rackAccordion->updatePanels();
                        } catch (const std::exception& e) {
                            appendLogMessage("Exception in rackAccordion->updatePanels: " + juce::String(e.what()));
                            juce::Logger::writeToLog("[PluginEditor] Exception in updatePanels: " + juce::String(e.what()));
                        } catch (...) {
                            appendLogMessage("Unknown exception in rackAccordion->updatePanels.");
                            juce::Logger::writeToLog("[PluginEditor] Unknown exception in updatePanels");
                        }
                    }
                }
            }
        },
        []() {
            // Cancel callback - nothing needed
        });
    dialog.release();
}

void AudioPluginAudioProcessorEditor::showVoiceEditorPanel(int moduleIndex) {
    juce::Logger::writeToLog("[PluginEditor] showVoiceEditorPanel(" + juce::String(moduleIndex) + ") called");
    // Always create a new VoiceEditorPanel for the window, owned by the window
    auto newVoiceEditorPanel = std::make_unique<VoiceEditorPanel>();
    newVoiceEditorPanel->setController(processorRef.getController());
    newVoiceEditorPanel->setModuleIndex(moduleIndex);
    newVoiceEditorPanel->syncAllOperatorSlidersWithDexed();
    if (!voiceEditorWindow) {
        juce::Logger::writeToLog("[PluginEditor] Creating voice editor window");
        voiceEditorWindow = std::make_unique<VoiceEditorWindow>(
            "Voice Editor", 
            juce::Colours::darkgrey, 
            juce::DocumentWindow::allButtons,
            this // Pass the editor instance to the window
        );
        // Give ownership of the panel to the window (window will delete it)
        voiceEditorWindow->setContentOwned(newVoiceEditorPanel.release(), true);
        voiceEditorWindow->setUsingNativeTitleBar(true);
        voiceEditorWindow->centreWithSize(1000, 600);
        voiceEditorWindow->setResizable(true, false);
    } else {
        // If window already exists, replace its content with a new panel
        voiceEditorWindow->setContentOwned(newVoiceEditorPanel.release(), true);
    }
    juce::Logger::writeToLog("[PluginEditor] Making voice editor window visible");
    voiceEditorWindow->setVisible(true);
    voiceEditorWindow->toFront(true);
    // Do not keep a unique_ptr to the panel in the editor anymore
    voiceEditorPanel.reset();
}

void AudioPluginAudioProcessorEditor::savePerformanceButtonClicked()
{
    auto dialog = std::make_unique<FileBrowserDialog>(
        "Save Performance File",
        "*.ini",
        juce::File(),
        FileBrowserDialog::DialogType::Performance);
    auto* dialogPtr = dialog.get();
    dialogPtr->showDialog(this,
        [this](const juce::File& file) {
            juce::String path = file.getFullPathName();
            if (!path.endsWithIgnoreCase(".ini"))
                path += ".ini";
            appendLogMessage("Saving performance: " + path);
            bool saved = false;
            try {
                saved = processorRef.savePerformanceFile(path);
            } catch (const std::exception& e) {
                appendLogMessage("Exception saving performance: " + juce::String(e.what()));
                juce::Logger::writeToLog("[PluginEditor] Exception in savePerformanceFile: " + juce::String(e.what()));
            } catch (...) {
                appendLogMessage("Unknown exception saving performance file.");
                juce::Logger::writeToLog("[PluginEditor] Unknown exception in savePerformanceFile");
            }
            if (saved) {
                appendLogMessage("Performance saved successfully.");
            } else {
                appendLogMessage("Failed to save performance.");
            }
        },
        []() {
            // Cancel callback - nothing needed
        });
    dialog.release();
}

void AudioPluginAudioProcessorEditor::showVoiceBrowser(int moduleIndex)
{
    if (!voiceBrowser)
        voiceBrowser = std::make_unique<VoiceBrowserComponent>();
    
    // Show as a dialog window (non-modal) with escape key support
    auto* dialog = new juce::DialogWindow("DX7 Voice Browser", juce::Colours::lightgrey, true, true);
    
    // Set up close callback for ESC key and window X button
    voiceBrowser->onClose = [dialog]() {
        dialog->setVisible(false);
        delete dialog;
    };
    
    // Set up voice loading callback using the same method as the "Open" button
    voiceBrowser->onVoiceLoaded = [this, moduleIndex](const std::vector<uint8_t>& voiceData) {
        // Use the existing voice loading mechanism from ModuleTabComponent::loadVoiceFile
        auto* controller = processorRef.getController();
        if (controller && moduleIndex >= 0 && moduleIndex < 16) {
            // Load the voice data into the specified module using the existing method
            controller->setPartVoiceData(moduleIndex, voiceData);
            juce::String voiceName = VoiceData::extractDX7VoiceName(voiceData);
            appendLogMessage("Voice loaded from browser: " + voiceName);
            
            // Update the voice editor panel if it's open for this module
            if (auto* voiceEditor = getVoiceEditorPanel()) {
                if (voiceEditor->getModuleIndex() == moduleIndex) {
                    // Refresh the voice editor to show the new voice data
                    showVoiceEditorPanel(moduleIndex);
                }
            }
        } else {
            appendLogMessage("Failed to load voice: invalid module index or controller");
        }
    };
      // Make ESC key work and allow window X button to close
    dialog->setUsingNativeTitleBar(true);
    
    // Don't release ownership - keep the voiceBrowser so we can reuse it
    dialog->setContentNonOwned(voiceBrowser.get(), true);
    dialog->centreWithSize(600, 400);
    dialog->setVisible(true);
}

