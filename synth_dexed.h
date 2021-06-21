#include <Arduino.h>
#include <Audio.h>
#include "config.h"
#if defined(USE_OPEN_AUDIO_LIB)
#include "OpenAudio_ArduinoLibrary.h"
#endif

/*****************************************************
   CODE; orig_code/synth.h
 *****************************************************/
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

//#define SUPER_PRECISE

// This IS not be present on MSVC.
// See http://stackoverflow.com/questions/126279/c99-stdint-h-header-and-ms-visual-studio
#ifdef _MSC_VER
typedef __int32 int32_t;
typedef unsigned __int32 uint32_t;
typedef __int16 SInt16;
#endif

#define LG_N 6
#define _N_ (1 << LG_N)

#if defined(__APPLE__)
#include <libkern/OSAtomic.h>
#define SynthMemoryBarrier() OSMemoryBarrier()
#elif defined(__GNUC__)
#if (__GNUC__ > 4) || (__GNUC__ == 4 && __GNUC_MINOR__ >= 1)
#define SynthMemoryBarrier() __sync_synchronize()
#endif
#endif


// #undef SynthMemoryBarrier()

#ifndef SynthMemoryBarrier
// need to understand why this must be defined
// #warning Memory barrier is not enabled
#define SynthMemoryBarrier()
#endif

template<typename T>
inline static T min(const T& a, const T& b) {
  return a < b ? a : b;
}

template<typename T>
inline static T max(const T& a, const T& b) {
  return a > b ? a : b;
}

#define QER(n,b) ( ((float)n)/(1<<b) )

#define FRAC_NUM float
#define SIN_FUNC sinf
// #define SIN_FUNC arm_sin_f32  // very fast but not as accurate
#define COS_FUNC cosf
// #define COS_FUNC arm_cos_f32  // very fast but not as accurate
#define LOG_FUNC logf
#define EXP_FUNC expf
#define SQRT_FUNC sqrtf
// #define ARM_SQRT_FUNC arm_sqrt_f32 // fast but not as accurate


//=====================================================
/*****************************************************
   CODE; orig_code/aligned_buf.h
 *****************************************************/
/*
   Copyright 2013 Google Inc.

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

// A convenient wrapper for buffers with alignment constraints

// Note that if we were on C++11, we'd use aligned_storage or somesuch.

template<typename T, size_t size, size_t alignment = 16>
class AlignedBuf {
  public:
    T *get() {
      return (T *)((((intptr_t)storage_) + alignment - 1) & -alignment);
    }
  private:
    unsigned char storage_[size * sizeof(T) + alignment];
};


//=====================================================
/*****************************************************
   CODE; orig_code/sin.h
 *****************************************************/
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

class Sin {
  public:
    Sin();

    static void init();
    static int32_t lookup(int32_t phase);
    static int32_t compute(int32_t phase);

    // A more accurate sine, both input and output Q30
    static int32_t compute10(int32_t phase);
};

#define SIN_LG_N_SAMPLES 10
#define SIN_N_SAMPLES (1 << SIN_LG_N_SAMPLES)

#define SIN_INLINE

// Use twice as much RAM for the LUT but avoid a little computation
#define SIN_DELTA

#ifdef SIN_DELTA
extern int32_t sintab[SIN_N_SAMPLES << 1];
#else
extern int32_t sintab[SIN_N_SAMPLES + 1];
#endif

#ifdef SIN_INLINE
inline
int32_t Sin::lookup(int32_t phase) {
  const int SHIFT = 24 - SIN_LG_N_SAMPLES;
  int lowbits = phase & ((1 << SHIFT) - 1);
#ifdef SIN_DELTA
  int phase_int = (phase >> (SHIFT - 1)) & ((SIN_N_SAMPLES - 1) << 1);
  int dy = sintab[phase_int];
  int y0 = sintab[phase_int + 1];

  return y0 + (((int64_t)dy * (int64_t)lowbits) >> SHIFT);
#else
  int phase_int = (phase >> SHIFT) & (SIN_N_SAMPLES - 1);
  int y0 = sintab[phase_int];
  int y1 = sintab[phase_int + 1];

  return y0 + (((int64_t)(y1 - y0) * (int64_t)lowbits) >> SHIFT);
#endif
}
#endif

