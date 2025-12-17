#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "VoiceEditorPanel.h"
#include "VoiceEditorWindow.h"
#include "VoiceBrowserComponent.h"
#include "BinaryData.h"
#include <iostream> // For logging
#include "FMRackVerticalSlider.h"

namespace
{
    struct SliderLabelPair
    {
        FMRackVerticalSlider* slider = nullptr;
        juce::Label* label = nullptr;
    };

    constexpr int kRotaryLabelHeight = 18;
    constexpr int kVerticalSliderWidth = 40;
    constexpr int kVerticalSliderLabelHeight = 16;

    // layoutRotarySliderGrid removed — rotary sliders are no longer used. Kept SliderLabelPair
    // and layoutVerticalSliderGrid for vertical-only sliders.

    void layoutVerticalSliderGrid(const juce::Rectangle<int>& area,
                                  const std::initializer_list<SliderLabelPair>& controls,
                                  int columns,
                                  int sliderHeight,
                                  int rowGap,
                                  int columnGap)
    {
        if (controls.size() == 0 || columns <= 0)
            return;

        const int totalControls = static_cast<int>(controls.size());
        const int rows = (totalControls + columns - 1) / columns;

        const int totalWidth = columns * kVerticalSliderWidth + (columns - 1) * columnGap;
        const int totalHeight = rows * (sliderHeight + kVerticalSliderLabelHeight) + (rows - 1) * rowGap;

        const int startX = area.getX() + juce::jmax(0, (area.getWidth() - totalWidth) / 2);
        const int startY = area.getY() + juce::jmax(0, (area.getHeight() - totalHeight) / 2);

        int index = 0;
        for (auto control : controls)
        {
            const int row = index / columns;
            const int column = index % columns;

            const int x = startX + column * (kVerticalSliderWidth + columnGap);
            const int y = startY + row * (sliderHeight + kVerticalSliderLabelHeight + rowGap);

            if (control.slider != nullptr)
                control.slider->setBounds(x, y, kVerticalSliderWidth, sliderHeight);

            if (control.label != nullptr)
            {
                control.label->setJustificationType(juce::Justification::centred);
                control.label->setBounds(x, y + sliderHeight, kVerticalSliderWidth, kVerticalSliderLabelHeight);
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

        // Apply DX7-inspired look and feel


        // Make the editor resizable
        setResizable(true, true);
        constrainer.setMinimumSize(1000, 380);
        constrainer.setMaximumSize(1800, 900);
        addAndMakeVisible(resizer);

        setSize (1100, 420);

        // Set up log text box (hidden by default for cleaner UI)
        logTextBox.setMultiLine(true);
        logTextBox.setReadOnly(true);
        logTextBox.setScrollbarsShown(true);
        logTextBox.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff222222));
        logTextBox.setColour(juce::TextEditor::textColourId, juce::Colours::white);
        logTextBox.setFont(juce::Font(juce::FontOptions(12.0f)));
        // Log box is hidden by default - use Options menu to show it
        logTextBox.setVisible(false);
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
                    param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(currentVal + 1)));
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
                    param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(currentVal - 1)));
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

        auto setupSlider = [this](FMRackVerticalSlider& slider, juce::Label& label, const juce::String& labelText, const juce::String& paramID, std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment) {
            addAndMakeVisible(slider);
            addAndMakeVisible(label);
            label.setText(labelText, juce::dontSendNotification);
            label.setJustificationType(juce::Justification::centred);
            attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processorRef.treeState, paramID, slider);
        };

    setupSlider(reverbSizeSlider, reverbSizeLabel, "Size", "reverbSize", reverbSizeAttachment);
    setupSlider(reverbHighDampSlider, reverbHighDampLabel, "HiDamp", "reverbHighDamp", reverbHighDampAttachment);
    setupSlider(reverbLowDampSlider, reverbLowDampLabel, "LoDamp", "reverbLowDamp", reverbLowDampAttachment);
    setupSlider(reverbLowPassSlider, reverbLowPassLabel, "LoPass", "reverbLowPass", reverbLowPassAttachment);
    setupSlider(reverbDiffusionSlider, reverbDiffusionLabel, "Diff", "reverbDiffusion", reverbDiffusionAttachment);
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
        // Remove look and feel before destruction

        
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
        // Singleton dialogs are owned here; no special handling needed beyond normal destruction.
    } catch (const std::exception& e) {
        juce::Logger::writeToLog("[PluginEditor] Exception in destructor: " + juce::String(e.what()));
    } catch (...) {
        juce::Logger::writeToLog("[PluginEditor] Unknown exception in destructor");
    }
}

