#include "OscilloscopeComponent.h"
#include "FMRackController.h"

OscilloscopeComponent::OscilloscopeComponent()
{
    // Initialize ring buffer to zero
    for (int i = 0; i < kBufferSize; ++i)
    {
        ringBuffer[i].store(0.0f, std::memory_order_relaxed);
    }
    
    displayBuffer.resize(kDisplaySamples, 0.0f);
}

OscilloscopeComponent::~OscilloscopeComponent()
{
    stopTimer();
}

void OscilloscopeComponent::setController(FMRackController* ctrl)
{
    controller = ctrl;
}

void OscilloscopeComponent::pushSamples(const float* samples, int numSamples)
{
    // Called from audio thread - use lock-free ring buffer
    for (int i = 0; i < numSamples; ++i)
    {
        int idx = writeIndex.load(std::memory_order_relaxed);
        ringBuffer[idx].store(samples[i], std::memory_order_relaxed);
        writeIndex.store((idx + 1) % kBufferSize, std::memory_order_release);
    }
}

void OscilloscopeComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    
    // Fill background
    g.setColour(backgroundColour);
    g.fillRoundedRectangle(bounds, 4.0f);
    
    // Draw border
    g.setColour(gridColour.brighter(0.3f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);
    
    // Draw center line (zero crossing)
    float centerY = bounds.getCentreY();
    g.setColour(gridColour);
    g.drawHorizontalLine(static_cast<int>(centerY), bounds.getX() + 2, bounds.getRight() - 2);
    
    // Draw grid lines (quarter divisions)
    float quarterHeight = bounds.getHeight() / 4.0f;
    g.setColour(gridColour.withAlpha(0.5f));
    g.drawHorizontalLine(static_cast<int>(bounds.getY() + quarterHeight), bounds.getX() + 2, bounds.getRight() - 2);
    g.drawHorizontalLine(static_cast<int>(bounds.getBottom() - quarterHeight), bounds.getX() + 2, bounds.getRight() - 2);
    
    // Draw waveform
    if (displayBuffer.empty() || bounds.getWidth() < 2)
        return;
    
    g.setColour(waveformColour);
    
    juce::Path waveformPath;
    float xScale = bounds.getWidth() / static_cast<float>(displayBuffer.size());
    float yScale = bounds.getHeight() * 0.45f * zoomLevel;  // Leave some margin
    
    bool pathStarted = false;
    
    for (size_t i = 0; i < displayBuffer.size(); ++i)
    {
        float x = bounds.getX() + static_cast<float>(i) * xScale;
        float y = centerY - displayBuffer[i] * yScale;
        
        // Clamp y to bounds
        y = juce::jlimit(bounds.getY() + 2, bounds.getBottom() - 2, y);
        
        if (!pathStarted)
        {
            waveformPath.startNewSubPath(x, y);
            pathStarted = true;
        }
        else
        {
            waveformPath.lineTo(x, y);
        }
    }
    
    g.strokePath(waveformPath, juce::PathStrokeType(1.5f));
    
    // Draw label
    g.setColour(juce::Colours::white.withAlpha(0.6f));
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.drawText("Oscilloscope", bounds.reduced(4), juce::Justification::topLeft);
}

void OscilloscopeComponent::resized()
{
    // Nothing special needed here
}

void OscilloscopeComponent::start()
{
    startTimerHz(30);  // 30 FPS refresh rate
}

void OscilloscopeComponent::stop()
{
    stopTimer();
}

void OscilloscopeComponent::timerCallback()
{
    // Copy all samples from ring buffer to a temporary buffer
    int currentWriteIndex = writeIndex.load(std::memory_order_acquire);
    std::vector<float> tempBuffer(kBufferSize);
    
    for (int i = 0; i < kBufferSize; ++i)
    {
        int readIndex = (currentWriteIndex + i) % kBufferSize;
        tempBuffer[i] = ringBuffer[readIndex].load(std::memory_order_relaxed);
    }
    
    // Find trigger point (rising edge zero crossing)
    int triggerPoint = findTriggerPoint(tempBuffer);
    
    // Copy samples starting from trigger point to display buffer
    for (size_t i = 0; i < displayBuffer.size(); ++i)
    {
        int srcIndex = triggerPoint + static_cast<int>(i);
        if (srcIndex < static_cast<int>(tempBuffer.size()))
            displayBuffer[i] = tempBuffer[srcIndex];
        else
            displayBuffer[i] = 0.0f;
    }
    
    repaint();
}

int OscilloscopeComponent::findTriggerPoint(const std::vector<float>& buffer)
{
    // Look for a rising edge zero crossing in the first half of the buffer
    // This leaves room to display samples after the trigger point
    const int searchLimit = static_cast<int>(buffer.size()) - static_cast<int>(displayBuffer.size());
    
    if (searchLimit <= 0)
        return 0;
    
    // Find a rising edge: previous sample <= triggerLevel and current sample > triggerLevel
    for (int i = 1; i < searchLimit; ++i)
    {
        float prev = buffer[i - 1];
        float curr = buffer[i];
        
        // Rising edge crossing the trigger level
        if (prev <= triggerLevel && curr > triggerLevel)
        {
            // Make sure there's some signal (not just noise near zero)
            // Check if there's meaningful amplitude in nearby samples
            float maxAmp = 0.0f;
            for (int j = i; j < std::min(i + 100, static_cast<int>(buffer.size())); ++j)
            {
                maxAmp = std::max(maxAmp, std::abs(buffer[j]));
            }
            
            // Only trigger if we have meaningful signal
            if (maxAmp > 0.01f)
            {
                return i;
            }
        }
    }
    
    // No trigger found, return start of buffer
    return 0;
}