//=====================================================
/*****************************************************
   CODE; orig_code/exp2.h
 *****************************************************/
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

class Exp2 {
  public:
    Exp2();

    static void init();

    // Q24 in, Q24 out
    static int32_t lookup(int32_t x);
};

#define EXP2_LG_N_SAMPLES 10
#define EXP2_N_SAMPLES (1 << EXP2_LG_N_SAMPLES)

#define EXP2_INLINE

extern int32_t exp2tab[EXP2_N_SAMPLES << 1];

#ifdef EXP2_INLINE
inline
int32_t Exp2::lookup(int32_t x) {
  const int SHIFT = 24 - EXP2_LG_N_SAMPLES;
  int lowbits = x & ((1 << SHIFT) - 1);
  int x_int = (x >> (SHIFT - 1)) & ((EXP2_N_SAMPLES - 1) << 1);
  int dy = exp2tab[x_int];
  int y0 = exp2tab[x_int + 1];

  int y = y0 + (((int64_t)dy * (int64_t)lowbits) >> SHIFT);
  return y >> (6 - (x >> 24));
}
#endif

class Tanh {
  public:
    static void init();

    // Q24 in, Q24 out
    static int32_t lookup(int32_t x);
};

#define TANH_LG_N_SAMPLES 10
#define TANH_N_SAMPLES (1 << TANH_LG_N_SAMPLES)

extern int32_t tanhtab[TANH_N_SAMPLES << 1];

inline
int32_t Tanh::lookup(int32_t x) {
  int32_t signum = x >> 31;
  x ^= signum;
  if (x >= (4 << 24)) {
    if (x >= (17 << 23)) {
      return signum ^ (1 << 24);
    }
    int32_t sx = ((int64_t) - 48408812 * (int64_t)x) >> 24;
    return signum ^ ((1 << 24) - 2 * Exp2::lookup(sx));
  } else {
    const int SHIFT = 26 - TANH_LG_N_SAMPLES;
    int lowbits = x & ((1 << SHIFT) - 1);
    int x_int = (x >> (SHIFT - 1)) & ((TANH_N_SAMPLES - 1) << 1);
    int dy = tanhtab[x_int];
    int y0 = tanhtab[x_int + 1];
    int y = y0 + (((int64_t)dy * (int64_t)lowbits) >> SHIFT);
    return y ^ signum;
  }
}

//=====================================================
/*****************************************************
   CODE; orig_code/fast_log.h
 *****************************************************/
/* ----------------------------------------------------------------------
  https://community.arm.com/tools/f/discussions/4292/cmsis-dsp-new-functionality-proposal/22621#22621
  Fast approximation to the log2() function.  It uses a two step
  process.  First, it decomposes the floating-point number into
  a fractional component F and an exponent E.  The fraction component
  is used in a polynomial approximation and then the exponent added
  to the result.  A 3rd order polynomial is used and the result
  when computing db20() is accurate to 7.984884e-003 dB.
** ------------------------------------------------------------------- */

static float log2f_approx_coeff[4] = {1.23149591368684f, -4.11852516267426f, 6.02197014179219f, -3.13396450166353f};

static float log2f_approx(float X)
{
  float *C = &log2f_approx_coeff[0];
  float Y;
  float F;
  int E;

  // This is the approximation to log2()
  F = frexpf(fabsf(X), &E);

  //  Y = C[0]*F*F*F + C[1]*F*F + C[2]*F + C[3] + E;
  Y = *C++;
  Y *= F;
  Y += (*C++);
  Y *= F;
  Y += (*C++);
  Y *= F;
  Y += (*C++);
  Y += E;
  return (Y);
}

// https://codingforspeed.com/using-faster-exponential-approximation/
inline float expf_approx(float x) {
  x = 1.0f + x / 1024;
  x *= x; x *= x; x *= x; x *= x;
  x *= x; x *= x; x *= x; x *= x;
  x *= x; x *= x;
  return x;
}

inline float unitToDb(float unit) {
  return 6.02f * log2f_approx(unit);
}

inline float dbToUnit(float db) {
  return expf_approx(db * 2.302585092994046f * 0.05f);
}

