#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "OperatorSliderLookAndFeel.h"

/**
 * Custom vertical slider widget used throughout the FMRack plugin.
 * Ensures consistent appearance and dimensions for all vertical sliders.
 */
class FMRackVerticalSlider : public juce::Slider
{
public:
    FMRackVerticalSlider()
    {
        setSliderStyle(juce::Slider::LinearVertical);
        setTextBoxStyle(juce::Slider::TextBoxBelow, false, 40, 16);
        setLookAndFeel(&OperatorSliderLookAndFeel::getInstance());
    }

    ~FMRackVerticalSlider() override = default;

    // Fixed dimensions for consistency
    static constexpr int kWidth = 40;
    static constexpr int kMinHeight = 60;
    static constexpr int kMaxHeight = 113;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FMRackVerticalSlider)
};