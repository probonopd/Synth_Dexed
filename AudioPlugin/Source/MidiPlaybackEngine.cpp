#include "MidiPlaybackEngine.h"

MidiPlaybackEngine::MidiPlaybackEngine()
    : juce::Thread("MidiPlaybackEngine")
{
}

MidiPlaybackEngine::~MidiPlaybackEngine()
{
    stop();
}

void MidiPlaybackEngine::loadMidiFile(const juce::MidiFile& midiFileIn)
{
    // Work on a mutable copy so we can convert timestamps to seconds
    juce::MidiFile midiFile = midiFileIn;
    midiFile.convertTimestampTicksToSeconds();

    std::unique_lock<std::mutex> lock(eventsMutex);
    events.clear();
    totalDuration = 0.0;

    for (int t = 0; t < midiFile.getNumTracks(); ++t)
    {
        const juce::MidiMessageSequence* seq = midiFile.getTrack(t);
        if (seq == nullptr)
            continue;

        for (int i = 0; i < seq->getNumEvents(); ++i)
        {
            const juce::MidiMessageSequence::MidiEventHolder* holder = seq->getEventPointer(i);
            if (holder == nullptr)
                continue;

            const juce::MidiMessage& msg = holder->message;
            double ts = msg.getTimeStamp();
            events.emplace_back(ts, msg);

            if (ts > totalDuration)
                totalDuration = ts;
        }
    }

    // Sort by timestamp
    std::sort(events.begin(), events.end(),
              [](const TimedMidiEvent& a, const TimedMidiEvent& b) {
                  return a.timestampSeconds < b.timestampSeconds;
              });

    nextEventIndex.store(0);
}

void MidiPlaybackEngine::play()
{
    nextEventIndex.store(0);
    playbackStartTime.store(juce::Time::getMillisecondCounterHiRes());
    playing.store(true);
    if (!isThreadRunning())
        startThread();
}

void MidiPlaybackEngine::stop()
{
    playing.store(false);
    stopThread(2000);
}

void MidiPlaybackEngine::fillMidiBuffer(juce::MidiBuffer& buffer, double sampleRate, int numSamples)
{
    if (!playing.load())
        return;

    double startTimeMs = playbackStartTime.load();
    double elapsed = (juce::Time::getMillisecondCounterHiRes() - startTimeMs) / 1000.0;
    double bufferDuration = (sampleRate > 0.0) ? (static_cast<double>(numSamples) / sampleRate) : 0.0;

    std::unique_lock<std::mutex> lock(eventsMutex);

    int idx = nextEventIndex.load();
    const int numEvents = static_cast<int>(events.size());

    while (idx < numEvents)
    {
        const TimedMidiEvent& ev = events[static_cast<size_t>(idx)];
        double evTime = ev.timestampSeconds;

        if (evTime < elapsed + bufferDuration)
        {
            // Only add events that are within or before this buffer window
            double offset = evTime - elapsed;
            int sampleOffset = static_cast<int>(offset * sampleRate);
            sampleOffset = juce::jlimit(0, numSamples - 1, sampleOffset);
            buffer.addEvent(ev.message, sampleOffset);
            ++idx;
        }
        else
        {
            break;
        }
    }

    nextEventIndex.store(idx);

    if (idx >= numEvents)
    {
        playing.store(false);
        if (onPlaybackFinished)
        {
            auto cb = onPlaybackFinished;
            juce::MessageManager::callAsync([cb]() { cb(); });
        }
    }
}

void MidiPlaybackEngine::run()
{
    // Playback is driven by fillMidiBuffer in processBlock; nothing to do here.
}
