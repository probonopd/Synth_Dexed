#include "PluginProcessor.h"
#include "PluginEditor.h"
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
    setSize (400, 400);

    // Set up log text box
    logTextBox.setMultiLine(true);
    logTextBox.setReadOnly(true);
    logTextBox.setScrollbarsShown(true);
    logTextBox.setColour(juce::TextEditor::backgroundColourId, juce::Colours::black);
    logTextBox.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    logTextBox.setFont(juce::Font(12.0f));
    addAndMakeVisible(logTextBox);

    // Add the rack accordion GUI
    rackAccordion = std::make_unique<RackAccordionComponent>(processorRef.getRack());
    rackAccordion->setEditor(this); // Set the editor pointer for child access
    addAndMakeVisible(rackAccordion.get());

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

    resized(); // Force layout after construction
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
}

//==============================================================================
void AudioPluginAudioProcessorEditor::paint (juce::Graphics& g)
{
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
    fileChooser = std::make_unique<juce::FileChooser>(
        "Select a MiniDexed .ini file", juce::File(), "*.ini");
    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file.existsAsFile())
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
            fileChooser.reset();
        });
}

