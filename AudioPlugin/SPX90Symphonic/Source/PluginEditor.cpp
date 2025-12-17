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

    // Use unified FMRackVerticalSlider instances and attach them to parameters

    // Speed (MOD FREQ)
    speedLabel.setText("MOD FREQ", juce::dontSendNotification);
    addAndMakeVisible(speedLabel);
    addAndMakeVisible(speedSlider);
    speedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.getParameters(), "speed", speedSlider);

    // Depth (MOD DEPTH)
    depthLabel.setText("MOD DEPTH", juce::dontSendNotification);
    addAndMakeVisible(depthLabel);
    addAndMakeVisible(depthSlider);
    depthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.getParameters(), "depth", depthSlider);

    // Mix (BALANCE)
    mixLabel.setText("BALANCE", juce::dontSendNotification);
    addAndMakeVisible(mixLabel);
    addAndMakeVisible(mixSlider);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.getParameters(), "mix", mixSlider);

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
