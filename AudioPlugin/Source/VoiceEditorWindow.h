#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Forward declaration
class AudioPluginAudioProcessorEditor;

class VoiceEditorWindow : public juce::DocumentWindow
{
public:
    VoiceEditorWindow(const juce::String& name, juce::Colour backgroundColour, int buttonsNeeded, AudioPluginAudioProcessorEditor* owner);
    
    void closeButtonPressed() override;
    
private:
    AudioPluginAudioProcessorEditor* editorOwner;
};