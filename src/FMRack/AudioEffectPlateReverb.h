/*  Stereo plate reverb
 *
 *  Adapted from MiniDexed (Holger Wirtz <wirtz@parasitstudio.de>)
 *
 *  Author: Piotr Zapart
 *          www.hexefx.com
 *
 * Copyright (c) 2020 by Piotr Zapart
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/***
 * Algorithm based on plate reverbs developed for SpinSemi FV-1 DSP chip
 * 
 * Allpass + modulated delay line based lush plate reverb
 * 
 * Input parameters are float in range 0.0 to 1.0:
 * 
 * size - reverb time
 * hidamp - hi frequency loss in the reverb tail
 * lodamp - low frequency loss in the reverb tail
 * lowpass - output/master lowpass filter, useful for darkening the reverb sound 
 * diffusion - lower settings will make the reverb tail more "echoey", optimal value 0.65
 * 
 */

#pragma once
#include <cstdint>
#include <algorithm>

namespace FMRack {

class AudioEffectPlateReverb {
public:
    AudioEffectPlateReverb(float samplerate);
    void doReverb(const float* inblockL, const float* inblockR, float* rvbblockL, float* rvbblockR, uint16_t len);

    // Parameter setters (float 0.0-1.0)
    void setSize(float n) { size(n); }
    void setLevel(float n) { level(n); }
    void setEnabled(bool enabled) { set_bypass(!enabled); }
    void setHiDamp(float n) { hidamp(n); }
    void setLoDamp(float n) { lodamp(n); }
    void setLowpass(float n) { lowpass(n); }
    void setDiffusion(float n) { diffusion(n); }
    void process(const float* leftIn, const float* rightIn, float* leftOut, float* rightOut, int numSamples);

    // Original parameter methods
    void size(float n);
    void hidamp(float n);
    void lodamp(float n);
    void lowpass(float n);
    void diffusion(float n);
    void level(float n);

    float get_size() const { return rv_time_k; }
    bool get_bypass() const { return bypass; }
    void set_bypass(bool state) { bypass = state; }
    void tgl_bypass() { bypass = !bypass; }
    float get_level() const { return reverb_level; }

private:
    bool bypass = false;
    float reverb_level = 0.5f;
    float input_attn = 0.5f;

    float in_allp_k;
    float in_allp1_bufL[224] = {};
    float in_allp2_bufL[420] = {};
    float in_allp3_bufL[856] = {};
    float in_allp4_bufL[1089] = {};
    uint16_t in_allp1_idxL = 0, in_allp2_idxL = 0, in_allp3_idxL = 0, in_allp4_idxL = 0;
    float in_allp_out_L = 0.0f;
    float in_allp1_bufR[156] = {};
    float in_allp2_bufR[520] = {};
    float in_allp3_bufR[956] = {};
    float in_allp4_bufR[1289] = {};
    uint16_t in_allp1_idxR = 0, in_allp2_idxR = 0, in_allp3_idxR = 0, in_allp4_idxR = 0;
    float in_allp_out_R = 0.0f;
    float lp_allp1_buf[2303] = {};
    float lp_allp2_buf[2905] = {};
    float lp_allp3_buf[3175] = {};
    float lp_allp4_buf[2398] = {};
    uint16_t lp_allp1_idx = 0, lp_allp2_idx = 0, lp_allp3_idx = 0, lp_allp4_idx = 0;
    float loop_allp_k;
    float lp_allp_out = 0.0f;
    float lp_dly1_buf[3423] = {};
    float lp_dly2_buf[4589] = {};
    float lp_dly3_buf[4365] = {};
    float lp_dly4_buf[3698] = {};
    uint16_t lp_dly1_idx = 0, lp_dly2_idx = 0, lp_dly3_idx = 0, lp_dly4_idx = 0;
    const uint16_t lp_dly1_offset_L = 201, lp_dly2_offset_L = 145, lp_dly3_offset_L = 1897, lp_dly4_offset_L = 280;
    const uint16_t lp_dly1_offset_R = 1897, lp_dly2_offset_R = 1245, lp_dly3_offset_R = 487, lp_dly4_offset_R = 780;
    float lp_hidamp_k = 1.0f;
    float lp_lodamp_k = 0.0f;
    float lpf1 = 0.0f, lpf2 = 0.0f, lpf3 = 0.0f, lpf4 = 0.0f;
    float hpf1 = 0.0f, hpf2 = 0.0f, hpf3 = 0.0f, hpf4 = 0.0f;
    float lp_lowpass_f = 0.0f;
    float lp_hipass_f = 0.0f;
    float master_lowpass_f = 0.0f;
    float master_lowpass_l = 0.0f, master_lowpass_r = 0.0f;
    const float rv_time_k_max = 0.95f;
    float rv_time_k = 0.5f;
    float rv_time_scaler = 1.0f;
    uint32_t lfo1_phase_acc = 0, lfo1_adder = 0;
    uint32_t lfo2_phase_acc = 0, lfo2_adder = 0;
};

} // namespace FMRack