//==============================================================================
void AudioPluginAudioProcessorEditor::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    
    // Draw dark charcoal FM synth-inspired background
    // Main panel with subtle vertical gradient (like brushed aluminum)
    juce::ColourGradient panelGradient(
        juce::Colour(0xFF2A2A2A),  // Slightly lighter at top
        0.0f, 0.0f,
        juce::Colour(0xFF1A1A1A),  // Darker at bottom
        0.0f, static_cast<float>(bounds.getHeight()),
        false);
    panelGradient.addColour(0.02, juce::Colour(0xFF353535));  // Top bevel highlight
    panelGradient.addColour(0.98, juce::Colour(0xFF151515));  // Bottom shadow
    
    g.setGradientFill(panelGradient);
    g.fillAll();
    
    // Draw subtle top edge highlight (like panel edge catch light)
    g.setColour(juce::Colour(0xFF404040));
    g.fillRect(0, 0, bounds.getWidth(), 2);
    g.setColour(juce::Colour(0xFF4A4A4A));
    g.fillRect(0, 0, bounds.getWidth(), 1);
    
    // Draw subtle bottom edge shadow
    g.setColour(juce::Colour(0xFF0A0A0A));
    g.fillRect(0, bounds.getHeight() - 2, bounds.getWidth(), 2);
}

void AudioPluginAudioProcessorEditor::resized()
{
    juce::Logger::writeToLog("[PluginEditor] resized() called");
    const int resizerSize = 16;
    resizer.setBounds(getWidth() - resizerSize, getHeight() - resizerSize, resizerSize, resizerSize);

    // Layout constants - reduced padding
    const int outerMargin = 8;
    const int sectionGap = 6;
    const int controlGap = 4;
    const int topButtonHeight = 24;
    const int headerHeight = 8; // Minimal header space (no logo text)

    // Main content area
    auto layoutBounds = getLocalBounds().reduced(outerMargin);
    layoutBounds.removeFromTop(headerHeight); // Minimal header space

    // Only show log box if visible (can be toggled via Options menu)
    if (logTextBox.isVisible())
    {
        const int logHeight = juce::jlimit(80, 120, getHeight() / 5);
        auto logArea = layoutBounds.removeFromBottom(logHeight);
        logArea = logArea.withTrimmedRight(resizerSize).reduced(0, 2);
        logTextBox.setBounds(logArea);
        layoutBounds.removeFromBottom(sectionGap);
    }

    auto topRow = layoutBounds.removeFromTop(topButtonHeight);
    layoutBounds.removeFromTop(sectionGap);

    const int largeButtonWidth = juce::jlimit(120, 160, topRow.getWidth() / 5);
    const int smallButtonWidth = 28;

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

    const int spacing = 10;
    int availableWidth = layoutBounds.getWidth();
    // Global Effects panel - compact width
    int effectsWidth = juce::jlimit(160, 200, availableWidth / 5);
    int rackWidth = availableWidth - effectsWidth - spacing;

    if (rackWidth < 700)
    {
        rackWidth = juce::jmax(700, availableWidth - effectsWidth - spacing);
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

    const int groupPadding = 6;
    const int groupLabelOffset = 18;
    const int toggleHeight = 20;
    const int rowGap = 6;
    const int columnGapRotary = 4;

    auto effectsContent = effectsArea.reduced(groupPadding).withTrimmedTop(groupLabelOffset);

    // Toggles row - stacked vertically for narrow panel
    auto toggleRow = effectsContent.removeFromTop(toggleHeight);
    compressorEnableButton.setBounds(toggleRow.removeFromLeft(toggleRow.getWidth() / 2 - 2));
    toggleRow.removeFromLeft(4);
    reverbEnableButton.setBounds(toggleRow);

    effectsContent.removeFromTop(rowGap);

    // Calculate slider layout - 3 columns x 2 rows for compact display
    const int verticalColumns = 3;
    int verticalSliderHeight = juce::jlimit(40, 70, (effectsContent.getHeight() - rowGap) / 2 - kVerticalSliderLabelHeight);
    if (verticalSliderHeight <= 0)
        verticalSliderHeight = 40;

    layoutVerticalSliderGrid(effectsContent,
                             {
                                 { &reverbSizeSlider, &reverbSizeLabel },
                                 { &reverbLevelSlider, &reverbLevelLabel },
                                 { &reverbDiffusionSlider, &reverbDiffusionLabel },
                                 { &reverbHighDampSlider, &reverbHighDampLabel },
                                 { &reverbLowDampSlider, &reverbLowDampLabel },
                                 { &reverbLowPassSlider, &reverbLowPassLabel }
                             },
                             verticalColumns,
                             verticalSliderHeight,
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
    if (!performanceFileDialog)
    {
        performanceFileDialog = std::make_unique<FileBrowserDialog>(
            "Select Performance File",
            "*.ini",
            juce::File(),
            FileBrowserDialog::DialogType::Performance);
    }

    auto* dialogPtr = performanceFileDialog.get();
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
}

void AudioPluginAudioProcessorEditor::showVoiceEditorPanel(int moduleIndex) {
    juce::Logger::writeToLog("[PluginEditor] showVoiceEditorPanel(" + juce::String(moduleIndex) + ") called");
    // Create the window once, then reuse it. Replacing DocumentWindow content repeatedly can
    // leave the window in a blank state on some platforms (especially when the old content is
    // deleted while still processing events). Instead, keep one VoiceEditorPanel instance and
    // retarget it to the requested module.
    if (!voiceEditorWindow) {
        juce::Logger::writeToLog("[PluginEditor] Creating voice editor window");
        voiceEditorWindow = std::make_unique<VoiceEditorWindow>(
            "Voice Editor",
            juce::Colours::darkgrey,
            juce::DocumentWindow::allButtons,
            this
        );

        voiceEditorPanel = std::make_unique<VoiceEditorPanel>();
        voiceEditorPanel->setController(processorRef.getController());

        // Window owns the content component. We'll keep a non-owning pointer via unique_ptr for
        // convenience, but release ownership to the window.
        voiceEditorWindow->setContentOwned(voiceEditorPanel.release(), true);
        voiceEditorWindow->setUsingNativeTitleBar(true);
        voiceEditorWindow->centreWithSize(1000, 680);
        voiceEditorWindow->setResizable(true, false);
    }

    if (auto* panel = dynamic_cast<VoiceEditorPanel*>(voiceEditorWindow->getContentComponent())) {
        panel->setController(processorRef.getController());
        panel->setModuleIndex(moduleIndex);
        panel->syncAllOperatorSlidersWithDexed();
        panel->resized();
        panel->repaint();
    }
    juce::Logger::writeToLog("[PluginEditor] Making voice editor window visible");
    voiceEditorWindow->setVisible(true);
    voiceEditorWindow->toFront(true);
    // Note: we keep the content owned by the window. The editor doesn't own the panel.
}

void AudioPluginAudioProcessorEditor::savePerformanceButtonClicked()
{
    if (!performanceFileDialog)
    {
        performanceFileDialog = std::make_unique<FileBrowserDialog>(
            "Save Performance File",
            "*.ini",
            juce::File(),
            FileBrowserDialog::DialogType::Performance);
    }

    auto* dialogPtr = performanceFileDialog.get();
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
        });
}

