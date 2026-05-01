#include "MidiDropZone.h"

// Helper to change MIDI channel on a channel message
static juce::MidiMessage withChannel(const juce::MidiMessage& msg, int newChannel)
{
    const uint8_t* raw = msg.getRawData();
    int size = msg.getRawDataSize();
    if (size < 1) return msg;
    std::vector<uint8_t> newData(raw, raw + size);
    newData[0] = static_cast<uint8_t>((newData[0] & 0xF0u) | (static_cast<uint8_t>(newChannel - 1) & 0x0Fu));
    return juce::MidiMessage(newData.data(), size, msg.getTimeStamp());
}

MidiDropZone::MidiDropZone()
{
    setSize(800, 40);
}

MidiDropZone::~MidiDropZone()
{
    if (trackDialog)
        trackDialog->exitModalState(0);
}

void MidiDropZone::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(2.0f);

    if (isDragOver)
        g.fillAll(juce::Colour(0xff2a4a6a));
    else
        g.fillAll(juce::Colour(0xff222222));

    // Dashed border
    float dash[] = { 4.0f, 4.0f };
    juce::Path borderPath;
    borderPath.addRectangle(bounds);
    juce::PathStrokeType stroke(1.5f);
    stroke.createDashedStroke(borderPath, borderPath, dash, 2);

    g.setColour(isDragOver ? juce::Colours::lightblue : juce::Colour(0xff555555));
    g.strokePath(borderPath, juce::PathStrokeType(1.0f));

    g.setColour(isDragOver ? juce::Colours::white : juce::Colours::grey);
    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    g.drawText(isDragOver ? "Release to load" : "Drop .mid file here",
               getLocalBounds(), juce::Justification::centred);
}

void MidiDropZone::resized()
{
}

bool MidiDropZone::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        juce::String lower = f.toLowerCase();
        if (lower.endsWith(".mid") || lower.endsWith(".midi"))
            return true;
    }
    return false;
}

void MidiDropZone::fileDragEnter(const juce::StringArray&, int, int)
{
    isDragOver = true;
    repaint();
}

void MidiDropZone::fileDragExit(const juce::StringArray&)
{
    isDragOver = false;
    repaint();
}

void MidiDropZone::filesDropped(const juce::StringArray& files, int, int)
{
    isDragOver = false;
    repaint();

    for (const auto& f : files)
    {
        juce::String lower = f.toLowerCase();
        if (lower.endsWith(".mid") || lower.endsWith(".midi"))
        {
            processDroppedFile(juce::File(f));
            return;
        }
    }
}

void MidiDropZone::processDroppedFile(const juce::File& file)
{
    juce::FileInputStream fis(file);
    if (!fis.openedOk())
        return;

    juce::MidiFile midiFile;
    if (!midiFile.readFrom(fis))
        return;

    showTrackChannelDialog(midiFile, file);
}

void MidiDropZone::showTrackChannelDialog(const juce::MidiFile& midiFile,
                                           const juce::File& originalFile)
{
    auto* dlgComp = new TrackChannelDialog(midiFile);
    trackChannelComp.reset(dlgComp);

    juce::MidiFile capturedMidi = midiFile;
    juce::File capturedFile = originalFile;

    trackChannelComp->onDone = [this, capturedMidi, capturedFile](bool accepted) mutable
    {
        if (trackDialog)
            trackDialog->exitModalState(0);

        if (!accepted)
        {
            trackChannelComp.reset();
            trackDialog.reset();
            return;
        }

        auto assignments = trackChannelComp->getAssignments();
        trackChannelComp.reset();
        trackDialog.reset();

        // Build new MIDI file with channel assignments (tick-based)
        juce::MidiFile outFile;
        outFile.setTicksPerQuarterNote(capturedMidi.getTimeFormat());

        for (const auto& assignment : assignments)
        {
            if (assignment.assignedChannel == 0)
                continue;

            const juce::MidiMessageSequence* srcSeq = capturedMidi.getTrack(assignment.originalTrack);
            if (srcSeq == nullptr) continue;

            juce::MidiMessageSequence newSeq;
            int newChannel = assignment.assignedChannel;

            for (int i = 0; i < srcSeq->getNumEvents(); ++i)
            {
                const auto* holder = srcSeq->getEventPointer(i);
                if (!holder) continue;

                juce::MidiMessage msg = holder->message;

                if (msg.isNoteOn() || msg.isNoteOff() || msg.isController() ||
                    msg.isPitchWheel() || msg.isChannelPressure() || msg.isAftertouch() ||
                    msg.isProgramChange())
                {
                    msg = withChannel(msg, newChannel);
                }

                newSeq.addEvent(msg);
            }

            newSeq.updateMatchedPairs();
            outFile.addTrack(newSeq);
        }

        // We pass the processed MIDI to the callback as a playback-ready copy
        // Convert to seconds for playback
        juce::MemoryOutputStream mos;
        outFile.writeTo(mos);

        juce::MemoryInputStream mis(mos.getData(), mos.getDataSize(), false);
        juce::MidiFile playbackMidi;
        if (playbackMidi.readFrom(mis))
        {
            juce::String name = capturedFile.getFileName();
            if (onMidiReady)
                onMidiReady(playbackMidi, name);
        }
    };

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setNonOwned(trackChannelComp.get());
    opts.dialogTitle = "Assign MIDI Tracks to Channels";
    opts.dialogBackgroundColour = juce::Colours::darkgrey;
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = false;
    opts.resizable = false;
    trackDialog.reset(opts.launchAsync());
}
