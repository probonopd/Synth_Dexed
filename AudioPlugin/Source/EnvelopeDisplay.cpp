#include "EnvelopeDisplay.h"

EnvelopeDisplay::EnvelopeDisplay() = default;

void EnvelopeDisplay::setEnvelope(const std::vector<float>& rates, const std::vector<float>& levels) {
    envRates = rates;
    envLevels = levels;
    repaint();
}

void EnvelopeDisplay::paint(juce::Graphics& g) {
    auto area = getLocalBounds().reduced(4).toFloat();
    g.setColour(juce::Colour(0xff444444));
    g.fillRoundedRectangle(area, 4.0f);
    g.setColour(juce::Colour(0xff00ff00));
    if (envRates.size() == 4 && envLevels.size() == 4) {
        juce::Path envPath;
        float w = area.getWidth();
        float h = area.getHeight();
        float x0 = area.getX();
        float y0 = area.getBottom();
        float x1 = x0 + w * 0.2f;
        float y1 = area.getY() + h * (1.0f - envLevels[0]);
        float x2 = x0 + w * 0.4f;
        float y2 = area.getY() + h * (1.0f - envLevels[1]);
        float x3 = x0 + w * 0.7f;
        float y3 = area.getY() + h * (1.0f - envLevels[2]);
        float x4 = area.getRight();
        float y4 = area.getY() + h * (1.0f - envLevels[3]);
        envPath.startNewSubPath(x0, y0);
        envPath.lineTo(x1, y1);
        envPath.lineTo(x2, y2);
        envPath.lineTo(x3, y3);
        envPath.lineTo(x4, y4);
        g.strokePath(envPath, juce::PathStrokeType(2.0f));
    }
    g.setColour(juce::Colours::white);
    g.setFont(8.0f);
    g.drawText("ENV", area, juce::Justification::centred);
}
