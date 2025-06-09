#include "OperatorSliderLookAndFeel.h"
#include <juce_graphics/juce_graphics.h>
#include <iostream> // Include iostream for std::cout

// Modern, flat, DX7-style operator slider look
void OperatorSliderLookAndFeel::drawLinearSlider(
    juce::Graphics& g, int x, int y, int width, int height,
    float sliderPos, float minSliderPos, float maxSliderPos,
    const juce::Slider::SliderStyle style, juce::Slider& slider)
{
    std::cout << "[OperatorSliderLookAndFeel] drawLinearSlider CALLED for slider: " << slider.getName() << std::endl;
    // Draw track background only (no grey lines)
    auto trackBounds = juce::Rectangle<float>((float)x + width * 0.4f, (float)y + 2, width * 0.2f, (float)height - 4);
    g.setColour(juce::Colour(0xff23272a)); // dark track
    g.fillRoundedRectangle(trackBounds, 3.0f);

    // Draw value fill (teal/cyan)
    if (style == juce::Slider::LinearVertical) {
        float valueTop = sliderPos;
        float valueBottom = (float)y + (float)height - 2;
        auto valueBounds = juce::Rectangle<float>(trackBounds.getX(), valueTop, trackBounds.getWidth(), valueBottom - valueTop);
        g.setColour(juce::Colour(0xff00bfae)); // cyan fill
        g.fillRect(valueBounds);
    }

    // Draw thin, vertical line thumb (not a circle)
    if (style == juce::Slider::LinearVertical) {
        float thumbY = sliderPos;
        float thumbX = trackBounds.getCentreX();
        float thumbLen = 3.0f;
        float thumbWidth = 10.0f;
        g.setColour(juce::Colour(0xff3ec6e0)); // blue thumb
        g.fillRect(thumbX - thumbWidth * 0.5f, thumbY - thumbLen * 0.5f, thumbWidth, thumbLen);
    }
}

// Implementation for drawLinearSliderThumb
void OperatorSliderLookAndFeel::drawLinearSliderThumb(
    juce::Graphics& g, int x, int y, int width, int height,
    float sliderPos, float minSliderPos, float maxSliderPos,
    const juce::Slider::SliderStyle style, juce::Slider& slider)
{
    // Draw a thin vertical line as the thumb (for vertical sliders)
    if (style == juce::Slider::LinearVertical) {
        float thumbY = sliderPos;
        float thumbX = x + width * 0.5f;
        float thumbLen = 18.0f;
        float thumbWidth = 3.0f;
        g.setColour(juce::Colour(0xff3ec6e0)); // blue thumb
        g.fillRect(thumbX - thumbWidth * 0.5f, thumbY - thumbLen * 0.5f, thumbWidth, thumbLen);
    } else {
        juce::LookAndFeel_V4::drawLinearSliderThumb(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
    }
}