//=====================================================
/*****************************************************
   CODE; orig_code/freqlut.h
 *****************************************************/
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

class Freqlut {
  public:
    static void init(FRAC_NUM sample_rate);
    static int32_t lookup(int32_t logfreq);
};

//=====================================================
/*****************************************************
   CODE; orig_code/lfo.h
 *****************************************************/
/*
   Copyright 2013 Google Inc.

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

// Low frequency oscillator, compatible with DX7

class Lfo {
  public:
    static void init(FRAC_NUM sample_rate);
    void reset(const uint8_t params[6]);

    // result is 0..1 in Q24
    int32_t getsample();

    // result is 0..1 in Q24
    int32_t getdelay();

    void keydown();
  private:
    static uint32_t unit_;

    uint32_t phase_;  // Q32
    uint32_t delta_;
    uint8_t waveform_;
    uint8_t randstate_;
    bool sync_;

    uint32_t delaystate_;
    uint32_t delayinc_;
    uint32_t delayinc2_;
};

//=====================================================
/*****************************************************
   CODE; orig_code/env.h
 *****************************************************/
/*
   Copyright 2017 Pascal Gauthier.
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

// DX7 envelope generation

#define ACCURATE_ENVELOPE

class Env {
  public:

    // The rates and levels arrays are calibrated to match the Dx7 parameters
    // (ie, value 0..99). The outlevel parameter is calibrated in microsteps
    // (ie units of approx .023 dB), with 99 * 32 = nominal full scale. The
    // rate_scaling parameter is in qRate units (ie 0..63).
    void init(const int rates[4], const int levels[4], int outlevel,
              int rate_scaling);

    void update(const int rates[4], const int levels[4], int outlevel,
                int rate_scaling);
    // Result is in Q24/doubling log format. Also, result is subsampled
    // for every N samples.
    // A couple more things need to happen for this to be used as a gain
    // value. First, the # of outputs scaling needs to be applied. Also,
    // modulation.
    // Then, of course, log to linear.
    int32_t getsample();

    void keydown(bool down);
    static int scaleoutlevel(int outlevel);
    void getPosition(char *step);

    static void init_sr(double sample_rate);
    void transfer(Env &src);

  private:

    // PG: This code is normalized to 44100, need to put a multiplier
    // if we are not using 44100.
    static uint32_t sr_multiplier;

    int rates_[4];
    int levels_[4];
    int outlevel_;
    int rate_scaling_;
    // Level is stored so that 2^24 is one doubling, ie 16 more bits than
    // the DX7 itself (fraction is stored in level rather than separate
    // counter)
    int32_t level_;
    int targetlevel_;
    bool rising_;
    int ix_;
    int inc_;
#ifdef ACCURATE_ENVELOPE
    int staticcount_;
#endif

    bool down_;

    void advance(int newix);
};


//=====================================================
/*****************************************************
   CODE; orig_code/pitchenv.h
 *****************************************************/
/*
   Copyright 2013 Google Inc.

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

// Computation of the DX7 pitch envelope

class PitchEnv {
  public:
    static void init(FRAC_NUM sample_rate);

    // The rates and levels arrays are calibrated to match the Dx7 parameters
    // (ie, value 0..99).
    void set(const int rates[4], const int levels[4]);

    // Result is in Q24/octave
    int32_t getsample();
    void keydown(bool down);
    void getPosition(char *step);
  private:
    static int unit_;
    int rates_[4];
    int levels_[4];
    int32_t level_;
    int targetlevel_;
    bool rising_;
    int ix_;
    int inc_;

    bool down_;

    void advance(int newix);
};

extern const uint8_t pitchenv_rate[];
extern const int8_t pitchenv_tab[];


//=====================================================
/*****************************************************
   CODE; orig_code/controllers.h
 *****************************************************/
