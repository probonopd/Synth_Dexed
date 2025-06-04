#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "VoiceEditorPanel.h"
#include "BinaryData.h"
#include <filesystem>
#include <juce_data_structures/juce_data_structures.h>

using namespace juce;

VoiceEditorPanel::VoiceEditorPanel()
{
    std::cout << "[VoiceEditorPanel] Constructor start" << std::endl;
    try {
        setBounds(0, 0, 1100, 700);
        setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(0xff2a2a2a));

        // Top controls
        algorithmLabel.setText("Algorithm", juce::dontSendNotification);
        algorithmLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(algorithmLabel);
        algorithmSelector.addItemList({"1","2","3","4","5","6","7","8","9","10","11","12","13","14","15","16","17","18","19","20","21","22","23","24","25","26","27","28","29","30","31","32"}, 1);
        algorithmSelector.setSelectedId(1);
        addAndMakeVisible(algorithmSelector);

        patchNameLabel.setText("Patch Name", juce::dontSendNotification);
        patchNameLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(patchNameLabel);
        patchNameEditor.setText("BRIGHT ST.");
        patchNameEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff222222));
        patchNameEditor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
        addAndMakeVisible(patchNameEditor);

        channelLabel.setText("Channel", juce::dontSendNotification);
        channelLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(channelLabel);
        for (int i = 1; i <= 16; ++i) channelSelector.addItem(juce::String(i), i);
        channelSelector.setSelectedId(1);
        addAndMakeVisible(channelSelector);

        // Status bar
        statusBar.setText("DX7 Voice Editor", juce::dontSendNotification);
        statusBar.setFont(juce::Font(juce::FontOptions(14.0f)));
        statusBar.setColour(juce::Label::backgroundColourId, juce::Colour(0xff333333));
        statusBar.setColour(juce::Label::textColourId, juce::Colours::white);
        statusBar.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(statusBar);

        // Help panel
        helpPanel.setText("Keyboard Scaling Right Depth (RD)\nRange: 0 – 99\n\nAmount of level scaling applied to notes to the right of the break point (higher notes).\n\nA higher value increases the volume of higher notes more, making them sound brighter and more present. A lower value decreases this effect, which can make higher notes sound more subdued. This balances the response of the instrument across its range.\n\nReference: Massey, The Complete DX7, chapter 12: Keyboard Level Scaling.", juce::dontSendNotification);
        helpPanel.setFont(juce::Font(12.0f));
        helpPanel.setColour(juce::Label::backgroundColourId, juce::Colour(0xff222222));
        helpPanel.setColour(juce::Label::textColourId, juce::Colours::white);
        helpPanel.setJustificationType(juce::Justification::topLeft);
        helpPanel.setBorderSize(juce::BorderSize<int>(10));
        addAndMakeVisible(helpPanel);

        // Create 6 operators
        operators.clear();
        for (int i = 0; i < 6; ++i) {
            auto op = std::make_unique<OperatorSliders>();
            op->label.setText(juce::String(6 - i), juce::dontSendNotification);
            op->label.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
            op->label.setColour(juce::Label::textColourId, juce::Colours::white);
            op->label.setJustificationType(juce::Justification::centred);
            addAndMakeVisible(*op);
            operators.push_back(std::move(op));
        }

        algorithmSelector.onChange = [this]() {
            try {
                int idx = algorithmSelector.getSelectedId() - 1;
                currentAlgorithm = idx;
                loadAlgorithmSvg(idx);
                for (int i = 0; i < operators.size(); ++i) {
                    operators[i]->repaint();
                }
                repaint();
            } catch (const std::exception& e) {
                std::cout << "[VoiceEditorPanel] Exception in algorithmSelector.onChange: " << e.what() << std::endl;
            } catch (...) {
                std::cout << "[VoiceEditorPanel] Unknown exception in algorithmSelector.onChange" << std::endl;
            }
        };
        // Load initial SVG
        loadAlgorithmSvg(algorithmSelector.getSelectedId() - 1);
    }
    catch (const std::exception& e) {
        std::cout << "[VoiceEditorPanel] Exception in constructor: " << e.what() << std::endl;
        throw;
    }
    catch (...) {
        std::cout << "[VoiceEditorPanel] Unknown exception in constructor" << std::endl;
        throw;
    }
}

VoiceEditorPanel::~VoiceEditorPanel() {
    // unique_ptr automatically handles cleanup
}

void VoiceEditorPanel::paint(Graphics& g) {
    g.fillAll(Colour(0xff332b28));
    // Draw SVG overlay if loaded
    if (algorithmSvg) {
        algorithmSvg->draw(g, 1.0f);
    }
}

