#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "VoiceEditorPanel.h"
#include "VoiceEditorWindow.h"
#include "BinaryData.h"

#include "DX7LookAndFeel.h"
#include <filesystem>
#include <memory>
#include <juce_data_structures/juce_data_structures.h>
#include "FMRackController.h"
#include "FMRackSliderConstants.h"

using namespace juce;

// Global instance for operator slider look


namespace
{
    constexpr int kOuterMargin = 6;
    constexpr int kSectionGap = 6;
    constexpr int kColumnGap = 6;
    constexpr int kTopRowHeight = 30;
    constexpr int kSvgPanelWidth = 140;
    constexpr int kOperatorLabelWidth = 28;
    constexpr int kWidgetWidth = 90;
    constexpr int kWidgetInnerGap = 4;
    constexpr int kWidgetColumnWidth = kWidgetWidth * 2 + kWidgetInnerGap;
    constexpr int kRowGap = 4;
    constexpr int kRowVerticalPadding = 1;
    constexpr int kRowHorizontalPadding = 2;
    constexpr int kSliderGap = FMRackSliderConstants::kSliderGap;  // Use global constant
    constexpr int kSliderLabelHeight = FMRackSliderConstants::kLabelHeight;
    constexpr int kSliderTextBoxHeight = FMRackSliderConstants::kTextBoxHeight;
    constexpr int kMinSliderWidth = FMRackSliderConstants::kSliderWidth;
}