/*
   Copyright 2013 Google Inc.

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

// State of MIDI controllers
const int kControllerPitch = 0;
const int kControllerPitchRange = 1;
const int kControllerPitchStep = 2;
const int kControllerPortamentoGlissando = 3;

class FmCore;

class FmMod {
  public:
    uint8_t range;
    bool pitch;
    bool amp;
    bool eg;
    uint8_t ctrl_mode;
    uint8_t _dummy_;

    FmMod()
    {
      range = 0;
      ctrl_mode = 0;
      pitch = false;
      amp = false;
      eg = false;
    }

    void setRange(uint8_t r)
    {
      range = r < 0 && r > 99 ? 0 : r;
    }

    void setTarget(uint8_t assign)
    {
      assign = assign < 0 && assign > 7 ? 0 : assign;
      pitch = assign & 1; // PITCH
      amp = assign & 2; // AMP
      eg = assign & 4; // EG
    }

    void setMode(uint8_t m)
    {
      ctrl_mode = m > MIDI_CONTROLLER_MODE_MAX ? 0 : m;
    }
};

class Controllers {
    void applyMod(int cc, FmMod &mod)
    {
      uint8_t total = 0;
      float range = mod.range / 100.0;

      switch (mod.ctrl_mode)
      {
        case 0:
          total = uint8_t(float(cc) * range); // LINEAR mode
          break;
        case 1:
          total = uint8_t(127.0 * range - (float(cc) * range)); // REVERSE mode
          break;
        case 2:
          total = uint8_t(range * float(cc) + (1.0 - range) * 127.0); // DIRECT BC mode by Thierry (opus.quatre)
          break;
      }

      if (mod.amp)
        amp_mod = max(amp_mod, total);

      if (mod.pitch)
        pitch_mod = max(pitch_mod, total);

      if (mod.eg)
        eg_mod = max(eg_mod, total);
    }

  public:
    int32_t values_[4];

    uint8_t amp_mod;
    uint8_t pitch_mod;
    uint8_t eg_mod;

    uint8_t aftertouch_cc;
    uint8_t breath_cc;
    uint8_t foot_cc;
    uint8_t modwheel_cc;
    bool portamento_enable_cc;
    int portamento_cc;
    bool portamento_gliss_cc;
    int masterTune;

    uint8_t opSwitch;

    FmMod wheel;
    FmMod foot;
    FmMod breath;
    FmMod at;

    Controllers() {
      amp_mod = 0;
      pitch_mod = 0;
      eg_mod = 0;
    }

    void refresh() {
      amp_mod = pitch_mod = eg_mod = 0;

      applyMod(modwheel_cc, wheel);
      applyMod(breath_cc, breath);
      applyMod(foot_cc, foot);
      applyMod(aftertouch_cc, at);

      if ( ! ((wheel.eg || foot.eg) || (breath.eg || at.eg)) )
        eg_mod = 127;
    }

    FmCore *core;
};


//=====================================================
/*****************************************************
   CODE; orig_code/PluginFx.h
 *****************************************************/
/**

   Copyright (c) 2013 Pascal Gauthier.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA

*/

class PluginFx {
    float s1, s2, s3, s4;
    float sampleRate;
    float sampleRateInv;
    float d, c;
    float R24;
    float rcor24, rcor24Inv;
    float bright;

    // 24 db multimode
    float mm;
    float mmt;
    int mmch;
    inline float NR24(float sample, float g, float lpc);

    // preprocess values taken the UI
    float rCutoff;
    float rReso;
    float rGain;

    // thread values; if these are different from the UI,
    // it needs to be recalculated.
    float pReso;
    float pCutoff;
    float pGain;

    // I am still keeping the 2pole w/multimode filter
    inline float NR(float sample, float g);
    bool bandPassSw;
    float rcor, rcorInv;
    int R;

    float dc_id;
    float dc_od;
    float dc_r;

  public:
    PluginFx();

    // this is set directly by the ui / parameter
    float Cutoff;
    float Reso;
    float Gain;

    void init(int sampleRate);
    void process(float *work, int sampleSize);
    float getGain(void);
};


//=====================================================
/*****************************************************
   CODE; orig_code/fm_op_kernel.h
 *****************************************************/
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

struct FmOpParams {
  int32_t level_in;      // value to be computed (from level to gain[0])
  int32_t gain_out;      // computed value (gain[1] to gain[0])
  int32_t freq;
  int32_t phase;
};

class FmOpKernel {
  public:
    // gain1 and gain2 represent linear step: gain for sample i is
    // gain1 + (1 + i) / 64 * (gain2 - gain1)

