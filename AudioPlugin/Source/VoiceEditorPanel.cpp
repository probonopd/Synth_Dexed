#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "VoiceEditorPanel.h"
#include "BinaryData.h"
#include "OperatorSliderLookAndFeel.h"
#include <filesystem>
#include <juce_data_structures/juce_data_structures.h>

using namespace juce;

// Global instance for operator slider look
static OperatorSliderLookAndFeel operatorSliderLookAndFeel;

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
        defaultHelpText = "DX7 Voice Editor\nHover over a control for help.";
        helpPanel.setText(defaultHelpText, juce::dontSendNotification);
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
            // Apply custom look and feel to all operator sliders
            for (int s = 0; s < op->NumSliders; ++s) {
                op->sliders[s].setLookAndFeel(&operatorSliderLookAndFeel);
            }
            addAndMakeVisible(*op);
            operators.push_back(std::move(op));
        }

        algorithmSelector.onChange = [this]() {
            try {
                int idx = algorithmSelector.getSelectedId() - 1;
                std::cout << "[VoiceEditorPanel] algorithmSelector.onChange triggered, idx=" << idx << std::endl;
                currentAlgorithm = idx;
                loadAlgorithmSvg(idx);
                std::cout << "[VoiceEditorPanel] Calling resized() and repaint() after SVG load" << std::endl;
                resized(); // Ensure operatorRowCenters is up to date
                repaint();
                for (int i = 0; i < operators.size(); ++i) {
                    operators[i]->repaint();
                }
            } catch (const std::exception& e) {
                std::cout << "[VoiceEditorPanel] Exception in algorithmSelector.onChange: " << e.what() << std::endl;
            } catch (...) {
                std::cout << "[VoiceEditorPanel] Unknown exception in algorithmSelector.onChange" << std::endl;
            }
        };
        // Load initial SVG
        loadAlgorithmSvg(algorithmSelector.getSelectedId() - 1);
        loadHelpJson();
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
    // Do not paint the SVG here anymore; it will be painted in paintOverChildren
}

