#include "KeyboardScalingDisplay.h"

KeyboardScalingDisplay::KeyboardScalingDisplay() = default;

void KeyboardScalingDisplay::setScalingParams(float bp, float ld, float rd, float lc, float rc) {
    breakPoint = bp;
    leftDepth = ld;
    rightDepth = rd;
    leftCurve = lc;
    rightCurve = rc;
    repaint();
}

void KeyboardScalingDisplay::paint(juce::Graphics& g) {
    auto area = getLocalBounds().reduced(4).toFloat();
    g.setColour(juce::Colour(0xff444444));
    g.fillRoundedRectangle(area, 4.0f);
    g.setColour(juce::Colour(0xffff8800));
    juce::Path ksPath;
    float w = area.getWidth();
    float h = area.getHeight();
    float x0 = area.getX();
    float y0 = area.getBottom();
    float xBP = x0 + w * breakPoint;
    float yBP = area.getY() + h * (1.0f - leftDepth);
    float yBP2 = area.getY() + h * (1.0f - rightDepth);
    ksPath.startNewSubPath(x0, y0);
    ksPath.quadraticTo(xBP, yBP, area.getRight(), yBP2);
    g.strokePath(ksPath, juce::PathStrokeType(2.0f));
    g.setColour(juce::Colours::white);
    g.setFont(8.0f);
    g.drawText("KS", area, juce::Justification::centred);
}