    // This is the basic FM operator. No feedback.
    static void compute(int32_t *output, const int32_t *input,
                        int32_t phase0, int32_t freq,
                        int32_t gain1, int32_t gain2, bool add);

    // This is a sine generator, no feedback.
    static void compute_pure(int32_t *output, int32_t phase0, int32_t freq,
                             int32_t gain1, int32_t gain2, bool add);

    // One op with feedback, no add.
    static void compute_fb(int32_t *output, int32_t phase0, int32_t freq,
                           int32_t gain1, int32_t gain2,
                           int32_t *fb_buf, int fb_gain, bool add);
};


//=====================================================
/*****************************************************
   CODE; orig_code/fm_core.h
 *****************************************************/
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

class FmOperatorInfo {
  public:
    int in;
    int out;
};

enum FmOperatorFlags {
  OUT_BUS_ONE = 1 << 0,
  OUT_BUS_TWO = 1 << 1,
  OUT_BUS_ADD = 1 << 2,
  IN_BUS_ONE = 1 << 4,
  IN_BUS_TWO = 1 << 5,
  FB_IN = 1 << 6,
  FB_OUT = 1 << 7
};

class FmAlgorithm {
  public:
    int ops[6];
};

class FmCore {
  public:
    virtual ~FmCore() {};
    static void dump();
    uint8_t get_carrier_operators(uint8_t algorithm);
    virtual void render(int32_t *output, FmOpParams *params, int algorithm, int32_t *fb_buf, int feedback_gain);
  protected:
    AlignedBuf<int32_t, _N_>buf_[2];
    const static FmAlgorithm algorithms[32];
};


//=====================================================
/*****************************************************
   CODE; orig_code/dx7note.h
 *****************************************************/
/*
   Copyright 2016-2017 Pascal Gauthier.
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

// This is the logic to put together a note from the MIDI description
// and run the low-level modules.

// It will continue to evolve a bit, as note-stealing logic, scaling,
// and real-time control of parameters live here.

#pragma once

struct VoiceStatus {
  uint32_t amp[6];
  char ampStep[6];
  char pitchStep;
};

class Dx7Note {
  public:
    Dx7Note();
    void init(const uint8_t patch[156], int midinote, int velocity, int srcnote, int porta, const Controllers *ctrls);

    // Note: this _adds_ to the buffer. Interesting question whether it's
    // worth it...
    void compute(int32_t *buf, int32_t lfo_val, int32_t lfo_delay, const Controllers *ctrls);

    void keyup();

    // TODO: some way of indicating end-of-note. Maybe should be a return
    // value from the compute method? (Having a count return from keyup
    // is also tempting, but if there's a dynamic parameter change after
    // keyup, that won't work.

    // PG:add the update
    void update(const uint8_t patch[156], int midinote, int velocity, int porta, const Controllers *ctrls);
    void peekVoiceStatus(VoiceStatus &status);
    void transferState(Dx7Note& src);
    void transferSignal(Dx7Note &src);
    void transferPortamento(Dx7Note &src);
    void oscSync();

  private:
    Env env_[6];
    FmOpParams params_[6];
    PitchEnv pitchenv_;
    int32_t basepitch_[6];
    int32_t fb_buf_[2];
    int32_t fb_shift_;
    int32_t ampmodsens_[6];
    int32_t opMode[6];

    int ampmoddepth_;
    int algorithm_;
    int pitchmoddepth_;
    int pitchmodsens_;

    int porta_rateindex_;
    int porta_gliss_;
    int32_t porta_curpitch_[6];
};

//=====================================================
/*****************************************************
   CODE; orig_code/dexed.h
 *****************************************************/
/*
   MicroDexed

   MicroDexed is a port of the Dexed sound engine
   (https://github.com/asb2m10/dexed) for the Teensy-3.5/3.6/4.x with audio shield.
   Dexed ist heavily based on https://github.com/google/music-synthesizer-for-android

   (c)2018-2021 H. Wirtz <wirtz@parasitstudio.de>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA

*/

