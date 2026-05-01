#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <string>

struct TrackAssignment {
    int originalTrack;
    std::string trackName;
    int assignedChannel; // 1-16, or 0 = skip
};

class TrackChannelDialog : public juce::Component {
public:
    explicit TrackChannelDialog(const juce::MidiFile& midiFile);

    void resized() override;
    void paint(juce::Graphics& g) override;

    const std::vector<TrackAssignment>& getAssignments() const { return assignments; }
    std::function<void(bool accepted)> onDone;

private:
    struct TrackRow {
        std::unique_ptr<juce::Label> nameLabel;
        std::unique_ptr<juce::Label> infoLabel;
        std::unique_ptr<juce::ComboBox> channelCombo;
    };

    juce::Label titleLabel;
    juce::Label headerTrack, headerChannel;
    juce::TextButton okButton { "OK" };
    juce::TextButton cancelButton { "Cancel" };
    juce::Viewport viewport;
    juce::Component rowContainer;

    std::vector<std::unique_ptr<TrackRow>> rows;
    std::vector<TrackAssignment> assignments;

    void buildRows(const juce::MidiFile& midiFile);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackChannelDialog)
};
