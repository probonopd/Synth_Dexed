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
    juce::Rectangle<float> topArea = area.withHeight(textHeight);
    float itemW = area.getWidth() / 3.0f;
    juce::String topValues[3] = {
        juce::String(breakPoint, 2),
        juce::String(leftDepth, 2),
        juce::String(rightDepth, 2)
    };
    juce::String topLabels[3] = { "BP", "LD", "RD" };
    for (int i = 0; i < 3; ++i) {
        juce::Rectangle<float> r(topArea.getX() + i * itemW, topArea.getY(), itemW, textHeight);
        g.setColour(hoveredParam == i ? juce::Colours::yellow : juce::Colours::white);
        g.drawText(topValues[i], r.withHeight(textHeight/2.0f), juce::Justification::centred, false);
        g.drawText(topLabels[i], r.withY(r.getY() + textHeight/2.0f).withHeight(textHeight/2.0f), juce::Justification::centred, false);
    }
    // Bottom: LeftCurve, RightCurve
    juce::Rectangle<float> bottomArea = area.withY(area.getBottom() - textHeight).withHeight(textHeight);
    itemW = area.getWidth() / 2.0f;
    juce::String bottomValues[2] = {
        juce::String(leftCurve, 2),
        juce::String(rightCurve, 2)
    };
    juce::String bottomLabels[2] = { "LC", "RC" };
    for (int i = 0; i < 2; ++i) {
        g.setColour(hoveredParam == (i + 3) ? juce::Colours::yellow : juce::Colours::white);
        juce::Rectangle<float> r(bottomArea.getX() + i * itemW, bottomArea.getY(), itemW, textHeight);
        g.drawText(bottomValues[i], r.withHeight(textHeight/2.0f), juce::Justification::centred, false);
        g.drawText(bottomLabels[i], r.withY(r.getY() + textHeight/2.0f).withHeight(textHeight/2.0f), juce::Justification::centred, false);
    }
}

void KeyboardScalingDisplay::mouseMove(const juce::MouseEvent& e) {
    auto area = getLocalBounds().reduced(4).toFloat();
    float textHeight = 14.0f;
    float w = area.getWidth();
    float itemW = w / 3.0f;
    hoveredParam = -1;
    // Top row (BP, LD, RD)
    for (int i = 0; i < 3; ++i) {
        juce::Rectangle<float> r(area.getX() + i * itemW, area.getY(), itemW, textHeight);
        if (r.contains(e.position)) { hoveredParam = i; repaint(); return; }
    }
    // Bottom row (LC, RC)
    itemW = w / 2.0f;
    for (int i = 0; i < 2; ++i) {
        juce::Rectangle<float> r(area.getX() + i * itemW, area.getBottom() - textHeight, itemW, textHeight);
        if (r.contains(e.position)) { hoveredParam = i + 3; repaint(); return; }
    }
    if (hoveredParam != -1) { hoveredParam = -1; repaint(); }
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
    hoveredParam = -1;
    repaint();
    if (auto* parent = getParentComponent()) {
        if (auto* op = dynamic_cast<VoiceEditorPanel::OperatorSliders*>(parent)) {
            if (auto* vep = dynamic_cast<VoiceEditorPanel*>(op->getParentComponent()))
                vep->restoreDefaultHelp();
        }
    }
}

void KeyboardScalingDisplay::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    if (hoveredParam == -1) return;
    float delta = wheel.deltaY > 0 ? 0.01f : -0.01f;
    int opIdx = -1;
    int paramOffset = -1;
    if (auto* parent = getParentComponent()) {
        if (auto* op = dynamic_cast<VoiceEditorPanel::OperatorSliders*>(parent)) {
            if (auto* vep = dynamic_cast<VoiceEditorPanel*>(op->getParentComponent())) {
                for (int idx = 0; idx < vep->operators.size(); ++idx) {
                    if (vep->operators[idx].get() == op) {
                        opIdx = idx;
                        break;
                    }
                }
                if (opIdx >= 0) {
                    float* param = nullptr;
                    uint8_t paramAddress = 0;
                    if (hoveredParam == 0) { // BP
                        breakPoint = juce::jlimit(0.0f, 1.0f, breakPoint + delta);
                        uint8_t value = static_cast<uint8_t>(breakPoint * 99.0f + 0.5f);
                        paramAddress = static_cast<uint8_t>(opIdx * 21 + 8);
                        vep->setDexedParam(paramAddress, value);
                    } else if (hoveredParam == 1) { // LD
                        leftDepth = juce::jlimit(0.0f, 1.0f, leftDepth + delta);
                        uint8_t value = static_cast<uint8_t>(leftDepth * 99.0f + 0.5f);
                        paramAddress = static_cast<uint8_t>(opIdx * 21 + 9);
                        vep->setDexedParam(paramAddress, value);
                    } else if (hoveredParam == 2) { // RD
                        rightDepth = juce::jlimit(0.0f, 1.0f, rightDepth + delta);
                        uint8_t value = static_cast<uint8_t>(rightDepth * 99.0f + 0.5f);
                        paramAddress = static_cast<uint8_t>(opIdx * 21 + 10);
                        vep->setDexedParam(paramAddress, value);
                    } else if (hoveredParam == 3) { // LC
                        leftCurve = juce::jlimit(-1.0f, 2.0f, leftCurve + delta);
                        // Map back to Dexed curve value (0=linear, 1=exp+, 2=exp++, -1=exp-)
                        uint8_t value = 0;
                        if (leftCurve == 0.0f) value = 0;
                        else if (leftCurve == 1.0f) value = 1;
                        else if (leftCurve == -1.0f) value = 2;
                        else if (leftCurve == 2.0f) value = 3;
                        paramAddress = static_cast<uint8_t>(opIdx * 21 + 11);
                        vep->setDexedParam(paramAddress, value);
                    } else if (hoveredParam == 4) { // RC
                        rightCurve = juce::jlimit(-1.0f, 2.0f, rightCurve + delta);
                        uint8_t value = 0;
                        if (rightCurve == 0.0f) value = 0;
                        else if (rightCurve == 1.0f) value = 1;
                        else if (rightCurve == -1.0f) value = 2;
                        else if (rightCurve == 2.0f) value = 3;
                        paramAddress = static_cast<uint8_t>(opIdx * 21 + 12);
                        vep->setDexedParam(paramAddress, value);
                    }
                }
            }
        }
    }
    repaint();
}