struct ProcessorVoice {
  int16_t midi_note;
  uint8_t velocity;
  int16_t porta;
  bool keydown;
  bool sustained;
  bool live;
  uint32_t key_pressed_timer;
  Dx7Note *dx7_note;
};

enum DexedVoiceOPParameters {
  DEXED_OP_EG_R1,           // 0
  DEXED_OP_EG_R2,           // 1
  DEXED_OP_EG_R3,           // 2
  DEXED_OP_EG_R4,           // 3
  DEXED_OP_EG_L1,           // 4
  DEXED_OP_EG_L2,           // 5
  DEXED_OP_EG_L3,           // 6
  DEXED_OP_EG_L4,           // 7
  DEXED_OP_LEV_SCL_BRK_PT,  // 8
  DEXED_OP_SCL_LEFT_DEPTH,  // 9
  DEXED_OP_SCL_RGHT_DEPTH,  // 10
  DEXED_OP_SCL_LEFT_CURVE,  // 11
  DEXED_OP_SCL_RGHT_CURVE,  // 12
  DEXED_OP_OSC_RATE_SCALE,  // 13
  DEXED_OP_AMP_MOD_SENS,    // 14
  DEXED_OP_KEY_VEL_SENS,    // 15
  DEXED_OP_OUTPUT_LEV,      // 16
  DEXED_OP_OSC_MODE,        // 17
  DEXED_OP_FREQ_COARSE,     // 18
  DEXED_OP_FREQ_FINE,       // 19
  DEXED_OP_OSC_DETUNE       // 20
};

#define DEXED_VOICE_OFFSET 126
enum DexedVoiceParameters {
  DEXED_PITCH_EG_R1,        // 0
  DEXED_PITCH_EG_R2,        // 1
  DEXED_PITCH_EG_R3,        // 2
  DEXED_PITCH_EG_R4,        // 3
  DEXED_PITCH_EG_L1,        // 4
  DEXED_PITCH_EG_L2,        // 5
  DEXED_PITCH_EG_L3,        // 6
  DEXED_PITCH_EG_L4,        // 7
  DEXED_ALGORITHM,          // 8
  DEXED_FEEDBACK,           // 9
  DEXED_OSC_KEY_SYNC,       // 10
  DEXED_LFO_SPEED,          // 11
  DEXED_LFO_DELAY,          // 12
  DEXED_LFO_PITCH_MOD_DEP,  // 13
  DEXED_LFO_AMP_MOD_DEP,    // 14
  DEXED_LFO_SYNC,           // 15
  DEXED_LFO_WAVE,           // 16
  DEXED_LFO_PITCH_MOD_SENS, // 17
  DEXED_TRANSPOSE,          // 18
  DEXED_NAME                // 19
};

/* #define DEXED_GLOBAL_PARAMETER_OFFSET 155
  enum DexedGlobalParameters {
  DEXED_PITCHBEND_RANGE,    // 0
  DEXED_PITCHBEND_STEP,     // 1
  DEXED_MODWHEEL_RANGE,     // 2
  DEXED_MODWHEEL_ASSIGN,    // 3
  DEXED_FOOTCTRL_RANGE,     // 4
  DEXED_FOOTCTRL_ASSIGN,    // 5
  DEXED_BREATHCTRL_RANGE,   // 6
  DEXED_BREATHCTRL_ASSIGN,  // 7
  DEXED_AT_RANGE,           // 8
  DEXED_AT_ASSIGN,          // 9
  DEXED_MASTER_TUNE,        // 10
  DEXED_OP1_ENABLE,         // 11
  DEXED_OP2_ENABLE,         // 12
  DEXED_OP3_ENABLE,         // 13
  DEXED_OP4_ENABLE,         // 14
  DEXED_OP5_ENABLE,         // 15
  DEXED_OP6_ENABLE,         // 16
  DEXED_MAX_NOTES,          // 17
  DEXED_PORTAMENTO_MODE,    // 18
  DEXED_PORTAMENTO_GLISSANDO, // 19
  DEXED_PORTAMENTO_TIME,    // 20
  }; */

// GLOBALS

//==============================================================================

