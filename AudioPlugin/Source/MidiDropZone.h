#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <functional>
#include "TrackChannelDialog.h"

class MidiDropZone : public juce::Component, public juce::FileDragAndDropTarget {
public:
    MidiDropZone();
    ~MidiDropZone() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;

    std::function<void(juce::MidiFile, juce::String)> onMidiReady;

private:
    bool isDragOver { false };

    void processDroppedFile(const juce::File& file);
    void showTrackChannelDialog(const juce::MidiFile& midiFile, const juce::File& originalFile);

    std::unique_ptr<juce::DialogWindow> trackDialog;
    std::unique_ptr<TrackChannelDialog> trackChannelComp;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiDropZone)
};
