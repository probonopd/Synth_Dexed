#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

class EnvelopeDisplay : public juce::Component {
public:
    EnvelopeDisplay();
    void setEnvelope(const std::vector<float>& rates, const std::vector<float>& levels);
    void paint(juce::Graphics& g) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    int hoveredParam = -1;
    std::function<void(int)> onHoveredParamChanged; // Callback for hovered param change
private:
    std::vector<float> envRates;
    std::vector<float> envLevels;
};
