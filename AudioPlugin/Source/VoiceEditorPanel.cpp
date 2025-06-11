#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "VoiceEditorPanel.h"
#include "BinaryData.h"
#include "OperatorSliderLookAndFeel.h"
#include <filesystem>
#include <juce_data_structures/juce_data_structures.h>
#include "FMRackController.h"

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
        float op6Center = operatorRowCenters[0];
        float op1Center = operatorRowCenters[5];
        float uiDistance = op1Center - op6Center;
        auto svgBounds = algorithmSvg->getDrawableBounds();
        float svgNumberDistance = svgBounds.getHeight() * 0.71f;
        float scaleFactor = uiDistance / svgNumberDistance;
        float offsetX = 10.0f;
        float offsetY = svgDrawArea.getY() - svgBounds.getY() * scaleFactor;
        auto transform = AffineTransform::scale(scaleFactor, scaleFactor)
            .followedBy(AffineTransform::translation(offsetX, offsetY));
        algorithmSvg->draw(g, 1.0f, transform);
    } else if (algorithmSvg) {
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
                juce::String name = obj->getProperty("name").toString();
                juce::String range = obj->getProperty("range").toString();
                juce::String desc = obj->getProperty("long_description").toString();
                if (desc.isEmpty()) desc = obj->getProperty("description").toString();
                juce::String shortDesc = obj->getProperty("description").toString();
                juce::String reference = obj->getProperty("reference").toString();

                juce::String hoverText;
                // First line: name (range)
                hoverText << name;
                if (!range.isEmpty())
                    hoverText << " (" << range << ")";
                hoverText << "\n";
                // Second line: short description or range
                if (!shortDesc.isEmpty() && shortDesc != desc)
                    hoverText << shortDesc << "\n";
                // Blank line
                hoverText << "\n";
                // Long description
                hoverText << desc << "\n";
                // Reference, if present
                if (!reference.isEmpty())
                    hoverText << "\nReference: " << reference << "\n";
                helpTextByKey[key] = hoverText.toStdString();
            }
        }
    }
    // Log all loaded keys
    for (const auto& pair : helpTextByKey)
        std::cout << "[VoiceEditorPanel] Loaded help key: '" << pair.first << "'" << std::endl;
}

