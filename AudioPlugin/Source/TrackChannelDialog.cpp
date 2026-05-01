#include "TrackChannelDialog.h"

TrackChannelDialog::TrackChannelDialog(const juce::MidiFile& midiFile)
{
    titleLabel.setText("Assign MIDI Tracks to Channels", juce::dontSendNotification);
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    titleLabel.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
    titleLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(titleLabel);

    headerTrack.setText("Track", juce::dontSendNotification);
    headerTrack.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(headerTrack);

    headerChannel.setText("Channel", juce::dontSendNotification);
    headerChannel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(headerChannel);

    addAndMakeVisible(viewport);
    viewport.setViewedComponent(&rowContainer, false);
    viewport.setScrollBarsShown(true, false);

    okButton.onClick = [this]()
    {
        // Collect assignments from combos
        for (size_t i = 0; i < rows.size(); ++i)
        {
            int comboIdx = rows[i]->channelCombo->getSelectedItemIndex();
            assignments[i].assignedChannel = comboIdx; // 0=Skip, 1=Ch1..16=Ch16
        }
        if (onDone) onDone(true);
    };
    addAndMakeVisible(okButton);

    cancelButton.onClick = [this]()
    {
        if (onDone) onDone(false);
    };
    addAndMakeVisible(cancelButton);

    buildRows(midiFile);

    setSize(500, 400);
}

void TrackChannelDialog::buildRows(const juce::MidiFile& midiFile)
{
    rows.clear();
    assignments.clear();

    const int numTracks = midiFile.getNumTracks();

    for (int t = 0; t < numTracks; ++t)
    {
        const juce::MidiMessageSequence* seq = midiFile.getTrack(t);
        if (seq == nullptr)
            continue;

        // Find track name meta event
        juce::String trackName = "Track " + juce::String(t + 1);
        int noteCount = 0;

        for (int i = 0; i < seq->getNumEvents(); ++i)
        {
            const auto* holder = seq->getEventPointer(i);
            if (holder == nullptr) continue;
            const juce::MidiMessage& msg = holder->message;

            if (msg.isTrackNameEvent())
                trackName = msg.getTextFromTextMetaEvent();

            if (msg.isNoteOn())
                ++noteCount;
        }

        auto row = std::make_unique<TrackRow>();

        row->nameLabel = std::make_unique<juce::Label>();
        row->nameLabel->setText(trackName, juce::dontSendNotification);
        row->nameLabel->setColour(juce::Label::textColourId, juce::Colours::white);
        rowContainer.addAndMakeVisible(*row->nameLabel);

        row->infoLabel = std::make_unique<juce::Label>();
        row->infoLabel->setText(juce::String(noteCount) + " notes", juce::dontSendNotification);
        row->infoLabel->setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        rowContainer.addAndMakeVisible(*row->infoLabel);

        row->channelCombo = std::make_unique<juce::ComboBox>();
        row->channelCombo->addItem("Skip", 1);
        for (int ch = 1; ch <= 16; ++ch)
            row->channelCombo->addItem("Ch " + juce::String(ch), ch + 1);

        int defaultChannel = (t % 16) + 1;
        row->channelCombo->setSelectedId(defaultChannel + 1, juce::dontSendNotification);
        rowContainer.addAndMakeVisible(*row->channelCombo);

        TrackAssignment ta;
        ta.originalTrack = t;
        ta.trackName = trackName.toStdString();
        ta.assignedChannel = defaultChannel;
        assignments.push_back(ta);

        rows.push_back(std::move(row));
    }
}

void TrackChannelDialog::resized()
{
    auto area = getLocalBounds().reduced(8);

    titleLabel.setBounds(area.removeFromTop(28));
    area.removeFromTop(4);

    auto headerRow = area.removeFromTop(20);
    headerTrack.setBounds(headerRow.removeFromLeft(headerRow.getWidth() * 2 / 3));
    headerChannel.setBounds(headerRow);
    area.removeFromTop(2);

    auto buttonArea = area.removeFromBottom(36);
    area.removeFromBottom(4);

    int buttonWidth = 80;
    cancelButton.setBounds(buttonArea.removeFromRight(buttonWidth));
    buttonArea.removeFromRight(8);
    okButton.setBounds(buttonArea.removeFromRight(buttonWidth));

    viewport.setBounds(area);

    const int rowHeight = 30;
    const int rowPad = 4;
    int containerHeight = static_cast<int>(rows.size()) * (rowHeight + rowPad);
    rowContainer.setSize(area.getWidth() - 16, juce::jmax(containerHeight, area.getHeight()));

    int y = 0;
    int w = rowContainer.getWidth();
    for (auto& row : rows)
    {
        int nameW = w * 2 / 5;
        int infoW = w * 2 / 5;
        int comboW = w - nameW - infoW - 4;

        row->nameLabel->setBounds(0, y, nameW, rowHeight);
        row->infoLabel->setBounds(nameW + 2, y, infoW, rowHeight);
        row->channelCombo->setBounds(nameW + infoW + 4, y, comboW, rowHeight);

        y += rowHeight + rowPad;
    }
}

void TrackChannelDialog::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff2a2a2a));
}
