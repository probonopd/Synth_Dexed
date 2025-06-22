#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "VoiceEditorPanel.h"
#include "VoiceEditorWindow.h"
#include <iostream> // For logging

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    try {
        // Use JUCE logging system for VST3 compatibility
        juce::Logger::writeToLog("[PluginEditor] Constructor. this=" + juce::String::toHexString((juce::pointer_sized_int)this));
        processorRef.setEditorPointer(this); // Register this editor with the processor

        juce::ignoreUnused (processorRef);
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
        };
        addAndMakeVisible(savePerformanceButton); // NEW
        savePerformanceButton.onClick = [this]() {
            try {
                savePerformanceButtonClicked();
            } catch (const std::exception& e) {
                juce::Logger::writeToLog("[PluginEditor] Exception in savePerformanceButtonClicked: " + juce::String(e.what()));
                logTextBox.insertTextAtCaret("[PluginEditor] Exception: " + juce::String(e.what()) + "\n");
            } catch (...) {
                juce::Logger::writeToLog("[PluginEditor] Unknown exception in savePerformanceButtonClicked");
                logTextBox.insertTextAtCaret("[PluginEditor] Unknown exception in savePerformanceButtonClicked\n");
            }
        };
        juce::Logger::writeToLog("[PluginEditor] After addAndMakeVisible(loadPerformanceButton)");

        resized(); // Force layout after construction
        juce::Logger::writeToLog("[PluginEditor] End of constructor");
        logTextBox.insertTextAtCaret("[PluginEditor] Constructor completed\n");
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
            // Remove content to break parent-child relationship and avoid dangling pointers
            voiceEditorWindow->setVisible(false);
            voiceEditorWindow->setContentOwned(nullptr, false); // Remove content after hiding
            voiceEditorWindow.reset();
        }
        if (rackAccordion) {
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
    juce::Logger::writeToLog("[PluginEditor] resized() called. this=" + juce::String::toHexString((juce::pointer_sized_int)this) + " size=" + juce::String(getWidth()) + "x" + juce::String(getHeight()));
    logTextBox.insertTextAtCaret("[PluginEditor] resized() called. size=" + juce::String(getWidth()) + "x" + juce::String(getHeight()) + "\n");
    
    int y = 10, h = 24, gap = 6;
    loadPerformanceButton.setBounds(10, y, 180, h);
    savePerformanceButton.setBounds(200, y, 180, h); // NEW: Place next to load button
    y += h + gap;
    int accordionHeight = getHeight() - y - 140;
    if (accordionHeight < 100) accordionHeight = 100; // Ensure minimum height
    if (rackAccordion) {
        rackAccordion->setBounds(10, y, getWidth() - 20, accordionHeight);
        juce::Logger::writeToLog("[PluginEditor] rackAccordion bounds set to: x=10, y=" + juce::String(y) + ", width=" + juce::String(getWidth() - 20) + ", height=" + juce::String(accordionHeight));
        logTextBox.insertTextAtCaret("[PluginEditor] rackAccordion bounds: " + juce::String(getWidth() - 20) + "x" + juce::String(accordionHeight) + "\n");
    }
    logTextBox.setBounds(10, getHeight() - 130, getWidth() - 20, 120);

    juce::Logger::writeToLog("[PluginEditor] resized() end");
}

void AudioPluginAudioProcessorEditor::appendLogMessage(const juce::String& message)
{
    logTextBox.moveCaretToEnd();
    logTextBox.insertTextAtCaret(message + "\n");
    if (rackAccordion)
        rackAccordion->updatePanels();
}

void AudioPluginAudioProcessorEditor::numModulesChanged() {
    if (rackAccordion)
        rackAccordion->setNumModulesVT((int)rackAccordion->numModulesSlider.getValue());
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

