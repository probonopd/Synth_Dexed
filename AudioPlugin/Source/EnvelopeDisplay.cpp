#include "EnvelopeDisplay.h"
#include "VoiceEditorPanel.h"
#include <array>

EnvelopeDisplay::EnvelopeDisplay() = default;

void EnvelopeDisplay::setEnvelope(const std::vector<float>& rates, const std::vector<float>& levels) {
    envRates = rates;
    envLevels = levels;
    repaint();
}

void EnvelopeDisplay::paint(juce::Graphics& g) {
    auto area = getLocalBounds().reduced(4).toFloat();
    float textHeight = 14.0f;
    juce::Rectangle<float> graphArea = area;
    graphArea.removeFromTop(textHeight);
    graphArea.removeFromBottom(textHeight);
    g.setColour(juce::Colours::white);
    // Draw envelope shape with rate-based time axis
    if (envRates.size() == 4 && envLevels.size() == 4) {
        float w = graphArea.getWidth();
        float h = graphArea.getHeight();
        float x0 = graphArea.getX();
        float y0 = graphArea.getBottom();
        // --- Compute time durations for each segment based on rates (DX7: higher rate = shorter time) ---
        std::array<float, 4> durations;
        float totalTime = 0.0f;
        for (int i = 0; i < 4; ++i) {
            // Avoid div by zero, add small epsilon
            float rate = juce::jlimit(0.01f, 1.0f, envRates[i]);
            // Inverse mapping: time = 1/(rate^2) for more DX7-like feel
            durations[i] = 1.0f / (rate * rate);
            totalTime += durations[i];
        }
        // Compute normalized time positions for each point
        std::array<float, 5> timePos = {0};
        for (int i = 1; i < 5; ++i) {
            timePos[i] = timePos[i-1] + durations[i-1] / totalTime;
        }
        // Draw envelope path
        juce::Path envPath;
        float prevX = x0 + w * timePos[0];
        float prevY = y0 - h * envLevels[0];
        envPath.startNewSubPath(prevX, prevY);
        for (int i = 1; i < 4; ++i) {
            float x = x0 + w * timePos[i];
            float y = y0 - h * envLevels[i];
            envPath.lineTo(x, y);
            prevX = x;
            prevY = y;
        }
        // Last point (L4)
        float x4 = x0 + w * timePos[4];
        float y4 = y0 - h * envLevels[3];
        envPath.lineTo(x4, y4);
        g.strokePath(envPath, juce::PathStrokeType(2.0f));
    }
    // Draw parameter values as separate items
    g.setFont(juce::Font(10.0f));
    g.setColour(juce::Colours::white);
    float w = area.getWidth();
    float itemW = w / 4.0f;
    // Top: Envelope rates
    for (int i = 0; i < 4; ++i) {
        juce::Rectangle<float> r(area.getX() + i * itemW, area.getY(), itemW, textHeight);
        if (hoveredParam == i)
            g.setColour(juce::Colours::yellow);
        else
            g.setColour(juce::Colours::white);
        juce::String valueTxt;
        juce::String labelTxt;
        switch (i) {
            case 0: labelTxt = "R1"; break;
            case 1: labelTxt = "R2"; break;
            case 2: labelTxt = "R3"; break;
            case 3: labelTxt = "R4"; break;
        }
        // Draw value centered at top, label below value, both centered
        valueTxt = juce::String(envRates[i], 2);
        g.drawText(valueTxt, r.withHeight(textHeight/2.0f), juce::Justification::centred, false);
        g.drawText(labelTxt, r.withY(r.getY() + textHeight/2.0f).withHeight(textHeight/2.0f), juce::Justification::centred, false);
    }
    // Bottom: Envelope levels
    for (int i = 0; i < 4; ++i) {
        juce::Rectangle<float> r(area.getX() + i * itemW, area.getBottom() - textHeight, itemW, textHeight);
        if (hoveredParam == i + 4)
            g.setColour(juce::Colours::yellow);
        else
            g.setColour(juce::Colours::white);
        juce::String valueTxt;
        juce::String labelTxt;
        switch (i) {
            case 0: labelTxt = "L1"; break;
            case 1: labelTxt = "L2"; break;
            case 2: labelTxt = "L3"; break;
            case 3: labelTxt = "L4"; break;
        }
        // For LC and RC (L3 and L4), show numeric value (not curve meaning)
        valueTxt = juce::String(envLevels[i], 2);
        g.drawText(valueTxt, r.withHeight(textHeight/2.0f), juce::Justification::centred, false);
        g.drawText(labelTxt, r.withY(r.getY() + textHeight/2.0f).withHeight(textHeight/2.0f), juce::Justification::centred, false);
    }
}

