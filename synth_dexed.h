//This file was created with Single-C-File
//Single-C-File was developed by Adrian Dawid.
/*
   Copyright 2014 Pascal Gauthier.
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
#ifndef ENGINEMKI_H_INCLUDED
#define ENGINEMKI_H_INCLUDED
#include <Arduino.h>
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
#ifndef __SYNTH_H
#define __SYNTH_H
#include <Audio.h>
#include <stdint.h>
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
#ifndef SynthMemoryBarrier
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
#define COS_FUNC cosf
#define LOG_FUNC logf
#define EXP_FUNC expf
#define SQRT_FUNC sqrtf
#endif  // __SYNTH_H
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
#ifndef __ALIGNED_BUF_H
#define __ALIGNED_BUF_H
#include<stddef.h>
template<typename T, size_t size, size_t alignment = 16>
class AlignedBuf {
  public:
    T *get() {
      return (T *)((((intptr_t)storage_) + alignment - 1) & -alignment);
    }
  private:
    unsigned char storage_[size * sizeof(T) + alignment];
};
#endif  // __ALIGNED_BUF_H
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
#ifndef __FM_OP_KERNEL_H
#define __FM_OP_KERNEL_H
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
#endif
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
#ifndef __CONTROLLERS_H
#define __CONTROLLERS_H
#include <stdio.h>
#include <string.h>
#include <math.h>
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
#ifndef CONFIG_H_INCLUDED
#define CONFIG_H_INCLUDED
/*************************************************
   MIDI note values
 *************************************************/