void AudioPluginAudioProcessorEditor::showVoiceBrowser(int /*moduleIndex*/)
{
    if (!voiceBrowser)
        voiceBrowser = std::make_unique<VoiceBrowserComponent>();

    // Create singleton window if needed
    if (!voiceBrowserWindow)
    {
        class VoiceBrowserDialogWindow : public juce::DialogWindow
        {
        public:
            using juce::DialogWindow::DialogWindow;

            void closeButtonPressed() override
            {
                setVisible(false);
            }
        };

        voiceBrowserWindow = std::make_unique<VoiceBrowserDialogWindow>(
            "DX7 Voice Browser",
            juce::Colours::lightgrey,
            true);

        voiceBrowserWindow->setUsingNativeTitleBar(true);
        voiceBrowserWindow->setResizable(true, false);
        voiceBrowserWindow->setResizeLimits(500, 350, 1200, 800);
        voiceBrowserWindow->setContentNonOwned(voiceBrowser.get(), true);
        voiceBrowserWindow->centreWithSize(600, 400);
    }

    // ESC should just hide the singleton window
    voiceBrowser->onClose = [this]() {
        if (voiceBrowserWindow)
            voiceBrowserWindow->setVisible(false);
    };
    
    // Set up voice loading callback using the same method as the "Open" button
    voiceBrowser->onVoiceLoaded = [this](const std::vector<uint8_t>& voiceData) {
        // Get the currently visible tab index at the time of loading
        int currentModuleIndex = 0; // Default to module 0
        if (rackAccordion) {
            currentModuleIndex = rackAccordion->getCurrentTabIndex();
            if (currentModuleIndex < 0 || currentModuleIndex >= 16) {
                currentModuleIndex = 0; // Fallback
            }
        }
        
        // Use the existing voice loading mechanism from ModuleTabComponent::loadVoiceFile
        auto* controller = processorRef.getController();
        if (controller && currentModuleIndex >= 0 && currentModuleIndex < 16) {
            // Load the voice data into the specified module using the existing method
            controller->setPartVoiceData(currentModuleIndex, voiceData);
            juce::String voiceName = VoiceData::extractDX7VoiceName(voiceData);
            appendLogMessage("Voice loaded from browser: " + voiceName + " into module " + juce::String(currentModuleIndex + 1));
            
            // Update the voice editor panel if it's open for this module
            if (auto* voiceEditor = getVoiceEditorPanel()) {
                if (voiceEditor->getModuleIndex() == currentModuleIndex) {
                    // Refresh the voice editor to show the new voice data
                    showVoiceEditorPanel(currentModuleIndex);
                }
            }
        } else {
            appendLogMessage("Failed to load voice: invalid module index or controller");
        }
    };
    
    voiceBrowserWindow->setVisible(true);
    voiceBrowserWindow->toFront(true);
}

