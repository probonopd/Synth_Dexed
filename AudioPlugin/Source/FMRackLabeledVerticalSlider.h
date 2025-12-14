#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "OperatorSliderLookAndFeel.h"

/**
 * Custom labeled vertical slider widget that combines a slider and its label.
 * Used throughout the FMRack plugin for consistent appearance with value display and labeling.
 * Value is shown at TOP of slider, label at BOTTOM.
 */
class FMRackLabeledVerticalSlider : public juce::Component
{
public:
    FMRackLabeledVerticalSlider()
    {
        slider.setSliderStyle(juce::Slider::LinearVertical);
        // Value at top, editable, no box around it
        slider.setTextBoxStyle(juce::Slider::TextBoxAbove, false, 28, 12);
        slider.setLookAndFeel(&OperatorSliderLookAndFeel::getInstance());
        slider.setNumDecimalPlacesToDisplay(0);
        slider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
        slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

        label.setColour(juce::Label::textColourId, juce::Colours::white);
        label.setFont(juce::Font(juce::FontOptions(9.0f)));
        label.setJustificationType(juce::Justification::centred);

        // Connect slider's onValueChange to our callback
        slider.onValueChange = [this]() {
            if (onValueChange) onValueChange();
        };

        addAndMakeVisible(slider);
        addAndMakeVisible(label);
    }

    ~FMRackLabeledVerticalSlider() override = default;

    void setLabelText(const juce::String& text)
    {
        label.setText(text, juce::dontSendNotification);
    }

    juce::Slider& getSlider() { return slider; }
    const juce::Slider& getSlider() const { return slider; }

    // Delegate methods to the internal slider
    void setTextBoxStyle(juce::Slider::TextEntryBoxPosition position, bool readOnly, int textBoxWidth, int textBoxHeight)
    {
        slider.setTextBoxStyle(position, readOnly, textBoxWidth, textBoxHeight);
    }

    void setNumDecimalPlacesToDisplay(int decimalPlaces)
    {
        slider.setNumDecimalPlacesToDisplay(decimalPlaces);
    }

    std::function<void()> onValueChange;

    double getValue() const
    {
        return slider.getValue();
    }

    void setValue(double newValue, juce::NotificationType notification = juce::sendNotificationAsync)
    {
        slider.setValue(newValue, notification);
    }

    juce::String getLabelText() const
    {
        return label.getText();
    }

    // Fixed dimensions for consistency - compact
    static constexpr int kWidth = 26;
    static constexpr int kLabelHeight = 10;
    static constexpr int kMinHeight = 70 + kLabelHeight;
    static constexpr int kMaxHeight = 80 + kLabelHeight;

    void resized() override
    {
        auto bounds = getLocalBounds();
        // Label at bottom with minimal gap (1px)
        label.setBounds(bounds.getX(), bounds.getBottom() - kLabelHeight, bounds.getWidth(), kLabelHeight);
        // Slider takes the rest (value box is at top, inside the slider)
        auto sliderBounds = bounds.withHeight(bounds.getHeight() - kLabelHeight - 1);
        slider.setBounds(sliderBounds);
    }

private:
    juce::Slider slider;
    juce::Label label;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FMRackLabeledVerticalSlider)
};