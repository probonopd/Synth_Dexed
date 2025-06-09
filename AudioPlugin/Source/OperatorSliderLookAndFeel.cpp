#include "OperatorSliderLookAndFeel.h"
#include <juce_graphics/juce_graphics.h>

// Modern, flat, DX7-style operator slider look
void OperatorSliderLookAndFeel::drawLinearSlider(
    juce::Graphics& g, int x, int y, int width, int height,
    float sliderPos, float minSliderPos, float maxSliderPos,
    const juce::Slider::SliderStyle style, juce::Slider& slider)
{
    // Draw track background with a vertical ColourGradient
    auto trackBounds = juce::Rectangle<float>((float)x + width * 0.4f, (float)y + 0, width * 0.2f, (float)height - 0);
    juce::ColourGradient trackGradient(
        juce::Colour(98, 76, 65), // Top color (dark), https://www.colorhexa.com/624c41
        trackBounds.getCentreX(), trackBounds.getY(),
        juce::Colour(113, 88, 75), // Bottom color (light), https://www.colorhexa.com/71584b
        trackBounds.getCentreX(), trackBounds.getBottom(),
        false
    );
    g.setGradientFill(trackGradient);
    g.fillRect(trackBounds);

/*
    // Draw value fill below the line thumb
    if (style == juce::Slider::LinearVertical) {
        float valueTop = sliderPos;
        float valueBottom = (float)y + (float)height - 2;
        auto valueBounds = juce::Rectangle<float>(trackBounds.getX(), valueTop, trackBounds.getWidth(), valueBottom - valueTop);
        g.setColour(juce::Colour(0xff23272a)); // dark track
        g.fillRect(valueBounds);
    }
*/
    // Draw thin, vertical line thumb (not a circle)
    if (style == juce::Slider::LinearVertical) {
        float thumbY = sliderPos;
        float thumbX = trackBounds.getCentreX();
        float thumbLen = 2.0f;
        float thumbWidth = 10.0f;
        g.setColour(juce::Colour(0xff00bfae)); // teal/cyan
        g.fillRect(thumbX - thumbWidth * 0.5f, thumbY - thumbLen * 0.5f, thumbWidth, thumbLen);
    }
}