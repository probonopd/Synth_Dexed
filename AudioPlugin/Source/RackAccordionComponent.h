#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../../src/FMRack/Rack.h"

// Forward declaration
class ModuleTabComponent;

class RackAccordionComponent : public juce::Component
{
public:
    RackAccordionComponent(FMRack::Rack* rackPtr);
    void resized() override;
    void paint(juce::Graphics&) override;
    void updatePanels();
    
    // Controls to move from PluginEditor
    juce::Slider numModulesSlider;
    juce::Label numModulesLabel;
    
private:
    FMRack::Rack* rack;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    std::vector<std::unique_ptr<ModuleTabComponent>> moduleTabs;
};

class ModuleTabComponent : public juce::Component {
public:
    ModuleTabComponent(FMRack::Module* modulePtr, int idx, RackAccordionComponent* parent);
    void resized() override;
    
    // Per-tab controls
    juce::Slider unisonVoicesSlider;
    juce::Label unisonVoicesLabel;
    juce::Slider unisonDetuneSlider;
    juce::Label unisonDetuneLabel;
    juce::Slider unisonPanSlider;
    juce::Label unisonPanLabel;
    // Optionally: add more controls here for module parameters
};
