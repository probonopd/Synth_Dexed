/*
 * Freeverb — Schroeder/Moorer reverb
 *
 * Based on the public-domain Freeverb algorithm by Jezar at Dreampoint.
 * Original code placed in the public domain by the author.
 *
 * This is a clean-room re-implementation optimised for the ESP32-S3:
 *   - Mono-in / mono-out topology (feeds into the Symphonic stereo widener)
 *   - Delay-line memory is a single external allocation (PSRAM-friendly)
 *   - Block-at-a-time processing: each comb/allpass runs all N samples
 *     before moving to the next, so its ~5-7 KB buffer stays in the
 *     32 KB PSRAM data-cache the whole time  (eliminates thrashing)
 *   - Comb/allpass buffer sizes scaled from 44 100 → 48 000 Hz
 *
 * License: Public Domain / Creative Commons Zero (CC0)
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace FMRack {

class AudioEffectFreeverb {
public:
    /// @param delayMem  Externally-allocated float buffer of at least
    ///                  TOTAL_DELAY_FLOATS elements.  Ownership is NOT taken;
    ///                  the caller manages the lifetime.
    explicit AudioEffectFreeverb(float *delayMem);

    /// Minimum number of floats the external delay buffer must hold.
    static constexpr int TOTAL_DELAY_FLOATS =
        1214 + 1293 + 1390 + 1476 + 1548 + 1623 + 1695 + 1760 +
        605 + 480 + 371 + 245;   // = 13 700

    /// Process a block of mono float samples in-place.
    /// @param data   Mono float buffer (−1 … +1), modified in-place.
    /// @param count  Number of samples (≤ 256 per call for stack usage).
    void process(float *data, int count);

    // ---- Parameter setters (safe to call from any thread) ----
    void setEnabled(bool e)    { enabled_ = e; }
    void setRoomSize(float v)  { roomSize_ = 0.7f + clamp01(v) * 0.28f; recalc(); }
    void setDamping(float v)   { damp_     = clamp01(v) * 0.4f;          recalc(); }
    void setWet(float v)       { wet_      = clamp01(v) * 3.0f; }
    void setDry(float v)       { dry_      = clamp01(v) * 2.0f; }

    // ---- Getters ----
    bool  isEnabled()  const { return enabled_; }
    float getRoomSize() const { return (roomSize_ - 0.7f) / 0.28f; }
    float getDamping()  const { return damp_ / 0.4f; }
    float getWet()      const { return wet_ / 3.0f; }
    float getDry()      const { return dry_ / 2.0f; }

    /// Zero all delay-line state (e.g. on preset change).
    void mute();

private:
    static float clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }
    void recalc();

    // ---- Tuning constants (buffer sizes for 48 kHz) ----
    static constexpr int NUM_COMBS    = 8;
    static constexpr int NUM_ALLPASS  = 4;

    static constexpr int COMB_LENS[NUM_COMBS] = {
        1214, 1293, 1390, 1476, 1548, 1623, 1695, 1760
    };
    static constexpr int ALLPASS_LENS[NUM_ALLPASS] = {
        605, 480, 371, 245
    };

    // ---- Comb filter (pointer-based, low-pass feedback comb) ----
    struct Comb {
        float *buf;
        int    len;
        int    idx;
        float  filterstore;
        float  feedback;
        float  damp1;
        float  damp2;

        void init(float *mem, int length) {
            buf = mem;  len = length;  idx = 0;
            filterstore = 0.0f;
            feedback = 0.0f;  damp1 = 0.0f;  damp2 = 0.0f;
            std::memset(buf, 0, len * sizeof(float));
        }

        inline float process(float input) {
            float out = buf[idx];
            filterstore = out * damp2 + filterstore * damp1;
            buf[idx] = input + filterstore * feedback;
            if (++idx >= len) idx = 0;
            return out;
        }
    };

    // ---- Allpass filter (pointer-based) ----
    struct Allpass {
        float *buf;
        int    len;
        int    idx;
        static constexpr float FEEDBACK = 0.5f;

        void init(float *mem, int length) {
            buf = mem;  len = length;  idx = 0;
            std::memset(buf, 0, len * sizeof(float));
        }

        inline float process(float input) {
            float bufout = buf[idx];
            float out = -input + bufout;
            buf[idx] = input + bufout * FEEDBACK;
            if (++idx >= len) idx = 0;
            return out;
        }
    };

    // ---- State ----
    bool  enabled_  = true;
    float roomSize_ = 0.84f;
    float damp_     = 0.2f;
    float wet_      = 1.0f;
    float dry_      = 0.6f;
    static constexpr float FIXED_GAIN = 0.04f;  // 2.7× original; allows audible tail for short FM notes

    Comb    combs_[NUM_COMBS];
    Allpass allpasses_[NUM_ALLPASS];
};

} // namespace FMRack
