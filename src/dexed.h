/*
   synth_dexed

   synth_dexed is a port of the Dexed sound engine (https://github.com/asb2m10/dexed)
   for microcontrollers.
   Dexed ist heavily based on https://github.com/google/music-synthesizer-for-android

   Copyright (c) 2013-2018 Pascal Gauthier.
   (c)2018-2024 H. Wirtz <wirtz@parasitstudio.de>

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

#pragma once

#include <stdint.h>
#include <cstdlib>
#if defined(TEENSYDUINO)
#include <Audio.h>
#endif

// Add DLL export/import declarations for Windows
#ifdef _WIN32
  #ifdef SYNTH_DEXED_EXPORTS
    #define DEXED_API __declspec(dllexport)
  #else
    #define DEXED_API __declspec(dllimport)
  #endif
#else
  #define DEXED_API
#endif

#include "fm_op_kernel.h"
#include "synth.h"
#include "env.h"
#include "aligned_buf.h"
#include "pitchenv.h"
#include "controllers.h"
#include "dx7note.h"
#include "lfo.h"
#include "EngineMsfa.h"
#include "EngineMkI.h"
#include "EngineOpl.h"

#define NUM_VOICE_PARAMETERS 156

#ifndef constrain
#define constrain(amt, low, high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt)))
#endif

#ifdef USE_FILTER
#define FILTER_POLES 4
typedef struct {
    float cutoff;
    float resonance;
    int32_t a0;
    int32_t a1;
    int32_t a2;
    int32_t b0;
    int32_t b1;
    int32_t b2;
    int32_t filterState[FILTER_POLES * 4];
} Filter;
#endif

struct ProcessorVoice {
  uint8_t midi_note;
  uint8_t velocity;
  int16_t porta;
  bool keydown;
  bool sustained;
  bool sostenuted;
  bool held;
  bool live;
  uint32_t key_pressed_timer;
  Dx7Note *dx7_note;
  uint8_t aftertouch; // Per-note aftertouch value
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

enum ADSR {
  ATTACK,
  DECAY,
  SUSTAIN,
  RELEASE
};

enum OPERATORS {
  OP1,
  OP2,
  OP3,
  OP4,
  OP5,
  OP6
};

enum CONTROLLER_ASSIGN {
  NONE,
  PITCH,
  AMP,
  PITCH_AMP,
  EG,
  PITCH_EG,
  AMP_EG,
  PITCH_AMP_EG
};

enum PORTAMENTO_MODE {
  RETAIN,
  FOLLOW
};

enum ON_OFF {
  OFF,
  ON
};

enum VELOCITY_SCALES {
  MIDI_VELOCITY_SCALING_OFF,
  MIDI_VELOCITY_SCALING_DX7,
  MIDI_VELOCITY_SCALING_DX7II
};

enum ENGINES {
  MSFA,
  MKI,
  OPL
};

// GLOBALS

//==============================================================================

class DEXED_API Dexed
{
  public:
    Dexed(uint8_t maxnotes, uint16_t rate);
    ~Dexed();

    // Global methods
    void activate(void);
    void deactivate(void);
    bool getMonoMode(void);
    void setMonoMode(bool mode);
    void setNoteRefreshMode(bool mode);
    uint8_t getMaxNotes(void);
    void doRefreshVoice(void);
    void setOPAll(uint8_t ops);
    bool decodeVoice(uint8_t* data, uint8_t* encoded_data);
    bool encodeVoice(uint8_t* encoded_data);
    bool getVoiceData(uint8_t* data_copy);
    void setVoiceDataElement(uint8_t address, uint8_t value);
    uint8_t getVoiceDataElement(uint8_t address);
    void loadInitVoice(void);
    void loadVoiceParameters(uint8_t* data);
    uint8_t getNumNotesPlaying(void);
    uint32_t getXRun(void);
    uint16_t getRenderTimeMax(void);
    void resetRenderTimeMax(void);
    void ControllersRefresh(void);
    void setVelocityScale(uint8_t offset, uint8_t max);
    void getVelocityScale(uint8_t* offset, uint8_t* max);
    void setVelocityScale(uint8_t setup);
    void setMaxNotes(uint8_t n);
    void setEngineType(uint8_t engine);
    uint8_t getEngineType(void);
    FmCore* getEngineAddress(void);
    int16_t checkSystemExclusive(const uint8_t* sysex, const uint16_t len);

    // Sound methods
    void keyup(uint8_t pitch);
    void keydown(uint8_t pitch, uint8_t velo);
    void setSustain(bool sustain);
    bool getSustain(void);
    void setSostenuto(bool sostenuto);
    bool getSostenuto(void);
    void setHold(bool hold);
    bool getHold(void);
    void panic(void);
    void notesOff(void);
    void resetControllers(void);
    void setMasterTune(int8_t mastertune);
    int8_t getMasterTune(void);
    void setPortamento(uint8_t portamento_mode, uint8_t portamento_glissando, uint8_t portamento_time);
    void setPortamentoMode(uint8_t portamento_mode);
    uint8_t getPortamentoMode(void);
    void setPortamentoGlissando(uint8_t portamento_glissando);
    uint8_t getPortamentoGlissando(void);
    void setPortamentoTime(uint8_t portamento_time);
    uint8_t getPortamentoTime(void);
    void setPBController(uint8_t pb_range, uint8_t pb_step);
    void setMWController(uint8_t mw_range, uint8_t mw_assign, uint8_t mw_mode);
    void setFCController(uint8_t fc_range, uint8_t fc_assign, uint8_t fc_mode);
    void setBCController(uint8_t bc_range, uint8_t bc_assign, uint8_t bc_mode);
    void setATController(uint8_t at_range, uint8_t at_assign, uint8_t at_mode);
    void setModWheel(uint8_t value);
    uint8_t getModWheel(void);
    void setBreathController(uint8_t value);
    uint8_t getBreathController(void);
    void setFootController(uint8_t value);
    uint8_t getFootController(void);
    void setAftertouch(uint8_t value);
    uint8_t getAftertouch(void);
    void setPitchbend(uint8_t value1, uint8_t value2);
    void setPitchbend(int16_t value);
    void setPitchbend(uint16_t value);
    int16_t getPitchbend(void);
    void setPitchbendRange(uint8_t range);
    uint8_t getPitchbendRange(void);
    void setPitchbendStep(uint8_t step);
    uint8_t getPitchbendStep(void);
    void setModWheelRange(uint8_t range);
    uint8_t getModWheelRange(void);
    void setModWheelTarget(uint8_t target);
    uint8_t getModWheelTarget(void);
    void setFootControllerRange(uint8_t range);
    uint8_t getFootControllerRange(void);
    void setFootControllerTarget(uint8_t target);
    uint8_t getFootControllerTarget(void);
    void setBreathControllerRange(uint8_t range);
    uint8_t getBreathControllerRange(void);
    void setBreathControllerTarget(uint8_t target);
    uint8_t getBreathControllerTarget(void);
    void setAftertouchRange(uint8_t range);
    uint8_t getAftertouchRange(void);
    void setAftertouchTarget(uint8_t target);
    uint8_t getAftertouchTarget(void);

    void setGain(float gain);
    float getGain(void);

#ifdef USE_COMPRESSOR
    void setCompAttack(float attack);
    float getCompAttack(void);
    void setCompRelease(float release);
    float getCompRelease(void);
    void setCompRatio(float ratio);
    float getCompRatio(void);
    void setCompThreshold(float thresh);
    float getCompThreshold(void);
    void setCompMakeupGain(float makeupGain);
    float getCompMakeupGain(void);
    void setCompEnable(bool enable);
    bool getCompEnable(void);
#endif

#ifdef USE_FILTER
    void setFilterEnable(bool enable);
    bool getFilterEnable(void);
#endif
    void setFilterCutoff(float v);
    void setFilterCutoffFrequency(float cutoff);
    float getFilterCutoff(void);
    float getFilterCutoffFrequency(void);
    void setFilterResonance(float resonance);
    float getFilterResonance(void);

    // Voice configuration methods
    void setOPRateAll(uint8_t rate);
    void setOPLevelAll(uint8_t level);
    void setOPRateAllCarrier(uint8_t step, uint8_t rate);
    void setOPLevelAllCarrier(uint8_t step, uint8_t level);
    void setOPRateAllModulator(uint8_t step, uint8_t rate);
    void setOPLevelAllModulator(uint8_t step, uint8_t level);
    void setOPRate(uint8_t op, uint8_t step, uint8_t rate);
    uint8_t getOPRate(uint8_t op, uint8_t step);
    void setOPLevel(uint8_t op, uint8_t step, uint8_t level);
    uint8_t getOPLevel(uint8_t op, uint8_t step);
    void setOPKeyboardLevelScalingBreakPoint(uint8_t op, uint8_t level);
    uint8_t getOPKeyboardLevelScalingBreakPoint(uint8_t op);
    void setOPKeyboardLevelScalingDepthLeft(uint8_t op, uint8_t depth);
    uint8_t getOPKeyboardLevelScalingDepthLeft(uint8_t op);
    void setOPKeyboardLevelScalingDepthRight(uint8_t op, uint8_t depth);
    uint8_t getOPKeyboardLevelScalingDepthRight(uint8_t op);
    void setOPKeyboardLevelScalingCurveLeft(uint8_t op, uint8_t curve);
    uint8_t getOPKeyboardLevelScalingCurveLeft(uint8_t op);
    void setOPKeyboardLevelScalingCurveRight(uint8_t op, uint8_t curve);
    uint8_t getOPKeyboardLevelScalingCurveRight(uint8_t op);
    void setOPKeyboardRateScale(uint8_t op, uint8_t scale);
    uint8_t getOPKeyboardRateScale(uint8_t op);
    void setOPAmpModulationSensity(uint8_t op, uint8_t sensitivity);
    uint8_t getOPAmpModulationSensity(uint8_t op);
    void setOPKeyboardVelocitySensity(uint8_t op, uint8_t sensitivity);
    uint8_t getOPKeyboardVelocitySensity(uint8_t op);
    void setOPOutputLevel(uint8_t op, uint8_t level);
    uint8_t getOPOutputLevel(uint8_t op);
    void setOPMode(uint8_t op, uint8_t mode);
    uint8_t getOPMode(uint8_t op);
    void setOPFrequencyCoarse(uint8_t op, uint8_t frq_coarse);
    uint8_t getOPFrequencyCoarse(uint8_t op);
    void setOPFrequencyFine(uint8_t op, uint8_t frq_fine);
    uint8_t getOPFrequencyFine(uint8_t op);
    void setOPDetune(uint8_t op, uint8_t detune);
    uint8_t getOPDetune(uint8_t op);
    void setPitchRate(uint8_t step, uint8_t rate);
    uint8_t getPitchRate(uint8_t step);
    void setPitchLevel(uint8_t step, uint8_t level);
    uint8_t getPitchLevel(uint8_t step);
    void setAlgorithm(uint8_t algorithm);
    uint8_t getAlgorithm(void);
    void setFeedback(uint8_t feedback);
    uint8_t getFeedback(void);
    void setOscillatorSync(bool sync);
    bool getOscillatorSync(void);
    void setLFOSpeed(uint8_t speed);
    uint8_t getLFOSpeed(void);
    void setLFODelay(uint8_t delay);
    uint8_t getLFODelay(void);
    void setLFOPitchModulationDepth(uint8_t depth);
    uint8_t getLFOPitchModulationDepth(void);
    void setLFOAmpModulationDepth(uint8_t delay);
    uint8_t getLFOAmpModulationDepth(void);
    void setLFOSync(bool sync);
    bool getLFOSync(void);
    void setLFOWaveform(uint8_t waveform);
    uint8_t getLFOWaveform(void);
    void setLFOPitchModulationSensitivity(uint8_t sensitivity);
    uint8_t getLFOPitchModulationSensitivity(void);
    void setTranspose(uint8_t transpose);
    uint8_t getTranspose(void);
    void setName(char name[11]);
    void getName(char buffer[11]);

    bool midiDataHandler(uint8_t midiChannel, uint8_t midiState, uint8_t midiData1, uint8_t midiData2);
    bool midiDataHandler(uint8_t midiChannel, uint8_t* midiData, int16_t len);

    ProcessorVoice* voices;

    void getSamples(int16_t* buffer, uint16_t n_samples);

  protected:
    uint8_t init_voice[NUM_VOICE_PARAMETERS] = {
      99, 99, 99, 99, 99, 99, 99, 00, 33, 00, 00, 00, 00, 00, 00, 00, 00, 00, 01, 00, 00, // OP6 eg_rate_1-4, level_1-4, kbd_lev_scl_brk_pt, kbd_lev_scl_lft_depth, kbd_lev_scl_rht_depth, kbd_lev_scl_lft_curve, kbd_lev_scl_rht_curve, kbd_rate_scaling, amp_mod_sensitivity, key_vel_sensitivity, operator_output_level, osc_mode, osc_freq_coarse, osc_freq_fine, osc_detune
      99, 99, 99, 99, 99, 99, 99, 00, 33, 00, 00, 00, 00, 00, 00, 00, 00, 00, 01, 00, 00, // OP5
      99, 99, 99, 99, 99, 99, 99, 00, 33, 00, 00, 00, 00, 00, 00, 00, 00, 00, 01, 00, 00, // OP4
      99, 99, 99, 99, 99, 99, 99, 00, 33, 00, 00, 00, 00, 00, 00, 00, 00, 00, 01, 00, 00, // OP4
      99, 99, 99, 99, 99, 99, 99, 00, 33, 00, 00, 00, 00, 00, 00, 00, 00, 00, 01, 00, 00, // OP4
      99, 99, 99, 99, 99, 99, 99, 00, 33, 00, 00, 00, 00, 00, 00, 00, 00, 00, 01, 00, 00, // OP4
      99, 99, 99, 99, 50, 50, 50, 50,                                                     // 4 * pitch EG rates, 4 * pitch EG level
      01, 00, 01,                                                                         // algorithm, feedback, osc sync
      35, 00, 00, 00, 01, 00,                                                             // lfo speed, lfo delay, lfo pitch_mod_depth, lfo_amp_mod_depth, lfo_sync, lfo_waveform
      03, 48,                                                                             // pitch_mod_sensitivity, transpose
      73, 78, 73, 84, 32, 86, 79, 73, 67, 69                                              // 10 * char for name ("INIT VOICE")
    };
    float samplerate;
    uint8_t data[NUM_VOICE_PARAMETERS];
    uint8_t max_notes;
    uint8_t used_notes;
    Controllers controllers;
    int32_t lastKeyDown;
    uint32_t xrun;
    uint16_t render_time_max;
    int16_t currentNote;
    bool sustain;
    bool sostenuto;
    bool hold;
    bool monoMode;
    bool noteRefreshMode;
    bool refreshVoice;
    uint8_t engineType;
    VoiceStatus voiceStatus;
    Lfo lfo;
    EngineMsfa* engineMsfa;
    EngineMkI* engineMkI;
    EngineOpl* engineOpl;
    uint8_t velocity_offset;
    uint8_t velocity_max;
    float velocity_diff;
    int16_t gain;
#ifdef USE_COMPRESSOR
    void Compress(const int16_t *in, int16_t *out, int16_t numSamples);
    void initCompressor(float threshold, float ratio, float attack, float release, float gain);
    int32_t linear_to_db(int32_t linear);
    int32_t db_to_linear(int32_t db);
    int32_t comp_threshold;
    int32_t comp_threshold_;
    int32_t comp_ratio;
    int32_t comp_attack;
    int32_t comp_release;
    int32_t comp_gain;
    int32_t comp_gain_;
    bool comp_enabled = true;
    static inline int32_t signed_saturate_rshift(int32_t val, int32_t bits, int32_t rshift);
#endif
#ifdef USE_FILTER
    Filter filter;
    void initFilter(float cutoff, float resonance);
    void updateCoefficients(void);
    int32_t lowpassFilter(int32_t val);
    bool filter_enabled = true;
#endif
};

#ifdef USE_COMPRESSOR
PROGMEM const int32_t linear_to_db_table[LOG_TABLE_SIZE] = {
    -11796480, -3945103, -3550537, -3319731, -3155971, -3028950, -2925165, -2837417, 
    -2761406, -2694359, -2634384, -2580129, -2530599, -2485036, -2442851, -2403577, 
    -2366840, -2332330, -2299793, -2269016, -2239818, -2212044, -2185563, -2160260, 
    -2136033, -2112796, -2090470, -2068987, -2048285, -2028309, -2009011, -1990346, 
    -1972273, -1954757, -1937764, -1921263, -1905227, -1889630, -1874450, -1859663, 
    -1845252, -1831196, -1817478, -1804084, -1790997, -1778205, -1765694, -1753452, 
    -1741467, -1729730, -1718230, -1706957, -1695904, -1685061, -1674421, -1663975, 
    -1653719, -1643643, -1633743, -1624013, -1614445, -1605036, -1595780, -1586672, 
    -1577707, -1568882, -1560191, -1551631, -1543198, -1534887, -1526697, -1518622, 
    -1510661, -1502809, -1495064, -1487423, -1479884, -1472443, -1465097, -1457846, 
    -1450686, -1443614, -1436630, -1429730, -1422912, -1416176, -1409518, -1402937, 
    -1396431, -1389999, -1383639, -1377349, -1371128, -1364974, -1358886, -1352862, 
    -1346901, -1341002, -1335164, -1329385, -1323664, -1318000, -1312391, -1306838, 
    -1301338, -1295890, -1290495, -1285150, -1279855, -1274608, -1269409, -1264258, 
    -1259153, -1254093, -1249077, -1244106, -1239177, -1234291, -1229447, -1224643, 
    -1219879, -1215155, -1210470, -1205823, -1201214, -1196642, -1192106, -1187606, 
    -1183141, -1178712, -1174316, -1169954, -1165625, -1161329, -1157065, -1152833, 
    -1148632, -1144461, -1140321, -1136211, -1132131, -1128079, -1124056, -1120062, 
    -1116095, -1112155, -1108243, -1104358, -1100498, -1096665, -1092857, -1089075, 
    -1085318, -1081585, -1077877, -1074192, -1070531, -1066894, -1063280, -1059688, 
    -1056120, -1052573, -1049048, -1045545, -1042064, -1038603, -1035164, -1031745, 
    -1028346, -1024968, -1021610, -1018271, -1014952, -1011652, -1008371, -1005109, 
    -1001865, -998640, -995433, -992244, -989073, -985919, -982783, -979664, 
    -976562, -973476, -970408, -967355, -964320, -961300, -958296, -955308, 
    -952335, -949378, -946436, -943510, -940598, -937701, -934819, -931951, 
    -929098, -926259, -923434, -920622, -917825, -915042, -912272, -909515, 
    -906772, -904042, -901324, -898620, -895929, -893250, -890584, -887930, 
    -885288, -882659, -880042, -877437, -874843, -872262, -869692, -867134, 
    -864587, -862051, -859527, -857013, -854511, -852020, -849540, -847070, 
    -844611, -842163, -839725, -837298, -834880, -832474, -830077, -827690, 
    -825313, -822946, -820589, -818242, -815904, -813576, -811257, -808948, 
    -806648, -804357, -802076, -799803, -797540, -795286, -793040, -790803, 
    -788575, -786356, -784146, -781943, -779750, -777565, -775388, -773219, 
    -771059, -768907, -766763, -764627, -762499, -760379, -758267, -756162, 
    -754066, -751977, -749895, -747822, -745755, -743697, -741645, -739601, 
    -737565, -735535, -733513, -731498, -729490, -727489, -725496, -723509, 
    -721529, -719556, -717589, -715630, -713677, -711731, -709791, -707859, 
    -705932, -704012, -702099, -700192, -698291, -696397, -694509, -692627, 
    -690752, -688882, -687019, -685162, -683310, -681465, -679626, -677793, 
    -675965, -674144, -672328, -670518, -668714, -666915, -665122, -663335, 
    -661554, -659777, -658007, -656242, -654482, -652728, -650979, -649236, 
    -647498, -645765, -644037, -642315, -640598, -638886, -637179, -635477, 
    -633780, -632089, -630402, -628720, -627044, -625372, -623705, -622043, 
    -620386, -618733, -617086, -615443, -613805, -612172, -610543, -608919, 
    -607299, -605684, -604074, -602468, -600867, -599270, -597678, -596090, 
    -594507, -592928, -591353, -589783, -588217, -586655, -585098, -583545, 
    -581996, -580451, -578910, -577374, -575842, -574313, -572789, -571269, 
    -569753, -568242, -566734, -565230, -563730, -562234, -560742, -559253, 
    -557769, -556289, -554812, -553339, -551870, -550405, -548943, -547486, 
    -546032, -544581, -543135, -541692, -540253, -538817, -537385, -535956, 
    -534532, -533110, -531693, -530278, -528868, -527460, -526056, -524656, 
    -523259, -521866, -520476, -519089, -517706, -516326, -514949, -513576, 
    -512206, -510839, -509476, -508115, -506758, -505405, -504054, -502707, 
    -501363, -500022, -498684, -497349, -496018, -494689, -493364, -492042, 
    -490722, -489406, -488093, -486783, -485476, -484172, -482871, -481573, 
    -480277, -478985, -477696, -476409, -475126, -473845, -472568, -471293, 
    -470021, -468751, -467485, -466221, -464961, -463703, -462447, -461195, 
    -459945, -458698, -457454, -456213, -454974, -453738, -452504, -451273, 
    -450045, -448820, -447597, -446377, -445159, -443944, -442732, -441522, 
    -440314, -439110, -437908, -436708, -435511, -434316, -433124, -431934, 
    -430747, -429562, -428380, -427201, -426023, -424848, -423676, -422506, 
    -421338, -420173, -419010, -417849, -416691, -415535, -414382, -413231, 
    -412082, -410935, -409791, -408649, -407510, -406372, -405237, -404105, 
    -402974, -401846, -400720, -399596, -398474, -397355, -396237, -395122, 
    -394009, -392899, -391790, -390684, -389579, -388477, -387377, -386280, 
    -385184, -384090, -382999, -381909, -380822, -379736, -378653, -377572, 
    -376493, -375416, -374341, -373268, -372197, -371128, -370061, -368996, 
    -367933, -366872, -365813, -364756, -363701, -362647, -361596, -360547, 
    -359499, -358454, -357411, -356369, -355329, -354291, -353256, -352221, 
    -351189, -350159, -349131, -348104, -347079, -346056, -345035, -344016, 
    -342999, -341983, -340969, -339957, -338947, -337939, -336932, -335927, 
    -334924, -333923, -332923, -331926, -330930, -329935, -328943, -327952, 
    -326963, -325975, -324990, -324006, -323023, -322043, -321064, -320087, 
    -319111, -318137, -317165, -316194, -315225, -314258, -313293, -312329, 
    -311366, -310405, -309446, -308489, -307533, -306579, -305626, -304675, 
    -303725, -302777, -301831, -300886, -299943, -299001, -298061, -297123, 
    -296186, -295250, -294316, -293384, -292453, -291523, -290596, -289669, 
    -288744, -287821, -286899, -285979, -285060, -284143, -283227, -282312, 
    -281399, -280488, -279578, -278669, -277762, -276856, -275952, -275049, 
    -274148, -273248, -272349, -271452, -270556, -269662, -268769, -267878, 
    -266987, -266099, -265211, -264325, -263441, -262558, -261676, -260795, 
    -259916, -259038, -258162, -257287, -256413, -255541, -254670, -253800, 
    -252931, -252064, -251199, -250334, -249471, -248609, -247749, -246889, 
    -246032, -245175, -244320, -243465, -242613, -241761, -240911, -240062, 
    -239214, -238368, -237523, -236679, -235836, -234995, -234154, -233315, 
    -232478, -231641, -230806, -229972, -229139, -228307, -227477, -226648, 
    -225820, -224993, -224167, -223343, -222520, -221698, -220877, -220057, 
    -219239, -218422, -217606, -216791, -215977, -215164, -214353, -213542, 
    -212733, -211925, -211118, -210313, -209508, -208705, -207902, -207101, 
    -206301, -205502, -204704, -203908, -203112, -202318, -201524, -200732, 
    -199941, -199151, -198362, -197574, -196787, -196001, -195217, -194433, 
    -193651, -192869, -192089, -191310, -190532, -189755, -188979, -188204, 
    -187430, -186657, -185885, -185114, -184344, -183576, -182808, -182041, 
    -181276, -180511, -179747, -178985, -178223, -177463, -176703, -175945, 
    -175187, -174431, -173676, -172921, -172168, -171415, -170664, -169913, 
    -169164, -168415, -167668, -166921, -166176, -165431, -164687, -163945, 
    -163203, -162462, -161723, -160984, -160246, -159509, -158773, -158038, 
    -157304, -156571, -155839, -155108, -154377, -153648, -152920, -152192, 
    -151466, -150740, -150015, -149292, -148569, -147847, -147126, -146406, 
    -145687, -144968, -144251, -143534, -142819, -142104, -141390, -140678, 
    -139966, -139254, -138544, -137835, -137126, -136419, -135712, -135006, 
    -134301, -133597, -132894, -132192, -131490, -130790, -130090, -129391, 
    -128693, -127996, -127300, -126604, -125910, -125216, -124523, -123831, 
    -123140, -122449, -121760, -121071, -120383, -119696, -119010, -118324, 
    -117640, -116956, -116273, -115591, -114909, -114229, -113549, -112870, 
    -112192, -111515, -110839, -110163, -109488, -108814, -108141, -107468, 
    -106797, -106126, -105456, -104786, -104118, -103450, -102783, -102117, 
    -101452, -100787, -100123, -99460, -98798, -98136, -97476, -96816, 
    -96156, -95498, -94840, -94183, -93527, -92872, -92217, -91563, 
    -90910, -90258, -89606, -88955, -88305, -87655, -87007, -86359, 
    -85711, -85065, -84419, -83774, -83130, -82486, -81843, -81201, 
    -80560, -79919, -79279, -78640, -78001, -77364, -76727, -76090, 
    -75455, -74820, -74185, -73552, -72919, -72287, -71655, -71025, 
    -70395, -69765, -69137, -68509, -67881, -67255, -66629, -66004, 
    -65379, -64755, -64132, -63510, -62888, -62267, -61647, -61027, 
    -60408, -59789, -59172, -58555, -57938, -57322, -56707, -56093, 
    -55479, -54866, -54254, -53642, -53031, -52420, -51811, -51201, 
    -50593, -49985, -49378, -48771, -48166, -47560, -46956, -46352, 
    -45748, -45146, -44544, -43942, -43341, -42741, -42142, -41543, 
    -40945, -40347, -39750, -39154, -38558, -37963, -37368, -36774, 
    -36181, -35588, -34996, -34405, -33814, -33224, -32634, -32046, 
    -31457, -30869, -30282, -29696, -29110, -28524, -27940, -27356, 
    -26772, -26189, -25607, -25025, -24444, -23863, -23283, -22704, 
    -22125, -21547, -20969, -20392, -19816, -19240, -18665, -18090, 
    -17516, -16942, -16369, -15797, -15225, -14654, -14083, -13513, 
    -12944, -12375, -11806, -11239, -10671, -10105, -9538, -8973, 
    -8408, -7843, -7280, -6716, -6153, -5591, -5030, -4469, 
    -3908, -3348, -2789, -2230, -1671, -1113, -556, 0
};

PROGMEM const int32_t db_to_linear_table[LOG_TABLE_SIZE] = {
    65536, 65550, 65565, 65580, 65595, 65609, 65624, 65639, 
    65654, 65668, 65683, 65698, 65713, 65728, 65742, 65757, 
    65772, 65787, 65802, 65816, 65831, 65846, 65861, 65876, 
    65890, 65905, 65920, 65935, 65950, 65965, 65980, 65994, 
    66009, 66024, 66039, 66054, 66069, 66084, 66098, 66113, 
    66128, 66143, 66158, 66173, 66188, 66203, 66218, 66232, 
    66247, 66262, 66277, 66292, 66307, 66322, 66337, 66352, 
    66367, 66382, 66397, 66412, 66427, 66442, 66456, 66471, 
    66486, 66501, 66516, 66531, 66546, 66561, 66576, 66591, 
    66606, 66621, 66636, 66651, 66666, 66681, 66696, 66711, 
    66726, 66741, 66756, 66771, 66786, 66801, 66816, 66831, 
    66847, 66862, 66877, 66892, 66907, 66922, 66937, 66952, 
    66967, 66982, 66997, 67012, 67027, 67042, 67058, 67073, 
    67088, 67103, 67118, 67133, 67148, 67163, 67178, 67193, 
    67209, 67224, 67239, 67254, 67269, 67284, 67299, 67315, 
    67330, 67345, 67360, 67375, 67390, 67406, 67421, 67436, 
    67451, 67466, 67481, 67497, 67512, 67527, 67542, 67557, 
    67573, 67588, 67603, 67618, 67634, 67649, 67664, 67679, 
    67694, 67710, 67725, 67740, 67755, 67771, 67786, 67801, 
    67816, 67832, 67847, 67862, 67878, 67893, 67908, 67923, 
    67939, 67954, 67969, 67985, 68000, 68015, 68030, 68046, 
    68061, 68076, 68092, 68107, 68122, 68138, 68153, 68168, 
    68184, 68199, 68214, 68230, 68245, 68261, 68276, 68291, 
    68307, 68322, 68337, 68353, 68368, 68384, 68399, 68414, 
    68430, 68445, 68461, 68476, 68491, 68507, 68522, 68538, 
    68553, 68569, 68584, 68599, 68615, 68630, 68646, 68661, 
    68677, 68692, 68708, 68723, 68739, 68754, 68769, 68785, 
    68800, 68816, 68831, 68847, 68862, 68878, 68893, 68909, 
    68924, 68940, 68955, 68971, 68987, 69002, 69018, 69033, 
    69049, 69064, 69080, 69095, 69111, 69126, 69142, 69158, 
    69173, 69189, 69204, 69220, 69235, 69251, 69267, 69282, 
    69298, 69313, 69329, 69345, 69360, 69376, 69391, 69407, 
    69423, 69438, 69454, 69470, 69485, 69501, 69516, 69532, 
    69548, 69563, 69579, 69595, 69610, 69626, 69642, 69657, 
    69673, 69689, 69704, 69720, 69736, 69752, 69767, 69783, 
    69799, 69814, 69830, 69846, 69862, 69877, 69893, 69909, 
    69924, 69940, 69956, 69972, 69987, 70003, 70019, 70035, 
    70051, 70066, 70082, 70098, 70114, 70129, 70145, 70161, 
    70177, 70193, 70208, 70224, 70240, 70256, 70272, 70287, 
    70303, 70319, 70335, 70351, 70367, 70382, 70398, 70414, 
    70430, 70446, 70462, 70478, 70493, 70509, 70525, 70541, 
    70557, 70573, 70589, 70605, 70620, 70636, 70652, 70668, 
    70684, 70700, 70716, 70732, 70748, 70764, 70780, 70796, 
    70811, 70827, 70843, 70859, 70875, 70891, 70907, 70923, 
    70939, 70955, 70971, 70987, 71003, 71019, 71035, 71051, 
    71067, 71083, 71099, 71115, 71131, 71147, 71163, 71179, 
    71195, 71211, 71227, 71243, 71259, 71275, 71291, 71307, 
    71323, 71339, 71355, 71371, 71388, 71404, 71420, 71436, 
    71452, 71468, 71484, 71500, 71516, 71532, 71548, 71565, 
    71581, 71597, 71613, 71629, 71645, 71661, 71677, 71694, 
    71710, 71726, 71742, 71758, 71774, 71790, 71807, 71823, 
    71839, 71855, 71871, 71887, 71904, 71920, 71936, 71952, 
    71968, 71985, 72001, 72017, 72033, 72049, 72066, 72082, 
    72098, 72114, 72131, 72147, 72163, 72179, 72196, 72212, 
    72228, 72244, 72261, 72277, 72293, 72309, 72326, 72342, 
    72358, 72374, 72391, 72407, 72423, 72440, 72456, 72472, 
    72489, 72505, 72521, 72538, 72554, 72570, 72587, 72603, 
    72619, 72636, 72652, 72668, 72685, 72701, 72717, 72734, 
    72750, 72767, 72783, 72799, 72816, 72832, 72848, 72865, 
    72881, 72898, 72914, 72930, 72947, 72963, 72980, 72996, 
    73013, 73029, 73045, 73062, 73078, 73095, 73111, 73128, 
    73144, 73161, 73177, 73194, 73210, 73227, 73243, 73260, 
    73276, 73293, 73309, 73326, 73342, 73359, 73375, 73392, 
    73408, 73425, 73441, 73458, 73474, 73491, 73507, 73524, 
    73540, 73557, 73573, 73590, 73607, 73623, 73640, 73656, 
    73673, 73690, 73706, 73723, 73739, 73756, 73772, 73789, 
    73806, 73822, 73839, 73856, 73872, 73889, 73905, 73922, 
    73939, 73955, 73972, 73989, 74005, 74022, 74039, 74055, 
    74072, 74089, 74105, 74122, 74139, 74155, 74172, 74189, 
    74205, 74222, 74239, 74256, 74272, 74289, 74306, 74322, 
    74339, 74356, 74373, 74389, 74406, 74423, 74440, 74456, 
    74473, 74490, 74507, 74524, 74540, 74557, 74574, 74591, 
    74607, 74624, 74641, 74658, 74675, 74691, 74708, 74725, 
    74742, 74759, 74776, 74792, 74809, 74826, 74843, 74860, 
    74877, 74893, 74910, 74927, 74944, 74961, 74978, 74995, 
    75012, 75028, 75045, 75062, 75079, 75096, 75113, 75130, 
    75147, 75164, 75181, 75197, 75214, 75231, 75248, 75265, 
    75282, 75299, 75316, 75333, 75350, 75367, 75384, 75401, 
    75418, 75435, 75452, 75469, 75486, 75503, 75520, 75537, 
    75554, 75571, 75588, 75605, 75622, 75639, 75656, 75673, 
    75690, 75707, 75724, 75741, 75758, 75775, 75792, 75809, 
    75826, 75843, 75860, 75878, 75895, 75912, 75929, 75946, 
    75963, 75980, 75997, 76014, 76031, 76049, 76066, 76083, 
    76100, 76117, 76134, 76151, 76168, 76186, 76203, 76220, 
    76237, 76254, 76271, 76289, 76306, 76323, 76340, 76357, 
    76374, 76392, 76409, 76426, 76443, 76460, 76478, 76495, 
    76512, 76529, 76547, 76564, 76581, 76598, 76616, 76633, 
    76650, 76667, 76685, 76702, 76719, 76736, 76754, 76771, 
    76788, 76805, 76823, 76840, 76857, 76875, 76892, 76909, 
    76927, 76944, 76961, 76979, 76996, 77013, 77031, 77048, 
    77065, 77083, 77100, 77117, 77135, 77152, 77169, 77187, 
    77204, 77221, 77239, 77256, 77274, 77291, 77308, 77326, 
    77343, 77361, 77378, 77395, 77413, 77430, 77448, 77465, 
    77483, 77500, 77518, 77535, 77552, 77570, 77587, 77605, 
    77622, 77640, 77657, 77675, 77692, 77710, 77727, 77745, 
    77762, 77780, 77797, 77815, 77832, 77850, 77867, 77885, 
    77902, 77920, 77937, 77955, 77973, 77990, 78008, 78025, 
    78043, 78060, 78078, 78095, 78113, 78131, 78148, 78166, 
    78183, 78201, 78219, 78236, 78254, 78271, 78289, 78307, 
    78324, 78342, 78360, 78377, 78395, 78412, 78430, 78448, 
    78465, 78483, 78501, 78518, 78536, 78554, 78572, 78589, 
    78607, 78625, 78642, 78660, 78678, 78695, 78713, 78731, 
    78749, 78766, 78784, 78802, 78819, 78837, 78855, 78873, 
    78890, 78908, 78926, 78944, 78962, 78979, 78997, 79015, 
    79033, 79050, 79068, 79086, 79104, 79122, 79139, 79157, 
    79175, 79193, 79211, 79229, 79246, 79264, 79282, 79300, 
    79318, 79336, 79354, 79371, 79389, 79407, 79425, 79443, 
    79461, 79479, 79497, 79514, 79532, 79550, 79568, 79586, 
    79604, 79622, 79640, 79658, 79676, 79694, 79712, 79729, 
    79747, 79765, 79783, 79801, 79819, 79837, 79855, 79873, 
    79891, 79909, 79927, 79945, 79963, 79981, 79999, 80017, 
    80035, 80053, 80071, 80089, 80107, 80125, 80143, 80161, 
    80179, 80197, 80215, 80234, 80252, 80270, 80288, 80306, 
    80324, 80342, 80360, 80378, 80396, 80414, 80432, 80451, 
    80469, 80487, 80505, 80523, 80541, 80559, 80577, 80596, 
    80614, 80632, 80650, 80668, 80686, 80704, 80723, 80741, 
    80759, 80777, 80795, 80814, 80832, 80850, 80868, 80886, 
    80905, 80923, 80941, 80959, 80977, 80996, 81014, 81032, 
    81050, 81069, 81087, 81105, 81123, 81142, 81160, 81178, 
    81196, 81215, 81233, 81251, 81270, 81288, 81306, 81324, 
    81343, 81361, 81379, 81398, 81416, 81434, 81453, 81471, 
    81489, 81508, 81526, 81544, 81563, 81581, 81599, 81618, 
    81636, 81655, 81673, 81691, 81710, 81728, 81747, 81765, 
    81783, 81802, 81820, 81839, 81857, 81875, 81894, 81912, 
    81931, 81949, 81968, 81986, 82005, 82023, 82041, 82060, 
    82078, 82097, 82115, 82134, 82152, 82171, 82189, 82208, 
    82226, 82245, 82263, 82282, 82300, 82319, 82337, 82356, 
    82375, 82393, 82412, 82430, 82449, 82467, 82486, 82504
};
#endif
