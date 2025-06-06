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
        std::cout << "[PluginEditor] Constructor. this=" << this << std::endl;
        setSize(800, 600); // Ensure the editor has a reasonable size
        processorRef.setEditorPointer(this); // Register this editor with the processor

        juce::ignoreUnused (processorRef);
        setSize (600, 600);

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
        std::cout << "[PluginEditor] After addAndMakeVisible(logTextBox)" << std::endl;

        // Add the rack accordion GUI
        rackAccordion = std::make_unique<RackAccordionComponent>(&processorRef);
        rackAccordion->setEditor(this); // Set the editor pointer for child access
        addAndMakeVisible(rackAccordion.get());
        std::cout << "[PluginEditor] After addAndMakeVisible(rackAccordion)" << std::endl;

        // Add the performance button back to the UI (keep in editor for now)
        addAndMakeVisible(loadPerformanceButton);
        loadPerformanceButton.onClick = [this]() {
            try {
                loadPerformanceButtonClicked();
            } catch (const std::exception& e) {
                std::cout << "[PluginEditor] Exception in loadPerformanceButtonClicked: " << e.what() << std::endl;
                logTextBox.insertTextAtCaret("[PluginEditor] Exception: " + juce::String(e.what()) + "\n");
            } catch (...) {
                std::cout << "[PluginEditor] Unknown exception in loadPerformanceButtonClicked" << std::endl;
                logTextBox.insertTextAtCaret("[PluginEditor] Unknown exception in loadPerformanceButtonClicked\n");
            }
        };
        std::cout << "[PluginEditor] After addAndMakeVisible(loadPerformanceButton)" << std::endl;

        resized(); // Force layout after construction
        std::cout << "[PluginEditor] End of constructor" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "[PluginEditor] Exception in constructor: " << e.what() << std::endl;
        logTextBox.insertTextAtCaret("[PluginEditor] Exception: " + juce::String(e.what()) + "\n");
    } catch (...) {
        std::cout << "[PluginEditor] Unknown exception in constructor" << std::endl;
        logTextBox.insertTextAtCaret("[PluginEditor] Unknown exception in constructor\n");
    }
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
    try {
        processorRef.setEditorPointer(nullptr);
        if (voiceEditorWindow) {
            voiceEditorWindow->setVisible(false);
            voiceEditorWindow.reset();
        }
        if (voiceEditorPanel) {
            voiceEditorPanel.reset();
        }
        if (rackAccordion) {
            rackAccordion.reset();
        }
    } catch (const std::exception& e) {
        std::cout << "[PluginEditor] Exception in destructor: " << e.what() << std::endl;
    } catch (...) {
        std::cout << "[PluginEditor] Unknown exception in destructor" << std::endl;
    }
}

//==============================================================================
void AudioPluginAudioProcessorEditor::paint (juce::Graphics& g)
{
    std::cout << "[PluginEditor] paint() called" << std::endl;
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void AudioPluginAudioProcessorEditor::resized()
{
    std::cout << "[PluginEditor] resized() called. this=" << this << " size=" << getWidth() << "x" << getHeight() << std::endl;
    int y = 10, h = 24, gap = 6;
    loadPerformanceButton.setBounds(10, y, 180, h);
    y += h + gap;
    int accordionHeight = getHeight() - y - 140;
    if (accordionHeight < 100) accordionHeight = 100; // Ensure minimum height
    if (rackAccordion)
        rackAccordion->setBounds(10, y, getWidth() - 20, accordionHeight);
    logTextBox.setBounds(10, getHeight() - 130, getWidth() - 20, 120);

    // Set bounds for the voice editor panel
    if (voiceEditorPanel)
        voiceEditorPanel->setBounds(0, 0, getWidth(), getHeight());
    std::cout << "[PluginEditor] resized() end" << std::endl;
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
                    std::cout << "[PluginEditor] Exception in loadPerformanceFile: " << e.what() << std::endl;
                } catch (...) {
                    appendLogMessage("Unknown exception loading performance file.");
                    std::cout << "[PluginEditor] Unknown exception in loadPerformanceFile" << std::endl;
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
                            std::cout << "[PluginEditor] Exception in updatePanels: " << e.what() << std::endl;
                        } catch (...) {
                            appendLogMessage("Unknown exception in rackAccordion->updatePanels.");
                            std::cout << "[PluginEditor] Unknown exception in updatePanels" << std::endl;
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

void AudioPluginAudioProcessorEditor::showVoiceEditorPanel() {
    std::cout << "[PluginEditor] showVoiceEditorPanel() called" << std::endl;
    
    // Create the VoiceEditorPanel lazily when first requested
    if (!voiceEditorPanel) {
        std::cout << "[PluginEditor] Creating VoiceEditorPanel lazily" << std::endl;
        voiceEditorPanel = std::make_unique<VoiceEditorPanel>();
        std::cout << "[PluginEditor] VoiceEditorPanel created" << std::endl;
    }
    
    // Create a new window for the VoiceEditorPanel
    if (!voiceEditorWindow) {
        std::cout << "[PluginEditor] Creating voice editor window" << std::endl;
        voiceEditorWindow = std::make_unique<VoiceEditorWindow>(
            "Voice Editor", 
            juce::Colours::darkgrey, 
            juce::DocumentWindow::allButtons,
            this // Pass the editor instance to the window
        );
        voiceEditorWindow->setContentOwned(voiceEditorPanel.get(), true);
        voiceEditorWindow->setUsingNativeTitleBar(true);
        voiceEditorWindow->centreWithSize(800, 600);
        voiceEditorWindow->setResizable(true, false);
    }
    
    std::cout << "[PluginEditor] Making voice editor window visible" << std::endl;
    voiceEditorWindow->setVisible(true);
    voiceEditorWindow->toFront(true);
}

