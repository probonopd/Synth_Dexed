#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <string>

struct TrackAssignment {
    int originalTrack { 0 };
    std::string trackName;
    int assignedChannel { 1 }; // 1-16, 0 = skip
};

class TrackChannelDialog : public juce::Component {
public:
    explicit TrackChannelDialog(const juce::MidiFile& midiFile);
    ~TrackChannelDialog() override = default;

    void resized() override;
    void paint(juce::Graphics&) override;

    const std::vector<TrackAssignment>& getAssignments() const { return assignments; }

    std::function<void(bool accepted)> onDone;

private:
    struct TrackRow {
        juce::Label nameLabel;
        juce::ComboBox channelCombo;
    };

    juce::Label titleLabel { {}, "Assign MIDI Tracks to Channels" };
    juce::TextButton okButton { "OK" };
    juce::TextButton cancelButton { "Cancel" };
    juce::Viewport viewport;
    juce::Component rowContainer;

    std::vector<std::unique_ptr<TrackRow>> rows;
    std::vector<TrackAssignment> assignments;

    void buildRows(const juce::MidiFile& midiFile);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackChannelDialog)
};
