#include "VoiceEditorWindow.h"
#include "PluginEditor.h"

VoiceEditorWindow::VoiceEditorWindow(const juce::String& name, juce::Colour backgroundColour, int buttonsNeeded, AudioPluginAudioProcessorEditor* owner)
    : juce::DocumentWindow(name, backgroundColour, buttonsNeeded), editorOwner(owner)
{
}

void VoiceEditorWindow::closeButtonPressed()
{
    setVisible(false);
    // Don't delete the window, just hide it so it can be reused
}