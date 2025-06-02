#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class EnvelopeDisplay : public juce::Component {
public:
    EnvelopeDisplay();
    void paint(juce::Graphics& g) override;
    void setEnvelope(const std::vector<float>& rates, const std::vector<float>& levels);
private:
    std::vector<float> envRates;
    std::vector<float> envLevels;
};