void VoiceEditorPanel::resized() {
    auto area = getLocalBounds();

    // Status bar at top
    statusBar.setBounds(area.removeFromTop(30).reduced(10, 5));

    // Top controls (algorithm, patch name, channel)
    auto topControls = area.removeFromTop(40).reduced(10, 0);
    int colW = 120, gap = 10;
    algorithmLabel.setBounds(topControls.getX(), topControls.getY(), colW, 24);
    algorithmSelector.setBounds(topControls.getX() + colW, topControls.getY(), 60, 24);
    patchNameLabel.setBounds(topControls.getX() + colW + 60 + gap, topControls.getY(), colW, 24);
    patchNameEditor.setBounds(topControls.getX() + colW + 60 + gap + colW, topControls.getY(), 180, 24);
    channelLabel.setBounds(topControls.getX() + colW + 60 + gap + colW + 180 + gap, topControls.getY(), colW, 24);
    channelSelector.setBounds(topControls.getX() + colW + 60 + gap + colW + 180 + gap + colW, topControls.getY(), 60, 24);

    // Layout: SVG (left), operator sliders (center), help panel (right)
    int helpPanelWidth = 280;
    int svgPanelWidth = 200;
    int margin = 10;
    int opPanelWidth = area.getWidth() - svgPanelWidth - helpPanelWidth - 3 * margin;
    int opPanelX = area.getX() + svgPanelWidth + margin;

    // SVG area: fill most of the left side vertically
    auto svgArea = juce::Rectangle<int>(area.getX() + margin, area.getY() + margin, svgPanelWidth - margin, area.getHeight() - 2 * margin);
    svgDrawArea = svgArea.toFloat(); // Save for paint()

    // Help panel on the right
    helpPanel.setBounds(area.getRight() - helpPanelWidth, area.getY() + margin, helpPanelWidth - margin, area.getHeight() - 2 * margin);

    // Operator sliders: fill center area, stacked vertically
    auto opArea = juce::Rectangle<int>(opPanelX, area.getY() + margin, opPanelWidth, area.getHeight() - 2 * margin);
    if (!operators.empty()) {
        // Add a left column for operator numbers (SVG-style), vertically aligned with each operator row
        int opCount = 6;
        int opH = (opArea.getHeight() - 10) / opCount;
        int opNumColW = 36;
        for (int i = 0; i < opCount; ++i) {
            int opIdx = i;
            int opNum = 6 - i;
            juce::Rectangle<int> opNumBounds(
                opArea.getX(),
                opArea.getY() + i * opH,
                opNumColW,
                opH - 2
            );
            if (operators[opIdx]) {
                operators[opIdx]->label.setBounds(opNumBounds.reduced(0, 0));
            }
            // Operator row (rest of row, right of number)
            auto opBounds = juce::Rectangle<int>(
                opArea.getX() + opNumColW,
                opArea.getY() + i * opH,
                opArea.getWidth() - opNumColW,
                opH - 2
            );
            if (operators[opIdx]) {
                operators[opIdx]->setBounds(opBounds);
            }
        }
    }
}

void VoiceEditorPanel::loadAlgorithmSvg(int algorithmIdx) {
    // Use JUCE BinaryData for SVGs
    juce::String symbol = "algorithm" + juce::String(algorithmIdx + 1).paddedLeft('0', 2) + "_svg";
    juce::String symbolSize = symbol + "Size";
    std::cout << "[VoiceEditorPanel] Attempting to load SVG from BinaryData: " << symbol << std::endl;
    const void* data = nullptr;
    int dataSize = 0;
    #define GET_BINARYDATA_PTR(N) \
        if (symbol == "algorithm" #N "_svg") { \
            data = BinaryData::algorithm##N##_svg; \
            dataSize = BinaryData::algorithm##N##_svgSize; \
        }
    GET_BINARYDATA_PTR(01) else GET_BINARYDATA_PTR(02) else GET_BINARYDATA_PTR(03) else GET_BINARYDATA_PTR(04) else GET_BINARYDATA_PTR(05) else GET_BINARYDATA_PTR(06) else GET_BINARYDATA_PTR(07) else GET_BINARYDATA_PTR(08) else GET_BINARYDATA_PTR(09) else GET_BINARYDATA_PTR(10) else GET_BINARYDATA_PTR(11) else GET_BINARYDATA_PTR(12) else GET_BINARYDATA_PTR(13) else GET_BINARYDATA_PTR(14) else GET_BINARYDATA_PTR(15) else GET_BINARYDATA_PTR(16) else GET_BINARYDATA_PTR(17) else GET_BINARYDATA_PTR(18) else GET_BINARYDATA_PTR(19) else GET_BINARYDATA_PTR(20) else GET_BINARYDATA_PTR(21) else GET_BINARYDATA_PTR(22) else GET_BINARYDATA_PTR(23) else GET_BINARYDATA_PTR(24) else GET_BINARYDATA_PTR(25) else GET_BINARYDATA_PTR(26) else GET_BINARYDATA_PTR(27) else GET_BINARYDATA_PTR(28) else GET_BINARYDATA_PTR(29) else GET_BINARYDATA_PTR(30) else GET_BINARYDATA_PTR(31) else GET_BINARYDATA_PTR(32);
    #undef GET_BINARYDATA_PTR
    if (data && dataSize > 0) {
        std::unique_ptr<juce::XmlElement> svgXml = juce::XmlDocument::parse(juce::String::fromUTF8((const char*)data, dataSize));
        if (svgXml) {
            algorithmSvg.reset(juce::Drawable::createFromSVG(*svgXml).release());
            std::cout << "[VoiceEditorPanel] SVG loaded successfully for algorithm " << (algorithmIdx + 1) << std::endl;
        } else {
            algorithmSvg.reset();
            std::cout << "[VoiceEditorPanel] SVG XML parse failed for BinaryData: " << symbol << std::endl;
        }
    } else {
        algorithmSvg.reset();
        std::cout << "[VoiceEditorPanel] SVG not found in BinaryData: " << symbol << std::endl;
    }
}

