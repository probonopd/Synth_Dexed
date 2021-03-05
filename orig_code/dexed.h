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

// GLOBALS

//==============================================================================

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

