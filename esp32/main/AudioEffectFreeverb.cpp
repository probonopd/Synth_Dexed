/*
 * Freeverb — Schroeder/Moorer reverb  (ESP32-S3 implementation)
 *
 * Based on the public-domain Freeverb algorithm by Jezar at Dreampoint.
 * License: Public Domain / Creative Commons Zero (CC0)
 *
 * Key optimisation: block-at-a-time processing.  Each comb/allpass filter
 * processes the entire sample block before the next filter runs.  This keeps
 * each ~5-7 KB delay buffer in the ESP32-S3 PSRAM data-cache (32 KB) and
 * avoids the catastrophic thrashing that per-sample interleaved access causes
 * when 12 buffers totalling ~55 KB compete for the same cache.
 */

#include "AudioEffectFreeverb.h"
#include <cstring>

// For taskYIELD() — constructor runs on core 0 alongside the USB daemon;
// yielding between PSRAM memsets lets the daemon handle USB re-enumeration.
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace FMRack {

// Static constexpr member definitions (required pre-C++17 inline).
constexpr int AudioEffectFreeverb::COMB_LENS[];
constexpr int AudioEffectFreeverb::ALLPASS_LENS[];

AudioEffectFreeverb::AudioEffectFreeverb(float *delayMem)
{
    float *p = delayMem;
    for (int i = 0; i < NUM_COMBS; ++i) {
        combs_[i].init(p, COMB_LENS[i]);
        p += COMB_LENS[i];
        taskYIELD();  // let USB daemon run between PSRAM memsets
    }
    for (int i = 0; i < NUM_ALLPASS; ++i) {
        allpasses_[i].init(p, ALLPASS_LENS[i]);
        p += ALLPASS_LENS[i];
        taskYIELD();
    }
    recalc();
}

void AudioEffectFreeverb::recalc()
{
    for (int i = 0; i < NUM_COMBS; ++i) {
        combs_[i].feedback = roomSize_;
        combs_[i].damp1    = damp_;
        combs_[i].damp2    = 1.0f - damp_;
    }
}

void AudioEffectFreeverb::mute()
{
    for (int i = 0; i < NUM_COMBS; ++i) {
        std::memset(combs_[i].buf, 0, combs_[i].len * sizeof(float));
        combs_[i].filterstore = 0.0f;
        combs_[i].idx = 0;
    }
    for (int i = 0; i < NUM_ALLPASS; ++i) {
        std::memset(allpasses_[i].buf, 0, allpasses_[i].len * sizeof(float));
        allpasses_[i].idx = 0;
    }
}

void AudioEffectFreeverb::process(float *data, int count)
{
    if (!enabled_ || count <= 0)
        return;  // bypass — data untouched

    const float wet  = wet_;
    const float dry  = dry_;
    const float gain = FIXED_GAIN;

    // Stack accumulator — max 256 samples = 1 KB (render task stack is 8 KB).
    float acc[256];
    while (count > 0) {
        const int chunk = (count > 256) ? 256 : count;

        // Prescale input and zero accumulator.
        for (int n = 0; n < chunk; ++n)
            acc[n] = 0.0f;

        // ---- Parallel comb filters (block-at-a-time) ----
        // Each comb's ~5-7 KB buffer stays in PSRAM cache for all 256 samples.
        for (int c = 0; c < NUM_COMBS; ++c) {
            for (int n = 0; n < chunk; ++n)
                acc[n] += combs_[c].process(data[n] * gain);
        }

        // ---- Series allpass filters (block-at-a-time) ----
        for (int a = 0; a < NUM_ALLPASS; ++a) {
            for (int n = 0; n < chunk; ++n)
                acc[n] = allpasses_[a].process(acc[n]);
        }

        // ---- Mix wet + dry ----
        for (int n = 0; n < chunk; ++n)
            data[n] = acc[n] * wet + data[n] * dry;

        data  += chunk;
        count -= chunk;
    }
}

} // namespace FMRack
