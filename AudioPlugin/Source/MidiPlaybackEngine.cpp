#include "MidiPlaybackEngine.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>

void MidiPlaybackEngine::loadMidiFile(const juce::MidiFile& midiFile) {
    std::lock_guard<std::mutex> lock(mutex);
    events.clear();
    playing = false;
    nextEventIndex = 0;

    auto file = midiFile;
    file.convertTimestampTicksToSeconds();

    for (int t = 0; t < file.getNumTracks(); ++t) {
        const auto* track = file.getTrack(t);
        for (int i = 0; i < track->getNumEvents(); ++i) {
            const auto& ev = track->getEventPointer(i)->message;
            if (!ev.isMetaEvent()) {
                events.push_back({ ev.getTimeStamp(), ev });
            }
        }
    }

    std::sort(events.begin(), events.end(),
        [](const TimedMidiEvent& a, const TimedMidiEvent& b) {
            return a.timestampSeconds < b.timestampSeconds;
        });
}

void MidiPlaybackEngine::play() {
    std::lock_guard<std::mutex> lock(mutex);
    nextEventIndex = 0;
    playing = true;
    playbackStartTimeMs = juce::Time::getMillisecondCounterHiRes();
}

void MidiPlaybackEngine::stop() {
    playing = false;
}

void MidiPlaybackEngine::fillMidiBuffer(juce::MidiBuffer& buffer, double sampleRate, int numSamples) {
    if (!playing.load()) return;

    std::lock_guard<std::mutex> lock(mutex);

    double nowMs = juce::Time::getMillisecondCounterHiRes();
    double elapsedSec = (nowMs - playbackStartTimeMs) / 1000.0;
    double bufferDurationSec = numSamples / sampleRate;
    double bufferEndSec = elapsedSec + bufferDurationSec;

    while (nextEventIndex < (int)events.size()) {
        const auto& ev = events[nextEventIndex];
        if (ev.timestampSeconds > bufferEndSec) break;

        double relSec = ev.timestampSeconds - elapsedSec;
        int sampleOffset = (int)std::max(0.0, relSec * sampleRate);
        sampleOffset = std::min(sampleOffset, numSamples - 1);

        buffer.addEvent(ev.message, sampleOffset);
        ++nextEventIndex;
    }

    if (nextEventIndex >= (int)events.size()) {
        playing = false;
        if (onPlaybackFinished) {
            auto cb = onPlaybackFinished;
            juce::MessageManager::callAsync([cb]() { cb(); });
        }
    }
}
