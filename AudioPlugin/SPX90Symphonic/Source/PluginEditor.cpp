/*
  ==============================================================================

    SPX90 Symphonic Effect - Plugin Editor Implementation
    
    Clean, minimal GUI inspired by the original SPX90's simplicity.

  ==============================================================================
*/

#include "PluginEditor.h"

//==============================================================================
SPX90SymphonicAudioProcessorEditor::SPX90SymphonicAudioProcessorEditor(SPX90SymphonicAudioProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    // Set up title
    titleLabel.setText("Symphonic", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(20.0f, juce::Font::bold)));
    titleLabel.setJustificationType(juce::Justification::centred);
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel);

    // Set up bypass button
    addAndMakeVisible(bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processorRef.getParameters(), "bypass", bypassButton);

    // Helper lambda to set up rotary sliders
    auto setupSlider = [this](juce::Slider& slider, juce::Label& label,
                               const juce::String& labelText, const juce::String& paramID,
                               std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment)
    {
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18);
        addAndMakeVisible(slider);

        label.setText(labelText, juce::dontSendNotification);
        label.setFont(juce::Font(juce::FontOptions(12.0f)));
        label.setJustificationType(juce::Justification::centred);
        label.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(label);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processorRef.getParameters(), paramID, slider);
    };

    setupSlider(speedSlider, speedLabel, "MOD FREQ", "speed", speedAttachment);
    setupSlider(depthSlider, depthLabel, "MOD DEPTH", "depth", depthAttachment);
    setupSlider(mixSlider, mixLabel, "BALANCE", "mix", mixAttachment);

    // Set editor size
    setSize(300, 220);
}

SPX90SymphonicAudioProcessorEditor::~SPX90SymphonicAudioProcessorEditor()
{
}

//==============================================================================
void SPX90SymphonicAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Dark background with subtle gradient
    juce::ColourGradient gradient(juce::Colour(0xff1a1a2e), 0.0f, 0.0f,
                                   juce::Colour(0xff16213e), 0.0f, static_cast<float>(getHeight()),
                                   false);
    g.setGradientFill(gradient);
    g.fillAll();

    // Subtle border
    g.setColour(juce::Colour(0xff333355));
    g.drawRect(getLocalBounds(), 2);
}

void SPX90SymphonicAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced(10);

    // Title at top
    titleLabel.setBounds(bounds.removeFromTop(30));
    bounds.removeFromTop(5);

    // Three knobs in a row: Mod Freq | Mod Depth | Balance
    const int knobSize = 70;
    const int labelHeight = 18;
    const int totalKnobWidth = knobSize * 3 + 20;  // 3 knobs + spacing
    const int startX = (bounds.getWidth() - totalKnobWidth) / 2;

    auto knobArea = bounds.removeFromTop(knobSize + labelHeight + 5);

    // Mod Freq knob (left)
    speedLabel.setBounds(startX, knobArea.getY(), knobSize, labelHeight);
    speedSlider.setBounds(startX, knobArea.getY() + labelHeight, knobSize, knobSize);

    // Mod Depth knob (center)
    int depthX = startX + knobSize + 10;
    depthLabel.setBounds(depthX, knobArea.getY(), knobSize, labelHeight);
    depthSlider.setBounds(depthX, knobArea.getY() + labelHeight, knobSize, knobSize);

    // Balance knob (right)
    int mixX = depthX + knobSize + 10;
    mixLabel.setBounds(mixX, knobArea.getY(), knobSize, labelHeight);
    mixSlider.setBounds(mixX, knobArea.getY() + labelHeight, knobSize, knobSize);

    // Bypass button at bottom
    bounds.removeFromTop(5);
    bypassButton.setBounds(bounds.removeFromTop(24).withSizeKeepingCentre(80, 24));
}
