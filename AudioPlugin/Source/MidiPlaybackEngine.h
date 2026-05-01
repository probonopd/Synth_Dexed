#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <vector>
#include <atomic>
#include <mutex>
#include <functional>

struct TimedMidiEvent {
    double timestampSeconds;
    juce::MidiMessage message;
    TimedMidiEvent(double t, juce::MidiMessage m) : timestampSeconds(t), message(std::move(m)) {}
};

class MidiPlaybackEngine : public juce::Thread {
public:
    MidiPlaybackEngine();
    ~MidiPlaybackEngine() override;

    void loadMidiFile(const juce::MidiFile& midiFile);
    void play();
    void stop();
    bool isPlaying() const { return playing.load(); }
    void fillMidiBuffer(juce::MidiBuffer& buffer, double sampleRate, int numSamples);
    void run() override;
    std::function<void()> onPlaybackFinished;

private:
    std::vector<TimedMidiEvent> events;
    std::atomic<bool> playing { false };
    std::atomic<double> playbackStartTime { 0.0 };
    std::atomic<int> nextEventIndex { 0 };
    std::mutex eventsMutex;
    double totalDuration { 0.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiPlaybackEngine)
};
