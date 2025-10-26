#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "VoiceEditorPanel.h"
#include "VoiceEditorWindow.h"
#include "VoiceBrowserComponent.h"
#include <iostream> // For logging

namespace
{
    struct SliderLabelPair
    {
        juce::Slider* slider = nullptr;
        juce::Label* label = nullptr;
    };

    constexpr int kRotaryLabelHeight = 18;

    void layoutRotarySliderGrid(const juce::Rectangle<int>& area,
                                const std::initializer_list<SliderLabelPair>& controls,
                                int columns,
                                int sliderSize,
                                int rowGap,
                                int columnGap)
    {
        if (controls.size() == 0 || columns <= 0)
            return;

        const int totalControls = static_cast<int>(controls.size());
        const int rows = (totalControls + columns - 1) / columns;

        const int totalWidth = columns * sliderSize + (columns - 1) * columnGap;
        const int totalHeight = rows * (sliderSize + kRotaryLabelHeight) + (rows - 1) * rowGap;

        const int startX = area.getX() + juce::jmax(0, (area.getWidth() - totalWidth) / 2);
        const int startY = area.getY() + juce::jmax(0, (area.getHeight() - totalHeight) / 2);

        int index = 0;
        for (auto control : controls)
        {
            const int row = index / columns;
            const int column = index % columns;

            const int x = startX + column * (sliderSize + columnGap);
            const int y = startY + row * (sliderSize + kRotaryLabelHeight + rowGap);

            if (control.slider != nullptr)
                control.slider->setBounds(x, y, sliderSize, sliderSize);

            if (control.label != nullptr)
            {
                control.label->setJustificationType(juce::Justification::centred);
                control.label->setBounds(x, y + sliderSize, sliderSize, kRotaryLabelHeight);
            }

            ++index;
        }
    }
}

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
    const int resizerSize = 16;
    resizer.setBounds(getWidth() - resizerSize, getHeight() - resizerSize, resizerSize, resizerSize);

    const int outerMargin = 12;
    const int sectionGap = 12;
    const int controlGap = 8;
    const int topButtonHeight = 30;
    const int logHeight = juce::jlimit(100, 180, getHeight() / 4);

    auto layoutBounds = getLocalBounds().reduced(outerMargin);

    auto logArea = layoutBounds.removeFromBottom(logHeight);
    logArea = logArea.withTrimmedRight(resizerSize).reduced(0, 4);
    logTextBox.setBounds(logArea);

    layoutBounds.removeFromBottom(sectionGap);

    auto topRow = layoutBounds.removeFromTop(topButtonHeight);
    layoutBounds.removeFromTop(sectionGap);

    const int largeButtonWidth = juce::jlimit(140, 200, topRow.getWidth() / 4 * 2);
    const int smallButtonWidth = 32;

    auto alignButton = [&](juce::Rectangle<int> area)
    {
        return juce::Rectangle<int>(area.getX(), area.getCentreY() - topButtonHeight / 2, area.getWidth(), topButtonHeight);
    };

    auto loadBounds = topRow.removeFromLeft(largeButtonWidth);
    loadPerformanceButton.setBounds(alignButton(loadBounds));
    topRow.removeFromLeft(controlGap);
    auto saveBounds = topRow.removeFromLeft(largeButtonWidth);
    savePerformanceButton.setBounds(alignButton(saveBounds));

    auto addBounds = topRow.removeFromRight(smallButtonWidth);
    addModuleButton.setBounds(alignButton(addBounds));
    topRow.removeFromRight(controlGap);
    auto removeBounds = topRow.removeFromRight(smallButtonWidth);
    removeModuleButton.setBounds(alignButton(removeBounds));

    const int spacing = 14;
    int availableWidth = layoutBounds.getWidth();
    int effectsWidth = juce::jmax(280, availableWidth / 3);
    int rackWidth = availableWidth - effectsWidth - spacing;

    if (rackWidth < 420)
    {
        rackWidth = juce::jmax(420, availableWidth - effectsWidth - spacing);
        effectsWidth = availableWidth - rackWidth - spacing;
    }

    rackWidth = juce::jmax(0, rackWidth);
    effectsWidth = juce::jmax(0, effectsWidth);

    auto rackArea = layoutBounds.removeFromLeft(rackWidth);
    if (rackAccordion)
        rackAccordion->setBounds(rackArea);

    layoutBounds.removeFromLeft(spacing);

    auto effectsArea = layoutBounds;
    effectsGroup.setBounds(effectsArea);

    const int groupPadding = 12;
    const int groupLabelOffset = 24;
    const int toggleHeight = 26;
    const int rowGap = 18;
    const int columnGapRotary = 16;

    auto effectsContent = effectsArea.reduced(groupPadding).withTrimmedTop(groupLabelOffset);

    auto toggleRow = effectsContent.removeFromTop(toggleHeight);
    int toggleWidth = juce::jmax(70, (toggleRow.getWidth() - columnGapRotary) / 2);
    if (toggleWidth * 2 + columnGapRotary > toggleRow.getWidth())
        toggleWidth = juce::jmax(40, (toggleRow.getWidth() - columnGapRotary) / 2);
    const int toggleTotalWidth = toggleWidth * 2 + columnGapRotary;
    const int toggleStartX = toggleRow.getX() + juce::jmax(0, (toggleRow.getWidth() - toggleTotalWidth) / 2);

    juce::Rectangle<int> compressorBounds(toggleStartX, toggleRow.getCentreY() - toggleHeight / 2, toggleWidth, toggleHeight);
    juce::Rectangle<int> reverbBounds = compressorBounds.translated(toggleWidth + columnGapRotary, 0);

    compressorEnableButton.setBounds(compressorBounds);
    reverbEnableButton.setBounds(reverbBounds);

    effectsContent.removeFromTop(rowGap);

    const int rotaryColumns = 3;
    const int rotaryRows = 2;
    const int availableGridWidth = juce::jmax(0, effectsContent.getWidth() - columnGapRotary * (rotaryColumns - 1));
    const int availableGridHeight = juce::jmax(0, effectsContent.getHeight() - rowGap * (rotaryRows - 1) - kRotaryLabelHeight * rotaryRows);
    const int widthLimitedSize = rotaryColumns > 0 ? availableGridWidth / rotaryColumns : availableGridWidth;
    const int heightLimitedSize = rotaryRows > 0 ? availableGridHeight / rotaryRows : availableGridHeight;
    int rotarySize = juce::jmin(110, juce::jmin(widthLimitedSize, heightLimitedSize));
    if (rotarySize <= 0)
        rotarySize = 70;

    layoutRotarySliderGrid(effectsContent,
                           {
                               { &reverbSizeSlider, &reverbSizeLabel },
                               { &reverbHighDampSlider, &reverbHighDampLabel },
                               { &reverbLowDampSlider, &reverbLowDampLabel },
                               { &reverbLowPassSlider, &reverbLowPassLabel },
                               { &reverbDiffusionSlider, &reverbDiffusionLabel },
                               { &reverbLevelSlider, &reverbLevelLabel }
                           },
                           rotaryColumns,
                           rotarySize,
                           rowGap,
                           columnGapRotary);

    if (voiceEditorWindow)
        voiceEditorWindow->setBounds(100, 100, 800, 600);

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
    
    // Create a custom dialog window that properly handles close events
    class VoiceBrowserDialog : public juce::DialogWindow
    {
    public:
        VoiceBrowserDialog(const juce::String& name, juce::Colour backgroundColour, bool escapeKeyTriggersCloseButton, bool addToDesktop)
            : juce::DialogWindow(name, backgroundColour, escapeKeyTriggersCloseButton, addToDesktop)
        {
        }
        
        void closeButtonPressed() override
        {
            setVisible(false);
            delete this;
        }
    };
    
    auto* dialog = new VoiceBrowserDialog("DX7 Voice Browser", juce::Colours::lightgrey, true, true);
    
    // Set up close callback for ESC key
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

