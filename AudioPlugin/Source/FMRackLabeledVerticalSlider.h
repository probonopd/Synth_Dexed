#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "FMRackVerticalSlider.h"
#include "FMRackSliderConstants.h"


/**
 * Custom labeled vertical slider widget that combines a slider and its label.
 * Used throughout the FMRack plugin for consistent appearance with value display and labeling.
 * Value is shown at TOP of slider, label at BOTTOM.
 * Uses global constants from FMRackSliderConstants.h for consistent sizing.
 */
class FMRackLabeledVerticalSlider : public juce::Component
{
public:
    FMRackLabeledVerticalSlider()
    {
        label.setColour(juce::Label::textColourId, juce::Colours::white);
        label.setFont(juce::Font(juce::FontOptions(FMRackSliderConstants::kSliderLabelFontSize)));
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

    FMRackVerticalSlider& getSlider() { return slider; }
    const FMRackVerticalSlider& getSlider() const { return slider; }

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

    // Use global constants for consistent sizing across the application
    static constexpr int kWidth = FMRackSliderConstants::kSliderWidth;
    static constexpr int kLabelHeight = FMRackSliderConstants::kLabelHeight;
    static constexpr int kMinHeight = FMRackSliderConstants::kMinSliderHeight + kLabelHeight + 4;
    static constexpr int kMaxHeight = FMRackSliderConstants::kMaxSliderHeight + kLabelHeight + 4;

    void resized() override
    {
        auto bounds = getLocalBounds();
        // Set width to fixed kWidth
        bounds.setWidth(kWidth);
        // Label at bottom with minimal gap (1px)
        label.setBounds(bounds.getX(), bounds.getBottom() - kLabelHeight, bounds.getWidth(), kLabelHeight);
        // Slider takes the rest (value box is at top, inside the slider)
        auto sliderBounds = bounds.withHeight(bounds.getHeight() - kLabelHeight - 1);
        slider.setBounds(sliderBounds);
    }

private:
    FMRackVerticalSlider slider;
    juce::Label label;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FMRackLabeledVerticalSlider)
};