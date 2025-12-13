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
// static OperatorSliderLookAndFeel operatorSliderLookAndFeel;

namespace
{
    constexpr int kOuterMargin = 12;
    constexpr int kSectionGap = 12;
    constexpr int kColumnGap = 16;
    constexpr int kTopRowHeight = 36;
    constexpr int kHelpPanelWidth = 300;
    constexpr int kSvgPanelWidth = 140;
    constexpr int kOperatorLabelWidth = 36;
    constexpr int kWidgetWidth = 128;
    constexpr int kWidgetInnerGap = 12;
    constexpr int kWidgetColumnWidth = kWidgetWidth * 2 + kWidgetInnerGap;
    constexpr int kRowGap = 10;
    constexpr int kRowVerticalPadding = 4;
    constexpr int kRowHorizontalPadding = 6;
    constexpr int kSliderGap = 8;
    constexpr int kSliderLabelHeight = 16;
    constexpr int kSliderTextBoxHeight = 18;
    constexpr int kMinSliderWidth = 28;

    void layoutSliderWithLabel(juce::Slider& slider,
                               juce::Label& label,
                               const juce::String& text,
                               const juce::Rectangle<int>& totalBounds,
                               int textBoxHeight)
    {
        auto sliderBounds = totalBounds.withHeight(totalBounds.getHeight() - kSliderLabelHeight);
        slider.setBounds(sliderBounds);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, sliderBounds.getWidth(), textBoxHeight);
        slider.setNumDecimalPlacesToDisplay(0);

        label.setText(text, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        juce::Rectangle<int> labelBounds(totalBounds.getX(),
                                         totalBounds.getBottom() - kSliderLabelHeight,
                                         totalBounds.getWidth(),
                                         kSliderLabelHeight);
        label.setBounds(labelBounds);
    }
}

