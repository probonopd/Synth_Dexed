#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <atomic>
#include <vector>
#include <mutex>
#include <functional>

struct TimedMidiEvent {
    double timestampSeconds;
    juce::MidiMessage message;
};

class MidiPlaybackEngine {
public:
    MidiPlaybackEngine() = default;
    ~MidiPlaybackEngine() = default;

    void loadMidiFile(const juce::MidiFile& midiFile);
    void play();
    void stop();
    bool isPlaying() const { return playing.load(); }

    // Call from processBlock on audio thread
    void fillMidiBuffer(juce::MidiBuffer& buffer, double sampleRate, int numSamples);

    std::function<void()> onPlaybackFinished;

private:
    std::vector<TimedMidiEvent> events;
    std::atomic<bool> playing { false };
    double playbackStartTimeMs { 0.0 };
    int nextEventIndex { 0 };
    std::mutex mutex;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiPlaybackEngine)
};
