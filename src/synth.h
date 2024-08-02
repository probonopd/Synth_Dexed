/*
   Copyright 2012 Google Inc.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#ifndef SYNTH_H
#define SYNTH_H

#include <stdint.h>

//#define SUPER_PRECISE

#define USE_FILTER
#define USE_COMPRESSOR

#define MIDI_CONTROLLER_MODE_MAX 2
#define TRANSPOSE_FIX 24
//#define VOICE_SILENCE_LEVEL 1100
#define MAX_MAKEUP_GAIN 10.0

#if defined(USE_COMPRESSOR)
#define LOG_TABLE_SIZE 1024
#endif

#define LG_N 6
#define _N_ (1 << LG_N)

/*template<typename T>
inline static T min(const T& a, const T& b) {
  return a < b ? a : b;
}

template<typename T>
inline static T max(const T& a, const T& b) {
  return a > b ? a : b;
}*/

#define FRAC_NUM float
#define SIN_FUNC sinf
// #define SIN_FUNC arm_sin_f32  // very fast but not as accurate
#define COS_FUNC cosf
// #define COS_FUNC arm_cos_f32  // very fast but not as accurate
#define LOG_FUNC logf
#define EXP_FUNC expf
#define SQRT_FUNC sqrtf
// #define ARM_SQRT_FUNC arm_sqrt_f32 // fast but not as accurate

#ifndef _MAPFLOAT
#define _MAPFLOAT
inline float mapfloat(float val, float in_min, float in_max, float out_min, float out_max) {
  return (val - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
#endif

#if defined(__circle__)

#include <circle/timer.h>

#ifndef M_PI
  #define M_PI 3.14159265358979323846
#endif

#define constrain(amt, low, high) ({ \
  __typeof__(amt) _amt = (amt); \
  __typeof__(low) _low = (low); \
  __typeof__(high) _high = (high); \
  (_amt < _low) ? _low : ((_amt > _high) ? _high : _amt); \
})

static inline uint32_t millis (void)
{
        return uint32_t(CTimer::Get ()->GetClockTicks () / (CLOCKHZ / 1000));
}

#endif
#endif
