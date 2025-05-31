#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    processorRef.setEditorPointer(this); // Register this editor with the processor

    juce::ignoreUnused (processorRef);
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize (400, 300);

    // Set up log text box
    logTextBox.setMultiLine(true);
    logTextBox.setReadOnly(true);
    logTextBox.setScrollbarsShown(true);
    logTextBox.setColour(juce::TextEditor::backgroundColourId, juce::Colours::black);
    logTextBox.setColour(juce::TextEditor::textColourId, juce::Colours::lime);
    addAndMakeVisible(logTextBox);

    // Set up load performance button
    addAndMakeVisible(loadPerformanceButton);
    loadPerformanceButton.onClick = [this]() { loadPerformanceButtonClicked(); };
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
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..
    logTextBox.setBounds(10, 150, getWidth() - 20, getHeight() - 160);
    loadPerformanceButton.setBounds(10, 10, 180, 30);
}

void AudioPluginAudioProcessorEditor::appendLogMessage(const juce::String& message)
{
    logTextBox.moveCaretToEnd();
    logTextBox.insertTextAtCaret(message + "\n");
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
            }
            fileChooser.reset(); // Release after use
        });
}
