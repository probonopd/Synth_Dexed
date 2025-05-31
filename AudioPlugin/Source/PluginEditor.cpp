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
    setSize (400, 400);

    // Set up log text box
    logTextBox.setMultiLine(true);
    logTextBox.setReadOnly(true);
    logTextBox.setScrollbarsShown(true);
    logTextBox.setColour(juce::TextEditor::backgroundColourId, juce::Colours::black);
    logTextBox.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    logTextBox.setFont(juce::Font(12.0f));
    addAndMakeVisible(logTextBox);

    // Set up knobs and labels
    numModulesLabel.setText("Modules", juce::dontSendNotification);
    addAndMakeVisible(numModulesLabel);
    numModulesSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    numModulesSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    numModulesSlider.setRange(1, 8, 1);
    numModulesSlider.setValue(1);
    numModulesSlider.onValueChange = [this] { numModulesChanged(); };
    addAndMakeVisible(numModulesSlider);

    unisonVoicesLabel.setText("Unison Voices", juce::dontSendNotification);
    addAndMakeVisible(unisonVoicesLabel);
    unisonVoicesSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    unisonVoicesSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    unisonVoicesSlider.setRange(1, 4, 1);
    unisonVoicesSlider.setValue(1);
    unisonVoicesSlider.onValueChange = [this] { unisonVoicesChanged(); };
    addAndMakeVisible(unisonVoicesSlider);

    unisonDetuneLabel.setText("Unison Detune", juce::dontSendNotification);
    addAndMakeVisible(unisonDetuneLabel);
    unisonDetuneSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    unisonDetuneSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    unisonDetuneSlider.setRange(0.0, 50.0, 0.1);
    unisonDetuneSlider.setValue(7.0);
    unisonDetuneSlider.onValueChange = [this] { unisonDetuneChanged(); };
    addAndMakeVisible(unisonDetuneSlider);

    unisonPanLabel.setText("Unison Pan", juce::dontSendNotification);
    addAndMakeVisible(unisonPanLabel);
    unisonPanSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    unisonPanSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    unisonPanSlider.setRange(0.0, 1.0, 0.01);
    unisonPanSlider.setValue(0.5);
    unisonPanSlider.onValueChange = [this] { unisonPanChanged(); };
    addAndMakeVisible(unisonPanSlider);

    // Add the performance button back to the UI
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
    int x = 10, y = 10, w = 120, h = 24, gap = 6;
    numModulesLabel.setBounds(x, y, w, h);
    numModulesSlider.setBounds(x + w + 4, y, 80, h);
    y += h + gap;
    unisonVoicesLabel.setBounds(x, y, w, h);
    unisonVoicesSlider.setBounds(x + w + 4, y, 80, h);
    y += h + gap;
    unisonDetuneLabel.setBounds(x, y, w, h);
    unisonDetuneSlider.setBounds(x + w + 4, y, 80, h);
    y += h + gap;
    unisonPanLabel.setBounds(x, y, w, h);
    unisonPanSlider.setBounds(x + w + 4, y, 80, h);
    y += h + gap;
    loadPerformanceButton.setBounds(x, y, 180, h);
    logTextBox.setBounds(10, 150, getWidth() - 20, getHeight() - 160);
}

void AudioPluginAudioProcessorEditor::appendLogMessage(const juce::String& message)
{
    logTextBox.moveCaretToEnd();
    logTextBox.insertTextAtCaret(message + "\n");
}

void AudioPluginAudioProcessorEditor::numModulesChanged() {
    processorRef.setNumModules((int)numModulesSlider.getValue());
}
void AudioPluginAudioProcessorEditor::unisonVoicesChanged() {
    processorRef.setUnisonVoices((int)unisonVoicesSlider.getValue());
}
void AudioPluginAudioProcessorEditor::unisonDetuneChanged() {
    processorRef.setUnisonDetune((float)unisonDetuneSlider.getValue());
}
void AudioPluginAudioProcessorEditor::unisonPanChanged() {
    processorRef.setUnisonPan((float)unisonPanSlider.getValue());
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
            fileChooser.reset();
        });
}