void VoiceEditorPanel::updateOperatorColors() {
    // Skip operator operations since operators vector is not initialized in minimal version
    // This will be implemented later when full operator support is added
    std::cout << "[VoiceEditorPanel] updateOperatorColors() called (minimal implementation)" << std::endl;
}

void VoiceEditorPanel::updateStatusBar(const String& text) {
    statusBar.setText(text, dontSendNotification);
}

void VoiceEditorPanel::setupOperatorSlider(Slider& slider, const String& /*name*/, int min, int max, int defaultValue) {
    // Example: slider.setRange((float)min, (float)max, 1.0f);
    slider.setRange(static_cast<double>(min), static_cast<double>(max), 1.0);
    slider.setValue(static_cast<double>(defaultValue));
    slider.setSliderStyle(Slider::LinearVertical);
    slider.setTextBoxStyle(Slider::TextBoxBelow, false, 40, 20);
    slider.setColour(Slider::trackColourId, Colour(0xff555555));
    slider.setColour(Slider::thumbColourId, Colour(0xffaaaaaa));
    slider.setColour(Slider::textBoxTextColourId, Colours::white);
    slider.setColour(Slider::textBoxBackgroundColourId, Colour(0xff333333));
}

std::vector<int> VoiceEditorPanel::getCarrierIndicesForAlgorithm(int algoIdx) const {
    // Ported from Python DX7_CARRIER_MAP, 0=OP1, 5=OP6
    static const std::vector<std::vector<int>> carrierMap = {
        {0, 2}, {0, 2}, {0, 3}, {0, 3}, {0, 2, 4}, {0, 2, 4}, {0, 2}, {0, 2},
        {0, 2}, {0, 3}, {0, 3}, {0, 2}, {0, 2}, {0, 2}, {0, 2}, {0},
        {0}, {0}, {0, 3, 4}, {0, 1, 3}, {0, 1, 3, 4}, {0, 2, 3, 4}, {0, 1, 3, 4},
        {0, 1, 2, 3, 4}, {0, 1, 2, 3, 4}, {0, 1, 3}, {0, 1, 3}, {0, 2, 5}, {0, 1, 2, 4},
        {0, 1, 2, 5}, {0, 1, 2, 3, 4}, {0, 1, 2, 3, 4, 5}
    };
    if (algoIdx < 0 || algoIdx >= (int)carrierMap.size()) return {};
    return carrierMap[algoIdx];
}

bool VoiceEditorPanel::isCarrier(int opIdx) const {
    // opIdx: 0 = OP1, 5 = OP6
    auto carriers = getCarrierIndicesForAlgorithm(currentAlgorithm);
    for (int c : carriers) if (c == opIdx) return true;
    return false;
}

void VoiceEditorPanel::OperatorSliders::paint(Graphics& g) {
    auto area = getLocalBounds().toFloat();
    // Draw background: greenish if carrier, else dark
    bool carrier = false;
    if (auto* parent = dynamic_cast<VoiceEditorPanel*>(getParentComponent())) {
        // Map row index to operator index: row 0 (top) = OP6, row 5 (bottom) = OP1
        int opNum = label.getText().getIntValue(); // label is "6" for OP6, ... "1" for OP1
        int opIdx = opNum - 1; // OP1 = 0, OP6 = 5
        carrier = parent->isCarrier(opIdx);
    }
    g.setColour(carrier ? Colour(0xff1e3d2f) : Colour(0xff222024));
    g.fillRoundedRectangle(area, 6.0f);
    // Draw turquoise border
    g.setColour(Colour(0xff00e6ff));
    g.drawRoundedRectangle(area, 6.0f, 2.0f);
}