void VoiceEditorPanel::paintOverChildren(juce::Graphics& g) {
    // Draw SVG with precise alignment based on operator 6 and operator 1 row centers
    if (algorithmSvg && operatorRowCenters.size() >= 6) {
        std::cout << "[VoiceEditorPanel] Drawing algorithm SVG with precise alignment (paintOverChildren)" << std::endl;
        float op6Center = operatorRowCenters[0];
        float op1Center = operatorRowCenters[5];
        float uiDistance = op1Center - op6Center;
        std::cout << "[VoiceEditorPanel] Op6 center: " << op6Center << ", Op1 center: " << op1Center << std::endl;
        std::cout << "[VoiceEditorPanel] UI distance between op6 and op1: " << uiDistance << std::endl;
        auto svgBounds = algorithmSvg->getDrawableBounds();
        float svgNumberDistance = svgBounds.getHeight() * 0.71f;
        float scaleFactor = uiDistance / svgNumberDistance;
        float offsetX = 10.0f;
        float offsetY = svgDrawArea.getY() - svgBounds.getY() * scaleFactor;
        auto transform = AffineTransform::scale(scaleFactor, scaleFactor)
            .followedBy(AffineTransform::translation(offsetX, offsetY));
        std::cout << "[VoiceEditorPanel] SVG number distance estimate: " << svgNumberDistance << std::endl;
        std::cout << "[VoiceEditorPanel] Scale factor: " << scaleFactor << std::endl;
        std::cout << "[VoiceEditorPanel] Final offset: " << offsetX << ", " << offsetY << std::endl;
        algorithmSvg->draw(g, 1.0f, transform);
    } else if (algorithmSvg) {
        std::cout << "[VoiceEditorPanel] Drawing SVG without alignment (fallback, paintOverChildren)" << std::endl;
        auto svgBounds = algorithmSvg->getDrawableBounds();
        float scale = std::min(svgDrawArea.getWidth() / svgBounds.getWidth(), 
                              svgDrawArea.getHeight() / svgBounds.getHeight());
        float offsetX = -svgBounds.getX() * scale;
        float offsetY = svgDrawArea.getY() - svgBounds.getY() * scale;
        juce::AffineTransform transform = juce::AffineTransform::scale(scale)
                                                                .translated(offsetX, offsetY);
        algorithmSvg->draw(g, 1.0f, transform);
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

    int helpPanelWidth = 280;
    int svgPanelWidth = 100;
    int margin = 10;

    // SVG draw area: always at the left edge of the window, full height minus margins
    svgDrawArea = juce::Rectangle<float>(0, area.getY() + margin, svgPanelWidth, area.getHeight() - 2 * margin);

    // Help panel on the right
    helpPanel.setBounds(area.getRight() - helpPanelWidth, area.getY() + margin, helpPanelWidth - margin, area.getHeight() - 2 * margin);

    // Operator rows: span from after SVG area to help panel
    auto opArea = juce::Rectangle<int>(svgPanelWidth, area.getY() + margin, area.getWidth() - helpPanelWidth - svgPanelWidth - margin, area.getHeight() - 2 * margin);

    // Store operator row centers for SVG alignment
    operatorRowCenters.clear();
    if (!operators.empty()) {
        int opCount = 6;
        int opH = (opArea.getHeight() - 10) / opCount;
        int opNumColW = 36;
        for (int i = 0; i < opCount; ++i) {
            int opIdx = i;
            float rowCenterY = opArea.getY() + i * opH + opH * 0.5f;
            operatorRowCenters.push_back(rowCenterY);

            // Operator number label: positioned in the SVG area (left part of full-width row)
            juce::Rectangle<int> opNumBounds(
                0, // always at the left edge
                opArea.getY() + i * opH,
                opNumColW,
                opH - 2
            );
            if (operators[opIdx]) {
                operators[opIdx]->label.setBounds(opNumBounds.reduced(0, 0));
            }

            // Operator controls: positioned after SVG area but still within full-width row
            auto opBounds = juce::Rectangle<int>(
                0,
                opArea.getY() + i * opH,
                opArea.getWidth(),  // Take remaining width
                opH - 2
            );
            if (operators[opIdx]) {
                operators[opIdx]->setBounds(opBounds);
            }
        }
    }
}

void VoiceEditorPanel::loadAlgorithmSvg(int algorithmIdx) {
    std::cout << "[VoiceEditorPanel] loadAlgorithmSvg called with algorithmIdx=" << algorithmIdx << std::endl;
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
    std::cout << "[VoiceEditorPanel] loadAlgorithmSvg finished, calling repaint()" << std::endl;
    repaint();
}

void VoiceEditorPanel::loadHelpJson() {
    using namespace juce;
    // Use JUCE BinaryData for VCED.json
    auto* data = BinaryData::VCED_json;
    int dataSize = BinaryData::VCED_jsonSize;
    std::cout << "[VoiceEditorPanel] Loading help JSON from BinaryData (embedded resource)" << std::endl;
    juce::String jsonStr = juce::String::fromUTF8((const char*)data, dataSize);
    var json = JSON::parse(jsonStr);
    if (!json.isObject()) {
        std::cout << "[VoiceEditorPanel] VCED.json parse failed" << std::endl;
        return;
    }
    helpJson = json;
    helpTextByKey.clear();
    if (auto* params = json["parameters"].getArray()) {
        for (auto& p : *params) {
            auto* obj = p.getDynamicObject();
            if (obj) {
                auto key = obj->getProperty("key").toString().toStdString();
                auto desc = obj->getProperty("long_description").toString();
                if (desc.isEmpty()) desc = obj->getProperty("description").toString();
                helpTextByKey[key] = desc.toStdString();
            }
        }
    }
    // Log all loaded keys
    for (const auto& pair : helpTextByKey)
        std::cout << "[VoiceEditorPanel] Loaded help key: '" << pair.first << "'" << std::endl;
}

void VoiceEditorPanel::showHelpForKey(const juce::String& key) {
    std::cout << "[VoiceEditorPanel] showHelpForKey: key=" << key << std::endl;
    auto it = helpTextByKey.find(key.toStdString());
    if (it != helpTextByKey.end()) {
        std::cout << "[VoiceEditorPanel] Found help for key: " << key << std::endl;
        helpPanel.setText(it->second, juce::sendNotification);
        helpPanel.repaint();
    } else {
        std::cout << "[VoiceEditorPanel] No help found for key: " << key << std::endl;
        helpPanel.setText(defaultHelpText, juce::sendNotification);
        helpPanel.repaint();
    }
}

void VoiceEditorPanel::restoreDefaultHelp() {
    helpPanel.setText(defaultHelpText, juce::sendNotification);
    helpPanel.repaint();
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
    // Define which operators are carriers; 0=OP1, 5=OP6
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
}

// OperatorSliders implementation
VoiceEditorPanel::OperatorSliders::OperatorSliders() {
    for (int i = 0; i < NumSliders; ++i) {
        sliders[i].addMouseListener(this, true); // Ensure mouse events are forwarded
    }
    envWidget.addMouseListener(this, true);
    ksWidget.addMouseListener(this, true);
}

VoiceEditorPanel::OperatorSliders::~OperatorSliders() {}

void VoiceEditorPanel::OperatorSliders::addAndLayoutSliderWithLabel(juce::Slider& slider, juce::Label& label, const juce::String& text, int& x, int y, int w, int h, int gap) {
    addAndMakeVisible(slider);
    label.setText(text, juce::dontSendNotification);
    label.setFont(juce::Font(juce::FontOptions(10.0f)));
    label.setColour(juce::Label::textColourId, juce::Colours::white);
    label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);
    slider.setSliderStyle(juce::Slider::LinearVertical);
    // slider.setLookAndFeel(&operatorSliderLookAndFeel);
    slider.setBounds(x, y, w, h); x += w + gap;
    label.setBounds(x - w, y + h, w, 20);
}