void EnvelopeDisplay::mouseMove(const juce::MouseEvent& e) {
    auto area = getLocalBounds().reduced(4).toFloat();
    float textHeight = 14.0f;
    float w = area.getWidth();
    float itemW = w / 4.0f;
    int prevHovered = hoveredParam;
    hoveredParam = -1;
    // Top row
    for (int i = 0; i < 4; ++i) {
        juce::Rectangle<float> r(area.getX() + i * itemW, area.getY(), itemW, textHeight);
        if (r.contains(e.position)) { hoveredParam = i; break; }
    }
    // Bottom row
    if (hoveredParam == -1) {
        for (int i = 0; i < 4; ++i) {
            juce::Rectangle<float> r(area.getX() + i * itemW, area.getBottom() - textHeight, itemW, textHeight);
            if (r.contains(e.position)) { hoveredParam = i + 4; break; }
        }
    }
    if (hoveredParam != prevHovered) {
        if (onHoveredParamChanged) onHoveredParamChanged(hoveredParam);
        repaint();
    }
}

void EnvelopeDisplay::mouseExit(const juce::MouseEvent& e) {
    hoveredParam = -1;
    repaint();
}

void EnvelopeDisplay::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    if (hoveredParam == -1) return;
    float delta = wheel.deltaY > 0 ? 0.01f : -0.01f;
    int opIdx = -1;
    int paramOffset = -1;
    if (auto* parent = getParentComponent()) {
        if (auto* op = dynamic_cast<VoiceEditorPanel::OperatorSliders*>(parent)) {
            // Find operator index in parent VoiceEditorPanel
            if (auto* vep = dynamic_cast<VoiceEditorPanel*>(op->getParentComponent())) {
                for (int idx = 0; idx < vep->operators.size(); ++idx) {
                    if (vep->operators[idx].get() == op) {
                        opIdx = idx;
                        break;
                    }
                }
                if (opIdx >= 0) {
                    if (hoveredParam < 4) {
                        // Envelope rate (R1-R4): offsets 0-3
                        paramOffset = hoveredParam;
                        envRates[hoveredParam] = juce::jlimit(0.0f, 1.0f, envRates[hoveredParam] + delta);
                        uint8_t value = static_cast<uint8_t>(envRates[hoveredParam] * 99.0f + 0.5f);
                        uint8_t paramAddress = static_cast<uint8_t>(opIdx * 21 + paramOffset);
                        vep->setDexedParam(paramAddress, value);
                    } else {
                        // Envelope level (L1-L4): offsets 4-7
                        int idx = hoveredParam - 4;
                        paramOffset = 4 + idx;
                        envLevels[idx] = juce::jlimit(0.0f, 1.0f, envLevels[idx] + delta);
                        uint8_t value = static_cast<uint8_t>(envLevels[idx] * 99.0f + 0.5f);
                        uint8_t paramAddress = static_cast<uint8_t>(opIdx * 21 + paramOffset);
                        vep->setDexedParam(paramAddress, value);
                    }
                }
            }
        }
    }
    repaint();
}

void EnvelopeDisplay::mouseEnter(const juce::MouseEvent& e) {
    // No-op: help text is now handled by callback in mouseMove
}