VoiceEditorPanel::VoiceEditorPanel()
{
    std::cout << "[VoiceEditorPanel] Constructor start" << std::endl;
    try {
        loadHelpJson(); // <-- Moved to the start to ensure helpJson and operatorSliderParamOffsets are initialized

        setBounds(0, 0, 1100, 700);
        setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(0xff2a2a2a));

        // Top controls
        algorithmLabel.setText("Algorithm", juce::dontSendNotification);
        algorithmLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(algorithmLabel);
        algorithmSelector.addItemList({"1","2","3","4","5","6","7","8","9","10","11","12","13","14","15","16","17","18","19","20","21","22","23","24","25","26","27","28","29","30","31","32"}, 1);
        algorithmSelector.setSelectedId(1);
        addAndMakeVisible(algorithmSelector);

        voiceNameLabel.setText("Name", juce::dontSendNotification);
        voiceNameLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(voiceNameLabel);
        voiceNameEditor.setText("UNKNOWN   ");
        voiceNameEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff222222));
        voiceNameEditor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
        addAndMakeVisible(voiceNameEditor);

        channelLabel.setText("Channel", juce::dontSendNotification);
        channelLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(channelLabel);
        for (int i = 1; i <= 16; ++i) channelSelector.addItem(juce::String(i), i);
        channelSelector.setSelectedId(1);
        addAndMakeVisible(channelSelector);

        // Request dump button
        requestDumpButton.setButtonText("Request dump");
        addAndMakeVisible(requestDumpButton);
        requestDumpButton.onClick = [this]() {
            if (controller) controller->requestSingleVoiceDump(channelSelector.getSelectedId());
        };

        // Help panel
        defaultHelpText = "DX7 Voice Editor\nHover over a control for help.";
        helpPanel.setText(defaultHelpText, juce::dontSendNotification);
    helpPanel.setFont(juce::Font(juce::FontOptions(12.0f)));
        helpPanel.setColour(juce::Label::backgroundColourId, juce::Colour(0xff222222));
        helpPanel.setColour(juce::Label::textColourId, juce::Colours::white);
        helpPanel.setJustificationType(juce::Justification::topLeft);
        helpPanel.setBorderSize(juce::BorderSize<int>(10));
        addAndMakeVisible(helpPanel);        // Create 6 operators
        operators.clear();
        for (int i = 0; i < 6; ++i) {
            auto op = std::make_unique<OperatorSliders>();
            op->label.setText(juce::String(i + 1), juce::dontSendNotification);
            op->label.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
            op->label.setColour(juce::Label::textColourId, juce::Colours::white);
            op->label.setJustificationType(juce::Justification::centred);
            addAndMakeVisible(*op);
            operators.push_back(std::move(op));
        }

        // --- Global controls (dynamic, JSON-driven, 3 rows) ---
        for (int i = 0; i < numGlobalSliders; ++i) {
            juce::String vcedKey = globalSliderKeys[i];
            auto it = operatorSliderParamOffsets.find(vcedKey);
            bool enabled = (it != operatorSliderParamOffsets.end());
            juce::String shortLabel;
            if (helpJson.isObject()) {
                // Search both 'parameters' and 'TX816Perf' for short label
                auto searchShort = [&](const char* arrName) {
                    if (auto* arr = helpJson[arrName].getArray()) {
                        for (auto& p : *arr) {
                            auto* obj = p.getDynamicObject();
                            if (obj && obj->hasProperty("key") && obj->getProperty("key").toString().equalsIgnoreCase(vcedKey)) {
                                if (obj->hasProperty("short"))
                                    shortLabel = obj->getProperty("short").toString();
                                break;
                            }
                        }
                    }
                };
                searchShort("parameters");
                searchShort("TX816Perf");
            }
            addAndMakeVisible(globalSliders[i]);
            // Note: Slider style and text box are already set by FMRackLabeledVerticalSlider
            globalSliders[i].setTextBoxStyle(juce::Slider::TextBoxBelow, false, 40, kSliderTextBoxHeight);
            globalSliders[i].setNumDecimalPlacesToDisplay(0);
            globalSliders[i].getSlider().setEnabled(enabled);
            globalSliders[i].setLabelText(enabled ? shortLabel : juce::String());
            setupOperatorSlider(globalSliders[i].getSlider(), vcedKey, 0, 99, 0);
            // Always set up the handler if enabled
            globalSliders[i].onValueChange = [this, i, vcedKey, enabled] {
                if (!isInitialized) {
                    std::cout << "[VoiceEditorPanel] globalSlider[" << i << "] onValueChange called before isInitialized, skipping" << std::endl;
                    return;
                }
                std::cout << "[VoiceEditorPanel] globalSlider[" << i << "] onValueChange, controller=" << controller << std::endl;
                if (!enabled) return;
                if (!controller) return;
                auto it2 = operatorSliderParamOffsets.find(vcedKey);
                if (it2 != operatorSliderParamOffsets.end())
                    setDexedParam(it2->second, static_cast<uint8_t>(globalSliders[i].getSlider().getValue()));
            };
            globalSliders[i].addMouseListener(this, false);
        }
        // PEG Envelope
        // Remove the label and add only the widget
        // pegEnvelopeLabel.setText("Pitch Envelope Generator", juce::dontSendNotification);
        // addAndMakeVisible(pegEnvelopeLabel); // REMOVE LABEL
        addAndMakeVisible(pegEnvelopeWidget);

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

        // Make the voice name editor editable, max 10 chars, and update Dexed state on change
        voiceNameEditor.setInputRestrictions(10, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 .-_*<>()[]#'\"!?$%&,:;/\\");
        voiceNameEditor.setReadOnly(false);
        voiceNameEditor.onTextChange = [this]() {
            juce::String newName = voiceNameEditor.getText().substring(0, 10);
            // Use thread-safe method instead of bypassing the mutex
            if (controller) {
                controller->setVoiceNameForModule(moduleIndex, newName);
            }
        };
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

void VoiceEditorPanel::initializeIfReady() {
    if (controller && moduleIndex >= 0) {
        if (!isInitialized) {
            isInitialized = true;
            std::cout << "[VoiceEditorPanel] isInitialized set to true" << std::endl;
        }
    }
}

void VoiceEditorPanel::paint(Graphics& g) {
    g.fillAll(Colour(0xff332b28));
    if (!isInitialized)
        return;

    if (globalAreaBounds.getWidth() > 0 && globalAreaBounds.getHeight() > 0) {
        auto background = globalAreaBounds.expanded(0, kRowVerticalPadding).toFloat();
        g.setColour(Colour(33, 33, 33));
        g.fillRoundedRectangle(background, 6.0f);
    }
}

void VoiceEditorPanel::paintOverChildren(juce::Graphics& g) {
    // Draw SVG algorithm diagram in the SVG column area
    if (!algorithmSvg || svgDrawArea.isEmpty())
        return;

    auto svgBounds = algorithmSvg->getDrawableBounds();
    if (svgBounds.isEmpty())
        return;

    // Scale to fit the SVG draw area while maintaining aspect ratio
    float scaleX = svgDrawArea.getWidth() / svgBounds.getWidth();
    float scaleY = svgDrawArea.getHeight() / svgBounds.getHeight();
    float scale = std::min(scaleX, scaleY);

    // Center the SVG in the draw area
    float scaledWidth = svgBounds.getWidth() * scale;
    float scaledHeight = svgBounds.getHeight() * scale;
    float offsetX = svgDrawArea.getX() + (svgDrawArea.getWidth() - scaledWidth) / 2.0f - svgBounds.getX() * scale;
    float offsetY = svgDrawArea.getY() + (svgDrawArea.getHeight() - scaledHeight) / 2.0f - svgBounds.getY() * scale;

    auto transform = juce::AffineTransform::scale(scale)
                         .translated(offsetX, offsetY);
    algorithmSvg->draw(g, 1.0f, transform);
}

void VoiceEditorPanel::resized() {
    if (!isInitialized) {
        std::cout << "[VoiceEditorPanel::resized] called before isInitialized, skipping layout" << std::endl;
        return;
    }
    auto bounds = getLocalBounds();
    if (getHeight() < 620)
        setSize(getWidth(), 620);

    auto layoutBounds = bounds.reduced(kOuterMargin);

    // Header row
    auto headerBounds = layoutBounds.removeFromTop(kTopRowHeight);
    juce::FlexBox headerFlex;
    headerFlex.flexDirection = juce::FlexBox::Direction::row;
    headerFlex.alignItems = juce::FlexBox::AlignItems::center;
    headerFlex.justifyContent = juce::FlexBox::JustifyContent::flexStart;

    auto fixedItem = [&](juce::Component& comp, float width)
    {
        return juce::FlexItem(comp).withWidth(width).withMinWidth(width).withMaxWidth(width).withHeight((float)kTopRowHeight);
    };
    auto flexItem = [&](juce::Component& comp, float minWidth)
    {
        return juce::FlexItem(comp).withMinWidth(minWidth).withFlex(1.0f).withHeight((float)kTopRowHeight);
    };
    auto addGap = [&](float width)
    {
        headerFlex.items.add(juce::FlexItem().withWidth(width));
    };

    headerFlex.items.add(fixedItem(algorithmLabel, 80.0f));
    addGap(6.0f);
    headerFlex.items.add(fixedItem(algorithmSelector, 84.0f));
    addGap(12.0f);
    headerFlex.items.add(fixedItem(voiceNameLabel, 60.0f));
    addGap(6.0f);
    headerFlex.items.add(flexItem(voiceNameEditor, 180.0f));
    addGap(12.0f);
    headerFlex.items.add(fixedItem(channelLabel, 74.0f));
    addGap(6.0f);
    headerFlex.items.add(fixedItem(channelSelector, 86.0f));
    addGap(18.0f);
    headerFlex.items.add(fixedItem(requestDumpButton, 148.0f));
    headerFlex.performLayout(headerBounds.toFloat());

    layoutBounds.removeFromTop(kSectionGap);

    // Right column: help panel
    auto helpArea = layoutBounds.removeFromRight(kHelpPanelWidth);
    helpPanel.setBounds(helpArea.reduced(6, 0));

    layoutBounds.removeFromRight(kColumnGap);

    // SVG area will be calculated after operator layout based on operator positions
    // (SVG is drawn between operator label and sliders within each operator row)

    if (layoutBounds.getHeight() <= 0)
        return;
    if (layoutBounds.getWidth() <= 0)
        return;

    const int opCount = static_cast<int>(operators.size());
    const int totalRows = opCount + numGlobalRows;
    const int totalRowGaps = juce::jmax(0, totalRows - 1) * kRowGap;
    const int availableHeight = layoutBounds.getHeight() - totalRowGaps;

    int rowHeight = juce::jmax(kSliderLabelHeight + 48,
                               availableHeight > 0 ? availableHeight / juce::jmax(1, totalRows)
                                                    : kSliderLabelHeight + 48);
    computedSliderHeight = juce::jmax(40, rowHeight - kSliderLabelHeight);

    const int sliderAreaWidth = layoutBounds.getWidth() - kOperatorLabelWidth - kColumnGap - kWidgetColumnWidth - 2 * kRowHorizontalPadding;
    int perSlider = kMinSliderWidth;
    if (sliderAreaWidth > 0) {
        const int usable = sliderAreaWidth - (OperatorSliders::NumSliders - 1) * kSliderGap;
        perSlider = usable > 0 ? usable / OperatorSliders::NumSliders : kMinSliderWidth;
    }
    computedSliderWidth = juce::jmax(kMinSliderWidth, perSlider);

    operatorRowCenters.clear();
    operatorRowCenters.reserve(opCount);

    juce::Rectangle<int> remaining = layoutBounds;
    int operatorStartY = -1;
    int operatorBottomY = -1;
    int globalStartY = -1;
    int globalBottomY = -1;
    int globalSliderIndex = 0;
    const int slidersPerRow = (numGlobalSliders + numGlobalRows - 1) / numGlobalRows;

    for (int row = 0; row < totalRows; ++row) {
        auto rowBounds = remaining.removeFromTop(rowHeight);
        if (row < totalRows - 1)
            remaining.removeFromTop(kRowGap);

        auto rowContent = rowBounds.reduced(0, kRowVerticalPadding);

        if (row < opCount) {
            if (operatorStartY < 0)
                operatorStartY = rowContent.getY();
            operatorBottomY = rowContent.getBottom();

            // Display operators in reverse order: OP6 (index 5) at top, OP1 (index 0) at bottom
            int operatorIdx = (opCount - 1) - row; // row 0 -> operator 5 (OP6), row 5 -> operator 0 (OP1)
            if (operators[operatorIdx]) {
                operators[operatorIdx]->operatorIndex = operatorIdx;
                operators[operatorIdx]->isCarrierOperator = isCarrier(operatorIdx);
                operators[operatorIdx]->setBounds(rowContent);
            }
            operatorRowCenters.push_back(static_cast<float>(rowContent.getCentreY()));
        } else {
            if (globalStartY < 0)
                globalStartY = rowContent.getY();
            globalBottomY = rowContent.getBottom();

            auto sliderArea = rowContent;
            sliderArea.removeFromRight(kWidgetColumnWidth);
            sliderArea.removeFromRight(kColumnGap);
            sliderArea.removeFromLeft(kOperatorLabelWidth);
            sliderArea = sliderArea.reduced(kRowHorizontalPadding, 0);

            int sliderX = sliderArea.getX();
            for (int i = 0; i < slidersPerRow && globalSliderIndex < numGlobalSliders; ++i) {
                juce::Rectangle<int> sliderBounds(sliderX,
                                                  sliderArea.getY(),
                                                  computedSliderWidth,
                                                  computedSliderHeight + FMRackLabeledVerticalSlider::kLabelHeight);
                globalSliders[globalSliderIndex].setBounds(sliderBounds);
                globalSliders[globalSliderIndex].getSlider().setEnabled(globalSliders[globalSliderIndex].getLabelText().isNotEmpty());
                sliderX += computedSliderWidth + kSliderGap;
                ++globalSliderIndex;
            }
        }
    }

    if (operatorStartY >= 0 && operatorBottomY > operatorStartY) {
        operatorAreaBounds = juce::Rectangle<int>(layoutBounds.getX(), operatorStartY, layoutBounds.getWidth(), operatorBottomY - operatorStartY);
        // Calculate SVG draw area: positioned after the operator label (36px), spanning operator rows
        // The SVG column is 100px wide, positioned at layoutBounds.getX() + 36
        svgDrawArea = juce::Rectangle<float>(
            static_cast<float>(layoutBounds.getX() + 36),
            static_cast<float>(operatorStartY),
            100.0f,
            static_cast<float>(operatorBottomY - operatorStartY)
        ).reduced(4, 6);
    } else {
        operatorAreaBounds = {};
        svgDrawArea = {};
    }

    if (globalStartY >= 0 && globalBottomY > globalStartY) {
        pegAreaBounds = juce::Rectangle<int>(layoutBounds.getRight() - kWidgetColumnWidth,
                                             globalStartY,
                                             kWidgetColumnWidth,
                                             globalBottomY - globalStartY);
        pegEnvelopeWidget.setBounds(pegAreaBounds.reduced(kRowHorizontalPadding, 2));
        globalAreaBounds = juce::Rectangle<int>(layoutBounds.getX(),
                                                globalStartY,
                                                layoutBounds.getWidth(),
                                                globalBottomY - globalStartY);
    } else {
        pegAreaBounds = {};
        pegEnvelopeWidget.setBounds({});
        globalAreaBounds = {};
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
    std::unique_ptr<juce::XmlElement> svgXml = juce::XmlDocument::parse(juce::String::fromUTF8(reinterpret_cast<const char*>(data), dataSize));
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
}

void VoiceEditorPanel::loadHelpJson() {
    using namespace juce;
    // Use JUCE BinaryData for VCED.json
    auto* data = BinaryData::VCED_json;
    int dataSize = BinaryData::VCED_jsonSize;
    std::cout << "[VoiceEditorPanel] Loading help JSON from BinaryData (embedded resource)" << std::endl;
    juce::String jsonStr = juce::String::fromUTF8(reinterpret_cast<const char*>(data), dataSize);
    var json = JSON::parse(jsonStr);
    if (!json.isObject()) {
        std::cout << "[VoiceEditorPanel] VCED.json parse failed" << std::endl;
        return;
    }
    helpJson = json;
    helpTextByKey.clear();
    operatorSliderParamOffsets.clear();
    // Instead of filtering by opSliderKeys, include all parameters with a parameter_number
    if (auto* params = json["parameters"].getArray()) {
        for (auto& p : *params) {
            auto* obj = p.getDynamicObject();
            if (obj && obj->hasProperty("key") && !obj->getProperty("parameter_number").isVoid()) {
                auto key = obj->getProperty("key").toString();
                const auto paramNum = static_cast<uint8_t>(obj->getProperty("parameter_number").toString().getIntValue());
                operatorSliderParamOffsets[key] = paramNum;
                std::cout << "[VoiceEditorPanel] operatorSliderParamOffsets: " << key << " -> " << static_cast<int>(paramNum) << std::endl;
            }
            auto keyStd = obj ? obj->getProperty("key").toString().toStdString() : std::string();
            juce::String name = obj ? obj->getProperty("name").toString() : juce::String();
            juce::String range = obj ? obj->getProperty("range").toString() : juce::String();
            juce::String desc = obj ? obj->getProperty("long_description").toString() : juce::String();
            if (desc.isEmpty() && obj) desc = obj->getProperty("description").toString();
            juce::String shortDesc = obj ? obj->getProperty("description").toString() : juce::String();
            juce::String reference = obj ? obj->getProperty("reference").toString() : juce::String();
            juce::String hoverText;
            hoverText << name;
            if (!range.isEmpty())
                hoverText << " (" << range << ")";
            hoverText << "\n";
            if (!shortDesc.isEmpty() && shortDesc != desc)
                hoverText << shortDesc << "\n";
            hoverText << "\n";
            hoverText << desc << "\n";
            if (!reference.isEmpty())
                hoverText << "\nReference: " << reference << "\n";
            helpTextByKey[keyStd] = hoverText.toStdString();
        }
    }
    // --- Add TX816Perf global parameters ---
    if (auto* perf = json["TX816Perf"].getArray()) {
        for (auto& p : *perf) {
            auto* obj = p.getDynamicObject();
            if (obj && obj->hasProperty("key") && !obj->getProperty("parameter_number").isVoid()) {
                auto key = obj->getProperty("key").toString();
                const auto paramNum = static_cast<uint8_t>(obj->getProperty("parameter_number").toString().getIntValue());
                operatorSliderParamOffsets[key] = paramNum;
                std::cout << "[VoiceEditorPanel] operatorSliderParamOffsets (TX816Perf): " << key << " -> " << static_cast<int>(paramNum) << std::endl;
            }
            auto keyStd = obj ? obj->getProperty("key").toString().toStdString() : std::string();
            juce::String name = obj ? obj->getProperty("name").toString() : juce::String();
            juce::String range = obj ? obj->getProperty("range").toString() : juce::String();
            juce::String desc = obj ? obj->getProperty("long_description").toString() : juce::String();
            if (desc.isEmpty() && obj) desc = obj->getProperty("description").toString();
            juce::String shortDesc = obj ? obj->getProperty("description").toString() : juce::String();
            juce::String reference = obj ? obj->getProperty("reference").toString() : juce::String();
            juce::String hoverText;
            hoverText << name;
            if (!range.isEmpty())
                hoverText << " (" << range << ")";
            hoverText << "\n";
            if (!shortDesc.isEmpty() && shortDesc != desc)
                hoverText << shortDesc << "\n";
            hoverText << "\n";
            hoverText << desc << "\n";
            if (!reference.isEmpty())
                hoverText << "\nReference: " << reference << "\n";
            helpTextByKey[keyStd] = hoverText.toStdString();
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

void VoiceEditorPanel::setupOperatorSlider(Slider& slider, const String& name, int /*min*/, int /*max*/, int /*defaultValue*/) {
    std::cout << "[VoiceEditorPanel] setupOperatorSlider called for '" << name << "'" << std::endl;
    double minValue = 0.0, maxValue = 99.0, defaultValue = 0.0;
    double step = 1.0;
    bool isDiscrete = false;
    std::vector<double> allowedValues;
    std::vector<juce::String> allowedLabels;
    auto searchSetup = [&](const char* arrName) {
        if (helpJson.isObject()) {
            if (auto* params = helpJson[arrName].getArray()) {
                for (auto& p : *params) {
                    auto* obj = p.getDynamicObject();
                    if (obj && obj->hasProperty("key") && obj->getProperty("key").toString().equalsIgnoreCase(name)) {
                        if (obj->hasProperty("min"))
                            minValue = obj->getProperty("min").toString().getDoubleValue();
                        if (obj->hasProperty("max"))
                            maxValue = obj->getProperty("max").toString().getDoubleValue();
                        if (obj->hasProperty("default"))
                            defaultValue = obj->getProperty("default").toString().getDoubleValue();
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
                                    allowedValues.push_back(static_cast<double>(static_cast<int>(v)));
                                }
                            }
                            std::sort(allowedValues.begin(), allowedValues.end());
                            if (!allowedValues.empty()) {
                                minValue = 0;
                                const auto valueCount = static_cast<int>(allowedValues.size());
                                maxValue = static_cast<double>(valueCount - 1);
                                const auto searchValue = static_cast<double>(static_cast<int>(defaultValue));
                                auto it = std::find(allowedValues.begin(), allowedValues.end(), searchValue);
                                if (it != allowedValues.end())
                                    defaultValue = static_cast<double>(std::distance(allowedValues.begin(), it));
                                else
                                    defaultValue = 0;
                            }
                        }
                        break;
                    }
                }
            }
        }
    };
    searchSetup("parameters");
    searchSetup("TX816Perf");
    slider.setRange(minValue, maxValue, step);
    slider.setValue(defaultValue, juce::dontSendNotification);
    if (isDiscrete && !allowedLabels.empty()) {
        slider.setTextValueSuffix("");
        slider.textFromValueFunction = [allowedLabels, allowedValues](double v) {
            const auto idx = static_cast<int>(v);
            if (idx >= 0 && idx < static_cast<int>(allowedLabels.size()))
                return allowedLabels[idx];
            return juce::String(idx);
        };
        slider.valueFromTextFunction = [allowedValues](const juce::String& text) {
            for (size_t i = 0; i < allowedValues.size(); ++i) {
                if (text == juce::String(static_cast<int>(allowedValues[i])))
                    return static_cast<double>(i);
            }
            return 0.0;
        };
    }
}

void VoiceEditorPanel::syncAllOperatorSlidersWithDexed() {
    if (!isInitialized) {
        std::cout << "[VoiceEditorPanel::syncAllOperatorSlidersWithDexed] called before isInitialized, skipping" << std::endl;
        return;
    }
    std::cout << "[VoiceEditorPanel] syncAllOperatorSlidersWithDexed: syncing all operator sliders from Dexed engine" << std::endl;
    // --- Show voice name from current Dexed engine (thread-safe) ---
    if (controller) {
        juce::String voiceName = controller->getVoiceNameForModule(moduleIndex);
        if (voiceName.isNotEmpty()) {
            std::cout << "[VoiceEditorPanel] syncAllOperatorSlidersWithDexed: Current voice name: '" << voiceName << "'" << std::endl;
            voiceNameEditor.setText(voiceName, juce::dontSendNotification);
        }
    }
    uint8_t opeBitmask = getDexedParam(static_cast<uint8_t>(155));
    int numOps = static_cast<int>(operators.size());
    for (int uiRowIdx = 0; uiRowIdx < numOps; ++uiRowIdx) {
        auto& op = operators[uiRowIdx];
        int dexedOpIdx = uiRowIdx;
        std::cout << "[VoiceEditorPanel] syncAllOperatorSlidersWithDexed: OP" << (dexedOpIdx+1) << " (UI row " << uiRowIdx << ")" << std::endl;
        // Set operator enable/disable (OPE slider)
        bool opEnabled = (opeBitmask & (1 << dexedOpIdx)) != 0;
        double oldOpeValue = op->sliders[0].getValue();
        op->sliders[0].setValue(opEnabled ? 1 : 0, juce::dontSendNotification);
    std::cout << "  OPE: opeBitmask=0x" << std::hex << static_cast<int>(opeBitmask) << std::dec << " enabled=" << opEnabled << " oldSlider=" << oldOpeValue << " newSlider=" << op->sliders[0].getValue() << std::endl;
        for (int s = 1; s < OperatorSliders::NumSliders; ++s) {
            const juce::String sliderName = OperatorSliders::sliderNames[s];
            auto it = operatorSliderParamOffsets.find(sliderName);
            if (it == operatorSliderParamOffsets.end()) {
                std::cout << "  [WARN] No param offset for slider '" << sliderName << "'\n";
                continue;
            }
            uint8_t paramAddress = static_cast<uint8_t>(dexedOpIdx * 21 + it->second);
            uint8_t value = getDexedParam(paramAddress);
            std::cout << "  OP" << (dexedOpIdx+1) << " " << sliderName << " paramAddr=" << static_cast<int>(paramAddress) << " value=" << static_cast<int>(value) << std::endl;
            syncOperatorSliderWithDexed(op->sliders[s].getSlider(), paramAddress, sliderName.toRawUTF8());
        }
        // --- Envelope widget update (use raw integer values) ---
        // DX7 operator envelope: R1-R4, L1-L4, consecutive in voice data
        std::vector<int> rawEnvRates, rawEnvLevels;
        for (int eg = 0; eg < 4; ++eg) {
            uint8_t r = getDexedParam(static_cast<uint8_t>(dexedOpIdx * 21 + eg)); // R1-R4: offsets 0-3
            uint8_t l = getDexedParam(static_cast<uint8_t>(dexedOpIdx * 21 + 4 + eg)); // L1-L4: offsets 4-7
            rawEnvRates.push_back(static_cast<int>(r));
            rawEnvLevels.push_back(static_cast<int>(l));
        }
        op->envWidget.setEnvelopeRaw(rawEnvRates, rawEnvLevels);
        // --- Keyboard scaling widget update (use raw integer values) ---
        uint8_t bp = getDexedParam(static_cast<uint8_t>(dexedOpIdx * 21 + 8)); // Breakpoint
        uint8_t ld = getDexedParam(static_cast<uint8_t>(dexedOpIdx * 21 + 9)); // Left depth
        uint8_t rd = getDexedParam(static_cast<uint8_t>(dexedOpIdx * 21 + 10)); // Right depth
        uint8_t lc = getDexedParam(static_cast<uint8_t>(dexedOpIdx * 21 + 11)); // Left curve
        uint8_t rc = getDexedParam(static_cast<uint8_t>(dexedOpIdx * 21 + 12)); // Right curve
        op->ksWidget.setScalingParamsRaw(static_cast<int>(bp), static_cast<int>(ld), static_cast<int>(rd), 
                                          static_cast<int>(lc), static_cast<int>(rc));
    }
    // --- Sync global controls from Dexed engine ---
    for (int i = 0; i < numGlobalSliders; ++i) {
        auto it = operatorSliderParamOffsets.find(globalSliderKeys[i]);
        if (it != operatorSliderParamOffsets.end()) {
            globalSliders[i].setValue(getDexedParam(static_cast<uint8_t>(it->second)), juce::dontSendNotification);
        }
    }
    // --- PEG Envelope ---
    std::vector<float> pegRates, pegLevels;
    for (int i = 0; i < 4; ++i) pegRates.push_back(getDexedParam(static_cast<uint8_t>(124 + i)) / 99.0f); // Example
    for (int i = 0; i < 4; ++i) pegLevels.push_back(getDexedParam(static_cast<uint8_t>(128 + i)) / 99.0f); // Example
    pegEnvelopeWidget.setEnvelope(pegRates, pegLevels);
    std::cout << "[VoiceEditorPanel] syncAllOperatorSlidersWithDexed: calling repaint() to update UI" << std::endl;
    repaint();
    std::cout << "[VoiceEditorPanel] syncAllOperatorSlidersWithDexed: completed" << std::endl;
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
    if (algoIdx < 0 || algoIdx >= static_cast<int>(carrierMap.size())) return {};
    return carrierMap[algoIdx];
}

bool VoiceEditorPanel::isCarrier(int opIdx) const {
    // opIdx: 0 = OP1, 5 = OP6
    auto carriers = getCarrierIndicesForAlgorithm(currentAlgorithm);
    for (int c : carriers) if (c == opIdx) return true;
    return false;
}

// OperatorSliders implementation
VoiceEditorPanel::OperatorSliders::OperatorSliders()
{
    // Set label texts for each slider and add mouse listeners
    for (int i = 0; i < NumSliders; ++i) {
        sliders[i].setLabelText(sliderNames[i]);
        addAndMakeVisible(sliders[i]);
        // Add mouse listener to the internal slider for hover help
        sliders[i].getSlider().addMouseListener(this, false);
    }

    // Set up slider ranges based on DX7 parameter specs
    // OPE: Operator Enable (0-1)
    sliders[OPE].getSlider().setRange(0, 1, 1);
    // TL: Total Level (0-99)
    sliders[TL].getSlider().setRange(0, 99, 1);
    // PM: Pitch Mode/Frequency Mode (0-1, 0=ratio, 1=fixed)
    sliders[PM].getSlider().setRange(0, 1, 1);
    // PC: Pitch Coarse (0-31)
    sliders[PC].getSlider().setRange(0, 31, 1);
    // PF: Pitch Fine (0-99)
    sliders[PF].getSlider().setRange(0, 99, 1);
    // PD: Pitch Detune (0-14, center=7)
    sliders[PD].getSlider().setRange(0, 14, 1);
    // AMS: Amplitude Modulation Sensitivity (0-3)
    sliders[AMS].getSlider().setRange(0, 3, 1);
    // TS: Touch Sensitivity / Velocity Sensitivity (0-7)
    sliders[TS].getSlider().setRange(0, 7, 1);
    // RS: Rate Scaling (0-7)
    sliders[RS].getSlider().setRange(0, 7, 1);

    addAndMakeVisible(label);
    addAndMakeVisible(envWidget);
    addAndMakeVisible(ksWidget);

    // Set up live help text update for envelope widget
    envWidget.onHoveredParamChanged = [this](int hovered) {
        if (auto* parent = dynamic_cast<VoiceEditorPanel*>(getParentComponent())) {
            if (hovered >= 0 && hovered < 8) {
                static const char* envKeys[] = {"R1", "R2", "R3", "R4", "L1", "L2", "L3", "L4"};
                parent->showHelpForKey(envKeys[hovered]);
            } else {
                parent->restoreDefaultHelp();
            }
        }
    };
    // Set up live help text update for keyboard scaling widget
    ksWidget.onHoveredParamChanged = [this](int hovered) {
        if (auto* parent = dynamic_cast<VoiceEditorPanel*>(getParentComponent())) {
            if (hovered >= 0 && hovered < 5) {
                static const char* ksKeys[] = {"BP", "LD", "RD", "LC", "RC"};
                parent->showHelpForKey(ksKeys[hovered]);
            } else {
                parent->restoreDefaultHelp();
            }
        }
    };
}

VoiceEditorPanel::OperatorSliders::~OperatorSliders() = default;

void VoiceEditorPanel::OperatorSliders::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    
    // Leave space for SVG column (will be drawn by parent)
    auto svgArea = bounds.removeFromLeft(36 + 100); // label (36) + SVG area (100)
    
    // Only draw operator label background (first 36px of svgArea)
    auto labelBounds = svgArea.removeFromLeft(36);
    if (isCarrierOperator) {
        g.setColour(juce::Colour(0xff2a4a30)); // Slightly brighter green for label
    } else {
        g.setColour(juce::Colour(0xff333333));
    }
    g.fillRoundedRectangle(labelBounds.toFloat(), 4.0f);
    
    // Draw operator background for the slider and widget area - green for carriers, dark gray for modulators
    if (isCarrierOperator) {
        g.setColour(juce::Colour(0xff1a3320)); // Dark green background for carriers
    } else {
        g.setColour(juce::Colour(0xff2a2a2a)); // Dark gray for modulators
    }
    g.fillRect(bounds);
}

void VoiceEditorPanel::OperatorSliders::resized()
{
    auto bounds = getLocalBounds();

    // Operator number label on the left
    auto labelArea = bounds.removeFromLeft(36);
    label.setBounds(labelArea.reduced(4));

    // Leave space for the SVG algorithm diagram (drawn by parent)
    bounds.removeFromLeft(100); // SVG column width

    // Envelope and keyboard scaling widgets on the right
    auto widgetArea = bounds.removeFromRight(256);
    auto envBounds = widgetArea.removeFromTop(widgetArea.getHeight() / 2);
    envWidget.setBounds(envBounds.reduced(4));
    ksWidget.setBounds(widgetArea.reduced(4));

    // Sliders in the middle
    bounds.removeFromRight(16); // Gap before widgets
    int sliderWidth = 40;
    int sliderGap = 8;
    int totalWidth = (sliderWidth * NumSliders) + (sliderGap * (NumSliders - 1));
    int startX = bounds.getCentreX() - (totalWidth / 2);

    for (int i = 0; i < NumSliders; ++i) {
        int x = startX + (i * (sliderWidth + sliderGap));
        sliders[i].setBounds(x, bounds.getY(), sliderWidth, bounds.getHeight());
    }
}

void VoiceEditorPanel::OperatorSliders::sliderMouseEnter(int sliderIdx)
{
    // Forward to parent VoiceEditorPanel
    if (auto* parent = dynamic_cast<VoiceEditorPanel*>(getParentComponent())) {
        parent->showHelpForKey(sliderNames[sliderIdx]);
    }
}

void VoiceEditorPanel::OperatorSliders::sliderMouseExit(int sliderIdx)
{
    // Forward to parent VoiceEditorPanel
    if (auto* parent = dynamic_cast<VoiceEditorPanel*>(getParentComponent())) {
        parent->restoreDefaultHelp();
    }
}

// --- VoiceEditorPanel hover help for global sliders ---
void VoiceEditorPanel::mouseEnter(const juce::MouseEvent& e) {
    for (int i = 0; i < numGlobalSliders; ++i) {
        if (e.eventComponent == &globalSliders[i].getSlider()) {
            showHelpForKey(globalSliderKeys[i]);
            return;
        }
    }
}

void VoiceEditorPanel::mouseExit(const juce::MouseEvent& e) {
    for (int i = 0; i < numGlobalSliders; ++i) {
        if (e.eventComponent == &globalSliders[i].getSlider()) {
            restoreDefaultHelp();
            return;
        }
    }
}

// --- OperatorSliders hover help for operator sliders, envelope, and keyboard scaling ---
void VoiceEditorPanel::OperatorSliders::mouseEnter(const juce::MouseEvent& e) {
    for (int i = 0; i < NumSliders; ++i) {
        if (e.eventComponent == &sliders[i].getSlider()) {
            sliderMouseEnter(i);
            return;
        }
    }
    // Envelope widget: show help for the hovered envelope parameter (R1, R2, R3, R4, L1, L2, L3, L4)
    if (e.eventComponent == &envWidget) {
        if (auto* parent = dynamic_cast<VoiceEditorPanel*>(getParentComponent())) {
            if (envWidget.hoveredParam >= 0 && envWidget.hoveredParam < 8) {
                static const char* envKeys[] = {"R1", "R2", "R3", "R4", "L1", "L2", "L3", "L4"};
                parent->showHelpForKey(envKeys[envWidget.hoveredParam]);
            } else {
                parent->restoreDefaultHelp();
            }
        }
        return;
    }
    // Keyboard scaling widget: show help for the hovered scaling parameter (BP, LD, RD, LC, RC)
    if (e.eventComponent == &ksWidget) {
        if (auto* parent = dynamic_cast<VoiceEditorPanel*>(getParentComponent())) {
            if (ksWidget.hoveredParam >= 0 && ksWidget.hoveredParam < 5) {
                static const char* ksKeys[] = {"BP", "LD", "RD", "LC", "RC"};
                parent->showHelpForKey(ksKeys[ksWidget.hoveredParam]);
            } else {
                parent->restoreDefaultHelp();
            }
        }
        return;
    }
}

void VoiceEditorPanel::OperatorSliders::mouseExit(const juce::MouseEvent& e) {
    for (int i = 0; i < NumSliders; ++i) {
        if (e.eventComponent == &sliders[i].getSlider()) {
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

// EnvelopeDisplay mouse hover
uint8_t VoiceEditorPanel::getDexedParam(uint8_t address) const {
    if (!isInitialized) {
        return 0;
    }
    if (!controller) { return 0; }
    // Use thread-safe method instead of bypassing the mutex
    return controller->getDexedParamForModule(moduleIndex, address);
}


std::pair<uint8_t, uint8_t> VoiceEditorPanel::getDexedRange(const char* sliderKey) const {
    const char* key = sliderKey;
    if (key && helpJson.isObject()) {
        auto searchRange = [&](const char* arrName) -> std::pair<uint8_t, uint8_t> {
            if (auto* params = helpJson[arrName].getArray()) {
                for (auto& p : *params) {
                    auto* obj = p.getDynamicObject();
                    if (obj && obj->hasProperty("key") && obj->getProperty("key").toString().equalsIgnoreCase(key)) {
                        uint8_t minVal = obj->hasProperty("min") ? static_cast<uint8_t>(obj->getProperty("min").toString().getIntValue()) : 0;
                        uint8_t maxVal = obj->hasProperty("max") ? static_cast<uint8_t>(obj->getProperty("max").toString().getIntValue()) : 99;
                        return {minVal, maxVal};
                    }
                }
            }
            return {static_cast<uint8_t>(0), static_cast<uint8_t>(99)};
        };
        auto r = searchRange("parameters");
        const std::pair<uint8_t, uint8_t> defaultRange{static_cast<uint8_t>(0), static_cast<uint8_t>(99)};
        if (r != defaultRange) return r;
        r = searchRange("TX816Perf");
        if (r != defaultRange) return r;
    }
    return {static_cast<uint8_t>(0), static_cast<uint8_t>(99)};
}

void VoiceEditorPanel::setController(FMRackController* controller_) {
    std::cout << "[VoiceEditorPanel::setController] called with controller_=" << controller_ << std::endl;
    controller = controller_;
    initializeIfReady();
}

void VoiceEditorPanel::setModuleIndex(int idx) {
    moduleIndex = idx;
    std::cout << "[VoiceEditorPanel::setModuleIndex] moduleIndex set to " << idx << std::endl;
    initializeIfReady();
}

void VoiceEditorPanel::onSingleVoiceDumpReceived(const std::vector<uint8_t>& data) {
    std::cout << "[VoiceEditorPanel::onSingleVoiceDumpReceived] called, data.size()=" << data.size() << ", moduleIndex=" << moduleIndex << std::endl;
    if (!data.empty()) {
        std::cout << "[VoiceEditorPanel::onSingleVoiceDumpReceived] first 32 bytes: ";
    for (size_t i = 0; i < std::min<size_t>(32, data.size()); ++i) std::cout << std::hex << static_cast<int>(data[i]) << " ";
        std::cout << std::dec << std::endl;
        std::cout << "[VoiceEditorPanel::onSingleVoiceDumpReceived] last 8 bytes: ";
    for (size_t i = (data.size() > 8 ? data.size() - 8 : 0); i < data.size(); ++i) std::cout << std::hex << static_cast<int>(data[i]) << " ";
        std::cout << std::dec << std::endl;
    }
    // Expect 163 bytes for DX7 single voice sysex: F0 43 00 00 01 1B ... 155 bytes ... checksum F7
    if (data.size() == 163 && data[0] == 0xF0 && data[1] == 0x43 && data[5] == 0x1B) {
        std::cout << "[VoiceEditorPanel::onSingleVoiceDumpReceived] Received DX7 single voice dump (163 bytes), updating UI." << std::endl;
        auto dataCopy = data;
        juce::MessageManager::callAsync([this, dataCopy]() {
            std::cout << "[VoiceEditorPanel::onSingleVoiceDumpReceived] UI update on main thread, moduleIndex=" << moduleIndex << std::endl;
            // Parse voice name from bytes 6+145 to 6+154 (10 bytes, ASCII, space-padded)
            if (dataCopy.size() >= 6 + 155) {
                std::string name(reinterpret_cast<const char*>(&dataCopy[6 + 145]), 10);
                // Print raw bytes for diagnosis
                std::cout << "[VoiceEditorPanel::onSingleVoiceDumpReceived] Raw voice name bytes: ";
                for (int i = 0; i < 10; ++i) {
                    const auto byteValue = static_cast<unsigned char>(dataCopy[6 + 145 + i]);
                    std::cout << std::hex << static_cast<int>(byteValue) << " ";
                }
                std::cout << std::dec << std::endl;
                // Remove trailing and leading spaces
                size_t first = name.find_first_not_of(' ');
                size_t last = name.find_last_not_of(' ');
                if (first != std::string::npos && last != std::string::npos)
                    name = name.substr(first, last - first + 1);
                else
                    name.clear();
                std::cout << "[VoiceEditorPanel::onSingleVoiceDumpReceived] Extracted voice name: '" << name << "'" << std::endl;
                if (!name.empty()) {
                    voiceNameEditor.setText(juce::String(name), juce::dontSendNotification);
                }
            }
            if (controller) {
                std::lock_guard<std::mutex> lock(controller->getMutex());
                const auto& modules = controller->getRack()->getModules();
                std::cout << "[VoiceEditorPanel::onSingleVoiceDumpReceived] modules.size()=" << modules.size() << std::endl;
                if (!modules.empty() && moduleIndex >= 0 && moduleIndex < static_cast<int>(modules.size())) {
                    auto* dexed = modules[moduleIndex]->getDexedEngine();
                    std::cout << "[VoiceEditorPanel::onSingleVoiceDumpReceived] dexed=" << dexed << std::endl;
                    if (dexed) {
                        uint8_t currentOpeBitmask = dexed->getVoiceDataElement(static_cast<uint8_t>(155));
                        if (currentOpeBitmask == 0x0) {
                            currentOpeBitmask = 0x3F;
                            std::cout << "[VoiceEditorPanel::onSingleVoiceDumpReceived] Current OPE bitmask was 0x0, defaulting to 0x3F (all operators enabled)" << std::endl;
                        } else {
                            std::cout << "[VoiceEditorPanel::onSingleVoiceDumpReceived] Preserving current OPE bitmask: 0x" << std::hex << static_cast<int>(currentOpeBitmask) << std::dec << std::endl;
                        }
                        for (int i = 0; i < 155; ++i) {
                            std::cout << "[VoiceEditorPanel::onSingleVoiceDumpReceived] Setting Dexed param " << i << " = " << static_cast<int>(dataCopy[6 + i]) << std::endl;
                            dexed->setVoiceDataElement(static_cast<uint8_t>(i), dataCopy[6 + i]);
                        }
                        dexed->setVoiceDataElement(static_cast<uint8_t>(155), currentOpeBitmask);
                        std::cout << "[VoiceEditorPanel::onSingleVoiceDumpReceived] Restored OPE bitmask: 0x" << std::hex << static_cast<int>(currentOpeBitmask) << std::dec << std::endl;
                        std::cout << "[VoiceEditorPanel::onSingleVoiceDumpReceived] Calling dexed->doRefreshVoice()" << std::endl;
                        dexed->doRefreshVoice();
                    } else {
                        std::cout << "[VoiceEditorPanel::onSingleVoiceDumpReceived] dexed is nullptr" << std::endl;
                    }
                } else {
                    std::cout << "[VoiceEditorPanel::onSingleVoiceDumpReceived] modules is empty or bad index" << std::endl;
                }
            } else {
                std::cout << "[VoiceEditorPanel::onSingleVoiceDumpReceived] controller is nullptr" << std::endl;
            }
            std::cout << "[VoiceEditorPanel::onSingleVoiceDumpReceived] Calling syncAllOperatorSlidersWithDexed() on main thread" << std::endl;
            syncAllOperatorSlidersWithDexed();
        });
    } else {
        std::cout << "[VoiceEditorPanel::onSingleVoiceDumpReceived] Received SysEx is not a valid DX7 single voice dump." << std::endl;
    }
}

void VoiceEditorPanel::setDexedParam(uint8_t address, uint8_t value) {
    if (!isInitialized) {
        return;
    }
    // Use thread-safe method instead of bypassing the mutex
    if (controller) {
        controller->setDexedParamForModule(moduleIndex, address, value);
    }
}

// --- Operator parameter offset mapping ---
// Build this dynamically from VCED.json
std::map<juce::String, uint8_t> VoiceEditorPanel::operatorSliderParamOffsets;

void VoiceEditorPanel::syncOperatorSliderWithDexed(juce::Slider& slider, uint8_t paramAddress, const char* sliderKey)
{
    uint8_t value = getDexedParam(paramAddress);
    auto range = getDexedRange(sliderKey);
    slider.setRange(range.first, range.second, 1.0);
    slider.setValue(value, juce::dontSendNotification);
    std::cout << "[VoiceEditorPanel::syncOperatorSliderWithDexed] " << sliderKey << ": value=" << static_cast<int>(value) << " range=[" << static_cast<int>(range.first) << "-" << static_cast<int>(range.second) << "] oldSlider=" << slider.getValue() << " newSlider=" << value << std::endl;
}
