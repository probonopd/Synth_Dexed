#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "EnvelopeDisplay.h"
#include "KeyboardScalingDisplay.h"
#include "OperatorSliderLookAndFeel.h"

// VoiceEditorPanel: A panel for editing a single DX7 voice, styled after the classic DX7 UI.
// Envelope and keyboard scaling widgets are placeholders for now.
class VoiceEditorPanel : public juce::Component {
public:
    VoiceEditorPanel();
    ~VoiceEditorPanel() override;
    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;

private:
    // Operator slider group
    struct OperatorSliders : public juce::Component {
        juce::Slider tl, ratio, coarse, fine, egR1, egR2, egR3, egR4, egL1, egL2, egL3, egL4, levSclBrkPt, sclLeftDepth, sclRightDepth, sclLeftCurve, sclRightCurve, rateScaling, ampModSense, keyVelSense, oscMode, freqCoarse, freqFine, detune, ampModEnable;
        juce::Label label;
        juce::Label tlLabel, ratioLabel, coarseLabel, fineLabel, egR1Label, egR2Label, egR3Label, egR4Label, egL1Label, egL2Label, egL3Label, egL4Label, levSclBrkPtLabel, sclLeftDepthLabel, sclRightDepthLabel, sclLeftCurveLabel, sclRightCurveLabel, rateScalingLabel, ampModSenseLabel, keyVelSenseLabel, oscModeLabel, freqCoarseLabel, freqFineLabel, detuneLabel, ampModEnableLabel;
        EnvelopeDisplay envWidget;
        KeyboardScalingDisplay ksWidget;

        OperatorSliders();
        ~OperatorSliders() override;
        void paint(juce::Graphics&) override;
        void resized() override;
        void addAndLayoutSliderWithLabel(juce::Slider& slider, juce::Label& label, const juce::String& text, int& x, int y, int w, int h, int gap);
        void setAllOperatorSliderLookAndFeel();
    };
    std::vector<std::unique_ptr<OperatorSliders>> operators;

    // Top controls
    juce::ComboBox algorithmSelector;
    juce::TextEditor patchNameEditor;
    juce::ComboBox channelSelector;
    juce::Label patchNameLabel;
    juce::Label algorithmLabel;
    juce::Label channelLabel;
    // Algorithm SVG overlay
    std::unique_ptr<juce::Drawable> algorithmSvg;
    int currentAlgorithm = 0;
    juce::Rectangle<float> svgDrawArea; // Area to draw SVG, for scaling
    std::vector<float> operatorRowCenters; // Y positions of operator row centers for SVG alignment

    // Status/help panel
    juce::Label statusBar;
    juce::Label helpPanel;

    void loadAlgorithmSvg(int algorithmIdx);
    void updateOperatorColors();
    void updateStatusBar(const juce::String& text);
    void setupOperatorSlider(juce::Slider& slider, const juce::String& name, int min, int max, int defaultValue);

    // Returns true if the given operator is a carrier for the current algorithm
    bool isCarrier(int opIdx) const;
    // Returns the carrier mask for the current algorithm (bitmask, op 0 = LSB)
    uint8_t getCarrierMaskForAlgorithm(int algoIdx) const;
    // Returns the carrier indices for the current algorithm (vector of operator indices, 0=OP1, 5=OP6)
    std::vector<int> getCarrierIndicesForAlgorithm(int algoIdx) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoiceEditorPanel)
};
