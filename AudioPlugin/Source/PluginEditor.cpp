#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "VoiceEditorPanel.h"
#include "VoiceEditorWindow.h"
#include "VoiceBrowserComponent.h"
#include "MidiBrowserComponent.h"
#include "MidiDropZone.h"
#include "BinaryData.h"
#include <iostream> // For logging
#include "FMRackVerticalSlider.h"
#include "FMRackSliderConstants.h"

namespace
{
    struct SliderLabelPair
    {
        FMRackVerticalSlider* slider = nullptr;
        juce::Label* label = nullptr;
    };

    constexpr int kRotaryLabelHeight = 18;
    constexpr int kVerticalSliderWidth = FMRackSliderConstants::kSliderWidth;
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
        
        // Safety check: if area is too small, don't attempt layout
        if (area.getWidth() < 10 || area.getHeight() < 10)
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
                control.label->setFont(juce::Font(juce::FontOptions(FMRackSliderConstants::kSliderLabelFontSize)));
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


    // Make the editor non-resizable to prevent crashes
    setResizable(false, false);
    // Remove resizer component since window is not resizable
    // constrainer.setMinimumSize(40, 380); // 40px min width, 380px min height
    // constrainer.setMaximumSize(1800, 900);
    // addAndMakeVisible(resizer);

        setSize (800, 540);

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

        addAndMakeVisible(initButton);
        initButton.onClick = [this] { initButtonClicked(); };

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
    reverbSizeSlider.setRange(0, 99, 1);
    setupSlider(reverbHighDampSlider, reverbHighDampLabel, "HiDamp", "reverbHighDamp", reverbHighDampAttachment);
    reverbHighDampSlider.setRange(0, 99, 1);
    setupSlider(reverbLowDampSlider, reverbLowDampLabel, "LoDamp", "reverbLowDamp", reverbLowDampAttachment);
    reverbLowDampSlider.setRange(0, 99, 1);
    setupSlider(reverbLowPassSlider, reverbLowPassLabel, "LoPass", "reverbLowPass", reverbLowPassAttachment);
    reverbLowPassSlider.setRange(0, 99, 1);
    setupSlider(reverbDiffusionSlider, reverbDiffusionLabel, "Diff", "reverbDiffusion", reverbDiffusionAttachment);
    reverbDiffusionSlider.setRange(0, 99, 1);
    setupSlider(reverbLevelSlider, reverbLevelLabel, "Level", "reverbLevel", reverbLevelAttachment);
    reverbLevelSlider.setRange(0, 99, 1);

        // Load help JSON and set up help panel
        loadMainWindowHelpJson();
        defaultHelpText = "FMRack - Multi-module FM Synthesizer\nHover over a control for help.";
        helpPanel.setText(defaultHelpText, juce::dontSendNotification);
        helpPanel.setFont(juce::Font(juce::FontOptions(12.0f)));
        helpPanel.setColour(juce::Label::backgroundColourId, juce::Colour(0xff222222));
        helpPanel.setColour(juce::Label::textColourId, juce::Colours::white);
        helpPanel.setJustificationType(juce::Justification::topLeft);
        helpPanel.setBorderSize(juce::BorderSize<int>(8));
        addAndMakeVisible(helpPanel);

        // Add mouse listeners for hover help on main window controls
        loadPerformanceButton.addMouseListener(this, false);
        savePerformanceButton.addMouseListener(this, false);
        addModuleButton.addMouseListener(this, false);
        removeModuleButton.addMouseListener(this, false);
        compressorEnableButton.addMouseListener(this, false);
        reverbEnableButton.addMouseListener(this, false);
        reverbSizeSlider.addMouseListener(this, false);
        reverbHighDampSlider.addMouseListener(this, false);
        reverbLowDampSlider.addMouseListener(this, false);
        reverbLowPassSlider.addMouseListener(this, false);
        reverbDiffusionSlider.addMouseListener(this, false);
        reverbLevelSlider.addMouseListener(this, false);

        resized(); // Force layout after construction
        juce::Logger::writeToLog("[PluginEditor] End of constructor");
        logTextBox.insertTextAtCaret("[PluginEditor] Constructor completed\n");

