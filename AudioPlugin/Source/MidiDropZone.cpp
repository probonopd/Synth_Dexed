#include "MidiDropZone.h"

MidiDropZone::MidiDropZone() {
    hintLabel.setText("Drop MIDI file here", juce::dontSendNotification);
    hintLabel.setJustificationType(juce::Justification::centred);
    hintLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    hintLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(hintLabel);
}

void MidiDropZone::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    juce::Colour bg = isDragOver ? juce::Colour(0xff3a5a3a) : juce::Colour(0xff2a2a2a);
    g.fillRoundedRectangle(bounds, 4.0f);

    juce::Colour border = isDragOver ? juce::Colours::limegreen : juce::Colour(0xff555555);
    g.setColour(border);
    g.drawRoundedRectangle(bounds.reduced(1.0f), 4.0f, 1.5f);
}

void MidiDropZone::resized() {
    hintLabel.setBounds(getLocalBounds());
}

bool MidiDropZone::isInterestedInFileDrag(const juce::StringArray& files) {
    for (const auto& f : files)
        if (f.endsWithIgnoreCase(".mid") || f.endsWithIgnoreCase(".midi"))
            return true;
    return false;
}

void MidiDropZone::fileDragEnter(const juce::StringArray&, int, int) {
    isDragOver = true;
    repaint();
}

void MidiDropZone::fileDragExit(const juce::StringArray&) {
    isDragOver = false;
    repaint();
}

void MidiDropZone::filesDropped(const juce::StringArray& files, int, int) {
    isDragOver = false;
    repaint();

    for (const auto& f : files) {
        juce::File file(f);
        if (file.existsAsFile() &&
            (file.hasFileExtension(".mid") || file.hasFileExtension(".midi"))) {
            processFile(file);
            break;
        }
    }
}

void MidiDropZone::processFile(const juce::File& file) {
    juce::FileInputStream fis(file);
    if (!fis.openedOk()) return;

    juce::MidiFile midi;
    if (!midi.readFrom(fis)) return;

    // Count note-bearing tracks
    int noteTracks = 0;
    for (int t = 0; t < midi.getNumTracks(); ++t) {
        for (int i = 0; i < midi.getTrack(t)->getNumEvents(); ++i) {
            if (midi.getTrack(t)->getEventPointer(i)->message.isNoteOn()) { ++noteTracks; break; }
        }
    }

    if (noteTracks <= 1) {
        // No dialog needed — fire directly
        if (onMidiReady) onMidiReady(midi, file.getFileName());
        hintLabel.setText("Loaded: " + file.getFileName(), juce::dontSendNotification);
        return;
    }

    showTrackDialog(midi, file);
}

void MidiDropZone::showTrackDialog(const juce::MidiFile& midi, const juce::File& originalFile) {
    trackDialogComp = std::make_unique<TrackChannelDialog>(midi);
    auto* compPtr = trackDialogComp.get();

    class TrackDialogWindow : public juce::DialogWindow {
    public:
        std::function<void()> onClose;
        TrackDialogWindow(const juce::String& title, juce::Component* c)
            : juce::DialogWindow(title, juce::Colour(0xff2a2a2a), true) {
            setContentNonOwned(c, true);
            setResizable(false, false);
            centreWithSize(420, 400);
            setVisible(true);
        }
        void closeButtonPressed() override { if (onClose) onClose(); }
    };

    juce::MidiFile midiCopy = midi;
    juce::String fileName = originalFile.getFileName();

    compPtr->onDone = [this, midiCopy, fileName](bool accepted) {
        if (accepted && trackDialogComp) {
            const auto& assignments = trackDialogComp->getAssignments();
            // Build remapped MIDI
            juce::MidiFile remapped;
            remapped.setTicksPerQuarterNote(midiCopy.getTimeFormat());
            for (const auto& asn : assignments) {
                if (asn.assignedChannel == 0) continue;
                if (asn.originalTrack >= midiCopy.getNumTracks()) continue;
                const auto* src = midiCopy.getTrack(asn.originalTrack);
                juce::MidiMessageSequence seq;
                for (int i = 0; i < src->getNumEvents(); ++i) {
                    auto msg = src->getEventPointer(i)->message;
                    if (!msg.isMetaEvent())
                        msg.setChannel(asn.assignedChannel);
                    seq.addEvent(msg);
                }
                remapped.addTrack(seq);
            }
            if (onMidiReady) onMidiReady(remapped, fileName);
            hintLabel.setText("Loaded: " + fileName, juce::dontSendNotification);
        }
        if (trackDialogWindow)
            trackDialogWindow->setVisible(false);
        trackDialogWindow.reset();
        trackDialogComp.reset();
    };

    auto* win = new TrackDialogWindow("Assign Tracks", compPtr);
    win->onClose = [this]() { trackDialogWindow.reset(); trackDialogComp.reset(); };
    trackDialogWindow.reset(win);
}
