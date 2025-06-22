#include "KeyboardScalingDisplay.h"
#include "VoiceEditorPanel.h"

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
    float textHeight = 14.0f;
    // Reserve space for text above and below
    juce::Rectangle<float> graphArea = area;
    graphArea.removeFromTop(textHeight);
    graphArea.removeFromBottom(textHeight);
    // All lines white
    g.setColour(juce::Colours::white);
    float w = graphArea.getWidth();
    float h = graphArea.getHeight();
    float x0 = graphArea.getX();
    float x1 = graphArea.getRight();
    float yBase = graphArea.getBottom();
    float xBP = x0 + w * breakPoint;
    float yBP = graphArea.getY() + h * (1.0f - leftDepth); // value at breakpoint (left)
    float yBP2 = graphArea.getY() + h * (1.0f - rightDepth); // value at breakpoint (right)
    int steps = 32;
    juce::Path ksPath;
    // Left segment: from x0 to xBP
    ksPath.startNewSubPath(x0, yBase);
    for (int i = 1; i <= steps; ++i) {
        float t = (float)i / steps;
        float x = x0 + (xBP - x0) * t;
        float curve = leftCurve; // 0=linear, 1=exp+, -1=exp-
        float y;
        if (curve == 0.0f) {
            y = yBase + (yBP - yBase) * t;
        } else {
            float expT = (curve > 0) ? std::pow(t, 2.0f + curve) : 1.0f - std::pow(1.0f - t, 2.0f - curve);
            y = yBase + (yBP - yBase) * expT;
        }
        ksPath.lineTo(x, y);
    }
    // Right segment: from xBP to x1
    float yStart = yBP;
    for (int i = 1; i <= steps; ++i) {
        float t = (float)i / steps;
        float x = xBP + (x1 - xBP) * t;
        float curve = rightCurve;
        float y;
        if (curve == 0.0f) {
            y = yStart + (yBP2 - yStart) * t;
        } else {
            float expT = (curve > 0) ? std::pow(t, 2.0f + curve) : 1.0f - std::pow(1.0f - t, 2.0f - curve);
            y = yStart + (yBP2 - yStart) * expT;
        }
        ksPath.lineTo(x, y);
    }
    g.strokePath(ksPath, juce::PathStrokeType(2.0f));
    // Draw parameter values above and below the graph
    g.setFont(juce::Font(10.0f));
    g.setColour(juce::Colours::white);
    // Top: Breakpoint, LeftDepth, RightDepth
    juce::String topText = juce::String::formatted("BP: %.2f  LD: %.2f  RD: %.2f", breakPoint, leftDepth, rightDepth);
    g.drawText(topText, area.withHeight(textHeight), juce::Justification::centredTop, false);
    // Bottom: LeftCurve, RightCurve
    juce::String bottomText = juce::String::formatted("LC: %.2f  RC: %.2f", leftCurve, rightCurve);
    g.drawText(bottomText, area.withY(area.getBottom() - textHeight).withHeight(textHeight), juce::Justification::centredBottom, false);
}

void KeyboardScalingDisplay::mouseEnter(const juce::MouseEvent& e) {
    if (auto* parent = getParentComponent()) {
        if (auto* op = dynamic_cast<VoiceEditorPanel::OperatorSliders*>(parent)) {
            if (auto* vep = dynamic_cast<VoiceEditorPanel*>(op->getParentComponent()))
                vep->showHelpForKey("RD"); // Or more specific
        }
    }
}

void KeyboardScalingDisplay::mouseExit(const juce::MouseEvent& e) {
    if (auto* parent = getParentComponent()) {
        if (auto* op = dynamic_cast<VoiceEditorPanel::OperatorSliders*>(parent)) {
            if (auto* vep = dynamic_cast<VoiceEditorPanel*>(op->getParentComponent()))
                vep->restoreDefaultHelp();
        }
    }
}