class Dexed
{
  public:
    Dexed(int rate);
    ~Dexed();
    void activate(void);
    void deactivate(void);
    bool isMonoMode(void);
    void setMonoMode(bool mode);
    void setRefreshMode(bool mode);
    //void getSamples(uint16_t n_samples, int16_t* buffer);
    void panic(void);
    void notesOff(void);
    void resetControllers(void);
    void setMaxNotes(uint8_t n);
    uint8_t getMaxNotes(void);
    void doRefreshVoice(void);
    void setOPs(uint8_t ops);
    bool decodeVoice(uint8_t* encoded_data, uint8_t* data);
    bool encodeVoice(uint8_t* encoded_data);
    bool getVoiceData(uint8_t* data_copy);
    bool loadVoiceParameters(uint8_t* data);
    bool loadGlobalParameters(uint8_t* data);
    bool initGlobalParameters(void);
    void keyup(int16_t pitch);
    void keydown(int16_t pitch, uint8_t velo);
    void setSustain(bool sustain);
    bool getSustain(void);
    uint8_t getNumNotesPlaying(void);
    void setPBController(uint8_t pb_range, uint8_t pb_step);
    void setMWController(uint8_t mw_range, uint8_t mw_assign, uint8_t mw_mode);
    void setFCController(uint8_t fc_range, uint8_t fc_assign, uint8_t fc_mode);
    void setBCController(uint8_t bc_range, uint8_t bc_assign, uint8_t bc_mode);
    void setATController(uint8_t at_range, uint8_t at_assign, uint8_t at_mode);
    void setPortamentoMode(uint8_t portamento_mode, uint8_t portamento_glissando, uint8_t portamento_time);

    ProcessorVoice voices[MAX_NOTES];
    Controllers controllers;
    PluginFx fx;

    uint8_t data[156] = {
      95, 29, 20, 50, 99, 95, 00, 00, 41, 00, 19, 00, 00, 03, 00, 06, 79, 00, 01, 00, 14, // OP6 eg_rate_1-4, level_1-4, kbd_lev_scl_brk_pt, kbd_lev_scl_lft_depth, kbd_lev_scl_rht_depth, kbd_lev_scl_lft_curve, kbd_lev_scl_rht_curve, kbd_rate_scaling, amp_mod_sensitivity, key_vel_sensitivity, operator_output_level, osc_mode, osc_freq_coarse, osc_freq_fine, osc_detune
      95, 20, 20, 50, 99, 95, 00, 00, 00, 00, 00, 00, 00, 03, 00, 00, 99, 00, 01, 00, 00, // OP5
      95, 29, 20, 50, 99, 95, 00, 00, 00, 00, 00, 00, 00, 03, 00, 06, 89, 00, 01, 00, 07, // OP4
      95, 20, 20, 50, 99, 95, 00, 00, 00, 00, 00, 00, 00, 03, 00, 02, 99, 00, 01, 00, 07, // OP3
      95, 50, 35, 78, 99, 75, 00, 00, 00, 00, 00, 00, 00, 03, 00, 07, 58, 00, 14, 00, 07, // OP2
      96, 25, 25, 67, 99, 75, 00, 00, 00, 00, 00, 00, 00, 03, 00, 02, 99, 00, 01, 00, 10, // OP1
      94, 67, 95, 60, 50, 50, 50, 50,                                                     // 4 * pitch EG rates, 4 * pitch EG level
      04, 06, 00,                                                                         // algorithm, feedback, osc sync
      34, 33, 00, 00, 00, 04,                                                             // lfo speed, lfo delay, lfo pitch_mod_depth, lfo_amp_mod_depth, lfo_sync, lfo_waveform
      03, 24,                                                                             // pitch_mod_sensitivity, transpose
      69, 68, 80, 56, 85, 76, 84, 00, 00, 00                                              // 10 * char for name ("DEFAULT   ")
    }; // FM-Piano

    int lastKeyDown;

  protected:
    static const uint8_t MAX_ACTIVE_NOTES = MAX_NOTES;
    uint8_t max_notes = MAX_ACTIVE_NOTES;
    int16_t currentNote;
    bool sustain;
    float vuSignal;
    bool monoMode;
    bool refreshMode;
    bool refreshVoice;
    uint8_t engineType;
    VoiceStatus voiceStatus;
    Lfo lfo;
    FmCore* engineMsfa;
    void getSamples(uint16_t n_samples, float32_t* buffer);
    void getSamples(uint16_t n_samples, int16_t* buffer);
};