        // Create MIDI browser and drop zone
        midiBrowser = std::make_unique<MidiBrowserComponent>();
        midiBrowser->onMidiReady = [this](juce::MidiFile midiFile, juce::String name)
        {
            processorRef.loadMidiForPlayback(midiFile);
            processorRef.startMidiPlayback();
            juce::Logger::writeToLog("[PluginEditor] MIDI playback started: " + name);
        };
        addChildComponent(*midiBrowser);

        midiDropZone = std::make_unique<MidiDropZone>();
        midiDropZone->onMidiReady = [this](juce::MidiFile midiFile, juce::String name)
        {
            processorRef.loadMidiForPlayback(midiFile);
            processorRef.startMidiPlayback();
            juce::Logger::writeToLog("[PluginEditor] MIDI drop playback started: " + name);
        };
        addAndMakeVisible(*midiDropZone);

        addAndMakeVisible(midiBrowserToggleButton);
        midiBrowserToggleButton.onClick = [this]()
        {
            midiBrowserVisible = !midiBrowserVisible;
            if (midiBrowser)
                midiBrowser->setVisible(midiBrowserVisible);
            resized();
        };

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
    try {
        juce::Logger::writeToLog("[PluginEditor] resized() called - width: " + juce::String(getWidth()) + ", height: " + juce::String(getHeight()));

        // Layout constants - reduced padding
        const int outerMargin = 8;
        const int sectionGap = 6;
        const int controlGap = 4;
        const int topButtonHeight = 24;
        const int headerHeight = 8; // Minimal header space (no logo text)
        const int helpPanelHeight = 80; // Help panel at bottom

        // Main content area
        auto layoutBounds = getLocalBounds().reduced(outerMargin);
        layoutBounds.removeFromTop(headerHeight); // Minimal header space

        // Reserve space for help panel at bottom (always visible)
        auto helpArea = layoutBounds.removeFromBottom(helpPanelHeight);
        helpArea = helpArea.reduced(0, 2);
        helpPanel.setBounds(helpArea);
        layoutBounds.removeFromBottom(sectionGap);

        // Only show log box if visible (can be toggled via Options menu)
        if (logTextBox.isVisible())
        {
            const int logHeight = juce::jlimit(80, 120, getHeight() / 5);
            auto logArea = layoutBounds.removeFromBottom(logHeight);
            logArea = logArea.reduced(0, 2);
            logTextBox.setBounds(logArea);
            layoutBounds.removeFromBottom(sectionGap);
        }

        auto topRow = layoutBounds.removeFromTop(topButtonHeight);
        layoutBounds.removeFromTop(sectionGap);

        // Scale button widths down gracefully if window is very small
        int largeButtonWidth = juce::jlimit(60, 160, topRow.getWidth() / 5);
        const int mediumButtonWidth = juce::jlimit(40, 60, topRow.getWidth() / 8);
        const int smallButtonWidth = juce::jlimit(20, 28, topRow.getWidth() / 20);

        auto alignButton = [&](juce::Rectangle<int> area)
        {
            return juce::Rectangle<int>(area.getX(), area.getCentreY() - topButtonHeight / 2, area.getWidth(), topButtonHeight);
        };

        // Only add buttons if there's actually space for them
        if (topRow.getWidth() > largeButtonWidth * 2 + mediumButtonWidth + controlGap * 4)
        {
            auto loadBounds = topRow.removeFromLeft(largeButtonWidth);
            loadPerformanceButton.setBounds(alignButton(loadBounds));
            topRow.removeFromLeft(controlGap);
            auto saveBounds = topRow.removeFromLeft(largeButtonWidth);
            savePerformanceButton.setBounds(alignButton(saveBounds));
            topRow.removeFromLeft(controlGap);
            auto initBounds = topRow.removeFromLeft(mediumButtonWidth);
            initButton.setBounds(alignButton(initBounds));
        }
        else if (topRow.getWidth() > largeButtonWidth * 2 + controlGap * 3)
        {
            auto loadBounds = topRow.removeFromLeft(largeButtonWidth);
            loadPerformanceButton.setBounds(alignButton(loadBounds));
            topRow.removeFromLeft(controlGap);
            auto saveBounds = topRow.removeFromLeft(largeButtonWidth);
            savePerformanceButton.setBounds(alignButton(saveBounds));
        }

        if (topRow.getWidth() > smallButtonWidth * 2 + controlGap * 2)
        {
            auto addBounds = topRow.removeFromRight(smallButtonWidth);
            addModuleButton.setBounds(alignButton(addBounds));
            topRow.removeFromRight(controlGap);
            auto removeBounds = topRow.removeFromRight(smallButtonWidth);
            removeModuleButton.setBounds(alignButton(removeBounds));
        }

        // MIDI browser toggle button (right side of top row)
        const int midiBrowserButtonWidth = juce::jlimit(80, 120, topRow.getWidth() / 4);
        if (topRow.getWidth() > midiBrowserButtonWidth + controlGap)
        {
            topRow.removeFromRight(controlGap);
            midiBrowserToggleButton.setBounds(alignButton(topRow.removeFromRight(midiBrowserButtonWidth)));
        }


        const int spacing = 10;
        int availableWidth = layoutBounds.getWidth();
        juce::Logger::writeToLog("[PluginEditor] availableWidth: " + juce::String(availableWidth));

        // If width is too small, hide components to avoid crash
        if (availableWidth < 200) {
            juce::Logger::writeToLog("[PluginEditor] Width too small, hiding components");
            if (rackAccordion) rackAccordion->setVisible(false);
            effectsGroup.setVisible(false);
            return;
        }

        // Ensure components are visible when width is adequate
        if (rackAccordion) rackAccordion->setVisible(true);
        effectsGroup.setVisible(true);

        // Gracefully handle very small widths
        const int minEffectsWidth = 50;   // Reduced minimum - can collapse more
        const int minRackWidth = 100;      // Reduced minimum for extreme cases

        // Global Effects panel - compact width, but don't force unrealistic minimums
        int effectsWidth = juce::jlimit(minEffectsWidth, 200, availableWidth / 6);
        int rackWidth = availableWidth - effectsWidth - spacing;

        // If space is too tight, give all to rack (hide effects temporarily if needed)
        if (rackWidth < minRackWidth && availableWidth > minRackWidth + 10)
        {
            rackWidth = juce::jmax(minRackWidth, availableWidth - spacing - 10);
            effectsWidth = juce::jmax(10, availableWidth - rackWidth - spacing);
        }
        else if (rackWidth < minRackWidth)
        {
            // Space is very tight, distribute proportionally
            rackWidth = juce::jmax(minRackWidth, availableWidth - spacing - minEffectsWidth);
            effectsWidth = juce::jmax(10, availableWidth - rackWidth - spacing);
        }

        // Final clamp to ensure positive values
        rackWidth = juce::jmax(10, rackWidth);
        effectsWidth = juce::jmax(10, effectsWidth);

        // Ensure total doesn't exceed available width
        if (rackWidth + effectsWidth + spacing > availableWidth)
        {
            int overflow = (rackWidth + effectsWidth + spacing) - availableWidth;
            effectsWidth = juce::jmax(10, effectsWidth - overflow);
        }

        juce::Logger::writeToLog("[PluginEditor] rackWidth: " + juce::String(rackWidth) + ", effectsWidth: " + juce::String(effectsWidth));

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

        auto effectsContent = effectsArea.reduced(groupPadding).withTrimmedTop(groupLabelOffset);

        // Toggles stacked vertically (not side by side)
        auto compressorToggleRow = effectsContent.removeFromTop(toggleHeight);
        compressorEnableButton.setBounds(compressorToggleRow);
        
        effectsContent.removeFromTop(2); // Small gap between toggles
        
        auto reverbToggleRow = effectsContent.removeFromTop(toggleHeight);
        reverbEnableButton.setBounds(reverbToggleRow);

        effectsContent.removeFromTop(rowGap);

        // Calculate slider layout - 3 columns x 2 rows for compact display
        // Use global slider height constant for consistent sizing
        const int verticalColumns = 3;
        int verticalSliderHeight = FMRackSliderConstants::kMinSliderHeight;

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
                                 FMRackSliderConstants::kSliderGap);

