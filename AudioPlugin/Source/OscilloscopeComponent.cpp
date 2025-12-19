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
    fftData.resize(kFFTSize / 2, 0.0f);
    fftBuffer.resize(kFFTSize, 0.0f);
    
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
    
    // ========== Oscilloscope section: 70% of height ==========
    float oscHeight = bounds.getHeight() * 0.7f;
    juce::Rectangle<float> oscBounds(bounds.getX(), bounds.getY(), bounds.getWidth(), oscHeight);
    float oscCenterY = oscBounds.getCentreY();  // Center of oscilloscope area only
    
    // Calculate peak amplitude for auto-scaling
    float displayPeak = 0.0f;
    for (size_t i = 0; i < displayBuffer.size(); ++i)
    {
        displayPeak = std::max(displayPeak, std::abs(displayBuffer[i]));
    }
    
    // Auto-scale to use ~90% of available vertical space
    // If peak is very small, use a sensible minimum scaling
    float targetPeak = 0.9f;  // Target peak should use 90% of available space
    if (displayPeak > 0.01f)
    {
        autoScaleGain = targetPeak / displayPeak;
    }
    else
    {
        autoScaleGain = 1.0f;  // No signal, use default scaling
    }
    
    float xScale = oscBounds.getWidth() / static_cast<float>(displayBuffer.size());
    float baseYScale = oscBounds.getHeight() * 0.45f;  // Base scale (leave margin)
    float yScale = baseYScale * autoScaleGain;  // Apply auto-scaling gain
    float xScaleZoomed = xScale * zoomLevel;  // Apply zoom to X-axis (time/horizontal)
    
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
            float x = oscBounds.getX() + static_cast<float>(i) * xScaleZoomed;
            float y = oscCenterY - trace[i] * yScale;
            y = juce::jlimit(oscBounds.getY() + 2, oscBounds.getBottom() - 2, y);
            
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
        float x = oscBounds.getX() + static_cast<float>(i) * xScaleZoomed;
        float y = oscCenterY - displayBuffer[i] * yScale;
        y = juce::jlimit(oscBounds.getY() + 2, oscBounds.getBottom() - 2, y);
        
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
    g.drawText("Oscilloscope", oscBounds.reduced(4), juce::Justification::topLeft);
    
    // ========== Draw Spectrum / FFT display below oscilloscope ==========
    float specHeight = bounds.getHeight() * 0.28f;  // Use 28% for spectrum (smaller to save space)
    float specY = oscBounds.getBottom() + 2;
    juce::Rectangle<float> specBounds(bounds.getX(), specY, bounds.getWidth(), specHeight);
    juce::ColourGradient specGradient(
        backgroundColour.brighter(0.05f), specBounds.getCentreX(), specBounds.getCentreY(),
        backgroundColour, specBounds.getX(), specBounds.getY(), true);
    g.setGradientFill(specGradient);
    g.fillRoundedRectangle(specBounds, 4.0f);
    
    // Draw spectrum border
    g.setColour(gridColour.brighter(0.3f));
    g.drawRoundedRectangle(specBounds.reduced(0.5f), 4.0f, 1.0f);
    
    // Draw frequency spectrum bars with fixed range (20Hz - 8kHz)
    if (!fftData.empty())
    {
        const float sampleRate = 44100.0f;
        const float nyquistFreq = sampleRate / 2.0f;  // 22050 Hz
        const float minFreq = 20.0f;   // 20 Hz (human hearing lower limit)
        const float maxFreq = 8000.0f; // 8 kHz
        
        float specBaselineY = specBounds.getBottom() - 18.0f;  // Leave room for axis labels
        
        // Find peaks for labeling (find all local maxima above threshold)
        std::vector<std::pair<int, float>> peaks;  // (bin, dB value)
        float peakThreshold = -30.0f;  // Only label peaks above -30dB
        
        for (size_t i = 1; i < fftData.size() - 1; ++i)
        {
            // Local maximum check
            if (fftData[i] > fftData[i-1] && fftData[i] > fftData[i+1] && fftData[i] > peakThreshold)
            {
                peaks.push_back({static_cast<int>(i), fftData[i]});
            }
        }
        
        // Sort peaks by dB value (descending) and keep top 3
        std::sort(peaks.begin(), peaks.end(), 
            [](const auto& a, const auto& b) { return a.second > b.second; });
        if (peaks.size() > 3)
            peaks.resize(3);
        
        // Draw spectrum bars 
        for (size_t i = 0; i < fftData.size(); ++i)
        {
            // Calculate frequency for this bin (linear)
            float binFreq = (static_cast<float>(i) / static_cast<float>(fftData.size())) * nyquistFreq;
            
            // Convert dB value to bar height (-80 dB to 0 dB range)
            float dBValue = juce::jlimit(-80.0f, 0.0f, fftData[i]);
            float normalizedHeight = (dBValue + 80.0f) / 80.0f;  // 0 to 1
            float barHeight = normalizedHeight * (specHeight * 0.7f);  // Leave more room for labels
            
            // Linear frequency to pixel mapping (20Hz-8kHz range)
            float freqNormalized = (binFreq - minFreq) / (maxFreq - minFreq);
            freqNormalized = juce::jlimit(0.0f, 1.0f, freqNormalized);
            
            float x = specBounds.getX() + freqNormalized * specBounds.getWidth();
            float y = specBaselineY - barHeight;
            
            // Fixed bar width of 2 pixels
            const float barWidth = 2.0f;
            
            // Color based on frequency (lower = red, mid = green, high = blue)
            float hue = freqNormalized * 0.3f;
            juce::Colour barColour = juce::Colour::fromHSV(hue, 0.8f, 0.9f, 0.8f);
            
            g.setColour(barColour);
            g.fillRect(x, y, barWidth, barHeight);
        }
        
        // Label multiple peaks (always show labels for peaks, even small ones)
        for (const auto& peak : peaks)
        {
            int binIdx = peak.first;
            float binFreq = (static_cast<float>(binIdx) / static_cast<float>(fftData.size())) * nyquistFreq;
            float dBValue = juce::jlimit(-80.0f, 0.0f, fftData[binIdx]);
            float normalizedHeight = (dBValue + 80.0f) / 80.0f;
            float barHeight = normalizedHeight * (specHeight * 0.7f);
            
            float freqNormalized = (binFreq - minFreq) / (maxFreq - minFreq);
            freqNormalized = juce::jlimit(0.0f, 1.0f, freqNormalized);
            float x = specBounds.getX() + freqNormalized * specBounds.getWidth();
            float y = specBaselineY - barHeight;
            
            // Always show peak frequency labels, regardless of bar height
            juce::String freqText = juce::String(static_cast<int>(binFreq)) + "Hz";
            float textY = y - 14.0f;
            
            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(juce::FontOptions(10.0f)));
            g.drawText(freqText, 
                juce::Rectangle<float>(x - 25, textY, 50, 12),
                juce::Justification::centred);
        }
    }
    
    // Draw frequency axis labels (20Hz, 500Hz, 1kHz, 2kHz, 4kHz, 8kHz) at bottom
    {
        const float minFreq = 20.0f;
        const float maxFreq = 8000.0f;
        float axisY = specBounds.getBottom() - 15.0f;
        
        std::vector<float> axisFreqs = {20.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f};
        g.setColour(juce::Colours::white.withAlpha(0.6f));
        g.setFont(juce::Font(juce::FontOptions(9.0f)));
        
        for (float freq : axisFreqs)
        {
            float freqNormalized = (freq - minFreq) / (maxFreq - minFreq);
            freqNormalized = juce::jlimit(0.0f, 1.0f, freqNormalized);
            float x = specBounds.getX() + freqNormalized * specBounds.getWidth();
            
            // Draw small tick mark
            g.drawVerticalLine(static_cast<int>(x), 
                static_cast<int>(specBounds.getBottom() - 16), 
                static_cast<int>(specBounds.getBottom() - 12));
            
            // Draw frequency label
            juce::String label = freq >= 1000.0f ? 
                juce::String(freq / 1000.0f, 1) + "k" : 
                juce::String(static_cast<int>(freq));
            
            g.drawText(label,
                juce::Rectangle<float>(x - 18, axisY, 36, 12),
                juce::Justification::centred);
        }
    }
    
    // Draw spectrum label
    g.setColour(juce::Colours::white.withAlpha(0.6f));
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.drawText("Spectrum", specBounds.reduced(4), juce::Justification::topLeft);
}

