#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>

class DX7LookAndFeel : public juce::LookAndFeel_V4
{
public:
    // FM Synth Color Palette - inspired by 80s digital synthesizers
    static constexpr juce::uint32 kPanelDark        = 0xFF1A1A1A;  // Dark charcoal panel
    static constexpr juce::uint32 kPanelMid         = 0xFF2A2A2A;  // Mid-tone panel
    static constexpr juce::uint32 kPanelLight       = 0xFF3A3A3A;  // Lighter panel areas
    static constexpr juce::uint32 kPanelHighlight   = 0xFF4A4A4A;  // Highlight/bevels
    
    static constexpr juce::uint32 kAccentTeal       = 0xFF40B0B0;  // Teal accent (like button labels)
    static constexpr juce::uint32 kAccentTealDark   = 0xFF2A8080;  // Darker teal
    static constexpr juce::uint32 kAccentOrange     = 0xFFE08030;  // Orange accent
    static constexpr juce::uint32 kAccentGreen      = 0xFF40C040;  // Green accent
    static constexpr juce::uint32 kAccentRed        = 0xFFD04040;  // Red accent
    static constexpr juce::uint32 kAccentBlue       = 0xFF4080D0;  // Blue accent
    
    static constexpr juce::uint32 kLCDGreen         = 0xFF30D878;  // Classic LCD green
    static constexpr juce::uint32 kLCDGreenDim      = 0xFF208050;  // Dimmer LCD green
    static constexpr juce::uint32 kLCDBackground    = 0xFF0A1810;  // LCD dark background
    static constexpr juce::uint32 kLCDBezel         = 0xFF080808;  // LCD bezel
    
    static constexpr juce::uint32 kTextBright       = 0xFFFFFFFF;  // Pure white text
    static constexpr juce::uint32 kTextMid          = 0xFFFFFFFF;  // White text
    static constexpr juce::uint32 kTextDim          = 0xFFE0E0E0;  // Slightly dimmed white
    
    static constexpr juce::uint32 kButtonFace       = 0xFF353535;  // Button face
    static constexpr juce::uint32 kButtonHighlight  = 0xFF454545;  // Button highlight
    static constexpr juce::uint32 kButtonShadow     = 0xFF1A1A1A;  // Button shadow

    DX7LookAndFeel()
    {
        // Set default colors using the FM synth palette
        setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(kPanelDark));
        setColour(juce::TextButton::buttonColourId, juce::Colour(kButtonFace));
        setColour(juce::TextButton::buttonOnColourId, juce::Colour(kAccentTealDark));
        setColour(juce::TextButton::textColourOffId, juce::Colour(kTextBright));
    // Use white text even when button is 'on' per UI requirement
    setColour(juce::TextButton::textColourOnId, juce::Colour(kTextBright));
        