VoiceEditorPanel::VoiceEditorPanel()
{
    std::cout << "[VoiceEditorPanel] Constructor start" << std::endl;
    try {
        loadHelpJson(); // <-- Moved to the start to ensure helpJson and operatorSliderParamOffsets are initialized

        // Enable key listening for ESC key
        setWantsKeyboardFocus(true);
        addKeyListener(this);

        setBounds(0, 0, 1100, 700);
        setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(0xff2a2a2a));

        // Top controls
        algorithmLabel.setText("Algorithm", juce::dontSendNotification);
        algorithmLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(algorithmLabel);
    algorithmSelector.addItemList({"1","2","3","4","5","6","7","8","9","10","11","12","13","14","15","16","17","18","19","20","21","22","23","24","25","26","27","28","29","30","31","32"}, 1);
    // Don't select an algorithm here: selecting can cause change callbacks later when we're
    // wired up to the engine. We'll pick it from the current voice once initialized.
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

        // Data button
        dataButton.setButtonText("Data");
        addAndMakeVisible(dataButton);
        dataButton.onClick = [this]() {
            copyVoiceDataToClipboard();
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
            // Use consistent TextBoxAbove style for all sliders
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
            globalSliders[i].addMouseListener(this, true);  // true = listen to child components too
        }
        // PEG Envelope
        // Remove the label and add only the widget
        // pegEnvelopeLabel.setText("Pitch Envelope Generator", juce::dontSendNotification);
        // addAndMakeVisible(pegEnvelopeLabel); // REMOVE LABEL
        addAndMakeVisible(pegEnvelopeWidget);
        
        // Oscilloscope for waveform display
        addAndMakeVisible(oscilloscope);
        oscilloscope.start();  // Start the timer for updates

        algorithmSelector.onChange = [this]() {
            try {
                int idx = algorithmSelector.getSelectedId() - 1;
                std::cout << "[VoiceEditorPanel] algorithmSelector.onChange triggered, idx=" << idx << std::endl;
                currentAlgorithm = idx;
                // Push algorithm change into Dexed voice data so it affects sound.
                // DX7 algorithm is stored at voice data element 134 (0..31).
                if (isInitialized && controller) {
                    setDexedParam(static_cast<uint8_t>(134), static_cast<uint8_t>(juce::jlimit(0, 31, idx)));
                }
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
    // Show a default SVG initially (until we can query the current voice once initialized).
    loadAlgorithmSvg(0);
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
    removeKeyListener(this);
    oscilloscope.stop();  // Stop the oscilloscope timer
    
    // Unregister oscilloscope from controller
    if (controller) {
        controller->setOscilloscope(nullptr);
    }
    // unique_ptr automatically handles cleanup
}

bool VoiceEditorPanel::keyPressed(const juce::KeyPress& key, juce::Component* /*originatingComponent*/) {
    std::cout << "[VoiceEditorPanel::keyPressed] Key pressed: " << key.getTextDescription() << std::endl;
    
    if (key == juce::KeyPress::escapeKey) {
        // Find the parent VoiceEditorWindow and close it
        if (auto* window = findParentComponentOfClass<VoiceEditorWindow>()) {
            window->setVisible(false);
            return true;
        }
    } else if (key.isKeyCode('C') && key.getModifiers().isCtrlDown()) {
        // Ctrl+C: Copy voice data to clipboard
        std::cout << "[VoiceEditorPanel] Ctrl+C detected - copying voice data" << std::endl;
        copyVoiceDataToClipboard();
        return true;
    } else if (key.isKeyCode('V') && key.getModifiers().isCtrlDown()) {
        // Ctrl+V: Paste voice data from clipboard
        std::cout << "[VoiceEditorPanel] Ctrl+V detected - pasting voice data" << std::endl;
        pasteVoiceDataFromClipboard();
        return true;
    }
    return false;
}

void VoiceEditorPanel::initializeIfReady() {
    if (controller && moduleIndex >= 0) {
        if (!isInitialized) {
            isInitialized = true;
            std::cout << "[VoiceEditorPanel] isInitialized set to true" << std::endl;

            // Now that we're initialized and can safely talk to the engine, pull the current
            // algorithm from the loaded voice and update UI without pushing anything back.
            const int algoIdx = static_cast<int>(getDexedParam(static_cast<uint8_t>(134)));
            if (algoIdx >= 0 && algoIdx < 32) {
                currentAlgorithm = algoIdx;
                algorithmSelector.setSelectedId(algoIdx + 1, juce::dontSendNotification);
                loadAlgorithmSvg(algoIdx);
            } else {
                algorithmSelector.setSelectedId(1, juce::dontSendNotification);
                currentAlgorithm = 0;
                loadAlgorithmSvg(0);
            }
        }
    }
}

void VoiceEditorPanel::paint(Graphics& g) {
    // Use dark charcoal background with subtle gradient
    auto bounds = getLocalBounds();
    juce::ColourGradient panelGradient(
        juce::Colour(0xFF2A2A2A),
        0.0f, 0.0f,
        juce::Colour(0xFF1A1A1A),
        0.0f, static_cast<float>(bounds.getHeight()),
        false);
    g.setGradientFill(panelGradient);
    g.fillAll();

    if (!isInitialized)
        return;

    if (globalAreaBounds.getWidth() > 0 && globalAreaBounds.getHeight() > 0) {
        auto background = globalAreaBounds.expanded(0, kRowVerticalPadding).toFloat();
        g.setColour(DX7LookAndFeel::getLCDBackgroundColour().withAlpha(0.7f));
        g.fillRoundedRectangle(background, 4.0f);
        g.setColour(DX7LookAndFeel::getPanelLightColour());
        g.drawRoundedRectangle(background, 4.0f, 1.0f);
    }
}

void VoiceEditorPanel::paintOverChildren(juce::Graphics& g) {
    // Draw SVG algorithm diagram in the SVG column area
    if (!algorithmSvg || svgDrawArea.isEmpty())
        return;

    // Use the fixed viewBox dimensions (all SVGs have viewBox="0 0 80 520")
    // This ensures consistent positioning regardless of actual content bounds
    constexpr float svgViewBoxWidth = 80.0f;
    constexpr float svgViewBoxHeight = 520.0f;

    // Scale to fit the SVG draw area while maintaining aspect ratio
    float scaleX = svgDrawArea.getWidth() / svgViewBoxWidth;
    float scaleY = svgDrawArea.getHeight() / svgViewBoxHeight;
    float scale = std::min(scaleX, scaleY);

    // Center the SVG in the draw area
    float scaledWidth = svgViewBoxWidth * scale;
    float scaledHeight = svgViewBoxHeight * scale;
    float offsetX = svgDrawArea.getX() + (svgDrawArea.getWidth() - scaledWidth) / 2.0f;
    float offsetY = svgDrawArea.getY() + (svgDrawArea.getHeight() - scaledHeight) / 2.0f;

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
    addGap(6.0f);
    headerFlex.items.add(fixedItem(dataButton, 80.0f));
    headerFlex.performLayout(headerBounds.toFloat());

    layoutBounds.removeFromTop(kSectionGap);

    // Right column: Global parameters section + PEG widget
    const int globalPanelWidth = static_cast<int>(layoutBounds.getWidth() * 0.28f);
    auto globalPanelArea = layoutBounds.removeFromRight(globalPanelWidth);
    
    layoutBounds.removeFromRight(kColumnGap);
    
    // Precompute operator row height and slider height so global sliders can
    // match the operator sliders vertically. This makes the right-panel
    // global sliders the same height as the operator sliders.
    const int opCount_pre = static_cast<int>(operators.size());
    const int totalRows_pre = opCount_pre;
    const int totalRowGaps_pre = juce::jmax(0, totalRows_pre - 1) * kRowGap;
    const int availableHeight_pre = layoutBounds.getHeight() - totalRowGaps_pre;
    int rowHeight_pre = juce::jmax(kSliderLabelHeight + 40,
                                   availableHeight_pre > 0 ? availableHeight_pre / juce::jmax(1, totalRows_pre)
                                                           : kSliderLabelHeight + 40);
    int precomputedSliderHeight = juce::jmax(35, rowHeight_pre - kSliderLabelHeight);
    
    // Layout global sliders in the right panel (stacked vertically in rows)
    {
        auto globalContent = globalPanelArea.reduced(2, 0);
    const int sliderWidth = FMRackSliderConstants::kSliderWidth;
    const int sliderHeight = precomputedSliderHeight;
        const int hGap = FMRackSliderConstants::kSliderGap;
        const int vGap = 4;
        const int slidersPerRow = juce::jmax(1, (globalContent.getWidth() + hGap) / (sliderWidth + hGap));
        
        int x = globalContent.getX();
        int y = globalContent.getY();
        for (int i = 0; i < numGlobalSliders; ++i) {
            if (i > 0 && (i % slidersPerRow) == 0) {
                x = globalContent.getX();
                y += sliderHeight + vGap;
            }
            globalSliders[i].setBounds(x, y, sliderWidth, sliderHeight);
            globalSliders[i].getSlider().setEnabled(globalSliders[i].getLabelText().isNotEmpty());
            x += sliderWidth + hGap;
        }
        
        // PEG widget below the global sliders
        int pegY = y + sliderHeight + vGap * 2;
        int remainingHeight = globalContent.getBottom() - pegY;
        
        // Reduce PEG widget height to give more room to oscilloscope
        // PEG gets 35% of remaining space, oscilloscope gets 65%
        int pegHeight = static_cast<int>(remainingHeight * 0.35f) - vGap;
        int oscY = pegY + pegHeight + vGap * 2;
        int oscHeight = globalContent.getBottom() - oscY;
        
        if (pegHeight > 40) {
            pegAreaBounds = juce::Rectangle<int>(globalContent.getX(), pegY, globalContent.getWidth(), pegHeight);
            pegEnvelopeWidget.setBounds(pegAreaBounds.reduced(2));
        } else {
            pegAreaBounds = {};
            pegEnvelopeWidget.setBounds({});
        }
        
        // Oscilloscope at the bottom right
        if (oscHeight > 40) {
            oscilloscope.setBounds(globalContent.getX() + 2, oscY, globalContent.getWidth() - 4, oscHeight - 4);
        } else {
            oscilloscope.setBounds({});
        }
    }

    // SVG area will be calculated after operator layout based on operator positions

    if (layoutBounds.getHeight() <= 0)
        return;
    if (layoutBounds.getWidth() <= 0)
        return;

    const int opCount = static_cast<int>(operators.size());
    
    // Reserve space for help panel at the bottom
    const int helpHeight = 120;
    auto helpArea = layoutBounds.removeFromBottom(helpHeight);
    helpPanel.setBounds(helpArea.reduced(4, 2));
    layoutBounds.removeFromBottom(kRowGap);
    
    // Calculate operator rows (6 operators only, no global rows here anymore)
    // Use the precomputed slider height from earlier so both global and
    // operator sliders use the same vertical sizing metric.
    const int totalRows = opCount;
    const int totalRowGaps = juce::jmax(0, totalRows - 1) * kRowGap;
    const int availableHeight = layoutBounds.getHeight() - totalRowGaps;

    int rowHeight = juce::jmax(kSliderLabelHeight + 40,
                               availableHeight > 0 ? availableHeight / juce::jmax(1, totalRows)
                                                    : kSliderLabelHeight + 40);
    // write back to the member so other code can still read it
    computedSliderHeight = precomputedSliderHeight;

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

    for (int row = 0; row < opCount; ++row) {
        auto rowBounds = remaining.removeFromTop(rowHeight);
        if (row < opCount - 1)
            remaining.removeFromTop(kRowGap);

        auto rowContent = rowBounds.reduced(0, kRowVerticalPadding);

        if (operatorStartY < 0)
            operatorStartY = rowContent.getY();
        operatorBottomY = rowContent.getBottom();

        // Display operators in reverse order: OP6 (index 5) at top, OP1 (index 0) at bottom
        int operatorIdx = (opCount - 1) - row;
        if (operators[operatorIdx]) {
            operators[operatorIdx]->operatorIndex = operatorIdx;
            operators[operatorIdx]->isCarrierOperator = isCarrier(operatorIdx);
            operators[operatorIdx]->setBounds(rowContent);
        }
        operatorRowCenters.push_back(static_cast<float>(rowContent.getCentreY()));
    }

    if (operatorStartY >= 0 && operatorBottomY > operatorStartY) {
        operatorAreaBounds = juce::Rectangle<int>(layoutBounds.getX(), operatorStartY, layoutBounds.getWidth(), operatorBottomY - operatorStartY);
        // Calculate SVG draw area: positioned after the operator label, spanning operator rows
        svgDrawArea = juce::Rectangle<float>(
            static_cast<float>(layoutBounds.getX() + kOperatorLabelWidth),
            static_cast<float>(operatorStartY),
            80.0f,
            static_cast<float>(operatorBottomY - operatorStartY)
        ).reduced(4, 6);
    } else {
        operatorAreaBounds = {};
        svgDrawArea = {};
    }
    
    globalAreaBounds = {}; // Global sliders are now in the right panel
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

void VoiceEditorPanel::setupOperatorSlider(FMRackVerticalSlider& slider, const String& name, int /*min*/, int /*max*/, int /*defaultValue*/) {
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

    // --- Sync algorithm selector + diagram from current Dexed voice ---
    // DX7 algorithm is stored at voice data element 134 (0..31). We must update UI without
    // returning the value to the engine (so dontSendNotification).
    {
        const int algoIdx = static_cast<int>(getDexedParam(static_cast<uint8_t>(134)));
        const int clampedAlgo = juce::jlimit(0, 31, algoIdx);
        if (clampedAlgo != currentAlgorithm) {
            currentAlgorithm = clampedAlgo;
            algorithmSelector.setSelectedId(currentAlgorithm + 1, juce::dontSendNotification);
            loadAlgorithmSvg(currentAlgorithm);
        }
    }

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
        // operatorIndex is the UI label number minus 1: 0=OP1 label, 5=OP6 label.
        // But Dexed voice data stores operators in REVERSE order:
        //   OP6 at bytes 0-20 (dexedOpIdx=0), OP1 at bytes 105-125 (dexedOpIdx=5).
        // The opSwitch bitmask also follows this: bit 0=OP6, bit 5=OP1.
        // So we need: dexedOpIdx = 5 - operatorIndex
        const int dexedOpIdx = 5 - op->operatorIndex;
        std::cout << "[VoiceEditorPanel] syncAllOperatorSlidersWithDexed: OP" << (op->operatorIndex+1) << " (UI row " << uiRowIdx << ", dexedOpIdx=" << dexedOpIdx << ")" << std::endl;
        // Set operator enable/disable (OPE slider)
        bool opEnabled = (opeBitmask & (1 << dexedOpIdx)) != 0;
        const bool oldOpeValue = op->opeButton.getToggleState();
        op->opeButton.setToggleState(opEnabled, juce::dontSendNotification);
        std::cout << "  OPE: opeBitmask=0x" << std::hex << static_cast<int>(opeBitmask) << std::dec << " enabled=" << opEnabled << " oldToggle=" << oldOpeValue << " newToggle=" << op->opeButton.getToggleState() << std::endl;
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
            if (s == OperatorSliders::PM) {
                op->pmButton.setToggleState(value != 0, juce::dontSendNotification);
            } else {
                syncOperatorSliderWithDexed(op->sliders[s].getSlider(), paramAddress, sliderName.toRawUTF8());
            }
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
    resized();
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

    // Binary parameters: use checkboxes instead of 0/1 sliders.
    opeButton.setButtonText("OPE");
    opeButton.setColour(juce::ToggleButton::textColourId, juce::Colours::white);
    addAndMakeVisible(opeButton);
    opeButton.addMouseListener(this, false);

    pmButton.setButtonText("PM");
    pmButton.setColour(juce::ToggleButton::textColourId, juce::Colours::white);
    addAndMakeVisible(pmButton);
    pmButton.addMouseListener(this, false);

    // Set up slider ranges based on DX7 parameter specs
    // OPE: Operator Enable (binary checkbox)
    // TL: Total Level (0-99)
    sliders[TL].getSlider().setRange(0, 99, 1);
    // PM: Pitch Mode/Frequency Mode (binary checkbox)
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

    // --- UI -> engine wiring ---
    // Operator controls live in the OperatorSliders component, but the actual writes must go through
    // VoiceEditorPanel::setDexedParam(), using an operator-specific address.
    // We rely on VoiceEditorPanel::operatorSliderParamOffsets (built from VCED.json) for the per-operator
    // offsets, and operatorIndex for which operator row this instance represents.
    //
    // operatorIndex is the UI label number minus 1: 0=OP1 label, 5=OP6 label.
    // But Dexed voice data stores operators in REVERSE order:
    //   OP6 at bytes 0-20 (dexedOpIdx=0), OP1 at bytes 105-125 (dexedOpIdx=5).
    // The opSwitch bitmask also follows this: bit 0=OP6, bit 5=OP1.
    // So we need: dexedOpIdx = 5 - operatorIndex

    auto sendOperatorParam = [this](const juce::String& key, uint8_t value)
    {
        if (auto* parent = dynamic_cast<VoiceEditorPanel*>(getParentComponent())) {
            if (!parent->isInitialized)
                return;
            auto it = VoiceEditorPanel::operatorSliderParamOffsets.find(key);
            if (it == VoiceEditorPanel::operatorSliderParamOffsets.end())
                return;
            // Convert UI operatorIndex to Dexed data index
            const int dexedOpIdx = 5 - operatorIndex;
            const uint8_t paramAddress = static_cast<uint8_t>(dexedOpIdx * 21 + it->second);
            parent->setDexedParam(paramAddress, value);
        }
    };

    // OPE is stored as a bitmask at address 155 for the whole voice.
    opeButton.onClick = [this]()
    {
        if (auto* parent = dynamic_cast<VoiceEditorPanel*>(getParentComponent())) {
            if (!parent->isInitialized) {
                std::cout << "[OperatorSliders] OPE button clicked but parent not initialized" << std::endl;
                return;
            }
            const uint8_t opeAddr = static_cast<uint8_t>(155);
            uint8_t mask = parent->getDexedParam(opeAddr);
            // Convert UI operatorIndex to Dexed bit position
            const int dexedOpIdx = 5 - operatorIndex;
            const uint8_t bit = static_cast<uint8_t>(1u << dexedOpIdx);
            const bool newState = opeButton.getToggleState();
            if (newState)
                mask = static_cast<uint8_t>(mask | bit);
            else
                mask = static_cast<uint8_t>(mask & ~bit);
            std::cout << "[OperatorSliders] OPE OP" << (operatorIndex + 1) << " clicked: newState=" << newState << " mask=0x" << std::hex << static_cast<int>(mask) << std::dec << std::endl;
            parent->setDexedParam(opeAddr, mask);
        } else {
            std::cout << "[OperatorSliders] OPE button clicked but parent is null" << std::endl;
        }
    };

    // PM is per-operator, binary (0=ratio, 1=fixed).
    pmButton.onClick = [this, sendOperatorParam]()
    {
        std::cout << "[OperatorSliders] PM OP" << (operatorIndex + 1) << " clicked: newState=" << pmButton.getToggleState() << std::endl;
        sendOperatorParam(VoiceEditorPanel::OperatorSliders::sliderNames[PM], pmButton.getToggleState() ? 1u : 0u);
    };

    // Remaining per-operator sliders.
    for (int i = 0; i < NumSliders; ++i) {
        if (i == OPE || i == PM)
            continue;
        sliders[i].onValueChange = [this, i, sendOperatorParam]()
        {
            const auto value = static_cast<uint8_t>(sliders[i].getSlider().getValue());
            sendOperatorParam(VoiceEditorPanel::OperatorSliders::sliderNames[i], value);
        };
    }
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

    // Envelope and keyboard scaling widgets on the right (side by side horizontally)
    auto widgetArea = bounds.removeFromRight(256);
    auto envBounds = widgetArea.removeFromLeft(widgetArea.getWidth() / 2);
    envWidget.setBounds(envBounds.reduced(4));
    ksWidget.setBounds(widgetArea.reduced(4));

    // Sliders in the middle
    bounds.removeFromRight(16); // Gap before widgets

    // Compute slider width/gap from available bounds so the sliders are packed
    // tightly (matching the global params spacing). Use the shared kSliderGap
    // constant and respect a sensible minimum width.
    const int gap = kSliderGap; // tight gap (2px)
    const int minW = kMinSliderWidth;
    int usableWidth = bounds.getWidth();
    int slotWidth = minW;
    if (usableWidth > 0) {
        int candidate = (usableWidth - (NumSliders - 1) * gap) / NumSliders;
        slotWidth = juce::jmax(minW, candidate);
    }

    int totalWidth = (slotWidth * NumSliders) + (gap * (NumSliders - 1));
    int startX = bounds.getCentreX() - (totalWidth / 2);

    for (int i = 0; i < NumSliders; ++i) {
        int x = startX + (i * (slotWidth + gap));
        if (i == OPE) {
            sliders[i].setVisible(false);
            opeButton.setVisible(true);
            opeButton.setBounds(x, bounds.getY() + 6, slotWidth, 24);
        } else if (i == PM) {
            sliders[i].setVisible(false);
            pmButton.setVisible(true);
            pmButton.setBounds(x, bounds.getY() + 6, slotWidth, 24);
        } else {
            sliders[i].setVisible(true);
            sliders[i].setBounds(x, bounds.getY(), slotWidth, bounds.getHeight());
        }
    }
}

void VoiceEditorPanel::OperatorSliders::sliderMouseEnter(int sliderIdx)
{
    juce::ignoreUnused(sliderIdx);
    // Forward to parent VoiceEditorPanel
    if (auto* parent = dynamic_cast<VoiceEditorPanel*>(getParentComponent())) {
        parent->showHelpForKey(sliderNames[sliderIdx]);
    }
}

void VoiceEditorPanel::OperatorSliders::sliderMouseExit(int sliderIdx)
{
    juce::ignoreUnused(sliderIdx);
    // Forward to parent VoiceEditorPanel
    if (auto* parent = dynamic_cast<VoiceEditorPanel*>(getParentComponent())) {
        parent->restoreDefaultHelp();
    }
}

// --- VoiceEditorPanel hover help for global sliders ---
void VoiceEditorPanel::mouseEnter(const juce::MouseEvent& e) {
    for (int i = 0; i < numGlobalSliders; ++i) {
        // Check both the labeled slider component and its internal slider
        if (e.eventComponent == &globalSliders[i] || 
            e.eventComponent == &globalSliders[i].getSlider()) {
            showHelpForKey(globalSliderKeys[i]);
            return;
        }
    }
}

void VoiceEditorPanel::mouseExit(const juce::MouseEvent& e) {
    for (int i = 0; i < numGlobalSliders; ++i) {
        // Check both the labeled slider component and its internal slider
        if (e.eventComponent == &globalSliders[i] || 
            e.eventComponent == &globalSliders[i].getSlider()) {
            restoreDefaultHelp();
            return;
        }
    }
}

// --- OperatorSliders hover help for operator sliders, envelope, and keyboard scaling ---
void VoiceEditorPanel::OperatorSliders::mouseEnter(const juce::MouseEvent& e) {
    if (e.eventComponent == &opeButton) {
        sliderMouseEnter(OPE);
        return;
    }
    if (e.eventComponent == &pmButton) {
        sliderMouseEnter(PM);
        return;
    }
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
    if (e.eventComponent == &opeButton) {
        sliderMouseExit(OPE);
        return;
    }
    if (e.eventComponent == &pmButton) {
        sliderMouseExit(PM);
        return;
    }
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
    
    // Register oscilloscope with controller to receive audio samples
    if (controller) {
        controller->setOscilloscope(&oscilloscope);
        oscilloscope.setController(controller);
    }
    
    initializeIfReady();
}

void VoiceEditorPanel::setModuleIndex(int idx) {
    moduleIndex = idx;
    std::cout << "[VoiceEditorPanel::setModuleIndex] moduleIndex set to " << idx << std::endl;
    
    // Update oscilloscope to monitor this module's output
    oscilloscope.setMonitoredModuleIndex(idx);
    
    initializeIfReady();
    // When re-targeting the editor to a different module (or reopening), immediately pull
    // current state from the engine so UI reflects reality (e.g., OPE bitmask).
    if (isInitialized) {
        syncAllOperatorSlidersWithDexed();
    }
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
                        // IMPORTANT: don't mutate OPE when applying a voice dump. If the received voice
                        // explicitly has OPE=0 (all operators off), that's a valid state.
                        const uint8_t newOpeBitmask = dataCopy[6 + 155];
                        std::cout << "[VoiceEditorPanel::onSingleVoiceDumpReceived] Applying OPE bitmask from dump: 0x" << std::hex << static_cast<int>(newOpeBitmask) << std::dec << std::endl;
                        for (int i = 0; i < 155; ++i) {
                            std::cout << "[VoiceEditorPanel::onSingleVoiceDumpReceived] Setting Dexed param " << i << " = " << static_cast<int>(dataCopy[6 + i]) << std::endl;
                            dexed->setVoiceDataElement(static_cast<uint8_t>(i), dataCopy[6 + i]);
                        }
                        dexed->setVoiceDataElement(static_cast<uint8_t>(155), newOpeBitmask);
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

            // Sync algorithm selector/SVG to the loaded voice data (so diagram matches sound).
            const int algoIdx = static_cast<int>(getDexedParam(static_cast<uint8_t>(134)));
            if (algoIdx >= 0 && algoIdx < 32) {
                currentAlgorithm = algoIdx;
                algorithmSelector.setSelectedId(algoIdx + 1, juce::dontSendNotification);
                loadAlgorithmSvg(algoIdx);
                resized();
                repaint();
            }
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


void VoiceEditorPanel::copyVoiceDataToClipboard() {
    std::cout << "[VoiceEditorPanel] ========================================" << std::endl;
    std::cout << "[VoiceEditorPanel] Copy operation initiated (Ctrl+C or Data button)" << std::endl;
    
    if (!isInitialized) {
        std::cout << "[VoiceEditorPanel] ERROR: Cannot copy - panel not initialized" << std::endl;
        return;
    }
    if (!controller) {
        std::cout << "[VoiceEditorPanel] ERROR: Cannot copy - no controller" << std::endl;
        return;
    }

    // Get all 156 bytes of voice data
    std::vector<uint8_t> voiceData;
    for (int i = 0; i < 156; ++i) {
        voiceData.push_back(getDexedParam(static_cast<uint8_t>(i)));
    }

    // Build F0 sysex string: F0 43 12 00 <156 bytes data> F7
    juce::String sysexHex = "F0 43 12 00 ";
    
    for (int i = 0; i < 156; ++i) {
        sysexHex << juce::String::toHexString(voiceData[i]).paddedLeft('0', 2);
        if (i < 155) sysexHex << " ";
    }
    
    sysexHex << " F7";

    // Copy to clipboard
    juce::SystemClipboard::copyTextToClipboard(sysexHex);

    // Log to console
    std::cout << "[VoiceEditorPanel] Voice data copied to clipboard (" << sysexHex.length() << " characters)" << std::endl;
    std::cout << "[VoiceEditorPanel] Data: " << sysexHex.substring(0, 80) << "..." << std::endl;
    std::cout << "[VoiceEditorPanel] ========================================" << std::endl;
}

void VoiceEditorPanel::pasteVoiceDataFromClipboard() {
    std::cout << "[VoiceEditorPanel] ========================================" << std::endl;
    std::cout << "[VoiceEditorPanel] Paste operation initiated (Ctrl+V)" << std::endl;
    
    if (!isInitialized) {
        std::cout << "[VoiceEditorPanel] ERROR: Cannot paste - panel not initialized" << std::endl;
        return;
    }
    if (!controller) {
        std::cout << "[VoiceEditorPanel] ERROR: Cannot paste - no controller" << std::endl;
        return;
    }

    juce::String clipboardText = juce::SystemClipboard::getTextFromClipboard();
    std::cout << "[VoiceEditorPanel] Clipboard length: " << clipboardText.length() << " characters" << std::endl;
    
    if (clipboardText.isEmpty()) {
        std::cout << "[VoiceEditorPanel] ERROR: Clipboard is empty" << std::endl;
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
            "Paste Voice Data", "Clipboard is empty.\n\nExpected format: F0 43 12 00 [156 hex bytes] F7");
        return;
    }
    
    std::cout << "[VoiceEditorPanel] Clipboard text (first 100 chars): " << clipboardText.substring(0, 100) << std::endl;

    // Parse the sysex data
    // Expected format: F0 43 12 00 [156 hex bytes] F7 or similar variants
    
    // Remove all whitespace and convert to uppercase
    juce::String cleanedText = clipboardText.removeCharacters(" \n\r\t").toUpperCase();
    std::cout << "[VoiceEditorPanel] After cleanup: " << cleanedText.length() << " characters" << std::endl;
    
    // Try to extract hex data between F0 and F7
    int startIdx = cleanedText.indexOf("F0");
    int endIdx = cleanedText.lastIndexOf("F7");
    
    if (startIdx == -1 || endIdx == -1 || endIdx <= startIdx) {
        std::cout << "[VoiceEditorPanel] ERROR: Invalid sysex format - F0/F7 markers not found" << std::endl;
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
            "Paste Voice Data", "Invalid sysex format: F0/F7 markers not found.\n\nExpected format: F0 43 12 00 [156 hex bytes] F7");
        return;
    }

    // Extract the data portion (skip F0 at start, skip F7 at end)
    juce::String hexData = cleanedText.substring(startIdx + 2, endIdx);
    std::cout << "[VoiceEditorPanel] Extracted hex data length: " << hexData.length() << " characters" << std::endl;
    
    // Expected: 43 12 00 followed by 156 bytes (312 hex characters)
    // Skip manufacturer ID and voice-specific header
    if (hexData.length() < 6) {
        std::cout << "[VoiceEditorPanel] ERROR: Data too short (header missing)" << std::endl;
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
            "Paste Voice Data", "Data too short - sysex header missing.");
        return;
    }

    // Skip the "43 12 00" header (6 characters)
    juce::String voiceHexData = hexData.substring(6);
    std::cout << "[VoiceEditorPanel] Voice data length: " << voiceHexData.length() << " hex characters (" << (voiceHexData.length() / 2) << " bytes)" << std::endl;
    
    // Each byte is 2 hex characters, so 156 bytes = 312 characters
    if (voiceHexData.length() < 312) {
        std::cout << "[VoiceEditorPanel] ERROR: Incomplete voice data - expected 312 hex chars, got " << voiceHexData.length() << std::endl;
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
            "Paste Voice Data", "Incomplete voice data.\n\nExpected 156 bytes (312 hex characters), got " + 
            juce::String(voiceHexData.length() / 2) + " bytes.");
        return;
    }

    // Parse the hex bytes into a vector (validation pass)
    std::vector<uint8_t> parsedData;
    parsedData.reserve(156);
    
    for (int i = 0; i < 156; ++i) {
        juce::String hexByte = voiceHexData.substring(i * 2, i * 2 + 2);
        int value = hexByte.getHexValue32();
        
        if (value < 0 || value > 255) {
            std::cout << "[VoiceEditorPanel] ERROR: Invalid hex value at byte " << i << ": '" << hexByte << "'" << std::endl;
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                "Paste Voice Data", "Invalid hex value at byte " + juce::String(i) + ": '" + hexByte + "'");
            return;
        }
        parsedData.push_back(static_cast<uint8_t>(value));
    }

    // Extract voice name from bytes 145-154 (10 ASCII characters)
    juce::String voiceName;
    for (int i = 145; i < 155 && i < static_cast<int>(parsedData.size()); ++i) {
        char c = static_cast<char>(parsedData[i]);
        if (c >= 32 && c < 127) {
            voiceName += c;
        } else {
            voiceName += ' ';
        }
    }
    voiceName = voiceName.trim();
    if (voiceName.isEmpty()) {
        voiceName = "(unnamed)";
    }

    std::cout << "[VoiceEditorPanel] Parsed successfully: 156 bytes, voice name: '" << voiceName << "'" << std::endl;
    std::cout << "[VoiceEditorPanel] Showing confirmation dialog..." << std::endl;

    // Show confirmation dialog before applying
    // We need to capture parsedData by value since we're using an async callback
    auto dataToApply = std::make_shared<std::vector<uint8_t>>(std::move(parsedData));
    
    juce::AlertWindow::showOkCancelBox(
        juce::MessageBoxIconType::QuestionIcon,
        "Paste Voice Data",
        "Apply pasted voice data?\n\nVoice name: " + voiceName + "\nData size: 156 bytes",
        "Apply",
        "Cancel",
        this,
        juce::ModalCallbackFunction::create([this, dataToApply, voiceName](int result) {
            if (result == 1) {
                std::cout << "[VoiceEditorPanel] User confirmed paste - applying " << dataToApply->size() << " bytes" << std::endl;
                
                // Apply all 156 bytes
                for (int i = 0; i < static_cast<int>(dataToApply->size()); ++i) {
                    setDexedParam(static_cast<uint8_t>(i), (*dataToApply)[i]);
                    if (i % 21 == 0) {
                        std::cout << "  [Paste] Applied bytes " << i << "-" << (i + 20) << " (Operator " << (6 - (i / 21)) << ")" << std::endl;
                    }
                }
                
                std::cout << "[VoiceEditorPanel] ========================================" << std::endl;
                std::cout << "[VoiceEditorPanel] Paste complete: " << dataToApply->size() << " bytes applied" << std::endl;
                std::cout << "[VoiceEditorPanel] Voice name: '" << voiceName << "'" << std::endl;
                std::cout << "[VoiceEditorPanel] ========================================" << std::endl;
                
                // Refresh the UI to show the new values
                syncAllOperatorSlidersWithDexed();
            } else {
                std::cout << "[VoiceEditorPanel] User cancelled paste operation" << std::endl;
            }
        })
    );
}

// --- Operator parameter offset mapping ---
// Build this dynamically from VCED.json
std::map<juce::String, uint8_t> VoiceEditorPanel::operatorSliderParamOffsets;

void VoiceEditorPanel::syncOperatorSliderWithDexed(FMRackVerticalSlider& slider, uint8_t paramAddress, const char* sliderKey)
{
    uint8_t value = getDexedParam(paramAddress);
    auto range = getDexedRange(sliderKey);
    slider.setRange(range.first, range.second, 1.0);
    slider.setValue(value, juce::dontSendNotification);
    std::cout << "[VoiceEditorPanel::syncOperatorSliderWithDexed] " << sliderKey << ": value=" << static_cast<int>(value) << " range=[" << static_cast<int>(range.first) << "-" << static_cast<int>(range.second) << "] oldSlider=" << slider.getValue() << " newSlider=" << value << std::endl;
}
