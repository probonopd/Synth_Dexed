#include "StereoDexed.h"
#include <algorithm>
#include <cmath>

// Render stereo samples with keyboard range limiting and panning
// left/right: output buffers (must be at least n samples)
// midiNotes: array of MIDI note numbers for each sample (optional, nullptr if not used)
void StereoDexed::getSamplesStereo(int16_t* left, int16_t* right, int n, const uint8_t* midiNotes) {
    // Temporary mono buffer
    int16_t* mono = new int16_t[n];
    dexed->getSamples(mono, n);
    // Pan: -1.0 (left) to 1.0 (right)
    float panL = std::cos((stereoPosition * 0.5f + 0.5f) * M_PI / 2.0f);
    float panR = std::sin((stereoPosition * 0.5f + 0.5f) * M_PI / 2.0f);
    for (int i = 0; i < n; ++i) {
        bool inRange = true;
        if (midiNotes) {
            int note = midiNotes[i];
            inRange = (note >= noteLimitLow && note <= noteLimitHigh);
        }
        int sample = inRange ? mono[i] : 0;
        left[i] = static_cast<int16_t>(std::clamp(static_cast<int>(sample * panL), -32768, 32767));
        right[i] = static_cast<int16_t>(std::clamp(static_cast<int>(sample * panR), -32768, 32767));
    }
    delete[] mono;
}

// Backward-compatible version (no midiNotes, always in range)
void StereoDexed::getSamplesStereo(int16_t* left, int16_t* right, int n) {
    getSamplesStereo(left, right, n, nullptr);
}
