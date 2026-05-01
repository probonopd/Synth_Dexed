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
     * @brief Set which module to monitor for the oscilloscope display.
     * If moduleIndex is -1, monitor the final output instead.
     */
    void setMonitoredModuleIndex(int moduleIndex);
    int getMonitoredModuleIndex() const { return monitoredModuleIndex; }

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
     * @brief Handle mouse wheel for zoom.
     */
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

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
    
    // Module monitoring
    std::atomic<int> monitoredModuleIndex{-1};  // -1 means monitor final output

    // Display buffer (copied from ring buffer on UI thread)
    std::vector<float> displayBuffer;
    
    // Persistence buffers for phosphor effect
    std::vector<float> persistenceBuffer;
    static constexpr int kPersistenceFrames = 8;  // Number of frames for decay
    std::array<std::vector<float>, kPersistenceFrames> traceHistory;
    int currentTraceIndex = 0;
    
    // Trigger settings with hysteresis for complex waveforms
    float triggerLevel = 0.0f;  // Center trigger level
    float hysteresisHigh = 0.05f;  // Upper threshold
    float hysteresisLow = -0.05f;  // Lower threshold
    bool triggerArmed = true;  // Ready to trigger on next rising edge
    int findTriggerPoint(const std::vector<float>& buffer);
    
    // Automatic gain detection for adaptive triggering
    float detectSignalPeak(const std::vector<float>& buffer);
    
    // Controller reference for getting audio samples
    FMRackController* controller = nullptr;

    // Display settings - phosphor green CRT style
    float zoomLevel = 1.0f;
    float autoScaleGain = 1.0f;  // Auto-scaling gain based on signal amplitude
    juce::Colour phosphorColour = juce::Colour(0xff33ff66);  // Phosphor green
    juce::Colour phosphorGlowColour = juce::Colour(0xff00cc44);  // Darker glow
    juce::Colour backgroundColour = juce::Colour(0xff0a0f0a);  // Very dark green-tinted black
    juce::Colour gridColour = juce::Colour(0xff1a2a1a);  // Dark green grid
    
    // FFT spectrum data
    static constexpr int kFFTSize = 512;  // FFT size for frequency analysis
    std::vector<float> fftData;  // Frequency spectrum magnitude
    std::vector<float> fftBuffer;  // Temporary buffer for FFT computation
    void computeFFT();  // Compute frequency spectrum from display buffer

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OscilloscopeComponent)
};