void VoiceEditorPanel::OperatorSliders::resized() {
    auto area = getLocalBounds();
    label.setBounds(area.removeFromLeft(32).reduced(2));
    ksWidget.setBounds(area.removeFromRight(40).reduced(2));
    envWidget.setBounds(area.removeFromRight(40).reduced(2));
    int sliderW = 32, sliderH = area.getHeight() - 20;
    int gap = 0;
    int y = area.getY();
    int x = area.getX() + 60;
    // Layout: OPE, TL | PM, PC, PF, PD | AMS, TS, RS
    // Draw vertical spacers after TL and PD
    for (int i = 0; i < NumSliders; ++i) {
        addAndLayoutSliderWithLabel(sliders[i], sliderLabels[i], sliderNames[i], x, y, sliderW, sliderH, gap);
    }
}

void VoiceEditorPanel::OperatorSliders::mouseEnter(const juce::MouseEvent& e) {
    std::cout << "[OperatorSliders] mouseEnter: eventComponent=" << e.eventComponent << std::endl;
    for (int i = 0; i < NumSliders; ++i) {
        if (e.eventComponent == &sliders[i]) {
            std::cout << "[OperatorSliders] mouseEnter on slider " << sliderNames[i] << std::endl;
            sliderMouseEnter(i);
            return;
        }
    }
    if (e.eventComponent == &envWidget) {
        std::cout << "[OperatorSliders] mouseEnter on envWidget" << std::endl;
        if (auto* parent = dynamic_cast<VoiceEditorPanel*>(getParentComponent()))
            parent->showHelpForKey("R1");
        return;
    }
    if (e.eventComponent == &ksWidget) {
        std::cout << "[OperatorSliders] mouseEnter on ksWidget" << std::endl;
        if (auto* parent = dynamic_cast<VoiceEditorPanel*>(getParentComponent()))
            parent->showHelpForKey("RD");
        return;
    }
}

void VoiceEditorPanel::OperatorSliders::mouseExit(const juce::MouseEvent& e) {
    std::cout << "[OperatorSliders] mouseExit: eventComponent=" << e.eventComponent << std::endl;
    for (int i = 0; i < NumSliders; ++i) {
        if (e.eventComponent == &sliders[i]) {
            std::cout << "[OperatorSliders] mouseExit on slider " << sliderNames[i] << std::endl;
            sliderMouseExit(i);
            return;
        }
    }
    if (e.eventComponent == &envWidget || e.eventComponent == &ksWidget) {
        std::cout << "[OperatorSliders] mouseExit on envWidget or ksWidget" << std::endl;
        if (auto* parent = dynamic_cast<VoiceEditorPanel*>(getParentComponent()))
            parent->restoreDefaultHelp();
        return;
    }
}

void VoiceEditorPanel::OperatorSliders::mouseMove(const juce::MouseEvent& e) {
    std::cout << "[OperatorSliders] mouseMove: eventComponent=" << e.eventComponent << std::endl;
    for (int i = 0; i < NumSliders; ++i) {
        if (sliders[i].isMouseOver(true)) {
            std::cout << "[OperatorSliders] mouseMove over slider " << sliderNames[i] << std::endl;
            sliderMouseEnter(i);
            return;
        }
    }
}

void VoiceEditorPanel::OperatorSliders::sliderMouseEnter(int sliderIdx) {
    std::cout << "[OperatorSliders] sliderMouseEnter: " << sliderNames[sliderIdx] << std::endl;
    if (auto* parent = dynamic_cast<VoiceEditorPanel*>(getParentComponent()))
        parent->showHelpForKey(sliderNames[sliderIdx]);
}

void VoiceEditorPanel::OperatorSliders::sliderMouseExit(int sliderIdx) {
    std::cout << "[OperatorSliders] sliderMouseExit: " << sliderNames[sliderIdx] << std::endl;
    if (auto* parent = dynamic_cast<VoiceEditorPanel*>(getParentComponent()))
        parent->restoreDefaultHelp();
}

// EnvelopeDisplay mouse hover
