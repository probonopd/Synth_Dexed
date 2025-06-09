#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Custom LookAndFeel for operator sliders: thumb is a vertical or horizontal line
class OperatorSliderLookAndFeel : public juce::LookAndFeel_V4 {
public:
    void drawLinearSlider(juce::Graphics&, int, int, int, int, float, float, float, const juce::Slider::SliderStyle, juce::Slider&) override;
};
