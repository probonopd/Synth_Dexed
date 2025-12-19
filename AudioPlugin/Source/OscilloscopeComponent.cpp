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
    persistenceBuffer.resize(kDisplaySamples, 0.0f);
    
    // Initialize trace history for phosphor persistence
    for (auto& trace : traceHistory)
    {
        trace.resize(kDisplaySamples, 0.0f);
    }
}

OscilloscopeComponent::~OscilloscopeComponent()
{
    stopTimer();
}

void OscilloscopeComponent::setController(FMRackController* ctrl)
{
    controller = ctrl;
}

void OscilloscopeComponent::setMonitoredModuleIndex(int moduleIndex)
{
    monitoredModuleIndex.store(moduleIndex, std::memory_order_relaxed);
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
    
    // Fill background with slight gradient for CRT depth effect
    juce::ColourGradient bgGradient(
        backgroundColour.brighter(0.1f), bounds.getCentreX(), bounds.getCentreY(),
        backgroundColour, bounds.getX(), bounds.getY(), true);
    g.setGradientFill(bgGradient);
    g.fillRoundedRectangle(bounds, 4.0f);
    
    // Draw subtle CRT screen edge (shadow)
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawRoundedRectangle(bounds.reduced(1.0f), 4.0f, 2.0f);
    
    // Draw border with slight phosphor tint
    g.setColour(gridColour.brighter(0.5f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);
    
    // Draw center line (zero crossing) with phosphor color
    float centerY = bounds.getCentreY();
    g.setColour(gridColour.brighter(0.3f));
    g.drawHorizontalLine(static_cast<int>(centerY), bounds.getX() + 4, bounds.getRight() - 4);
    
    // Draw grid lines (quarter divisions)
    float quarterHeight = bounds.getHeight() / 4.0f;
    g.setColour(gridColour);
    g.drawHorizontalLine(static_cast<int>(bounds.getY() + quarterHeight), bounds.getX() + 4, bounds.getRight() - 4);
    g.drawHorizontalLine(static_cast<int>(bounds.getBottom() - quarterHeight), bounds.getX() + 4, bounds.getRight() - 4);
    
    // Draw vertical divisions
    float quarterWidth = bounds.getWidth() / 4.0f;
    for (int i = 1; i < 4; ++i)
    {
        float x = bounds.getX() + quarterWidth * i;
        g.drawVerticalLine(static_cast<int>(x), bounds.getY() + 4, bounds.getBottom() - 4);
    }
    
    if (displayBuffer.empty() || bounds.getWidth() < 2)
        return;
    
    float xScale = bounds.getWidth() / static_cast<float>(displayBuffer.size());
    float yScale = bounds.getHeight() * 0.42f * zoomLevel;  // Leave margin
    
    // Draw persistence traces (older traces with decreasing opacity)
    for (int traceIdx = 0; traceIdx < kPersistenceFrames; ++traceIdx)
    {
        int historyIndex = (currentTraceIndex - traceIdx - 1 + kPersistenceFrames) % kPersistenceFrames;
        const auto& trace = traceHistory[historyIndex];
        
        if (trace.empty())
            continue;
        
        // Older traces are more faded
        float fadeAmount = static_cast<float>(traceIdx + 1) / static_cast<float>(kPersistenceFrames + 1);
        float alpha = 0.4f * (1.0f - fadeAmount);
        
        if (alpha < 0.05f)
            continue;
        
        juce::Path tracePath;
        bool pathStarted = false;
        
        for (size_t i = 0; i < trace.size(); ++i)
        {
            float x = bounds.getX() + static_cast<float>(i) * xScale;
            float y = centerY - trace[i] * yScale;
            y = juce::jlimit(bounds.getY() + 2, bounds.getBottom() - 2, y);
            
            if (!pathStarted)
            {
                tracePath.startNewSubPath(x, y);
                pathStarted = true;
            }
            else
            {
                tracePath.lineTo(x, y);
            }
        }
        
        g.setColour(phosphorGlowColour.withAlpha(alpha));
        g.strokePath(tracePath, juce::PathStrokeType(2.0f + fadeAmount * 2.0f));
    }
    
    // Build current waveform path
    juce::Path waveformPath;
    bool pathStarted = false;
    
    for (size_t i = 0; i < displayBuffer.size(); ++i)
    {
        float x = bounds.getX() + static_cast<float>(i) * xScale;
        float y = centerY - displayBuffer[i] * yScale;
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
    
    // Draw glow effect (wider, dimmer stroke behind main waveform)
    g.setColour(phosphorGlowColour.withAlpha(0.3f));
    g.strokePath(waveformPath, juce::PathStrokeType(4.0f));
    
    g.setColour(phosphorGlowColour.withAlpha(0.5f));
    g.strokePath(waveformPath, juce::PathStrokeType(2.5f));
    
    // Draw main waveform (bright phosphor)
    g.setColour(phosphorColour);
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
    
    // Find trigger point using hysteresis
    int triggerPoint = findTriggerPoint(tempBuffer);
    
    // Store current display buffer to trace history before updating
    traceHistory[currentTraceIndex] = displayBuffer;
    currentTraceIndex = (currentTraceIndex + 1) % kPersistenceFrames;
    
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

float OscilloscopeComponent::detectSignalPeak(const std::vector<float>& buffer)
{
    // Find the peak amplitude in the buffer for adaptive triggering
    float peak = 0.0f;
    for (size_t i = 0; i < buffer.size(); ++i)
    {
        peak = std::max(peak, std::abs(buffer[i]));
    }
    return peak;
}

int OscilloscopeComponent::findTriggerPoint(const std::vector<float>& buffer)
{
    // Hysteresis (Schmitt trigger) for stable triggering on complex waveforms
    // This works better for chords and triads by requiring the signal to
    // cross both a low and high threshold before triggering
    
    const int searchLimit = static_cast<int>(buffer.size()) - static_cast<int>(displayBuffer.size());
    
    if (searchLimit <= 0)
        return 0;
    
    // Detect signal peak and set adaptive thresholds
    float peak = detectSignalPeak(buffer);
    if (peak < 0.01f)
    {
        // No meaningful signal, just return start
        return 0;
    }
    
    // Set hysteresis thresholds based on signal amplitude
    // Use 10% and 20% of peak for low/high thresholds
    float adaptiveLow = -peak * 0.15f;
    float adaptiveHigh = peak * 0.15f;
    
    // State machine for hysteresis trigger:
    // 1. Wait for signal to go below low threshold (armed)
    // 2. Then look for rising edge crossing high threshold (trigger)
    
    bool armed = false;
    int bestTriggerPoint = 0;
    float bestTriggerSlope = 0.0f;
    
    // First pass: find all potential trigger points using hysteresis
    for (int i = 1; i < searchLimit; ++i)
    {
        float curr = buffer[i];
        float prev = buffer[i - 1];
        
        if (!armed)
        {
            // Wait for signal to drop below low threshold to arm
            if (curr < adaptiveLow)
            {
                armed = true;
            }
        }
        else
        {
            // Armed - look for rising edge crossing high threshold
            if (prev <= adaptiveHigh && curr > adaptiveHigh)
            {
                // Calculate slope (rate of change) at this crossing
                float slope = curr - prev;
                
                // Prefer steeper slopes (cleaner trigger points)
                if (slope > bestTriggerSlope)
                {
                    bestTriggerSlope = slope;
                    bestTriggerPoint = i;
                }
                
                // Disarm until signal goes low again
                armed = false;
                
                // If we found a good trigger with reasonable slope, use it
                if (bestTriggerSlope > peak * 0.05f)
                {
                    break;
                }
            }
        }
    }
    
    // If hysteresis didn't find anything, fall back to simple zero-crossing
    if (bestTriggerPoint == 0)
    {
        for (int i = 1; i < searchLimit; ++i)
        {
            float prev = buffer[i - 1];
            float curr = buffer[i];
            
            // Rising edge crossing zero
            if (prev <= 0.0f && curr > 0.0f)
            {
                // Check for meaningful amplitude after this point
                float maxAmp = 0.0f;
                for (int j = i; j < std::min(i + 100, static_cast<int>(buffer.size())); ++j)
                {
                    maxAmp = std::max(maxAmp, std::abs(buffer[j]));
                }
                
                if (maxAmp > 0.01f)
                {
                    return i;
                }
            }
        }
    }
    
    return bestTriggerPoint;
}