        setColour(juce::ComboBox::backgroundColourId, juce::Colour(kLCDBackground));
    // Combo box text should be white
    setColour(juce::ComboBox::textColourId, juce::Colour(kTextBright));
        setColour(juce::ComboBox::outlineColourId, juce::Colour(kLCDBezel));
        setColour(juce::ComboBox::arrowColourId, juce::Colour(kLCDGreen));
        
        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(kPanelMid));
    // Popup menu text should be white
    setColour(juce::PopupMenu::textColourId, juce::Colour(kTextBright));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(kAccentTealDark));
        setColour(juce::PopupMenu::highlightedTextColourId, juce::Colour(kTextBright));
        
        setColour(juce::Slider::backgroundColourId, juce::Colour(kPanelDark));
        setColour(juce::Slider::trackColourId, juce::Colour(kAccentTeal));
        setColour(juce::Slider::thumbColourId, juce::Colour(kTextBright));
        setColour(juce::Slider::textBoxTextColourId, juce::Colour(kTextBright));
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        
        setColour(juce::Label::textColourId, juce::Colour(kTextBright));
        setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        
        setColour(juce::TextEditor::backgroundColourId, juce::Colour(kLCDBackground));
    // Text editors use white text
    setColour(juce::TextEditor::textColourId, juce::Colour(kTextBright));
        setColour(juce::TextEditor::outlineColourId, juce::Colour(kLCDBezel));
        setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(kAccentTeal));
        
        setColour(juce::ScrollBar::thumbColourId, juce::Colour(kAccentTeal));
        setColour(juce::ScrollBar::backgroundColourId, juce::Colour(kPanelDark));
        
        setColour(juce::GroupComponent::outlineColourId, juce::Colour(kPanelLight));
        setColour(juce::GroupComponent::textColourId, juce::Colour(kTextBright));
        
        setColour(juce::ToggleButton::textColourId, juce::Colour(kTextBright));
        setColour(juce::ToggleButton::tickColourId, juce::Colour(kLCDGreen));
        setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(kButtonFace));
        
        setColour(juce::TabbedComponent::backgroundColourId, juce::Colour(kPanelDark));
        setColour(juce::TabbedComponent::outlineColourId, juce::Colour(kPanelLight));
        setColour(juce::TabbedButtonBar::tabOutlineColourId, juce::Colour(kPanelLight));
    // Ensure all tab text is white
    setColour(juce::TabbedButtonBar::tabTextColourId, juce::Colour(kTextBright));
        setColour(juce::TabbedButtonBar::frontTextColourId, juce::Colour(kTextBright));
    }
    
    //==============================================================================
    // Button Drawing
    //==============================================================================
    void drawButtonBackground(juce::Graphics& g,
                              juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
        auto cornerSize = 3.0f;
        
        // Create subtle gradient for 3D membrane button effect
        juce::Colour baseColour = backgroundColour;
        if (shouldDrawButtonAsDown)
        {
            baseColour = baseColour.darker(0.3f);
        }
        else if (shouldDrawButtonAsHighlighted)
        {
            baseColour = baseColour.brighter(0.15f);
        }
        
        // Draw button shadow (inset effect)
        g.setColour(juce::Colour(kPanelDark));
        g.fillRoundedRectangle(bounds.translated(0, 1), cornerSize);
        
        // Draw button body with subtle gradient
        juce::ColourGradient gradient(
            baseColour.brighter(0.1f),
            bounds.getX(), bounds.getY(),
            baseColour.darker(0.15f),
            bounds.getX(), bounds.getBottom(),
            false);
        
        g.setGradientFill(gradient);
        g.fillRoundedRectangle(bounds, cornerSize);
        
        // Draw border - subtle dark edge
        g.setColour(juce::Colour(kPanelDark).withAlpha(0.8f));
        g.drawRoundedRectangle(bounds, cornerSize, 1.0f);
        
        // Draw subtle highlight on top edge (membrane button style)
        if (!shouldDrawButtonAsDown)
        {
            g.setColour(juce::Colour(kPanelHighlight).withAlpha(0.4f));
            g.drawLine(bounds.getX() + cornerSize, bounds.getY() + 1,
                       bounds.getRight() - cornerSize, bounds.getY() + 1, 0.5f);
        }
    }
    
    //==============================================================================
    // Slider Drawing
    //==============================================================================
    void drawLinearSlider(juce::Graphics& g,
                          int x, int y, int width, int height,
                          float sliderPos,
                          float /*minSliderPos*/,
                          float /*maxSliderPos*/,
                          const juce::Slider::SliderStyle /*style*/,
                          juce::Slider& slider) override
    {
        // Ensure consistent visual spacing/height across different slider instances
        // by using a fixed-ish track length (clamped to available bounds) and
        // uniform padding. This makes sliders in different panels (e.g., the
        // operator sliders and the global parameter sliders) visually match.
        auto trackWidth = juce::jmin(6.0f, static_cast<float>(width) * 0.25f);

        // Default target visible track length in pixels. If the component is
        // smaller, the track will be clamped to fit. This helps make sliders
        // across different areas the same visual height/length.
        static constexpr float kDefaultTrackLength = 84.0f;

        auto centerX = static_cast<float>(x) + static_cast<float>(width) * 0.5f;
        auto centerY = static_cast<float>(y) + static_cast<float>(height) * 0.5f;

        if (slider.isHorizontal())
        {
            float available = static_cast<float>(width) - 8.0f;
            float trackLen = juce::jmin(kDefaultTrackLength, available);
            float half = trackLen * 0.5f;
            juce::Point<float> startPoint(centerX - half, centerY);
            juce::Point<float> endPoint(centerX + half, centerY);

            // proceed with drawing using these points
            
            // Draw track background with inset effect
            juce::Path backgroundTrack;
            backgroundTrack.startNewSubPath(startPoint);
            backgroundTrack.lineTo(endPoint);
            g.setColour(juce::Colour(kPanelDark));
            g.strokePath(backgroundTrack, { trackWidth + 2, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
            g.setColour(juce::Colour(kLCDBezel));
            g.strokePath(backgroundTrack, { trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

            // Draw filled part with teal accent
            juce::Path valueTrack;
            auto kx = sliderPos;
            auto ky = centerY;
            juce::Point<float> minPoint = startPoint;
            juce::Point<float> maxPoint = { kx, ky };
            valueTrack.startNewSubPath(minPoint);
            valueTrack.lineTo(maxPoint);
            g.setColour(juce::Colour(kAccentTeal));
            g.strokePath(valueTrack, { trackWidth - 2, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

            // Draw thumb with metallic look (reused below)
            auto thumbWidth = getSliderThumbRadius(slider) * 2.0f;
            auto thumbRect = juce::Rectangle<float>(thumbWidth, thumbWidth).withCentre({ kx, ky });

            // Thumb gradient for brushed metal effect
            juce::ColourGradient thumbGradient(
                juce::Colour(0xFFB0B0B0),
                thumbRect.getX(), thumbRect.getY(),
                juce::Colour(0xFF606060),
                thumbRect.getRight(), thumbRect.getBottom(),
                false);

            g.setGradientFill(thumbGradient);
            g.fillEllipse(thumbRect);

            g.setColour(juce::Colour(kPanelDark));
            g.drawEllipse(thumbRect.reduced(0.5f), 1.0f);

            // Center indicator line on thumb
            g.setColour(juce::Colour(0xFF555555));
            g.drawLine(thumbRect.getCentreX() - thumbWidth * 0.25f, thumbRect.getCentreY(),
                       thumbRect.getCentreX() + thumbWidth * 0.25f, thumbRect.getCentreY(), 1.0f);
            return;
        }

        // Vertical slider handling
        {
            float available = static_cast<float>(height) - 8.0f;
            float trackLen = juce::jmin(kDefaultTrackLength, available);
            float half = trackLen * 0.5f;
            juce::Point<float> startPoint(centerX, centerY + half);
            juce::Point<float> endPoint(centerX, centerY - half);

            // Draw track background with inset effect
            juce::Path backgroundTrack;
            backgroundTrack.startNewSubPath(startPoint);
            backgroundTrack.lineTo(endPoint);
            g.setColour(juce::Colour(kPanelDark));
            g.strokePath(backgroundTrack, { trackWidth + 2, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
            g.setColour(juce::Colour(kLCDBezel));
            g.strokePath(backgroundTrack, { trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

            // Draw filled part with teal accent
            juce::Path valueTrack;

            auto kx = centerX;
            auto ky = sliderPos;

            juce::Point<float> minPoint = startPoint;
            juce::Point<float> maxPoint = { kx, ky };

            valueTrack.startNewSubPath(minPoint);
            valueTrack.lineTo(maxPoint);
            g.setColour(juce::Colour(kAccentTeal));
            g.strokePath(valueTrack, { trackWidth - 2, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

            // Draw thumb with metallic look
            auto thumbWidth = getSliderThumbRadius(slider) * 2.0f;
            auto thumbRect = juce::Rectangle<float>(thumbWidth, thumbWidth).withCentre({ kx, ky });

            // Thumb gradient for brushed metal effect
            juce::ColourGradient thumbGradient(
                juce::Colour(0xFFB0B0B0),
                thumbRect.getX(), thumbRect.getY(),
                juce::Colour(0xFF606060),
                thumbRect.getRight(), thumbRect.getBottom(),
                false);

            g.setGradientFill(thumbGradient);
            g.fillEllipse(thumbRect);

            g.setColour(juce::Colour(kPanelDark));
            g.drawEllipse(thumbRect.reduced(0.5f), 1.0f);

            // Center indicator line on thumb
            g.setColour(juce::Colour(0xFF555555));
            g.drawLine(thumbRect.getCentreX() - thumbWidth * 0.25f, thumbRect.getCentreY(),
                       thumbRect.getCentreX() + thumbWidth * 0.25f, thumbRect.getCentreY(), 1.0f);
        }
        
        // (Rendering handled above per orientation)
    }
    
    void drawRotarySlider(juce::Graphics& g,
                          int x, int y, int width, int height,
                          float sliderPosProportional,
                          float rotaryStartAngle,
                          float rotaryEndAngle,
                          juce::Slider& /*slider*/) override
    {
        auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(4);
        auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
        auto toAngle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
        auto lineW = juce::jmin(5.0f, radius * 0.25f);
        auto arcRadius = radius - lineW * 0.5f;
        
        // Background arc with inset effect
        juce::Path backgroundArc;
        backgroundArc.addCentredArc(bounds.getCentreX(),
                                    bounds.getCentreY(),
                                    arcRadius,
                                    arcRadius,
                                    0.0f,
                                    rotaryStartAngle,
                                    rotaryEndAngle,
                                    true);
        
        g.setColour(juce::Colour(kPanelDark));
        g.strokePath(backgroundArc, juce::PathStrokeType(lineW + 2, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(juce::Colour(kLCDBezel));
        g.strokePath(backgroundArc, juce::PathStrokeType(lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        
        // Value arc with teal accent
        if (sliderPosProportional > 0.0f)
        {
            juce::Path valueArc;
            valueArc.addCentredArc(bounds.getCentreX(),
                                   bounds.getCentreY(),
                                   arcRadius,
                                   arcRadius,
                                   0.0f,
                                   rotaryStartAngle,
                                   toAngle,
                                   true);
            
            g.setColour(juce::Colour(kAccentTeal));
            g.strokePath(valueArc, juce::PathStrokeType(lineW - 1, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        
        // Draw knob body with brushed metal gradient
        auto knobRadius = radius * 0.6f;
        auto knobBounds = juce::Rectangle<float>(bounds.getCentreX() - knobRadius,
                                                  bounds.getCentreY() - knobRadius,
                                                  knobRadius * 2.0f,
                                                  knobRadius * 2.0f);
        
        // Brushed metal gradient
        juce::ColourGradient knobGradient(
            juce::Colour(0xFF909090),
            knobBounds.getX(), knobBounds.getY(),
            juce::Colour(0xFF505050),
            knobBounds.getRight(), knobBounds.getBottom(),
            false);
        knobGradient.addColour(0.5, juce::Colour(0xFF707070));
        
        g.setGradientFill(knobGradient);
        g.fillEllipse(knobBounds);
        
        g.setColour(juce::Colour(kPanelDark));
        g.drawEllipse(knobBounds.reduced(0.5f), 1.0f);
        
        // Draw pointer indicator
        juce::Path pointer;
        auto pointerLength = knobRadius * 0.7f;
        auto pointerThickness = 2.5f;
        
        pointer.addRoundedRectangle(-pointerThickness * 0.5f, -knobRadius + 4, pointerThickness, pointerLength, 1.0f);
        
        g.setColour(juce::Colour(kLCDGreen));
        g.fillPath(pointer, juce::AffineTransform::rotation(toAngle).translated(bounds.getCentreX(), bounds.getCentreY()));
    }
    
    //==============================================================================
    // Tab Bar Drawing
    //==============================================================================
    void drawTabButton(juce::TabBarButton& button, juce::Graphics& g, bool isMouseOver, bool isMouseDown) override
    {
        auto activeArea = button.getActiveArea();
        auto isFrontTab = button.isFrontTab();
        
        // Dark panel with subtle gradient for tabs
        juce::Colour bgColour = isFrontTab ? juce::Colour(kPanelMid) : juce::Colour(kPanelDark);
        
        if (isMouseOver && !isFrontTab)
            bgColour = bgColour.brighter(0.08f);
        
        if (isMouseDown)
            bgColour = bgColour.darker(0.1f);
        
        // Draw tab background with subtle gradient
        juce::ColourGradient tabGradient(
            bgColour.brighter(0.05f),
            static_cast<float>(activeArea.getX()), static_cast<float>(activeArea.getY()),
            bgColour.darker(0.05f),
            static_cast<float>(activeArea.getX()), static_cast<float>(activeArea.getBottom()),
            false);
        g.setGradientFill(tabGradient);
        g.fillRect(activeArea);
        
        // Draw border
        g.setColour(juce::Colour(kPanelDark));
        g.drawRect(activeArea, 1);
        
        // Draw teal accent line for active tab
        if (isFrontTab)
        {
            g.setColour(juce::Colour(kAccentTeal));
            g.drawLine(static_cast<float>(activeArea.getX()), static_cast<float>(activeArea.getY()),
                       static_cast<float>(activeArea.getRight()), static_cast<float>(activeArea.getY()), 2.0f);
        }
    }
    
    void drawTabAreaBehindFrontButton(juce::TabbedButtonBar& /*bar*/, juce::Graphics& g, int w, int h) override
    {
        g.setColour(juce::Colour(kPanelDark));
        g.fillRect(0, 0, w, h);
    }
    
    //==============================================================================
    // Group Component Drawing
    //==============================================================================
    void drawGroupComponentOutline(juce::Graphics& g, int width, int height,
                                   const juce::String& text, const juce::Justification& position,
                                   juce::GroupComponent& group) override
    {
        juce::ignoreUnused(position);
        
        const float textH = 15.0f;
        const float indent = 3.0f;
        const float textEdgeGap = 4.0f;
        float cs = 5.0f;
        
        float x = indent;
        float y = textH - 3.0f;
        float w = juce::jmax(0.0f, static_cast<float>(width) - x * 2.0f);
        float h = juce::jmax(0.0f, static_cast<float>(height) - y - indent);
        cs = juce::jmin(cs, juce::jmin(w * 0.5f, h * 0.5f));
        float cs2 = 2.0f * cs;
        
        float textW = text.isEmpty() ? 0.0f : static_cast<float>(text.length()) * 8.0f + textEdgeGap * 2.0f;
        textW = juce::jmin(textW, juce::jmax(0.0f, w - cs2 - textEdgeGap * 2.0f));
        float textX = cs + textEdgeGap;
        
        juce::Path p;
        p.startNewSubPath(x + textX + textW, y);
        p.lineTo(x + w - cs, y);
        
        p.addArc(x + w - cs2, y, cs2, cs2, 0, juce::MathConstants<float>::halfPi);
        p.lineTo(x + w, y + h - cs);
        
        p.addArc(x + w - cs2, y + h - cs2, cs2, cs2, juce::MathConstants<float>::halfPi, juce::MathConstants<float>::pi);
        p.lineTo(x + cs, y + h);
        
        p.addArc(x, y + h - cs2, cs2, cs2, juce::MathConstants<float>::pi, juce::MathConstants<float>::pi * 1.5f);
        p.lineTo(x, y + cs);
        
        p.addArc(x, y, cs2, cs2, juce::MathConstants<float>::pi * 1.5f, juce::MathConstants<float>::twoPi);
        p.lineTo(x + textX, y);
        
        float alpha = group.isEnabled() ? 1.0f : 0.5f;
        
        g.setColour(juce::Colour(kPanelLight).withMultipliedAlpha(alpha));
        g.strokePath(p, juce::PathStrokeType(1.0f));
        
    // Group titles should be white
    g.setColour(juce::Colour(kTextBright).withMultipliedAlpha(alpha));
        g.setFont(textH);
        g.drawText(text, static_cast<int>(x + textX), 0, static_cast<int>(textW), static_cast<int>(textH),
                   juce::Justification::centred, true);
    }
    
    //==============================================================================
    // Toggle Button Drawing
    //==============================================================================
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                          bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        auto fontSize = juce::jmin(15.0f, static_cast<float>(button.getHeight()) * 0.75f);
        auto tickWidth = fontSize * 1.1f;
        
        drawTickBox(g, button, 4.0f, (static_cast<float>(button.getHeight()) - tickWidth) * 0.5f,
                    tickWidth, tickWidth,
                    button.getToggleState(),
                    button.isEnabled(),
                    shouldDrawButtonAsHighlighted,
                    shouldDrawButtonAsDown);
        
        g.setColour(button.findColour(juce::ToggleButton::textColourId));
        g.setFont(fontSize);
        
        if (!button.isEnabled())
            g.setOpacity(0.5f);
        
        g.drawFittedText(button.getButtonText(),
                         button.getLocalBounds().withTrimmedLeft(juce::roundToInt(tickWidth) + 10)
                               .withTrimmedRight(2),
                         juce::Justification::centredLeft, 10);
    }
    
    void drawTickBox(juce::Graphics& g, juce::Component& component,
                     float x, float y, float w, float h,
                     bool ticked, bool isEnabled,
                     bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        juce::ignoreUnused(component, shouldDrawButtonAsDown);
        
        juce::Rectangle<float> tickBounds(x, y, w, h);
        
        // Draw checkbox background with inset effect
        g.setColour(juce::Colour(kPanelDark));
        g.fillRoundedRectangle(tickBounds.expanded(1), 4.0f);
        g.setColour(juce::Colour(kLCDBackground));
        g.fillRoundedRectangle(tickBounds, 3.0f);
        
        
        // Draw border
        g.setColour(shouldDrawButtonAsHighlighted ? juce::Colour(kAccentTeal) : juce::Colour(kPanelLight));
        g.drawRoundedRectangle(tickBounds, 3.0f, 1.0f);
        
        // Draw tick
        if (ticked)
        {
            auto tick = getTickShape(0.75f);
            g.setColour(isEnabled ? juce::Colour(kLCDGreen) : juce::Colour(kButtonFace));
            
            auto transform = tick.getTransformToScaleToFit(tickBounds.reduced(4, 5), true);
            g.fillPath(tick, transform);
        }
    }
    
    //==============================================================================
    // Utility Methods - Static color accessors
    //==============================================================================
    // Provide consistent thumb radius so thumbs look the same across areas
    int getSliderThumbRadius(juce::Slider& slider) override
    {
        // Base thumb size on a fraction of the smaller dimension but clamp to sensible values
        auto minDim = juce::jmin(slider.getWidth(), slider.getHeight());
        int r = static_cast<int>(juce::jmax(6.0f, juce::jmin(14.0f, minDim * 0.08f)));
        return r;
    }
    static juce::Colour getPanelDarkColour() { return juce::Colour(kPanelDark); }
    static juce::Colour getPanelMidColour() { return juce::Colour(kPanelMid); }
    static juce::Colour getPanelLightColour() { return juce::Colour(kPanelLight); }
    static juce::Colour getBackgroundColour() { return juce::Colour(kPanelDark); }
    static juce::Colour getAccentTealColour() { return juce::Colour(kAccentTeal); }
    static juce::Colour getAccentTealDarkColour() { return juce::Colour(kAccentTealDark); }
    static juce::Colour getLCDGreenColour() { return juce::Colour(kLCDGreen); }
    static juce::Colour getLCDBackgroundColour() { return juce::Colour(kLCDBackground); }
    static juce::Colour getTextBrightColour() { return juce::Colour(kTextBright); }
    static juce::Colour getTextDimColour() { return juce::Colour(kTextDim); }
    static juce::Colour getAccentOrangeColour() { return juce::Colour(kAccentOrange); }
    static juce::Colour getAccentGreenColour() { return juce::Colour(kAccentGreen); }
    static juce::Colour getAccentRedColour() { return juce::Colour(kAccentRed); }
    static juce::Colour getAccentBlueColour() { return juce::Colour(kAccentBlue); }
    
private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DX7LookAndFeel)
};
