#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

class KeyboardScalingDisplay : public juce::Component {
public:
    KeyboardScalingDisplay();
    void paint(juce::Graphics& g) override;
    void setScalingParams(float breakPoint, float leftDepth, float rightDepth, float leftCurve, float rightCurve);
    // New method that accepts raw integer values
    void setScalingParamsRaw(int breakPoint, int leftDepth, int rightDepth, int leftCurve, int rightCurve);
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    int hoveredParam = -1;
    std::function<void(int)> onHoveredParamChanged; // Callback for hovered param change
private:
    float breakPoint = 0.5f;
    float leftDepth = 0.0f;
    float rightDepth = 0.0f;
    float leftCurve = 0.0f;
    float rightCurve = 0.0f;
    // Raw values for display
    int rawBreakPoint = 0;
    int rawLeftDepth = 0;
    int rawRightDepth = 0;
    int rawLeftCurve = 0;
    int rawRightCurve = 0;
};