void OscilloscopeComponent::resized()
{
    // Nothing special needed here
}

void OscilloscopeComponent::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    (void)event;  // Unused parameter
    
    // Zoom horizontally (X-axis / time) with scroll wheel
    // Positive delta = scroll up = zoom in (more samples visible = smaller zoom level)
    // Negative delta = scroll down = zoom out (fewer samples visible = larger zoom level)
    float zoomFactor = wheel.deltaY > 0 ? 0.9f : 1.1f;
    
    zoomLevel *= zoomFactor;
    
    // Calculate dynamic bounds based on display data
    float xScale = getLocalBounds().getWidth() / static_cast<float>(displayBuffer.size());
    
    // Maximum zoom out: all samples fit exactly in available width with no gaps
    // Total width needed = displayBuffer.size() * xScale * zoomLevel <= available width
    // So: zoomLevel <= available width / (displayBuffer.size() * xScale)
    float maxZoomOut = 1.0f;  // At minimum, don't zoom out beyond 1.0 (native resolution)
    if (displayBuffer.size() > 0 && xScale > 0.0f)
    {
        maxZoomOut = getLocalBounds().getWidth() / (displayBuffer.size() * xScale);
    }
    
    // Clamp zoom level to reasonable bounds
    // 0.1 = 10x zoom in (very detailed view), maxZoomOut = fit all data in window
    zoomLevel = juce::jlimit(0.1f, maxZoomOut, zoomLevel);
    
    repaint();
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
    
    // Compute FFT for frequency spectrum
    computeFFT();
    
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

