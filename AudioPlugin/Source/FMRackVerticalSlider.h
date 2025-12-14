#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "OperatorSliderLookAndFeel.h"

/**
 * Custom vertical slider widget used throughout the FMRack plugin.
 * Ensures consistent appearance and dimensions for all vertical sliders.
 * Value shown at top, editable, no box outline.
 */
class FMRackVerticalSlider : public juce::Slider
{
public:
    FMRackVerticalSlider()
    {
        setSliderStyle(juce::Slider::LinearVertical);
        setTextBoxStyle(juce::Slider::TextBoxAbove, false, 28, 12);
        setLookAndFeel(&OperatorSliderLookAndFeel::getInstance());
        setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    }

    ~FMRackVerticalSlider() override = default;

    // Fixed dimensions for consistency - compact
    static constexpr int kWidth = 26;
    static constexpr int kMinHeight = 45;
    static constexpr int kMaxHeight = 80;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FMRackVerticalSlider)
};