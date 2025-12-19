#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <array>

class FMRackController;

/**
 * @brief A simple oscilloscope component that displays audio waveforms.
 * 
 * This component uses a ring buffer to store audio samples and displays
 * them as a waveform. It uses a timer to periodically update the display.
 */
class OscilloscopeComponent : public juce::Component, private juce::Timer
{
public:
    OscilloscopeComponent();
    ~OscilloscopeComponent() override;

    /**
     * @brief Set the controller to get audio samples from.
     */
    void setController(FMRackController* controller);

    /**
     * @brief Push new audio samples into the ring buffer.
     * Call this from the audio thread with new samples.
     */
    void pushSamples(const float* samples, int numSamples);

    /**
     * @brief Paint the oscilloscope waveform.
     */
    void paint(juce::Graphics& g) override;

    /**
     * @brief Called when the component is resized.
     */
    void resized() override;

    /**
     * @brief Start the display timer.
     */
    void start();

    /**
     * @brief Stop the display timer.
     */
    void stop();

private:
    void timerCallback() override;

    // Ring buffer for audio samples - lock-free for audio thread safety
    static constexpr int kBufferSize = 4096;  // Larger buffer for trigger search
    static constexpr int kDisplaySamples = 1024;  // Number of samples to display
    std::array<std::atomic<float>, kBufferSize> ringBuffer;
    std::atomic<int> writeIndex{0};

    // Display buffer (copied from ring buffer on UI thread)
    std::vector<float> displayBuffer;
    
    // Trigger settings
    float triggerLevel = 0.0f;  // Trigger at zero crossing
    int findTriggerPoint(const std::vector<float>& buffer);
    
    // Controller reference for getting audio samples
    FMRackController* controller = nullptr;

    // Display settings
    float zoomLevel = 1.0f;
    juce::Colour waveformColour = juce::Colour(0xff00ff88);  // Bright green
    juce::Colour backgroundColour = juce::Colour(0xff1a1a1a);
    juce::Colour gridColour = juce::Colour(0xff333333);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OscilloscopeComponent)
};