        if (voiceEditorWindow)
            voiceEditorWindow->setBounds(100, 100, 800, 600);

        // Layout drop zone strip at bottom of help area
        if (midiDropZone)
            midiDropZone->setBounds(helpArea.removeFromBottom(40));

        // MIDI browser overlay (shown when toggle is on)
        if (midiBrowser)
        {
            if (midiBrowserVisible)
            {
                auto browserBounds = getLocalBounds().reduced(outerMargin);
                browserBounds.removeFromTop(headerHeight + topButtonHeight + sectionGap);
                browserBounds.removeFromBottom(helpPanelHeight + 40 + sectionGap);
                midiBrowser->setBounds(browserBounds);
            }
        }

        juce::Logger::writeToLog("[PluginEditor] resized() completed successfully");
    } catch (const std::exception& e) {
        juce::Logger::writeToLog("[PluginEditor] Exception in resized(): " + juce::String(e.what()));
    } catch (...) {
        juce::Logger::writeToLog("[PluginEditor] Unknown exception in resized()");
    }
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

void AudioPluginAudioProcessorEditor::initButtonClicked()
{
    try {
        appendLogMessage("Initializing plugin to default state...");

        // Create a new default performance and set it
        if (auto* controller = processorRef.getController())
        {
            FMRack::Performance defaultPerformance;
            defaultPerformance.setDefaults(1, 1); // 1 module, 1 voice each
            controller->setPerformance(defaultPerformance);
        }

        // Set numModules parameter to match
        if (auto* param = processorRef.treeState.getParameter("numModules"))
        {
            param->setValueNotifyingHost(param->convertTo0to1(1.0f));
        }

        // Set reverb to disabled but at maximum level
        if (auto* param = processorRef.treeState.getParameter("reverbEnable"))
        {
            param->setValueNotifyingHost(0.0f); // Disable reverb
        }
        if (auto* param = processorRef.treeState.getParameter("reverbLevel"))
        {
            param->setValueNotifyingHost(1.0f); // Set to 99 (maximum level) - normalized value
        }

        // Force UI update to show only 1 tab
        if (rackAccordion)
        {
            rackAccordion->updatePanels();
        }

        appendLogMessage("Plugin initialized to default state.");
    }
    catch (const std::exception& e) {
        appendLogMessage("Exception during initialization: " + juce::String(e.what()));
        juce::Logger::writeToLog("[PluginEditor] Exception in initButtonClicked: " + juce::String(e.what()));
    }
    catch (...) {
        appendLogMessage("Unknown exception during initialization.");
        juce::Logger::writeToLog("[PluginEditor] Unknown exception in initButtonClicked");
    }
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

void AudioPluginAudioProcessorEditor::loadMainWindowHelpJson()
{
    using namespace juce;
    // Use JUCE BinaryData for MainWindowHelp.json
    auto* data = BinaryData::MainWindowHelp_json;
    int dataSize = BinaryData::MainWindowHelp_jsonSize;
    juce::Logger::writeToLog("[PluginEditor] Loading MainWindowHelp.json from BinaryData");
    juce::String jsonStr = juce::String::fromUTF8(reinterpret_cast<const char*>(data), dataSize);
    var json = JSON::parse(jsonStr);
    if (!json.isObject()) {
        juce::Logger::writeToLog("[PluginEditor] MainWindowHelp.json parse failed");
        return;
    }
    helpJson = json;
    helpTextByKey.clear();

    // Read default help text
    if (json.hasProperty("defaultHelp"))
        defaultHelpText = json["defaultHelp"].toString();

    // Parse controls array from MainWindowHelp.json
    if (auto* controls = json["controls"].getArray()) {
        for (auto& c : *controls) {
            auto* obj = c.getDynamicObject();
            if (!obj || !obj->hasProperty("key"))
                continue;
            auto keyStd = obj->getProperty("key").toString().toStdString();
            juce::String name = obj->getProperty("name").toString();
            juce::String desc = obj->getProperty("long_description").toString();
            if (desc.isEmpty())
                desc = obj->getProperty("description").toString();
            juce::String shortDesc = obj->getProperty("description").toString();

            juce::String hoverText;
            hoverText << name << "\n";
            if (!shortDesc.isEmpty() && shortDesc != desc)
                hoverText << shortDesc << "\n";
            hoverText << "\n" << desc;

            helpTextByKey[keyStd] = hoverText.toStdString();
        }
    }
    
    // Load TX816Perf.json for performance parameters
    auto* tx816Data = BinaryData::TX816Perf_json;
    int tx816DataSize = BinaryData::TX816Perf_jsonSize;
    juce::Logger::writeToLog("[PluginEditor] Loading TX816Perf.json from BinaryData");
    juce::String tx816JsonStr = juce::String::fromUTF8(reinterpret_cast<const char*>(tx816Data), tx816DataSize);
    var tx816Json = JSON::parse(tx816JsonStr);
    if (tx816Json.isObject()) {
        // Parse parameters array from TX816Perf.json
        if (auto* params = tx816Json["parameters"].getArray()) {
            for (auto& p : *params) {
                auto* obj = p.getDynamicObject();
                if (!obj || !obj->hasProperty("key"))
                    continue;
                auto keyStd = obj->getProperty("key").toString().toStdString();
                juce::String name = obj->getProperty("long").toString();
                if (name.isEmpty())
                    name = obj->getProperty("short").toString();
                juce::String desc = obj->getProperty("description").toString();

                juce::String hoverText;
                hoverText << name << "\n";
                hoverText << "\n" << desc;

                helpTextByKey[keyStd] = hoverText.toStdString();
            }
        }
        juce::Logger::writeToLog("[PluginEditor] Loaded TX816Perf help entries");
    } else {
        juce::Logger::writeToLog("[PluginEditor] TX816Perf.json parse failed");
    }
    
    juce::Logger::writeToLog("[PluginEditor] Loaded " + juce::String((int)helpTextByKey.size()) + " total help entries");
}

void AudioPluginAudioProcessorEditor::showHelpForKey(const juce::String& key)
{
    auto it = helpTextByKey.find(key.toStdString());
    if (it != helpTextByKey.end()) {
        helpPanel.setText(it->second, juce::dontSendNotification);
    } else {
        helpPanel.setText(defaultHelpText, juce::dontSendNotification);
    }
}

void AudioPluginAudioProcessorEditor::restoreDefaultHelp()
{
    helpPanel.setText(defaultHelpText, juce::dontSendNotification);
}

void AudioPluginAudioProcessorEditor::mouseEnter(const juce::MouseEvent& e)
{
    // Map controls to help keys
    if (e.eventComponent == &loadPerformanceButton) {
        showHelpForKey("loadPerformance");
    } else if (e.eventComponent == &savePerformanceButton) {
        showHelpForKey("savePerformance");
    } else if (e.eventComponent == &addModuleButton) {
        showHelpForKey("addModule");
    } else if (e.eventComponent == &removeModuleButton) {
        showHelpForKey("removeModule");
    } else if (e.eventComponent == &compressorEnableButton) {
        showHelpForKey("compressorEnable");
    } else if (e.eventComponent == &reverbEnableButton) {
        showHelpForKey("reverbEnable");
    } else if (e.eventComponent == &reverbSizeSlider) {
        showHelpForKey("reverbSize");
    } else if (e.eventComponent == &reverbHighDampSlider) {
        showHelpForKey("reverbHighDamp");
    } else if (e.eventComponent == &reverbLowDampSlider) {
        showHelpForKey("reverbLowDamp");
    } else if (e.eventComponent == &reverbLowPassSlider) {
        showHelpForKey("reverbLowPass");
    } else if (e.eventComponent == &reverbDiffusionSlider) {
        showHelpForKey("reverbDiffusion");
    } else if (e.eventComponent == &reverbLevelSlider) {
        showHelpForKey("reverbLevel");
    }
}

void AudioPluginAudioProcessorEditor::mouseExit(const juce::MouseEvent& e)
{
    // Restore default help when mouse leaves a control
    if (e.eventComponent == &loadPerformanceButton ||
        e.eventComponent == &savePerformanceButton ||
        e.eventComponent == &addModuleButton ||
        e.eventComponent == &removeModuleButton ||
        e.eventComponent == &compressorEnableButton ||
        e.eventComponent == &reverbEnableButton ||
        e.eventComponent == &reverbSizeSlider ||
        e.eventComponent == &reverbHighDampSlider ||
        e.eventComponent == &reverbLowDampSlider ||
        e.eventComponent == &reverbLowPassSlider ||
        e.eventComponent == &reverbDiffusionSlider ||
        e.eventComponent == &reverbLevelSlider) {
        restoreDefaultHelp();
    }
}

