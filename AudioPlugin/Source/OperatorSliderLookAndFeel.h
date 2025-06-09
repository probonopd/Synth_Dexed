#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Custom LookAndFeel for operator sliders: thumb is a vertical or horizontal line
class OperatorSliderLookAndFeel : public juce::LookAndFeel_V4 {
public:
    void drawLinearSliderThumb(juce::Graphics& g, int x, int y, int width, int height,
                               float sliderPos, float minSliderPos, float maxSliderPos,
                               const juce::Slider::SliderStyle style, juce::Slider& slider) override {
        auto isEnabled = slider.isEnabled();
        juce::Colour thumbColour = slider.findColour(juce::Slider::thumbColourId).withMultipliedAlpha(isEnabled ? 1.0f : 0.5f);
        g.setColour(thumbColour);
        int lineW = 3;
        int lineH = height - 4;
        int lineL = width - 4;
        int cx = x + width / 2;
        int cy = y + height / 2;
        if (style == juce::Slider::LinearVertical) {
            int thumbY = (int)sliderPos;
            g.fillRect(cx - lineW / 2, thumbY - lineH / 2, lineW, lineH);
        } else if (style == juce::Slider::LinearHorizontal) {
            int thumbX = (int)sliderPos;
            g.fillRect(thumbX - lineL / 2, cy - lineW / 2, lineL, lineW);
        } else {
            juce::LookAndFeel_V4::drawLinearSliderThumb(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        }
    }
};
