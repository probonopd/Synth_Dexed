#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "VoiceEditorPanel.h"
#include "VoiceEditorWindow.h"
#include <iostream> // For logging

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    std::cout << "[PluginEditor] Constructor. this=" << this << std::endl;
    setSize(800, 600); // Ensure the editor has a reasonable size
    processorRef.setEditorPointer(this); // Register this editor with the processor

    juce::ignoreUnused (processorRef);
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
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
    rackAccordion = std::make_unique<RackAccordionComponent>(processorRef.getRack());
    rackAccordion->setEditor(this); // Set the editor pointer for child access
    addAndMakeVisible(rackAccordion.get());
    std::cout << "[PluginEditor] After addAndMakeVisible(rackAccordion)" << std::endl;

    // Remove all per-tab control setup from here, only set up numModulesSlider/Label
    rackAccordion->numModulesSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    rackAccordion->numModulesSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    rackAccordion->numModulesSlider.setRange(1, 8, 1);
    rackAccordion->numModulesSlider.setValue(1);
    rackAccordion->numModulesSlider.onValueChange = [this] { numModulesChanged(); };
    rackAccordion->addAndMakeVisible(rackAccordion->numModulesSlider);
    rackAccordion->numModulesLabel.setText("Modules", juce::dontSendNotification);
    rackAccordion->addAndMakeVisible(rackAccordion->numModulesLabel);

    // Add the performance button back to the UI (keep in editor for now)
    addAndMakeVisible(loadPerformanceButton);
    loadPerformanceButton.onClick = [this]() { loadPerformanceButtonClicked(); };
    std::cout << "[PluginEditor] After addAndMakeVisible(loadPerformanceButton)" << std::endl;

    // Remove VoiceEditorPanel instantiation from constructor
    // It will be created lazily when the user clicks the button

    resized(); // Force layout after construction
    std::cout << "[PluginEditor] End of constructor" << std::endl;
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
    // Unregister this editor with the processor first
    processorRef.setEditorPointer(nullptr);
    
    // Properly close the voice editor window if it exists
    if (voiceEditorWindow) {
        voiceEditorWindow->setVisible(false);
        voiceEditorWindow.reset();
    }
    
    // Clear the voice editor panel
    if (voiceEditorPanel) {
        voiceEditorPanel.reset();
    }
    
    // Clear the rack accordion component
    if (rackAccordion) {
        rackAccordion.reset();
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
    processorRef.setNumModules((int)numModulesSlider.getValue());
    if (rackAccordion)
        rackAccordion->updatePanels();
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
                if (processorRef.loadPerformanceFile(file.getFullPathName())) {
                    appendLogMessage("Performance loaded successfully.");
                } else {
                    appendLogMessage("Failed to load performance file.");
                }
                if (rackAccordion)
                    rackAccordion->updatePanels();
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

