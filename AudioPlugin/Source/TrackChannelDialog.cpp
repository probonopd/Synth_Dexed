#include "TrackChannelDialog.h"

static juce::String getTrackName(const juce::MidiMessageSequence* track) {
    for (int i = 0; i < track->getNumEvents(); ++i) {
        const auto& msg = track->getEventPointer(i)->message;
        if (msg.isTrackNameEvent())
            return msg.getTextFromTextMetaEvent();
    }
    return {};
}

static int countNoteEvents(const juce::MidiMessageSequence* track) {
    int count = 0;
    for (int i = 0; i < track->getNumEvents(); ++i)
        if (track->getEventPointer(i)->message.isNoteOn())
            ++count;
    return count;
}

TrackChannelDialog::TrackChannelDialog(const juce::MidiFile& midiFile) {
    addAndMakeVisible(titleLabel);
    titleLabel.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
    titleLabel.setJustificationType(juce::Justification::centred);
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);

    addAndMakeVisible(viewport);
    viewport.setViewedComponent(&rowContainer, false);
    viewport.setScrollBarsShown(true, false);

    addAndMakeVisible(okButton);
    addAndMakeVisible(cancelButton);

    okButton.onClick = [this] {
        for (int i = 0; i < (int)rows.size(); ++i) {
            int id = rows[i]->channelCombo.getSelectedId();
            // id==17 means Skip (channel 0), id 1-16 means channel 1-16
            assignments[i].assignedChannel = (id == 17) ? 0 : id;
        }
        if (onDone) onDone(true);
    };
    cancelButton.onClick = [this] {
        if (onDone) onDone(false);
    };

    buildRows(midiFile);
}

void TrackChannelDialog::buildRows(const juce::MidiFile& midiFile) {
    rowContainer.removeAllChildren();
    rows.clear();
    assignments.clear();

    for (int t = 0; t < midiFile.getNumTracks(); ++t) {
        const auto* track = midiFile.getTrack(t);
        int noteCount = countNoteEvents(track);
        if (noteCount == 0) continue;

        auto row = std::make_unique<TrackRow>();
        juce::String name = getTrackName(track);
        if (name.isEmpty()) name = "Track " + juce::String(t + 1);
        name += " (" + juce::String(noteCount) + " notes)";

        row->nameLabel.setText(name, juce::dontSendNotification);
        row->nameLabel.setColour(juce::Label::textColourId, juce::Colours::white);

        row->channelCombo.clear();
        row->channelCombo.addItem("Skip", 17);
        for (int ch = 1; ch <= 16; ++ch)
            row->channelCombo.addItem("Ch " + juce::String(ch), ch);

        int defaultCh = ((int)assignments.size() % 16) + 1;
        row->channelCombo.setSelectedId(defaultCh, juce::dontSendNotification);

        rowContainer.addAndMakeVisible(row->nameLabel);
        rowContainer.addAndMakeVisible(row->channelCombo);

        TrackAssignment asn;
        asn.originalTrack = t;
        asn.trackName = name.toStdString();
        asn.assignedChannel = defaultCh;
        assignments.push_back(asn);

        rows.push_back(std::move(row));
    }

    const int rowH = 32;
    rowContainer.setSize(400, (int)rows.size() * rowH + 8);

    int y = 4;
    for (auto& row : rows) {
        row->nameLabel.setBounds(4, y, 260, rowH - 4);
        row->channelCombo.setBounds(268, y, 120, rowH - 4);
        y += rowH;
    }
}

void TrackChannelDialog::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff2a2a2a));
}

void TrackChannelDialog::resized() {
    auto area = getLocalBounds().reduced(8);
    titleLabel.setBounds(area.removeFromTop(28));
    area.removeFromTop(4);
    auto buttons = area.removeFromBottom(36);
    area.removeFromBottom(4);
    viewport.setBounds(area);
    cancelButton.setBounds(buttons.removeFromRight(80));
    buttons.removeFromRight(8);
    okButton.setBounds(buttons.removeFromRight(80));
}