void OscilloscopeComponent::computeFFT()
{
    // FFT computation using DFT with proper normalization
    // Copy display buffer to FFT buffer, remove DC offset, and apply window
    std::fill(fftBuffer.begin(), fftBuffer.end(), 0.0f);
    
    int fftSize = static_cast<int>(fftBuffer.size());
    int displaySize = static_cast<int>(displayBuffer.size());
    int copySize = std::min(fftSize, displaySize);
    
    // Step 1: Remove DC offset (mean)
    float dcOffset = 0.0f;
    for (int i = 0; i < copySize; ++i)
    {
        dcOffset += displayBuffer[i];
    }
    dcOffset /= static_cast<float>(copySize);
    
    // Step 2: Apply Hann window while copying (removes DC offset)
    for (int i = 0; i < copySize; ++i)
    {
        float window = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * i / (copySize - 1)));
        fftBuffer[i] = (displayBuffer[i] - dcOffset) * window;
    }
    
    // Step 3: Calculate magnitude at each frequency bin using DFT
    int numBins = static_cast<int>(fftData.size());
    
    for (int bin = 0; bin < numBins; ++bin)
    {
        float realPart = 0.0f;
        float imagPart = 0.0f;
        
        // DFT: sum of samples * exp(-2πijk/N) for each sample
        for (int k = 0; k < fftSize; ++k)
        {
            float angle = -2.0f * juce::MathConstants<float>::pi * bin * k / fftSize;
            realPart += fftBuffer[k] * std::cos(angle);
            imagPart += fftBuffer[k] * std::sin(angle);
        }
        
        // Magnitude: normalize by FFT size
        float magnitude = std::sqrt(realPart * realPart + imagPart * imagPart) / static_cast<float>(fftSize);
        
        // For real signals, the positive frequency content is doubled (except DC and Nyquist)
        if (bin > 0 && bin < numBins - 1)
            magnitude *= 2.0f;
        
        // Convert to dB scale (log)
        if (magnitude > 1e-6f)
            fftData[bin] = 20.0f * std::log10(magnitude);  // 20*log for amplitude (not power)
        else
            fftData[bin] = -80.0f;  // Minimum dB value
    }
}