#ifndef _MIDINOTES_H
#define _MIDINOTES_H
#define MIDI_A0   21
#define MIDI_AIS0 22
#define MIDI_B0   23
#define MIDI_C1   24
#define MIDI_CIS1 25
#define MIDI_D1   26
#define MIDI_DIS1 27
#define MIDI_E1   28
#define MIDI_F1   29
#define MIDI_FIS1 30
#define MIDI_G1   31
#define MIDI_GIS1 32
#define MIDI_A1   33
#define MIDI_AIS1 34
#define MIDI_B1   35
#define MIDI_C2   36
#define MIDI_CIS2 37
#define MIDI_D2   38
#define MIDI_DIS2 39
#define MIDI_E2   40
#define MIDI_F2   41
#define MIDI_FIS2 42
#define MIDI_G2   43
#define MIDI_GIS2 44
#define MIDI_A2   45
#define MIDI_AIS2 46
#define MIDI_B2   47
#define MIDI_C3   48
#define MIDI_CIS3 49
#define MIDI_D3   50
#define MIDI_DIS3 51
#define MIDI_E3   52
#define MIDI_F3   53
#define MIDI_FIS3 54
#define MIDI_G3   55
#define MIDI_GIS3 56
#define MIDI_A3   57
#define MIDI_AIS3 58
#define MIDI_B3   59
#define MIDI_C4   60
#define MIDI_CIS4 61
#define MIDI_D4   62
#define MIDI_DIS4 63
#define MIDI_E4   64
#define MIDI_F4   65
#define MIDI_FIS4 66
#define MIDI_G4   67
#define MIDI_GIS4 68
#define MIDI_A4   69
#define MIDI_AIS4 70
#define MIDI_B4   71
#define MIDI_C5   72
#define MIDI_CIS5 73
#define MIDI_D5   74
#define MIDI_DIS5 75
#define MIDI_E5   76
#define MIDI_F5   77
#define MIDI_FIS5 78
#define MIDI_G5   79
#define MIDI_GIS5 80
#define MIDI_A5   81
#define MIDI_AIS5 82
#define MIDI_B5   83
#define MIDI_C6   84
#define MIDI_CIS6 85
#define MIDI_D6   86
#define MIDI_DIS6 87
#define MIDI_E6   88
#define MIDI_F6   89
#define MIDI_FIS6 90
#define MIDI_G6   91
#define MIDI_GIS6 92
#define MIDI_A6   93
#define MIDI_AIS6 94
#define MIDI_B6   95
#define MIDI_C7   96
#define MIDI_CIS7 97
#define MIDI_D7   98
#define MIDI_DIS7 99
#define MIDI_E7   100
#define MIDI_F7   101
#define MIDI_FIS7 102
#define MIDI_G7   103
#define MIDI_GIS7 104
#define MIDI_A7   105
#define MIDI_AIS7 106
#define MIDI_B7   107
#define MIDI_C8   108
#endif
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
#ifndef TEENSY_BOARD_DETECTION_H_INCLUDED
#define TEENSY_BOARD_DETECTION_H_INCLUDED
#if defined(__IMXRT1062__) || defined (ARDUINO_TEENSY40) || defined (ARDUINO_TEENSY41)
#define TEENSY4
#if defined (ARDUINO_TEENSY40)
#define TEENSY4_0
#elif defined (ARDUINO_TEENSY41)
#define TEENSY4_1
#endif
#endif
#if defined(__MK66FX1M0__)
#  define TEENSY3_6
#endif
#if defined (__MK64FX512__)
#define TEENSY3_5
#endif
#endif
#define VERSION "1.0.15"
#define MIDI_DEVICE_DIN Serial1
#define MIDI_DEVICE_USB 1
#define MIDI_DEVICE_USB_HOST 1
#define AUDIO_DEVICE_USB
#define TEENSY_AUDIO_BOARD
#define DEFAULT_MIDI_CHANNEL MIDI_CHANNEL_OMNI
#define SYSEXBANK_DEFAULT 0
#define SYSEXSOUND_DEFAULT 0
#define SERIAL_SPEED 230400
#define SHOW_XRUN 1
#define SHOW_CPU_LOAD_MSEC 5000
#define DEXED_ENGINE DEXED_ENGINE_MODERN // DEXED_ENGINE_MARKI // DEXED_ENGINE_OPL
#define NUM_DEXED 2 // 1 or 2 - nothing else!
#define USE_FX 1
#define MOD_DELAY_SAMPLE_BUFFER int32_t(TIME_MS2SAMPLES(20.0)) // 20.0 ms delay buffer. 
#define MOD_WAVEFORM WAVEFORM_TRIANGLE // WAVEFORM_SINE WAVEFORM_TRIANGLE WAVEFORM_SAWTOOTH WAVEFORM_SAWTOOTH_REVERSE
#define MOD_FILTER_OUTPUT MOD_BUTTERWORTH_FILTER_OUTPUT // MOD_LINKWITZ_RILEY_FILTER_OUTPUT MOD_BUTTERWORTH_FILTER_OUTPUT MOD_NO_FILTER_OUTPUT
#define MOD_FILTER_CUTOFF_HZ 2000
#ifdef TEENSY_AUDIO_BOARD
#define SGTL5000_AUDIO_ENHANCE 1
#define SGTL5000_HEADPHONE_VOLUME 0.8
#endif
#if defined(TEENSY4)
#define USE_PLATEREVERB 1
#endif
#define SAMPLE_RATE 44100
#ifndef TEENSY_AUDIO_BOARD
#if AUDIO_BLOCK_SAMPLES == 64
#define AUDIO_MEM 512
#else
#define AUDIO_MEM 384
#endif
#else // IF TEENSY_AUDIO_BOARD
/*
  13: 3.16 Volts p-p
  14: 2.98 Volts p-p
  15: 2.83 Volts p-p
  16: 2.67 Volts p-p
  17: 2.53 Volts p-p
  18: 2.39 Volts p-p
  19: 2.26 Volts p-p
  20: 2.14 Volts p-p
  21: 2.02 Volts p-p
  22: 1.91 Volts p-p
  23: 1.80 Volts p-p
  24: 1.71 Volts p-p
  25: 1.62 Volts p-p
  26: 1.53 Volts p-p
  27: 1.44 Volts p-p
  28: 1.37 Volts p-p
  29: 1.29 Volts p-p  (default)
  30: 1.22 Volts p-p
  31: 1.16 Volts p-p
*/
#define SGTL5000_LINEOUT_LEVEL 29
#endif
#if AUDIO_BLOCK_SAMPLES == 64
#define AUDIO_MEM 512
#else
#define AUDIO_MEM 384
#endif
#if NUM_DEXED > 1
#if defined(TEENSY3_6)
#define DELAY_MAX_TIME 250
#elif defined(TEENSY4)
#define DELAY_MAX_TIME 750
#else
#define DELAY_MAX_TIME 125
#endif
#else
#if defined(TEENSY3_6)
#define DELAY_MAX_TIME 500
#elif defined(TEENSY4)
#define DELAY_MAX_TIME 1000
#else
#define DELAY_MAX_TIME 250
#endif
#endif
#define ENABLE_LCD_UI 1
#define STANDARD_LCD_I2C
#ifdef STANDARD_LCD_I2C
#define LCD_I2C_ADDRESS 0x27
#define LCD_cols  16
#define LCD_rows  2
#define I2C_DISPLAY
#endif
#ifdef OLED_SPI
#define LCD_cols  16
#define LCD_rows  4
#define U8X8_DISPLAY
#define DISPLAY_LCD_SPI
#define U8X8_DISPLAY_CLASS U8X8_SSD1322_NHD_256X64_4W_HW_SPI
#define U8X8_CS_PIN 9
#define U8X8_DC_PIN 15
#define U8X8_RESET_PIN 14
#endif
#define VOICE_SELECTION_MS 60000
#define BACK_FROM_VOLUME_MS 2000
#define MESSAGE_WAIT_TIME 1000
#define MIDI_DECAY_TIMER 50
#define SDCARD_AUDIO_CS_PIN    10
#define SDCARD_AUDIO_MOSI_PIN  7
#define SDCARD_AUDIO_SCK_PIN   14
#ifndef TEENSY4
#define SDCARD_TEENSY_CS_PIN    BUILTIN_SDCARD
#define SDCARD_TEENSY_MOSI_PIN  11
#define SDCARD_TEENSY_SCK_PIN   13
#else
#define SDCARD_TEENSY_CS_PIN    10
#define SDCARD_TEENSY_MOSI_PIN  11
#define SDCARD_TEENSY_SCK_PIN   13
#endif
#define NUM_ENCODER 2
#define ENC_L_PIN_A  3
#define ENC_L_PIN_B  2
#define BUT_L_PIN    4
#if defined(TEENSY3_6)
#define ENC_R_PIN_A  28
#define ENC_R_PIN_B  29
#define BUT_R_PIN    30
#else
#if defined(TEENSY4_0)
#define ENC_R_PIN_A  6
#define ENC_R_PIN_B  5
#define BUT_R_PIN    8
#else // ARDUINO_TEENSY41
#define ENC_R_PIN_A  24
#define ENC_R_PIN_B  5
#define BUT_R_PIN    9
#endif
#endif
#define BUT_DEBOUNCE_MS 20
#define LONG_BUTTON_PRESS 500
#define AUTOSTORE_MS 5000
#define EEPROM_START_ADDRESS 0xFF
#define MAX_BANKS 100
#define MAX_VOICES 32 // voices per bank
#define MAX_FX 99
#define MAX_PERFORMANCE 99
#define MAX_VOICECONFIG 99
#define BANK_NAME_LEN 11 // 10 (plus '\0')
#define VOICE_NAME_LEN 11 // 10 (plus '\0')
#define FILENAME_LEN BANK_NAME_LEN + VOICE_NAME_LEN
#define FX_CONFIG_PATH "FXCFG"
#define FX_CONFIG_NAME "FXCFG"
#define PERFORMANCE_CONFIG_PATH "PCFG"
#define PERFORMANCE_CONFIG_NAME "PCFG"
#define VOICE_CONFIG_PATH "VCFG"
#define VOICE_CONFIG_NAME "VCFG"
#define FAV_CONFIG_PATH "FAVCFG"
#define FAV_CONFIG_NAME "FAVCFG"
#define MAX_DEXED 2 // No! - even don't think about increasing this number! IT WILL PRODUCE MASSIVE PROBLEMS!
#define CONTROL_RATE_MS 50
#define EEPROM_MARKER 0x4242
#ifdef MIDI_DEVICE_USB
#define USBCON 1
#endif
#ifdef TEENSY4
#define MAX_NOTES 32
#endif
#if defined(TEENSY3_6)
# if defined(USE_FX)
#  if NUM_DEXED == 1
#   if F_CPU == 256000000
#    define MAX_NOTES 20
#   elif F_CPU == 240000000
#    define MAX_NOTES 18
#   elif F_CPU == 216000000
#    define MAX_NOTES 16
#   else
#    warning >>> You should consider to use 216MHz overclocking to get polyphony of 16 instead 12 voices <<<
#    define MAX_NOTES 12
#   endif
#  else
#   if F_CPU == 256000000
#    define MAX_NOTES 8
#   elif F_CPU == 240000000
#    define MAX_NOTES 7
#   elif F_CPU == 216000000
#    define MAX_NOTES 6
#   else
#    define MAX_NOTES 5
#   endif
#  endif
# else
#  if NUM_DEXED == 1
#   if F_CPU == 256000000
#    define MAX_NOTES 20
#   elif F_CPU == 216000000
#    define MAX_NOTES 20
#   else
#    define MAX_NOTES 16
#   endif
#  else
#   if F_CPU == 256000000
#    define MAX_NOTES 20
#   elif F_CPU == 216000000
#    define MAX_NOTES 16
#   else
#    define MAX_NOTES 8
#   endif
#  endif
# endif
#endif
#ifdef TEENSY3_5
#undef MIDI_DEVICE_USB_HOST
#define MAX_NOTES 11
#undef USE_FX
#endif
#define TRANSPOSE_FIX 24
#define VOICE_SILENCE_LEVEL 4000
#ifdef TGA_AUDIO_BOARD
#endif
#define USE_TEENSY_DSP 1
#define TIME_MS2SAMPLES(x) floor(uint32_t(x) * AUDIO_SAMPLE_RATE / 1000)
#define SAMPLES2TIME_MS(x) float(uint32_t(x) * 1000 / AUDIO_SAMPLE_RATE)
#define MOD_NO_FILTER_OUTPUT 0
#define MOD_BUTTERWORTH_FILTER_OUTPUT 1
#define MOD_LINKWITZ_RILEY_FILTER_OUTPUT 2
#if defined(TEENSY_DAC_SYMMETRIC)
#define MONO_MIN 1
#define MONO_MAX 1
#define MONO_DEFAULT 1
#else
#define MONO_MIN 0
#define MONO_MAX 3
#define MONO_DEFAULT 0
#endif
#define MIDI_CONTROLLER_MODE_MAX 2
#define VOLUME_MIN 0
#define VOLUME_MAX 100
#define VOLUME_DEFAULT 80
#define PANORAMA_MIN 0
#define PANORAMA_MAX 40
#define PANORAMA_DEFAULT 20
#define MIDI_CHANNEL_MIN MIDI_CHANNEL_OMNI
#define MIDI_CHANNEL_MAX 16
#define MIDI_CHANNEL_DEFAULT MIDI_CHANNEL_OMNI
#define INSTANCE_LOWEST_NOTE_MIN 21
#define INSTANCE_LOWEST_NOTE_MAX 108
#define INSTANCE_LOWEST_NOTE_DEFAULT 21
#define INSTANCE_HIGHEST_NOTE_MIN 21
#define INSTANCE_HIGHEST_NOTE_MAX 108
#define INSTANCE_HIGHEST_NOTE_DEFAULT 108
#define CHORUS_FREQUENCY_MIN 0
#define CHORUS_FREQUENCY_MAX 100
#define CHORUS_FREQUENCY_DEFAULT 0
#define CHORUS_WAVEFORM_MIN 0
#define CHORUS_WAVEFORM_MAX 1
#define CHORUS_WAVEFORM_DEFAULT 0
#define CHORUS_DEPTH_MIN 0
#define CHORUS_DEPTH_MAX 100
#define CHORUS_DEPTH_DEFAULT 0
#define CHORUS_LEVEL_MIN 0
#define CHORUS_LEVEL_MAX 100
#define CHORUS_LEVEL_DEFAULT 0
#define DELAY_TIME_MIN 0
#define DELAY_TIME_MAX DELAY_MAX_TIME/10
#define DELAY_TIME_DEFAULT 0
#define DELAY_FEEDBACK_MIN 0
#define DELAY_FEEDBACK_MAX 100
#define DELAY_FEEDBACK_DEFAULT 0
#define DELAY_LEVEL_MIN 0
#define DELAY_LEVEL_MAX 100
#define DELAY_LEVEL_DEFAULT 0
#define REVERB_ROOMSIZE_MIN 0
#define REVERB_ROOMSIZE_MAX 100
#define REVERB_ROOMSIZE_DEFAULT 0
#define REVERB_DAMPING_MIN 0
#define REVERB_DAMPING_MAX 100
#define REVERB_DAMPING_DEFAULT 0
#define REVERB_LEVEL_MIN 0
#define REVERB_LEVEL_MAX 100
#define REVERB_LEVEL_DEFAULT 0
#define REVERB_SEND_MIN 0
#define REVERB_SEND_MAX 100
#define REVERB_SEND_DEFAULT 0
#define FILTER_CUTOFF_MIN 0
#define FILTER_CUTOFF_MAX 100
#define FILTER_CUTOFF_DEFAULT 0
#define FILTER_RESONANCE_MIN 0
#define FILTER_RESONANCE_MAX 100
#define FILTER_RESONANCE_DEFAULT 0
#define TRANSPOSE_MIN 0
#define TRANSPOSE_MAX 48
#define TRANSPOSE_DEFAULT 12
#define TUNE_MIN 1
#define TUNE_MAX 199
#define TUNE_DEFAULT 100
#define SOUND_INTENSITY_MIN 0
#define SOUND_INTENSITY_MAX 127
#define SOUND_INTENSITY_DEFAULT 100
#define SOUND_INTENSITY_AMP_MAX 1.27
#define POLYPHONY_MIN 0
#define POLYPHONY_MAX MAX_NOTES
#define POLYPHONY_DEFAULT MAX_NOTES
#define ENGINE_MIN 0
#define ENGINE_MAX 2
#define ENGINE_DEFAULT 0
#define MONOPOLY_MIN 0
#define MONOPOLY_MAX 1
#define MONOPOLY_DEFAULT 0
#define NOTE_REFRESH_MIN 0
#define NOTE_REFRESH_MAX 1
#define NOTE_REFRESH_DEFAULT 0
#define PB_RANGE_MIN 0
#define PB_RANGE_MAX 12
#define PB_RANGE_DEFAULT 1
#define PB_STEP_MIN 0
#define PB_STEP_MAX 12
#define PB_STEP_DEFAULT 0
#define MW_RANGE_MIN 0
#define MW_RANGE_MAX 99
#define MW_RANGE_DEFAULT 50
#define MW_ASSIGN_MIN 0
#define MW_ASSIGN_MAX 7
#define MW_ASSIGN_DEFAULT 0 // Bitmapped: 0: Pitch, 1: Amp, 2: Bias
#define MW_MODE_MIN 0
#define MW_MODE_MAX MIDI_CONTROLLER_MODE_MAX
#define MW_MODE_DEFAULT 0
#define FC_RANGE_MIN 0
#define FC_RANGE_MAX 99
#define FC_RANGE_DEFAULT 50
#define FC_ASSIGN_MIN 0
#define FC_ASSIGN_MAX 7
#define FC_ASSIGN_DEFAULT 0 // Bitmapped: 0: Pitch, 1: Amp, 2: Bias
#define FC_MODE_MIN 0
#define FC_MODE_MAX MIDI_CONTROLLER_MODE_MAX
#define FC_MODE_DEFAULT 0
#define BC_RANGE_MIN 0
#define BC_RANGE_MAX 99
#define BC_RANGE_DEFAULT 50
#define BC_ASSIGN_MIN 0
#define BC_ASSIGN_MAX 7
#define BC_ASSIGN_DEFAULT 0 // Bitmapped: 0: Pitch, 1: Amp, 2: Bias
#define BC_MODE_MIN 0
#define BC_MODE_MAX MIDI_CONTROLLER_MODE_MAX
#define BC_MODE_DEFAULT 0
#define AT_RANGE_MIN 0
#define AT_RANGE_MAX 99
#define AT_RANGE_DEFAULT 50
#define AT_ASSIGN_MIN 0
#define AT_ASSIGN_MAX 7
#define AT_ASSIGN_DEFAULT 0 // Bitmapped: 0: Pitch, 1: Amp, 2: Bias
#define AT_MODE_MIN 0
#define AT_MODE_MAX MIDI_CONTROLLER_MODE_MAX
#define AT_MODE_DEFAULT 0
#define OP_ENABLED_MIN 0
#define OP_ENABLED_MAX 0x3f
#define OP_ENABLED_DEFAULT OP_ENABLED_MAX
#define PORTAMENTO_MODE_MIN 0
#define PORTAMENTO_MODE_MAX 1
#define PORTAMENTO_MODE_DEFAULT 0 // 0: Retain, 1: Follow
#define PORTAMENTO_GLISSANDO_MIN 0
#define PORTAMENTO_GLISSANDO_MAX 1
#define PORTAMENTO_GLISSANDO_DEFAULT 0
#define PORTAMENTO_TIME_MIN 0
#define PORTAMENTO_TIME_MAX 99
#define PORTAMENTO_TIME_DEFAULT 0
#define INSTANCES_MIN 1
#define INSTANCES_MAX NUM_DEXED
#define INSTANCES_DEFAULT 1
#define INSTANCE_NOTE_START_MIN MIDI_AIS0
#define INSTANCE_NOTE_START_MAX MIDI_B7
#define INSTANCE_NOTE_START_DEFAULT INSTANCE_NOTE_START_MIN
#define INSTANCE_NOTE_END_MIN MIDI_AIS0
#define INSTANCE_NOTE_END_MAX MIDI_B7
#define INSTANCE_NOTE_END_DEFAULT INSTANCE_NOTE_END_MAX
#define SOFT_MIDI_THRU_MIN 0
#define SOFT_MIDI_THRU_MAX 1
#define SOFT_MIDI_THRU_DEFAULT 1
#define VELOCITY_LEVEL_MIN 100
#define VELOCITY_LEVEL_MAX 127
#define VELOCITY_LEVEL_DEFAULT 100
#define PERFORMANCE_NUM_MIN 0
#define PERFORMANCE_NUM_MAX MAX_PERFORMANCE
#define PERFORMANCE_NUM_DEFAULT -1
#define FX_NUM_MIN 0
#define FX_NUM_MAX MAX_FX
#define FX_NUM_DEFAULT -1
#define VOICECONFIG_NUM_MIN 0
#define VOICECONFIG_NUM_MAX MAX_VOICECONFIG
#define VOICECONFIG_NUM_DEFAULT -1
#define EQ_BASS_MIN -10
#define EQ_BASS_MAX 10
#define EQ_BASS_DEFAULT 0
#define EQ_TREBLE_MIN -10
#define EQ_TREBLE_MAX 10
#define EQ_TREBLE_DEFAULT 0
typedef struct dexed_s {
  uint8_t lowest_note;
  uint8_t highest_note;
  uint8_t transpose;
  uint8_t tune;
  uint8_t sound_intensity;
  uint8_t pan;
  uint8_t polyphony;
  uint8_t velocity_level;
  uint8_t engine;
  uint8_t monopoly;
  uint8_t note_refresh;
  uint8_t pb_range;
  uint8_t pb_step;
  uint8_t mw_range;
  uint8_t mw_assign;
  uint8_t mw_mode;
  uint8_t fc_range;
  uint8_t fc_assign;
  uint8_t fc_mode;
  uint8_t bc_range;
  uint8_t bc_assign;
  uint8_t bc_mode;
  uint8_t at_range;
  uint8_t at_assign;
  uint8_t at_mode;
  uint8_t portamento_mode;
  uint8_t portamento_glissando;
  uint8_t portamento_time;
  uint8_t op_enabled;
  uint8_t midi_channel;
} dexed_t;
typedef struct fx_s {
  uint8_t filter_cutoff[MAX_DEXED];
  uint8_t filter_resonance[MAX_DEXED];
  uint8_t chorus_frequency[MAX_DEXED];
  uint8_t chorus_waveform[MAX_DEXED];
  uint8_t chorus_depth[MAX_DEXED];
  uint8_t chorus_level[MAX_DEXED];
  uint8_t delay_time[MAX_DEXED];
  uint8_t delay_feedback[MAX_DEXED];
  uint8_t delay_level[MAX_DEXED];
  uint8_t reverb_send[MAX_DEXED];
  uint8_t reverb_roomsize;
  uint8_t reverb_damping;
  uint8_t reverb_level;
  int8_t eq_bass;
  int8_t eq_treble;
} fx_t;
typedef struct performance_s {
  uint8_t bank[MAX_DEXED];
  uint8_t voice[MAX_DEXED];
  int8_t voiceconfig_number[MAX_DEXED];
  int8_t fx_number;
} performance_t;
typedef struct sys_s {
  uint8_t instances;
  uint8_t vol;
  uint8_t mono;
  uint8_t soft_midi_thru;
  int8_t performance_number;
  uint8_t favorites;
} sys_t;
typedef struct configuration_s {
  sys_t sys;
  fx_t fx;
  performance_t performance;
  dexed_t dexed[MAX_DEXED];
  uint16_t _marker_;
} config_t;
#if !defined(_MAPFLOAT)
#define _MAPFLOAT
inline float mapfloat(float val, float in_min, float in_max, float out_min, float out_max)
{
  return (val - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
#endif
#endif // CONFIG_H_INCLUDED
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
#endif  // __CONTROLLERS_H
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
#ifndef __FM_CORE_H
#define __FM_CORE_H
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
#endif  // __FM_CORE_H
class EngineMkI : public FmCore {
  public:
    EngineMkI();
    void render(int32_t *output, FmOpParams *params, int algorithm, int32_t *fb_buf, int32_t feedback_shift);
    void compute(int32_t *output, const int32_t *input, int32_t phase0, int32_t freq, int32_t gain1, int32_t gain2,
                 bool add);
    void compute_pure(int32_t *output, int32_t phase0, int32_t freq, int32_t gain1, int32_t gain2,
                      bool add);
    void compute_fb(int32_t *output, int32_t phase0, int32_t freq, int32_t gain1, int32_t gain2,
                    int32_t *fb_buf, int fb_gain, bool add);
    void compute_fb2(int32_t *output, FmOpParams *params, int32_t gain01, int32_t gain02, int32_t *fb_buf, int fb_shift);
    void compute_fb3(int32_t *output, FmOpParams *params, int32_t gain01, int32_t gain02, int32_t *fb_buf, int fb_shift);
};
#endif  // ENGINEMKI_H_INCLUDED
/**
   Copyright (c) 2014 Pascal Gauthier.
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
#ifndef ENGINEOPL_H_INCLUDED
#define ENGINEOPL_H_INCLUDED
#include "Arduino.h"
class EngineOpl : public FmCore {
  public:
    virtual void render(int32_t *output, FmOpParams *params, int algorithm,
                        int32_t *fb_buf, int32_t feedback_shift);
    void compute(int32_t *output, const int32_t *input, int32_t phase0, int32_t freq, int32_t gain1, int32_t gain2, bool add);
    void compute_pure(int32_t *output, int32_t phase0, int32_t freq, int32_t gain1, int32_t gain2, bool add);
    void compute_fb(int32_t *output, int32_t phase0, int32_t freq,
                    int32_t gain1, int32_t gain2,
                    int32_t *fb_buf, int fb_gain, bool add);
};
#endif  // ENGINEOPL_H_INCLUDED
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
#ifndef PLUGINFX_H_INCLUDED
#define PLUGINFX_H_INCLUDED
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
#endif  // PLUGINFX_H_INCLUDED
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
#ifndef __ALIGNED_BUF_H
#define __ALIGNED_BUF_H
template<typename T, size_t size, size_t alignment = 16>
class AlignedBuf {
  public:
    T *get() {
      return (T *)((((intptr_t)storage_) + alignment - 1) & -alignment);
    }
  private:
    unsigned char storage_[size * sizeof(T) + alignment];
};
#endif  // __ALIGNED_BUF_H
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
#ifndef __CONTROLLERS_H
#define __CONTROLLERS_H
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
#endif  // __CONTROLLERS_H
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
#ifndef DEXED_H_INCLUDED
#define DEXED_H_INCLUDED
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
#ifndef SYNTH_DX7NOTE_H_
#define SYNTH_DX7NOTE_H_
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
#ifndef __ENV_H
#define __ENV_H
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
#endif  // __ENV_H
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
#ifndef __PITCHENV_H
#define __PITCHENV_H
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
#endif  // __PITCHENV_H
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
#endif  // SYNTH_DX7NOTE_H_
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
/*
   Copyright 2014 Pascal Gauthier.
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
#ifndef ENGINEMKI_H_INCLUDED
#define ENGINEMKI_H_INCLUDED
class EngineMkI : public FmCore {
  public:
    EngineMkI();
    void render(int32_t *output, FmOpParams *params, int algorithm, int32_t *fb_buf, int32_t feedback_shift);
    void compute(int32_t *output, const int32_t *input, int32_t phase0, int32_t freq, int32_t gain1, int32_t gain2,
                 bool add);
    void compute_pure(int32_t *output, int32_t phase0, int32_t freq, int32_t gain1, int32_t gain2,
                      bool add);
    void compute_fb(int32_t *output, int32_t phase0, int32_t freq, int32_t gain1, int32_t gain2,
                    int32_t *fb_buf, int fb_gain, bool add);
    void compute_fb2(int32_t *output, FmOpParams *params, int32_t gain01, int32_t gain02, int32_t *fb_buf, int fb_shift);
    void compute_fb3(int32_t *output, FmOpParams *params, int32_t gain01, int32_t gain02, int32_t *fb_buf, int fb_shift);
};
#endif  // ENGINEMKI_H_INCLUDED
/**
   Copyright (c) 2014 Pascal Gauthier.
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
#ifndef ENGINEOPL_H_INCLUDED
#define ENGINEOPL_H_INCLUDED
class EngineOpl : public FmCore {
  public:
    virtual void render(int32_t *output, FmOpParams *params, int algorithm,
                        int32_t *fb_buf, int32_t feedback_shift);
    void compute(int32_t *output, const int32_t *input, int32_t phase0, int32_t freq, int32_t gain1, int32_t gain2, bool add);
    void compute_pure(int32_t *output, int32_t phase0, int32_t freq, int32_t gain1, int32_t gain2, bool add);
    void compute_fb(int32_t *output, int32_t phase0, int32_t freq,
                    int32_t gain1, int32_t gain2,
                    int32_t *fb_buf, int fb_gain, bool add);
};
#endif  // ENGINEOPL_H_INCLUDED
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
#ifndef PLUGINFX_H_INCLUDED
#define PLUGINFX_H_INCLUDED
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
#endif  // PLUGINFX_H_INCLUDED
extern bool load_sysex(uint8_t bank, uint8_t voice_number);
extern AudioControlSGTL5000 sgtl5000_1;
extern float vol;
extern float vol_right;
extern float vol_left;
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
enum DexedEngineResolution {
  DEXED_ENGINE_MODERN,	    // 0
  DEXED_ENGINE_MARKI,	    // 1
  DEXED_ENGINE_OPL	    // 2
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
class Dexed
{
  public:
    Dexed(int rate);
    ~Dexed();
    void activate(void);
    void deactivate(void);
    uint8_t getEngineType();
    void setEngineType(uint8_t tp);
    bool isMonoMode(void);
    void setMonoMode(bool mode);
    void setRefreshMode(bool mode);
    void getSamples(uint16_t n_samples, int16_t* buffer);
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
    EngineMkI* engineMkI;
    EngineOpl* engineOpl;
};
#endif  // PLUGINPROCESSOR_H_INCLUDED
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
#ifndef SYNTH_DX7NOTE_H_
#define SYNTH_DX7NOTE_H_
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
#endif  // SYNTH_DX7NOTE_H_
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
#ifndef __ENV_H
#define __ENV_H
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
#endif  // __ENV_H
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
/* ----------------------------------------------------------------------
* https://community.arm.com/tools/f/discussions/4292/cmsis-dsp-new-functionality-proposal/22621#22621
* Fast approximation to the log2() function.  It uses a two step
* process.  First, it decomposes the floating-point number into
* a fractional component F and an exponent E.  The fraction component
* is used in a polynomial approximation and then the exponent added
* to the result.  A 3rd order polynomial is used and the result
* when computing db20() is accurate to 7.984884e-003 dB.
** ------------------------------------------------------------------- */

float log2f_approx_coeff[4] = {1.23149591368684f, -4.11852516267426f, 6.02197014179219f, -3.13396450166353f};

float log2f_approx(float X)
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
  return(Y);
}

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
#ifndef __FM_CORE_H
#define __FM_CORE_H
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
#endif  // __FM_CORE_H
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
#ifndef __FM_OP_KERNEL_H
#define __FM_OP_KERNEL_H
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
#endif
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
/* class Lfo {
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
};*/
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
#ifndef SYNTH_MODULE_H
#define SYNTH_MODULE_H
class Module {
  public:
    static const int lg_n = 6;
    static const int n = 1 << lg_n;
    virtual void process(const int32_t **inbufs, const int32_t *control_in,
                         const int32_t *control_last, int32_t **outbufs) = 0;
};
#endif  // SYNTH_MODULE_H
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
#ifndef __PITCHENV_H
#define __PITCHENV_H
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
#endif  // __PITCHENV_H
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
#ifndef SYNTH_PORTA_H_
#define SYNTH_PORTA_H_
struct Porta {
  public:
    static void init_sr(double sampleRate);
    static int32_t rates[128];
};
#endif  // SYNTH_PORTA_H_
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
#pragma once
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
#ifndef DEXED_H_INCLUDED
#define DEXED_H_INCLUDED
extern bool load_sysex(uint8_t bank, uint8_t voice_number);
extern AudioControlSGTL5000 sgtl5000_1;
extern float vol;
extern float vol_right;
extern float vol_left;
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
enum DexedEngineResolution {
  DEXED_ENGINE_MODERN,	    // 0
  DEXED_ENGINE_MARKI,	    // 1
  DEXED_ENGINE_OPL	    // 2
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
class Dexed
{
  public:
    Dexed(int rate);
    ~Dexed();
    void activate(void);
    void deactivate(void);
    uint8_t getEngineType();
    void setEngineType(uint8_t tp);
    bool isMonoMode(void);
    void setMonoMode(bool mode);
    void setRefreshMode(bool mode);
    void getSamples(uint16_t n_samples, int16_t* buffer);
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
    EngineMkI* engineMkI;
    EngineOpl* engineOpl;
};
#endif  // PLUGINPROCESSOR_H_INCLUDED
#include <AudioStream.h>
class AudioSourceMicroDexed : public AudioStream, public Dexed {
  public:
    const uint16_t audio_block_time_us = 1000000 / (SAMPLE_RATE / AUDIO_BLOCK_SAMPLES);
    uint32_t xrun = 0;
    uint16_t render_time_max = 0;
    AudioSourceMicroDexed(int sample_rate) : AudioStream(0, NULL), Dexed(sample_rate) { };
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
#ifndef __SYNTH_H
#define __SYNTH_H
#ifdef _MSC_VER
typedef __int32 int32_t;
typedef unsigned __int32 uint32_t;
typedef __int16 SInt16;
#endif
#define LG_N 6
#define _N_ (1 << LG_N)
#if defined(__APPLE__)
#define SynthMemoryBarrier() OSMemoryBarrier()
#elif defined(__GNUC__)
#if (__GNUC__ > 4) || (__GNUC__ == 4 && __GNUC_MINOR__ >= 1)
#define SynthMemoryBarrier() __sync_synchronize()
#endif
#endif
#ifndef SynthMemoryBarrier
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
#define COS_FUNC cosf
#define LOG_FUNC logf
#define EXP_FUNC expf
#define SQRT_FUNC sqrtf
#endif  // __SYNTH_H
