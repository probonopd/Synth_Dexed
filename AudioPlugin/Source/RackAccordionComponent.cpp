#include "RackAccordionComponent.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>

// ================= RackAccordionComponent =================
RackAccordionComponent::RackAccordionComponent(FMRack::Rack* rackPtr)
    : rack(rackPtr)
{
    addAndMakeVisible(numModulesLabel);
    addAndMakeVisible(numModulesSlider);
    addAndMakeVisible(tabs);
    numModulesLabel.setText("Modules", juce::dontSendNotification);
    numModulesSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    numModulesSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    numModulesSlider.setRange(1, 8, 1);
    numModulesSlider.setValue(1);
    updatePanels();
}

void RackAccordionComponent::updatePanels()
{
    tabs.clearTabs();
    moduleTabs.clear();
    if (!rack) return;
    const auto& modules = rack->getModules();
    int numModules = (int)modules.size();
    for (int i = 0; i < numModules; ++i)
    {
        auto tab = std::make_unique<ModuleTabComponent>(modules[i].get(), i, this);
        tabs.addTab("Module " + juce::String(i + 1), juce::Colours::lightblue, tab.get(), false);
        moduleTabs.push_back(std::move(tab));
    }
    resized();
}

void RackAccordionComponent::resized()
{
    int x = 10, y = 10, w = 120, h = 24, gap = 6;
    numModulesLabel.setBounds(x, y, w, h);
    numModulesSlider.setBounds(x + w + 4, y, 80, h);
    y += h + gap;
    int tabY = y;
    tabs.setBounds(0, tabY, getWidth(), getHeight() - tabY);
}

void RackAccordionComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::darkgrey);
}

// ================= ModuleTabComponent =================
ModuleTabComponent::ModuleTabComponent(FMRack::Module* modulePtr, int idx, RackAccordionComponent* parent)
{
    unisonVoicesLabel.setText("Unison Voices", juce::dontSendNotification);
    addAndMakeVisible(unisonVoicesLabel);
    unisonVoicesSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    unisonVoicesSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    unisonVoicesSlider.setRange(1, 4, 1);
    unisonVoicesSlider.setValue(1);
    addAndMakeVisible(unisonVoicesSlider);

    unisonDetuneLabel.setText("Unison Detune", juce::dontSendNotification);
    addAndMakeVisible(unisonDetuneLabel);
    unisonDetuneSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    unisonDetuneSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    unisonDetuneSlider.setRange(0.0, 50.0, 0.1);
    unisonDetuneSlider.setValue(7.0);
    addAndMakeVisible(unisonDetuneSlider);

    unisonPanLabel.setText("Unison Pan", juce::dontSendNotification);
    addAndMakeVisible(unisonPanLabel);
    unisonPanSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    unisonPanSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    unisonPanSlider.setRange(0.0, 1.0, 0.01);
    unisonPanSlider.setValue(0.5);
    addAndMakeVisible(unisonPanSlider);
}

void ModuleTabComponent::resized()
{
    int x = 10, y = 10, w = 120, h = 24, gap = 6;
    unisonVoicesLabel.setBounds(x, y, w, h);
    unisonVoicesSlider.setBounds(x + w + 4, y, 80, h);
    y += h + gap;
    unisonDetuneLabel.setBounds(x, y, w, h);
    unisonDetuneSlider.setBounds(x + w + 4, y, 80, h);
    y += h + gap;
    unisonPanLabel.setBounds(x, y, w, h);
    unisonPanSlider.setBounds(x + w + 4, y, 80, h);
    // Add more controls here if needed
}