void VoiceEditorPanel::showHelpForKey(const juce::String& key) {
    auto it = helpTextByKey.find(key.toStdString());
    if (it != helpTextByKey.end()) {
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

void VoiceEditorPanel::setupOperatorSlider(Slider& slider, const String& name, int /*min*/, int /*max*/, int /*defaultValue*/) {
    std::cout << "[VoiceEditorPanel] setupOperatorSlider called for '" << name << "'" << std::endl;
    double minValue = 0.0, maxValue = 99.0, defaultValue = 0.0;
    double step = 1.0;
    bool isDiscrete = false;
    std::vector<double> allowedValues;
    std::vector<juce::String> allowedLabels;
    if (helpJson.isObject()) {
        if (auto* params = helpJson["parameters"].getArray()) {
            for (auto& p : *params) {
                auto* obj = p.getDynamicObject();
                if (obj && obj->hasProperty("key") && obj->getProperty("key").toString().equalsIgnoreCase(name)) {
                    if (obj->hasProperty("min"))
                        minValue = obj->getProperty("min").toString().getDoubleValue();
                    else
                        std::cout << "[VoiceEditorPanel] Warning: Slider '" << name << "' has no 'min' property." << std::endl;
                    if (obj->hasProperty("max"))
                        maxValue = obj->getProperty("max").toString().getDoubleValue();
                    else
                        std::cout << "[VoiceEditorPanel] Warning: Slider '" << name << "' has no 'max' property." << std::endl;
                    if (obj->hasProperty("default"))
                        defaultValue = obj->getProperty("default").toString().getDoubleValue();
                    else
                        defaultValue = minValue;
                    if (obj->hasProperty("values")) {
                        isDiscrete = true;
                        step = 1.0;
                        auto valuesVar = obj->getProperty("values");
                        if (valuesVar.isObject()) {
                            auto* valuesObj = valuesVar.getDynamicObject();
                            auto& props = valuesObj->getProperties();
                            for (int i = 0; i < props.size(); ++i) {
                                auto keyStr = props.getName(i).toString();
                                allowedValues.push_back(keyStr.getDoubleValue());
                                allowedLabels.push_back(props.getValueAt(i).toString());
                            }
                        } else if (valuesVar.isArray()) {
                            auto* arr = valuesVar.getArray();
                            for (auto& v : *arr) {
                                allowedValues.push_back((int)v);
                            }
                        }
                        std::sort(allowedValues.begin(), allowedValues.end());
                        if (!allowedValues.empty()) {
                            minValue = 0;
                            maxValue = (double)(allowedValues.size() - 1);
                            auto it = std::find(allowedValues.begin(), allowedValues.end(), (int)defaultValue);
                            if (it != allowedValues.end())
                                defaultValue = std::distance(allowedValues.begin(), it);
                            else
                                defaultValue = 0;
                            // Log allowable values for this slider
                            std::cout << "[VoiceEditorPanel] Slider '" << name << "' allowable values: ";
                            for (size_t i = 0; i < allowedValues.size(); ++i) {
                                std::cout << allowedValues[i];
                                if (i != allowedValues.size() - 1) std::cout << ", ";
                            }
                            std::cout << std::endl;
                        }
                    }
                    // Log allowable range for all sliders with min/max
                    if (obj->hasProperty("min") && obj->hasProperty("max")) {
                        std::cout << "[VoiceEditorPanel] Slider '" << name << "' allowable range: "
                                  << obj->getProperty("min").toString() << " to " << obj->getProperty("max").toString() << std::endl;
                    }
                    // Enforce min/max for all sliders
                    minValue = obj->hasProperty("min") ? obj->getProperty("min").toString().getDoubleValue() : minValue;
                    maxValue = obj->hasProperty("max") ? obj->getProperty("max").toString().getDoubleValue() : maxValue;
                    break;
                }
            }
        }
    }
    /*
    // Not discrete: normal slider
    slider.setRange(minValue, maxValue, step);
    slider.setValue(defaultValue);
    slider.setSliderStyle(Slider::LinearVertical);
    slider.setTextBoxStyle(Slider::TextBoxAbove, false, 40, 10); // Value box above the slider
    // No decimal places in the text box
    slider.setNumDecimalPlacesToDisplay(0);
    slider.setColour(Slider::trackColourId, Colour(0xff555555));
    slider.setColour(Slider::thumbColourId, Colour(0xffaaaaaa));
    slider.setColour(Slider::textBoxTextColourId, Colours::white);
    slider.setColour(Slider::textBoxBackgroundColourId, Colour(0xff333333));
    if (isDiscrete) {
        slider.setNumDecimalPlacesToDisplay(0);
    }
    */
}

// Synchronize slider value with Dexed engine
void VoiceEditorPanel::syncOperatorSliderWithDexed(Slider& slider, uint8_t paramAddress, const char* sliderKey) {
    if (controller && sliderKey) {
        uint8_t value = getDexedParam(paramAddress);
        auto range = getDexedRange(sliderKey);
        slider.setValue(static_cast<double>(value), juce::dontSendNotification);
        slider.setRange(range.first, range.second, 1.0);
    }
}

void VoiceEditorPanel::syncAllOperatorSlidersWithDexed() {
    static const uint8_t dexedParamOffsets[OperatorSliders::NumSliders] = {
        0, 16, 14, 18, 19, 20, 15, 17, 13
    };
    std::cout << "[VoiceEditorPanel] syncAllOperatorSlidersWithDexed: syncing all operator sliders from Dexed engine" << std::endl;
    uint8_t opeBitmask = getDexedParam(155);
    int numOps = static_cast<int>(operators.size());
    for (int uiRowIdx = 0; uiRowIdx < numOps; ++uiRowIdx) {
        auto& op = operators[uiRowIdx];
        int dexedOpIdx = numOps - 1 - uiRowIdx; // UI row 0 (top) = OP6 (dexed 5), row 5 (bottom) = OP1 (dexed 0)
        op->sliders[0].setValue((opeBitmask & (1 << dexedOpIdx)) ? 1 : 0, juce::dontSendNotification);
        for (int s = 1; s < OperatorSliders::NumSliders; ++s) {
            uint8_t paramAddress = static_cast<uint8_t>(dexedOpIdx * 21 + dexedParamOffsets[s]);
            uint8_t value = getDexedParam(paramAddress);
            std::cout << "  OP" << (dexedOpIdx+1) << " " << OperatorSliders::sliderNames[s] << " paramAddr=" << (int)paramAddress << " value=" << (int)value << std::endl;
            syncOperatorSliderWithDexed(op->sliders[s], paramAddress, OperatorSliders::sliderNames[s]);
        }
    }
    repaint();
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
    g.setColour(carrier ? Colour(44, 67, 63) : Colour(33, 33, 33)); // greenish for carrier, dark for non-carrier
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
    label.setBounds(x - w, y + h, w, 10); // Text label (slider name) below the slider
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, w, 10); // Value box below the slider
    // No decimal places in the text box
    slider.setNumDecimalPlacesToDisplay(0);
    slider.setColour(Slider::textBoxTextColourId, juce::Colours::white);
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

    static const uint8_t dexedParamOffsets[NumSliders] = {
        0, 16, 14, 18, 19, 20, 15, 17, 13
    };

    // Find our UI row index (0 = top, 5 = bottom)
    int uiRowIdx = -1;
    if (auto* parent = dynamic_cast<VoiceEditorPanel*>(getParentComponent())) {
        for (int idx = 0; idx < parent->operators.size(); ++idx) {
            if (parent->operators[idx].get() == this) {
                uiRowIdx = idx;
                break;
            }
        }
    }
    int dexedOpIdx = (uiRowIdx >= 0) ? (5 - uiRowIdx) : 0;

    for (int i = 0; i < NumSliders; ++i) {
        addAndLayoutSliderWithLabel(sliders[i], sliderLabels[i], sliderNames[i], x, y, sliderW, sliderH, gap);
        if (auto* parent = dynamic_cast<VoiceEditorPanel*>(getParentComponent())) {
            uint8_t paramAddress = static_cast<uint8_t>(dexedOpIdx * 21 + dexedParamOffsets[i]);
            parent->syncOperatorSliderWithDexed(sliders[i], paramAddress, sliderNames[i]);
            if (i == 0) {
                sliders[i].setRange(0, 1, 1.0);
                sliders[i].onValueChange = [parent]() {
                    uint8_t bitmask = 0;
                    for (int op = 0; op < parent->operators.size(); ++op) {
                        int dexedOp = 5 - op;
                        auto& opSliders = parent->operators[op];
                        int val = static_cast<int>(opSliders->sliders[0].getValue());
                        if (val != 0) bitmask |= (1 << dexedOp);
                    }
                    parent->setDexedParam(155, bitmask);
                };
            } else {
                sliders[i].onValueChange = [parent, paramAddress, i, this]() {
                    int value = static_cast<int>(sliders[i].getValue());
                    parent->setDexedParam(paramAddress, static_cast<uint8_t>(value));
                };
            }
        }
    }
}

void VoiceEditorPanel::OperatorSliders::mouseEnter(const juce::MouseEvent& e) {
    for (int i = 0; i < NumSliders; ++i) {
        if (e.eventComponent == &sliders[i]) {
            sliderMouseEnter(i);
            return;
        }
    }
    if (e.eventComponent == &envWidget) {
        if (auto* parent = dynamic_cast<VoiceEditorPanel*>(getParentComponent()))
            parent->showHelpForKey("R1");
        return;
    }
    if (e.eventComponent == &ksWidget) {
        if (auto* parent = dynamic_cast<VoiceEditorPanel*>(getParentComponent()))
            parent->showHelpForKey("RD");
        return;
    }
}

void VoiceEditorPanel::OperatorSliders::mouseExit(const juce::MouseEvent& e) {
    for (int i = 0; i < NumSliders; ++i) {
        if (e.eventComponent == &sliders[i]) {
            sliderMouseExit(i);
            return;
        }
    }
    if (e.eventComponent == &envWidget || e.eventComponent == &ksWidget) {
        if (auto* parent = dynamic_cast<VoiceEditorPanel*>(getParentComponent()))
            parent->restoreDefaultHelp();
        return;
    }
}

void VoiceEditorPanel::OperatorSliders::mouseMove(const juce::MouseEvent& e) {
    for (int i = 0; i < NumSliders; ++i) {
        if (sliders[i].isMouseOver(true)) {
            sliderMouseEnter(i);
            return;
        }
    }
}

void VoiceEditorPanel::OperatorSliders::sliderMouseEnter(int sliderIdx) {
    if (auto* parent = dynamic_cast<VoiceEditorPanel*>(getParentComponent()))
        parent->showHelpForKey(sliderNames[sliderIdx]);
}

void VoiceEditorPanel::OperatorSliders::sliderMouseExit(int sliderIdx) {
    if (auto* parent = dynamic_cast<VoiceEditorPanel*>(getParentComponent()))
        parent->restoreDefaultHelp();
}

// EnvelopeDisplay mouse hover
uint8_t VoiceEditorPanel::getDexedParam(uint8_t address) const {
    if (!controller) { std::cout << "[VoiceEditorPanel] getDexedParam: controller is null" << std::endl; return 0; }
    auto rack = controller->getRack();
    if (!rack) { std::cout << "[VoiceEditorPanel] getDexedParam: rack is null" << std::endl; return 0; }
    const auto& modules = rack->getModules();
    if (modules.empty()) { std::cout << "[VoiceEditorPanel] getDexedParam: modules empty" << std::endl; return 0; }
    auto* dexed = modules[0]->getDexedEngine();
    if (!dexed) { std::cout << "[VoiceEditorPanel] getDexedParam: dexed is null" << std::endl; return 0; }
    uint8_t v = dexed->getVoiceDataElement(address);
    std::cout << "[VoiceEditorPanel] getDexedParam: address=" << (int)address << " value=" << (int)v << std::endl;
    return v;
}


std::pair<uint8_t, uint8_t> VoiceEditorPanel::getDexedRange(const char* sliderKey) const {
    const char* key = sliderKey;
    if (key && helpJson.isObject()) {
        if (auto* params = helpJson["parameters"].getArray()) {
            for (auto& p : *params) {
                auto* obj = p.getDynamicObject();
                if (obj && obj->hasProperty("key") && obj->getProperty("key").toString().equalsIgnoreCase(key)) {
                    uint8_t minVal = obj->hasProperty("min") ? (uint8_t)obj->getProperty("min").toString().getIntValue() : 0;
                    uint8_t maxVal = obj->hasProperty("max") ? (uint8_t)obj->getProperty("max").toString().getIntValue() : 99;
                    return {minVal, maxVal};
                }
            }
        }
    }
    // Fallback
    return {0, 99};
}

void VoiceEditorPanel::setController(FMRackController* controller_) {
    controller = controller_;
    std::cout << "[VoiceEditorPanel] setController called. controller=" << controller << std::endl;
}

void VoiceEditorPanel::setDexedParam(uint8_t address, uint8_t value) {
    if (controller) {
        controller->setDexedParam(address, value);
    }
}
