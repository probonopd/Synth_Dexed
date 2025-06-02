#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "EnvelopeDisplay.h"
#include "KeyboardScalingDisplay.h"

// VoiceEditorPanel: A panel for editing a single DX7 voice, styled after the classic DX7 UI.
// Envelope and keyboard scaling widgets are placeholders for now.
class VoiceEditorPanel : public juce::Component {
public:
    VoiceEditorPanel();
    ~VoiceEditorPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    // Operator slider group
    struct OperatorSliders : public juce::Component {
        // Main operator controls (expanded for full DX7 parameter set)
        juce::Slider tl, ratio, coarse, fine, egR1, egR2, egR3, egR4, egL1, egL2, egL3, egL4, levSclBrkPt, sclLeftDepth, sclRightDepth, sclLeftCurve, sclRightCurve, rateScaling, ampModSense, keyVelSense, oscMode, freqCoarse, freqFine, detune, ampModEnable;
        juce::Label label;
        juce::Label tlLabel, ratioLabel, coarseLabel, fineLabel, egR1Label, egR2Label, egR3Label, egR4Label, egL1Label, egL2Label, egL3Label, egL4Label, levSclBrkPtLabel, sclLeftDepthLabel, sclRightDepthLabel, sclLeftCurveLabel, sclRightCurveLabel, rateScalingLabel, ampModSenseLabel, keyVelSenseLabel, oscModeLabel, freqCoarseLabel, freqFineLabel, detuneLabel, ampModEnableLabel;
        // Envelope and keyboard scaling widgets
        EnvelopeDisplay envWidget;
        KeyboardScalingDisplay ksWidget;

        OperatorSliders() {
            // Add main label
            addAndMakeVisible(label);
            // Add envelope and keyboard scaling widgets
            addAndMakeVisible(envWidget);
            addAndMakeVisible(ksWidget);
            // Add the controls shown in the Python reference, in order:
            addAndMakeVisible(tl); setupSliderLabel(tlLabel, "TL");
            addAndMakeVisible(egR1); setupSliderLabel(egR1Label, "PM");
            addAndMakeVisible(egR2); setupSliderLabel(egR2Label, "PC");
            addAndMakeVisible(egR3); setupSliderLabel(egR3Label, "PF");
            addAndMakeVisible(egR4); setupSliderLabel(egR4Label, "PD");
            addAndMakeVisible(ampModSense); setupSliderLabel(ampModSenseLabel, "AMS");
            addAndMakeVisible(keyVelSense); setupSliderLabel(keyVelSenseLabel, "TS");
            addAndMakeVisible(rateScaling); setupSliderLabel(rateScalingLabel, "RS");
        }
        
        void setupSliderLabel(juce::Label& sliderLabel, const juce::String& text) {
            sliderLabel.setText(text, juce::dontSendNotification);
            sliderLabel.setFont(juce::Font(juce::FontOptions(10.0f)));
            sliderLabel.setColour(juce::Label::textColourId, juce::Colours::white);
            sliderLabel.setJustificationType(juce::Justification::centred);
            addAndMakeVisible(sliderLabel);
        }

        ~OperatorSliders() override {
        }

        void paint(juce::Graphics&) override;
        
        void resized() override {
            auto area = getLocalBounds();

            // Operator number label at left
            label.setBounds(area.removeFromLeft(32).reduced(2));

            // Envelope and keyboard scaling widgets at right
            ksWidget.setBounds(area.removeFromRight(40).reduced(2));
            envWidget.setBounds(area.removeFromRight(40).reduced(2));

            // Arrange the visible sliders in a horizontal row, all vertical
            int sliderW = 32, sliderH = area.getHeight() - 20;
            int gap = 8;
            int y = area.getY();
            int x = area.getX();

            // TL
            tl.setSliderStyle(juce::Slider::LinearVertical);
            tl.setBounds(x, y, sliderW, sliderH); x += sliderW + gap;
            tlLabel.setBounds(x - sliderW, y + sliderH, sliderW, 20);
            // PM
            egR1.setSliderStyle(juce::Slider::LinearVertical);
            egR1.setBounds(x, y, sliderW, sliderH); x += sliderW + gap;
            egR1Label.setBounds(x - sliderW, y + sliderH, sliderW, 20);
            // PC
            egR2.setSliderStyle(juce::Slider::LinearVertical);
            egR2.setBounds(x, y, sliderW, sliderH); x += sliderW + gap;
            egR2Label.setBounds(x - sliderW, y + sliderH, sliderW, 20);
            // PF
            egR3.setSliderStyle(juce::Slider::LinearVertical);
            egR3.setBounds(x, y, sliderW, sliderH); x += sliderW + gap;
            egR3Label.setBounds(x - sliderW, y + sliderH, sliderW, 20);
            // PD
            egR4.setSliderStyle(juce::Slider::LinearVertical);
            egR4.setBounds(x, y, sliderW, sliderH); x += sliderW + gap;
            egR4Label.setBounds(x - sliderW, y + sliderH, sliderW, 20);
            // AMS
            ampModSense.setSliderStyle(juce::Slider::LinearVertical);
            ampModSense.setBounds(x, y, sliderW, sliderH); x += sliderW + gap;
            ampModSenseLabel.setBounds(x - sliderW, y + sliderH, sliderW, 20);
            // TS
            keyVelSense.setSliderStyle(juce::Slider::LinearVertical);
            keyVelSense.setBounds(x, y, sliderW, sliderH); x += sliderW + gap;
            keyVelSenseLabel.setBounds(x - sliderW, y + sliderH, sliderW, 20);
            // RS
            rateScaling.setSliderStyle(juce::Slider::LinearVertical);
            rateScaling.setBounds(x, y, sliderW, sliderH); x += sliderW + gap;
            rateScalingLabel.setBounds(x - sliderW, y + sliderH, sliderW, 20);
        }
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
