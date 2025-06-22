#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "EnvelopeDisplay.h"
#include "KeyboardScalingDisplay.h"
#include <juce_data_structures/juce_data_structures.h>
#include <map>
#include "FMRackController.h"

// #include "OperatorSliderLookAndFeel.h"

// VoiceEditorPanel: A panel for editing a single DX7 voice, styled after the classic DX7 UI.
// Envelope and keyboard scaling widgets are placeholders for now.
class VoiceEditorPanel : public juce::Component {
public:
    VoiceEditorPanel();
    ~VoiceEditorPanel() override;
    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;
    void showHelpForKey(const juce::String& key);
    void restoreDefaultHelp();

    // Set the controller pointer for backend access
    void setController(FMRackController* controller_);
    // Set a Dexed parameter (address, value)
    void setDexedParam(uint8_t address, uint8_t value);
    // Get a Dexed parameter (address)
    uint8_t getDexedParam(uint8_t address) const;

    // Get Dexed parameter min/max range by sliderKey
    std::pair<uint8_t, uint8_t> getDexedRange(const char* sliderKey) const;
   
    // Operator slider group
    struct OperatorSliders : public juce::Component {
        // Define slider list macro for enum and string names
#define OPERATOR_SLIDER_LIST \
    X(OPE) \
    X(TL) \
    X(PM) \
    X(PC) \
    X(PF) \
    X(PD) \
    X(AMS) \
    X(TS) \
    X(RS)

        enum SliderId {
#define X(name) name,
            OPERATOR_SLIDER_LIST
#undef X
            NumSliders
        };
        static constexpr const char* sliderNames[NumSliders] = {
#define X(name) #name,
            OPERATOR_SLIDER_LIST
#undef X
        };
        std::array<juce::Slider, NumSliders> sliders;
        std::array<juce::Label, NumSliders> sliderLabels;
        juce::Label label;
        EnvelopeDisplay envWidget;
        KeyboardScalingDisplay ksWidget;

        OperatorSliders(); // Declaration only, implementation in cpp file
        ~OperatorSliders() override;
        void paint(juce::Graphics&) override;
        void resized() override;
        void addAndLayoutSliderWithLabel(juce::Slider& slider, juce::Label& label, const juce::String& text, int& x, int y, int w, int h, int gap);
        void mouseEnter(const juce::MouseEvent&) override;
        void mouseExit(const juce::MouseEvent&) override;
        void mouseMove(const juce::MouseEvent&) override;
        void sliderMouseEnter(int sliderIdx);
        void sliderMouseExit(int sliderIdx);
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

    // help panel
    juce::Label helpPanel;

    // Help data loaded from JSON
    juce::var helpJson;
    std::map<std::string, std::string> helpTextByKey;
    juce::String defaultHelpText;

    void loadAlgorithmSvg(int algorithmIdx);
    void setupOperatorSlider(juce::Slider& slider, const juce::String& name, int min, int max, int defaultValue);
    void loadHelpJson();

    // Returns true if the given operator is a carrier for the current algorithm
    bool isCarrier(int opIdx) const;
    // Returns the carrier mask for the current algorithm (bitmask, op 0 = LSB)
    uint8_t getCarrierMaskForAlgorithm(int algoIdx) const;
    // Returns the carrier indices for the current algorithm (vector of operator indices, 0=OP1, 5=OP6)
    std::vector<int> getCarrierIndicesForAlgorithm(int algoIdx) const;

    // Synchronize slider value with Dexed engine, using sliderKey for range
    void syncOperatorSliderWithDexed(juce::Slider& slider, uint8_t paramAddress, const char* sliderKey);
    // Synchronize all operator sliders with Dexed engine (call after loading a new performance)
    void syncAllOperatorSlidersWithDexed();

    friend struct OperatorSliders;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoiceEditorPanel)
private:
    FMRackController* controller = nullptr;
    juce::TextButton requestDumpButton;
    void onSingleVoiceDumpReceived(const std::vector<uint8_t>& data);
};
