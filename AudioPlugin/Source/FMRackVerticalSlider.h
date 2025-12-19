#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "FMRackSliderConstants.h"


/**
 * Custom vertical slider widget used throughout the FMRack plugin.
 * Ensures consistent appearance and dimensions for all vertical sliders.
 * Value shown at top, editable, no box outline.
 * Uses global constants from FMRackSliderConstants.h for consistent sizing.
 */


class FMRackVerticalSlider : public juce::Slider
{
    // Internal LookAndFeel for teal line thumb
    class TealLineLookAndFeel : public juce::LookAndFeel_V4 {
    public:
        void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                              float sliderPos, float minSliderPos, float maxSliderPos,
                              const juce::Slider::SliderStyle style, juce::Slider& slider) override {
            juce::ignoreUnused(minSliderPos, maxSliderPos, slider);
            // Draw track background as a solid brown vertical bar
            auto trackBounds = juce::Rectangle<float>((float)x + width * 0.4f, (float)y, width * 0.2f, (float)height);
            juce::ColourGradient trackGradient(
                juce::Colour(0xff5b4637), // dark brown
                trackBounds.getCentreX(), trackBounds.getY(),
                juce::Colour(0xff735946), // lighter brown
                trackBounds.getCentreX(), trackBounds.getBottom(),
                false
            );
            g.setGradientFill(trackGradient);
            g.fillRect(trackBounds);

            // Draw thin, vertical thumb line in teal (no circle)
            if (style == juce::Slider::LinearVertical) {
                float thumbY = sliderPos;
                float thumbX = trackBounds.getCentreX();
                float thumbLen = 2.0f;
                float thumbWidth = 12.0f;
                g.setColour(juce::Colour(0xff00bfae)); // teal line
                g.fillRect(thumbX - thumbWidth * 0.5f, thumbY - thumbLen * 0.5f, thumbWidth, thumbLen);
            }
        }
        juce::Font getLabelFont(juce::Label& label) override {
            juce::ignoreUnused(label);
            return juce::Font(juce::FontOptions(FMRackSliderConstants::kSliderValueFontSize));
        }
    };

    inline static TealLineLookAndFeel tealLookAndFeel;

public:
    explicit FMRackVerticalSlider()
    {
        setSliderStyle(juce::Slider::LinearVertical);
        setTextBoxStyle(juce::Slider::TextBoxAbove, false, 
                        FMRackSliderConstants::kTextBoxWidth, 
                        FMRackSliderConstants::kTextBoxHeight);
        setLookAndFeel(&tealLookAndFeel);
        setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    }

    ~FMRackVerticalSlider() override = default;

    // Use global constants for consistent sizing across the application
    static constexpr int kWidth = FMRackSliderConstants::kSliderWidth;
    static constexpr int kMinHeight = FMRackSliderConstants::kMinSliderHeight;
    static constexpr int kMaxHeight = FMRackSliderConstants::kMaxSliderHeight;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FMRackVerticalSlider)
};