//=====================================================
/*****************************************************
   CODE; orig_code/porta.h
 *****************************************************/
/*
   Copyright 2019 Jean Pierre Cimalando.

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

struct Porta {
  public:
    static void init_sr(double sampleRate);
    static int32_t rates[128];
};


//=====================================================
/*****************************************************
   CODE; orig_code/source_microdexed.h
 *****************************************************/

class AudioSynthDexed : public AudioStream, public Dexed {
  public:
    const uint16_t audio_block_time_us = 1000000 / (SAMPLE_RATE / AUDIO_BLOCK_SAMPLES);
    uint32_t xrun = 0;
    uint16_t render_time_max = 0;

    AudioSynthDexed(int sample_rate) : AudioStream(0, NULL), Dexed(sample_rate) { };

    void update(void)
    {
      if (in_update == true)
      {
        xrun++;
        return;
      }
      else
        in_update = true;

      elapsedMicros render_time;
      audio_block_t *lblock;

      lblock = allocate();

      if (!lblock)
      {
        in_update = false;
        return;
      }

      getSamples(AUDIO_BLOCK_SAMPLES, lblock->data);

      if (render_time > audio_block_time_us) // everything greater audio_block_time_us (2.9ms for buffer size of 128) is a buffer underrun!
        xrun++;

      if (render_time > render_time_max)
        render_time_max = render_time;

      transmit(lblock, 0);
      release(lblock);

      in_update = false;
    };

  private:
    volatile bool in_update = false;
};

class AudioSynthDexed_F32 : public AudioStream_F32, public Dexed {
  public:
    const uint16_t audio_block_time_us = 1000000 / (SAMPLE_RATE / AUDIO_BLOCK_SAMPLES);
    uint32_t xrun = 0;
    uint16_t render_time_max = 0;

    AudioSynthDexed_F32(int sample_rate) : AudioStream_F32(0, NULL), Dexed(sample_rate) { };

    void update(void)
    {
      if (in_update == true)
      {
        xrun++;
        return;
      }
      else
        in_update = true;

      elapsedMicros render_time;
      audio_block_f32_t *lblock;

      lblock = allocate_f32();

      if (!lblock)
      {
        in_update = false;
        return;
      }

      getSamples(AUDIO_BLOCK_SAMPLES, lblock->data);

      if (render_time > audio_block_time_us) // everything greater audio_block_time_us (2.9ms for buffer size of 128) is a buffer underrun!
        xrun++;

      if (render_time > render_time_max)
        render_time_max = render_time;

      AudioStream_F32::transmit(lblock, 0);
      AudioStream_F32::release(lblock);

      in_update = false;
    };

  private:
    volatile bool in_update = false;
};

//=====================================================
/*****************************************************
   CODE; orig_code/PluginFx.cpp
 *****************************************************/

//=====================================================
/*****************************************************
   CODE; orig_code/dexed.cpp
 *****************************************************/

//=====================================================
/*****************************************************
   CODE; orig_code/dx7note.cpp
 *****************************************************/

//=====================================================
/*****************************************************
   CODE; orig_code/env.cpp
 *****************************************************/

//=====================================================
/*****************************************************
   CODE; orig_code/exp2.cpp
 *****************************************************/

//=====================================================
/*****************************************************
   CODE; orig_code/fm_core.cpp
 *****************************************************/

//=====================================================
/*****************************************************
   CODE; orig_code/fm_op_kernel.cpp
 *****************************************************/

//=====================================================
/*****************************************************
   CODE; orig_code/freqlut.cpp
 *****************************************************/

//=====================================================
/*****************************************************
   CODE; orig_code/lfo.cpp
 *****************************************************/

//=====================================================
/*****************************************************
   CODE; orig_code/pitchenv.cpp
 *****************************************************/

//=====================================================
/*****************************************************
   CODE; orig_code/porta.cpp
 *****************************************************/

//=====================================================
/*****************************************************
   CODE; orig_code/sin.cpp
 *****************************************************/

//=====================================================