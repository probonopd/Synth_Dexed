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

#include "synth.h"
#include "dexed.h"
#include "fm_core.h"
#include "exp2.h"
#include "sin.h"
#include "freqlut.h"
#include "controllers.h"
#include "PluginFx.h"
#include <unistd.h>
#include <limits.h>
#include "porta.h"
#ifdef USE_TEENSY_DSP
#include <Audio.h>
#endif

Dexed::Dexed(uint8_t maxnotes, int rate)
{
  Exp2::init();
  Tanh::init();
  Sin::init();

  Freqlut::init(rate);
  Lfo::init(rate);
  PitchEnv::init(rate);
  Env::init_sr(rate);
  Porta::init_sr(rate);
  fx.init(rate);

  engineMsfa = new FmCore;
  max_notes=maxnotes;
  currentNote = 0;
  resetControllers();
  controllers.masterTune = 0;
  controllers.opSwitch = 0x3f; // enable all operators
  lastKeyDown = -1;
  vuSignal = 0.0;
  controllers.core = engineMsfa;
  lfo.reset(data + 137);
  sustain = false;
  voices=NULL;

  setMaxNotes(max_notes);
  setMonoMode(false);
  loadInitVoice();

  xrun = 0;
  render_time_max = 0;
}

Dexed::~Dexed()
{
  currentNote = -1;

  for (uint8_t note = 0; note < max_notes; note++)
    delete voices[note].dx7_note;

  for (uint8_t note = 0; note < max_notes; note++)
    delete &voices[note];

  delete(engineMsfa);
}

void Dexed::setMaxNotes(uint8_t new_max_notes)
{
  uint8_t i=0;
  
  max_notes=constrain(max_notes,0,_MAX_NOTES);

#ifdef DEBUG
  Serial.print("Allocating memory for ");
  Serial.print(max_notes,DEC);
  Serial.println(" notes.");
  Serial.println();
#endif

  if(voices)
  {
    panic();
    for (i = 0; i < max_notes; i++)
    {
      if(voices[i].dx7_note)
    	delete voices[i].dx7_note;
    }
    delete(voices);
  }

  max_notes=constrain(new_max_notes,0,_MAX_NOTES);

  if(max_notes>0)
  {
    voices=new ProcessorVoice[max_notes]; // sizeof(ProcessorVoice) = 20
    for (i = 0; i < max_notes; i++)
    {
      voices[i].dx7_note = new Dx7Note; // sizeof(Dx7Note) = 692
      voices[i].keydown = false;
      voices[i].sustained = false;
      voices[i].live = false;
      voices[i].key_pressed_timer = 0;
    }
  }
  else
     voices=NULL;
}

void Dexed::activate(void)
{
  panic();
  controllers.refresh();
}

void Dexed::deactivate(void)
{
  panic();
}

void Dexed::getSamples(uint16_t n_samples, int16_t* buffer)
{
  uint16_t i, j;
  uint8_t note;
  float sumbuf[n_samples];
#ifdef USE_SIMPLE_COMPRESSOR
  float s;
  const double decayFactor = 0.99992;
#endif

  if (refreshVoice)
  {
    for (i = 0; i < max_notes; i++)
    {
      if ( voices[i].live )
        voices[i].dx7_note->update(data, voices[i].midi_note, voices[i].velocity, voices[i].porta, &controllers);
    }
    lfo.reset(data + 137);
    refreshVoice = false;
  }

  for (i = 0; i < n_samples; i += _N_)
  {
    AlignedBuf<int32_t, _N_> audiobuf;

    for (uint8_t j = 0; j < _N_; ++j)
    {
      audiobuf.get()[j] = 0;
      sumbuf[i + j] = 0.0;
    }

    int32_t lfovalue = lfo.getsample();
    int32_t lfodelay = lfo.getdelay();

    for (note = 0; note < max_notes; note++)
    {
      if (voices[note].live)
      {
        voices[note].dx7_note->compute(audiobuf.get(), lfovalue, lfodelay, &controllers);

        for (j = 0; j < _N_; ++j)
        {
          sumbuf[i + j] += signed_saturate_rshift(audiobuf.get()[j] >> 4, 24, 9) / 32768.0;
          audiobuf.get()[j] = 0;
          /*
                    int32_t val = audiobuf.get()[j];
                    val = val >> 4;
                    int32_t clip_val = val < -(1 << 24) ? 0x8000 : val >= (1 << 24) ? 0x7fff : val >> 9;
                    float f = ((float) clip_val) / (float) 0x8000;
                    if ( f > 1.0 ) f = 1.0;
                    if ( f < -1.0 ) f = -1.0;
                    sumbuf[j] += f;
                    audiobuf.get()[j] = 0;
          */
        }
      }
    }
  }

  fx.process(sumbuf, n_samples); // Needed for fx.Gain()!!!

#ifdef USE_SIMPLE_COMPRESSOR
  // mild compression
  for (i = 0; i < n_samples; i++)
  {
    s = abs(sumbuf[i]);
    if (s > vuSignal)
      vuSignal = s;
    //else if (vuSignal > 0.001f)
    else if (vuSignal > 0.0005f)
      vuSignal *= decayFactor;
    else
      vuSignal = 0.0;
  }
#endif

  //arm_scale_f32(sumbuf, 0.00015, sumbuf, AUDIO_BLOCK_SAMPLES);
  arm_float_to_q15(sumbuf, buffer, AUDIO_BLOCK_SAMPLES);
}

void Dexed::keydown(int16_t pitch, uint8_t velo) {
  if ( velo == 0 ) {
    keyup(pitch);
    return;
  }

  pitch += data[144] - TRANSPOSE_FIX;

  int previousKeyDown = lastKeyDown;
  lastKeyDown = pitch;

  int porta = -1;
  if ( controllers.portamento_enable_cc && previousKeyDown >= 0 )
    porta = controllers.portamento_cc;

  uint8_t note = currentNote;
  uint8_t keydown_counter = 0;

  if (!monoMode && refreshMode)
  {
    for (uint8_t i = 0; i < max_notes; i++)
    {
      if (voices[i].midi_note == pitch && voices[i].keydown == false && voices[i].live && voices[i].sustained == true)
      {
        // retrigger or refresh note?
        voices[i].dx7_note->keyup();
        voices[i].midi_note = pitch;
        voices[i].velocity = velo;
        voices[i].keydown = true;
        voices[i].sustained = sustain;
        voices[i].live = true;
        voices[i].dx7_note->init(data, pitch, velo, pitch, porta, &controllers);
        voices[i].key_pressed_timer = millis();
        return;
      }
    }
  }

  for (uint8_t i = 0; i <= max_notes; i++)
  {
    if (i == max_notes)
    {
      uint32_t min_timer = 0xffff;

      if (monoMode)
        break;

      // no free sound slot found, so use the oldest note slot
      for (uint8_t n = 0; n < max_notes; n++)
      {
        if (voices[n].key_pressed_timer < min_timer)
        {
          min_timer = voices[n].key_pressed_timer;
          note = n;
        }
      }
      voices[note].keydown = false;
      voices[note].sustained = false;
      voices[note].live = false;
      voices[note].key_pressed_timer = 0;
      keydown_counter--;
    }

    if (!voices[note].keydown)
    {
      currentNote = (note + 1) % max_notes;
      voices[note].midi_note = pitch;
      voices[note].velocity = velo;
      voices[note].sustained = sustain;
      voices[note].keydown = true;
      int srcnote = (previousKeyDown >= 0) ? previousKeyDown : pitch;
      voices[note].dx7_note->init(data, pitch, velo, srcnote, porta, &controllers);
      if ( data[136] )
        voices[note].dx7_note->oscSync();
      voices[i].key_pressed_timer = millis();
      keydown_counter++;
      break;
    }
    else
    {
      keydown_counter++;
    }
    note = (note + 1) % max_notes;
  }


  if (keydown_counter == 0)
    lfo.keydown();

  if ( monoMode ) {
    for (uint8_t i = 0; i < max_notes; i++) {
      if ( voices[i].live ) {
        // all keys are up, only transfer signal
        if ( ! voices[i].keydown ) {
          voices[i].live = false;
          voices[note].dx7_note->transferSignal(*voices[i].dx7_note);
          break;
        }
        if ( voices[i].midi_note < pitch ) {
          voices[i].live = false;
          voices[note].dx7_note->transferState(*voices[i].dx7_note);
          break;
        }
        return;
      }
    }
  }

  voices[note].live = true;
}

void Dexed::keyup(int16_t pitch) {
  uint8_t note;

  pitch = constrain(pitch, 0, 127);

  pitch += data[144] - TRANSPOSE_FIX;

  for (note = 0; note < max_notes; note++) {
    if ( voices[note].midi_note == pitch && voices[note].keydown ) {
      voices[note].keydown = false;
      voices[note].key_pressed_timer = 0;

      break;
    }
  }

  // note not found ?
  if ( note >= max_notes ) {
    return;
  }

  if ( monoMode ) {
    int16_t highNote = -1;
    uint8_t target = 0;
    for (int8_t i = 0; i < max_notes; i++) {
      if ( voices[i].keydown && voices[i].midi_note > highNote ) {
        target = i;
        highNote = voices[i].midi_note;
      }
    }

    if ( highNote != -1 && voices[note].live ) {
      voices[note].live = false;
      voices[note].key_pressed_timer = 0;
      voices[target].live = true;
      voices[target].dx7_note->transferState(*voices[note].dx7_note);
    }
  }

  if ( sustain ) {
    voices[note].sustained = true;
  } else {
    voices[note].dx7_note->keyup();
  }
}

void Dexed::doRefreshVoice(void)
{
  refreshVoice = true;
}

void Dexed::setOPAll(uint8_t ops)
{
  controllers.opSwitch = ops;
}

bool Dexed::getMonoMode(void) {
  return monoMode;
}

void Dexed::setMonoMode(bool mode) {
  if (monoMode == mode)
    return;

  notesOff();
  monoMode = mode;
}

void Dexed::setRefreshMode(bool mode) {
  refreshMode = mode;
}

void Dexed::setSustain(bool s)
{
  if (sustain == s)
    return;

  sustain = s;
}

bool Dexed::getSustain(void)
{
  return sustain;
}

void Dexed::panic(void)
{
  for (uint8_t i = 0; i < max_notes; i++)
  {
    if (voices[i].live == true) {
      voices[i].keydown = false;
      voices[i].live = false;
      voices[i].sustained = false;
      voices[i].key_pressed_timer = 0;
      if ( voices[i].dx7_note != NULL ) {
        voices[i].dx7_note->oscSync();
      }
    }
  }
  setSustain(0);
}

void Dexed::resetControllers(void)
{
  controllers.values_[kControllerPitch] = 0x2000;
  controllers.values_[kControllerPitchRange] = 0;
  controllers.values_[kControllerPitchStep] = 0;
  controllers.values_[kControllerPortamentoGlissando] = 0;

  controllers.modwheel_cc = 0;
  controllers.foot_cc = 0;
  controllers.breath_cc = 0;
  controllers.aftertouch_cc = 0;
  controllers.portamento_enable_cc = false;
  controllers.portamento_cc = 0;
  controllers.refresh();
}

void Dexed::notesOff(void) {
  for (uint8_t i = 0; i < max_notes; i++) {
    if (voices[i].live == true) {
      voices[i].keydown = false;
      voices[i].live = false;
    }
  }
}

uint8_t Dexed::getMaxNotes(void)
{
  return max_notes;
}

uint8_t Dexed::getNumNotesPlaying(void)
{
  uint8_t op_carrier = controllers.core->get_carrier_operators(data[134]); // look for carriers
  uint8_t i;
  uint8_t count_playing_voices = 0;

  for (i = 0; i < max_notes; i++)
  {
    if (voices[i].live == true)
    {
      uint8_t op_amp = 0;
      uint8_t op_carrier_num = 0;

      memset(&voiceStatus, 0, sizeof(VoiceStatus));
      voices[i].dx7_note->peekVoiceStatus(voiceStatus);

      for (uint8_t op = 0; op < 6; op++)
      {
        if ((op_carrier & (1 << op)))
        {
          // this voice is a carrier!
          op_carrier_num++;
          if (voiceStatus.amp[op] <= VOICE_SILENCE_LEVEL && voiceStatus.ampStep[op] == 4)
          {
            // this voice produces no audio output
            op_amp++;
          }
        }
      }

      if (op_amp == op_carrier_num)
      {
        // all carrier-operators are silent -> disable the voice
        voices[i].live = false;
        voices[i].sustained = false;
        voices[i].keydown = false;
#ifdef DEBUG
        Serial.print(F("Shutdown voice: "));
        Serial.println(i, DEC);
#endif
      }
      else
        count_playing_voices++;
    }
  }
  return (count_playing_voices);
}

bool Dexed::decodeVoice(uint8_t* new_data, uint8_t* encoded_data)
{
  uint8_t* p_data = new_data;
  uint8_t op;
  uint8_t tmp;
  char dexed_voice_name[11];

  panic();

  for (op = 0; op < 6; op++)
  {
    //  DEXED_OP_EG_R1,           // 0
    //  DEXED_OP_EG_R2,           // 1
    //  DEXED_OP_EG_R3,           // 2
    //  DEXED_OP_EG_R4,           // 3
    //  DEXED_OP_EG_L1,           // 4
    //  DEXED_OP_EG_L2,           // 5
    //  DEXED_OP_EG_L3,           // 6
    //  DEXED_OP_EG_L4,           // 7
    //  DEXED_OP_LEV_SCL_BRK_PT,  // 8
    //  DEXED_OP_SCL_LEFT_DEPTH,  // 9
    //  DEXED_OP_SCL_RGHT_DEPTH,  // 10
    memcpy(&new_data[op * 21], &encoded_data[op * 17], 11);
    tmp = encoded_data[(op * 17) + 11];
    *(p_data + DEXED_OP_SCL_LEFT_CURVE + (op * 21)) = (tmp & 0x03);
    *(p_data + DEXED_OP_SCL_RGHT_CURVE + (op * 21)) = (tmp & 0x0c) >> 2;
    tmp = encoded_data[(op * 17) + 12];
    *(p_data + DEXED_OP_OSC_DETUNE + (op * 21)) = (tmp & 0x78) >> 3;
    *(p_data + DEXED_OP_OSC_RATE_SCALE + (op * 21)) = (tmp & 0x07);
    tmp = encoded_data[(op * 17) + 13];
    *(p_data + DEXED_OP_KEY_VEL_SENS + (op * 21)) = (tmp & 0x1c) >> 2;
    *(p_data + DEXED_OP_AMP_MOD_SENS + (op * 21)) = (tmp & 0x03);
    *(p_data + DEXED_OP_OUTPUT_LEV + (op * 21)) = encoded_data[(op * 17) + 14];
    tmp = encoded_data[(op * 17) + 15];
    *(p_data + DEXED_OP_FREQ_COARSE + (op * 21)) = (tmp & 0x3e) >> 1;
    *(p_data + DEXED_OP_OSC_MODE + (op * 21)) = (tmp & 0x01);
    *(p_data + DEXED_OP_FREQ_FINE + (op * 21)) = encoded_data[(op * 17) + 16];
  }
  //  DEXED_PITCH_EG_R1,        // 0
  //  DEXED_PITCH_EG_R2,        // 1
  //  DEXED_PITCH_EG_R3,        // 2
  //  DEXED_PITCH_EG_R4,        // 3
  //  DEXED_PITCH_EG_L1,        // 4
  //  DEXED_PITCH_EG_L2,        // 5
  //  DEXED_PITCH_EG_L3,        // 6
  //  DEXED_PITCH_EG_L4,        // 7
  memcpy(&new_data[DEXED_VOICE_OFFSET], &encoded_data[102], 8);
  tmp = encoded_data[110];
  *(p_data + DEXED_VOICE_OFFSET + DEXED_ALGORITHM) = (tmp & 0x1f);
  tmp = encoded_data[111];
  *(p_data + DEXED_VOICE_OFFSET + DEXED_OSC_KEY_SYNC) = (tmp & 0x08) >> 3;
  *(p_data + DEXED_VOICE_OFFSET + DEXED_FEEDBACK) = (tmp & 0x07);
  //  DEXED_LFO_SPEED,          // 11
  //  DEXED_LFO_DELAY,          // 12
  //  DEXED_LFO_PITCH_MOD_DEP,  // 13
  //  DEXED_LFO_AMP_MOD_DEP,    // 14
  memcpy(&new_data[DEXED_VOICE_OFFSET + DEXED_LFO_SPEED], &encoded_data[112], 4);
  tmp = encoded_data[116];
  *(p_data + DEXED_VOICE_OFFSET + DEXED_LFO_PITCH_MOD_SENS) = (tmp & 0x30) >> 4;
  *(p_data + DEXED_VOICE_OFFSET + DEXED_LFO_WAVE) = (tmp & 0x0e) >> 1;
  *(p_data + DEXED_VOICE_OFFSET + DEXED_LFO_SYNC) = (tmp & 0x01);
  *(p_data + DEXED_VOICE_OFFSET + DEXED_TRANSPOSE) = encoded_data[117];
  memcpy(&new_data[DEXED_VOICE_OFFSET + DEXED_NAME], &encoded_data[118], 10);
  panic();
  doRefreshVoice();

  strncpy(dexed_voice_name, (char *)&encoded_data[118], sizeof(dexed_voice_name) - 1);
  dexed_voice_name[10] = '\0';
#ifdef DEBUG
  Serial.print(F("Voice ["));
  Serial.print(dexed_voice_name);
  Serial.println(F("] decoded."));
#endif

  return (true);
}

bool Dexed::encodeVoice(uint8_t* encoded_data)
{
  uint8_t* p_data = data;
  uint8_t op;

  for (op = 0; op < 6; op++)
  {
    //  DEXED_OP_EG_R1,           // 0
    //  DEXED_OP_EG_R2,           // 1
    //  DEXED_OP_EG_R3,           // 2
    //  DEXED_OP_EG_R4,           // 3
    //  DEXED_OP_EG_L1,           // 4
    //  DEXED_OP_EG_L2,           // 5
    //  DEXED_OP_EG_L3,           // 6
    //  DEXED_OP_EG_L4,           // 7
    //  DEXED_OP_LEV_SCL_BRK_PT,  // 8
    //  DEXED_OP_SCL_LEFT_DEPTH,  // 9
    //  DEXED_OP_SCL_RGHT_DEPTH,  // 10
    memcpy(&encoded_data[op * 17], &data[op * 21], 11);
    encoded_data[(op * 17) + 11] = ((*(p_data + DEXED_OP_SCL_RGHT_CURVE + (op * 21)) & 0x0c) << 2) | (*(p_data + DEXED_OP_SCL_LEFT_CURVE + (op * 21)) & 0x03);
    encoded_data[(op * 17) + 12] = ((*(p_data + DEXED_OP_OSC_DETUNE + (op * 21)) & 0x0f) << 3) | (*(p_data + DEXED_OP_OSC_RATE_SCALE + (op * 21)) & 0x07);
    encoded_data[(op * 17) + 13] = ((*(p_data + DEXED_OP_KEY_VEL_SENS + (op * 21)) & 0x07) << 2) | (*(p_data + DEXED_OP_AMP_MOD_SENS + (op * 21)) & 0x03);
    encoded_data[(op * 17) + 14] = *(p_data + DEXED_OP_OUTPUT_LEV + (op * 21));
    encoded_data[(op * 17) + 15] = ((*(p_data + DEXED_OP_FREQ_COARSE + (op * 21)) & 0x1f) << 1) | (*(p_data + DEXED_OP_OSC_MODE + (op * 21)) & 0x01);
    encoded_data[(op * 17) + 16] = *(p_data + DEXED_OP_FREQ_FINE + (op * 21));
  }
  //  DEXED_PITCH_EG_R1,        // 0
  //  DEXED_PITCH_EG_R2,        // 1
  //  DEXED_PITCH_EG_R3,        // 2
  //  DEXED_PITCH_EG_R4,        // 3
  //  DEXED_PITCH_EG_L1,        // 4
  //  DEXED_PITCH_EG_L2,        // 5
  //  DEXED_PITCH_EG_L3,        // 6
  //  DEXED_PITCH_EG_L4,        // 7
  memcpy(&encoded_data[102], &data[DEXED_VOICE_OFFSET], 8);
  encoded_data[110] = (*(p_data + DEXED_VOICE_OFFSET + DEXED_ALGORITHM) & 0x1f);
  encoded_data[111] = (((*(p_data + DEXED_VOICE_OFFSET + DEXED_OSC_KEY_SYNC) & 0x01) << 3) | ((*(p_data + DEXED_VOICE_OFFSET + DEXED_FEEDBACK)) & 0x07));
  //  DEXED_LFO_SPEED,          // 11
  //  DEXED_LFO_DELAY,          // 12
  //  DEXED_LFO_PITCH_MOD_DEP,  // 13
  //  DEXED_LFO_AMP_MOD_DEP,    // 14
  memcpy(&encoded_data[112], &data[DEXED_VOICE_OFFSET + DEXED_LFO_SPEED], 4);
  encoded_data[116] = (((*(p_data + DEXED_VOICE_OFFSET + DEXED_LFO_PITCH_MOD_SENS) & 0x07) << 4) | (((*(p_data + DEXED_VOICE_OFFSET + DEXED_LFO_WAVE)) & 0x07) << 1) | ((*(p_data + DEXED_VOICE_OFFSET + DEXED_LFO_SYNC)) & 0x01));
  encoded_data[117] = *(p_data + DEXED_VOICE_OFFSET + DEXED_TRANSPOSE);
  memset(&encoded_data[118], 0, 10);
  memcpy(&encoded_data[118], &data[DEXED_VOICE_OFFSET + DEXED_NAME], 10);

  return (true);
}

bool Dexed::getVoiceData(uint8_t* data_copy)
{
  memcpy(data_copy, data, sizeof(data));
  return (true);
}

void Dexed::setVoiceDataElement(uint8_t address, uint8_t value)
{
  address = constrain(address, 0, NUM_VOICE_PARAMETERS);
  data[address] = value;
}

uint8_t Dexed::getVoiceDataElement(uint8_t address)
{
  address = constrain(address, 0, NUM_VOICE_PARAMETERS);
  return (data[address]);
}

void Dexed::loadVoiceParameters(uint8_t* new_data)
{
#ifdef DEBUG
  char dexed_voice_name[11];
#endif

  panic();
  memcpy(&data, new_data, 155);
  doRefreshVoice();
#ifdef DEBUG
  strncpy(dexed_voice_name, (char *)&new_data[145], sizeof(dexed_voice_name) - 1);
  dexed_voice_name[10] = '\0';

  Serial.print(F("Voice ["));
  Serial.print(dexed_voice_name);
  Serial.println(F("] loaded."));
#endif
}

void Dexed::loadInitVoice(void)
{
  loadVoiceParameters(init_voice);
}

void Dexed::setPBController(uint8_t pb_range, uint8_t pb_step)
{
#ifdef DEBUG
  Serial.println(F("Dexed::setPBController"));
#endif

  pb_range = constrain(pb_range, 0, 12);
  pb_step = constrain(pb_step, 0, 12);

  controllers.values_[kControllerPitchRange] = pb_range;
  controllers.values_[kControllerPitchStep] = pb_step;

  controllers.refresh();
}

void Dexed::setMWController(uint8_t mw_range, uint8_t mw_assign, uint8_t mw_mode)
{
#ifdef DEBUG
  Serial.println(F("Dexed::setMWController"));
#endif

  mw_range = constrain(mw_range, 0, 99);
  mw_assign = constrain(mw_assign, 0, 7);
  mw_mode = constrain(mw_mode, 0, MIDI_CONTROLLER_MODE_MAX);

  controllers.wheel.setRange(mw_range);
  controllers.wheel.setTarget(mw_assign);
  controllers.wheel.setMode(mw_mode);

  controllers.refresh();
}

void Dexed::setFCController(uint8_t fc_range, uint8_t fc_assign, uint8_t fc_mode)
{
#ifdef DEBUG
  Serial.println(F("Dexed::setFCController"));
#endif

  fc_range = constrain(fc_range, 0, 99);
  fc_assign = constrain(fc_assign, 0, 7);
  fc_mode = constrain(fc_mode, 0, MIDI_CONTROLLER_MODE_MAX);

  controllers.foot.setRange(fc_range);
  controllers.foot.setTarget(fc_assign);
  controllers.foot.setMode(fc_mode);

  controllers.refresh();
}

void Dexed::setBCController(uint8_t bc_range, uint8_t bc_assign, uint8_t bc_mode)
{
#ifdef DEBUG
  Serial.println(F("Dexed::setBCController"));
#endif

  bc_range = constrain(bc_range, 0, 99);
  bc_assign = constrain(bc_assign, 0, 7);
  bc_mode = constrain(bc_mode, 0, MIDI_CONTROLLER_MODE_MAX);

  controllers.breath.setRange(bc_range);
  controllers.breath.setTarget(bc_assign);
  controllers.breath.setMode(bc_mode);

  controllers.refresh();
}

void Dexed::setATController(uint8_t at_range, uint8_t at_assign, uint8_t at_mode)
{
#ifdef DEBUG
  Serial.println(F("Dexed::setATController"));
#endif

  at_range = constrain(at_range, 0, 99);
  at_assign = constrain(at_assign, 0, 7);
  at_mode = constrain(at_mode, 0, MIDI_CONTROLLER_MODE_MAX);

  controllers.at.setRange(at_range);
  controllers.at.setTarget(at_assign);
  controllers.at.setMode(at_mode);

  controllers.refresh();
}

void Dexed::setPortamentoMode(uint8_t portamento_mode, uint8_t portamento_glissando, uint8_t portamento_time)
{
  portamento_mode = constrain(portamento_mode, 0, 1);
  portamento_glissando = constrain(portamento_glissando, 0, 1);
  portamento_mode = constrain(portamento_mode, 0, 99);

  controllers.portamento_cc = portamento_time;
  controllers.portamento_enable_cc = portamento_mode > 63;

  if (portamento_time > 0)
    controllers.portamento_enable_cc = true;
  else
    controllers.portamento_enable_cc = false;

  controllers.values_[kControllerPortamentoGlissando] = portamento_glissando;

  controllers.refresh();
}

uint32_t Dexed::getXRun(void)
{
  return (xrun);
}

uint16_t Dexed::getRenderTimeMax(void)
{
  return (render_time_max);
}

void Dexed::resetRenderTimeMax(void)
{
  render_time_max = 0;
}

void Dexed::ControllersRefresh(void)
{
  controllers.refresh();
}

void Dexed::setMasterTune(int8_t mastertune)
{
  mastertune = constrain(mastertune, -99, 99);

  controllers.masterTune = (int(mastertune / 100.0 * 0x4000) << 11) * (1.0 / 12.0);
}

int8_t Dexed::getMasterTune(void)
{
  return (controllers.masterTune);
}

void Dexed::setModWheel(uint8_t value)
{
  value = constrain(value, 0, 127);

  controllers.modwheel_cc = value;
}

uint8_t Dexed::getModWheel(void)
{
  return (controllers.modwheel_cc);
}

void Dexed::setBreathController(uint8_t value)
{
  value = constrain(value, 0, 127);

  controllers.breath_cc = value;
}

uint8_t Dexed::getBreathController(void)
{
  return (controllers.breath_cc);
}

void Dexed::setFootController(uint8_t value)
{
  value = constrain(value, 0, 127);

  controllers.foot_cc = value;
}

uint8_t Dexed::getFootController(void)
{
  return (controllers.foot_cc);
}

void Dexed::setAftertouch(uint8_t value)
{
  value = constrain(value, 0, 127);

  controllers.aftertouch_cc = value;
}

uint8_t Dexed::getAftertouch(void)
{
  return (controllers.aftertouch_cc);
}

void Dexed::setPitchbend(int16_t value)
{
  value = constrain(value, -8192, 8191);

  controllers.values_[kControllerPitch] = value + 0x2000; // -8192 to +8191 --> 0 to 16383
}

int16_t Dexed::getPitchbend(void)
{
  return (controllers.values_[kControllerPitch] - 0x2000);
}

void Dexed::setPitchbendRange(uint8_t range)
{
  range = constrain(range, 0, 12);

  controllers.values_[kControllerPitchRange] = range;
}

uint8_t Dexed::getPitchbendRange(void)
{
  return (controllers.values_[kControllerPitchRange]);
}

void Dexed::setPitchbendStep(uint8_t step)
{
  step = constrain(step, 0, 12);

  controllers.values_[kControllerPitchStep] = step;
}

uint8_t Dexed::getPitchbendStep(void)
{
  return (controllers.values_[kControllerPitchStep]);
}

void Dexed::setModWheelRange(uint8_t range)
{
  range = constrain(range, 0, 12);

  controllers.wheel.setRange(range);
}

uint8_t Dexed::getModWheelRange(void)
{
  return (controllers.wheel.getRange());
}

void Dexed::setModWheelTarget(uint8_t target)
{
  target = constrain(target, 0, 7);

  controllers.wheel.setTarget(target);
}

uint8_t Dexed::getModWheelTarget(void)
{
  return (controllers.wheel.getTarget());
}

void Dexed::setFootControllerRange(uint8_t range)
{
  range = constrain(range, 0, 12);

  controllers.foot.setRange(range);
}

uint8_t Dexed::getFootControllerRange(void)
{
  return (controllers.foot.getRange());
}

void Dexed::setFootControllerTarget(uint8_t target)
{
  target = constrain(target, 0, 7);

  controllers.foot.setTarget(target);
}

uint8_t Dexed::getFootControllerTarget(void)
{
  return (controllers.foot.getTarget());
}

void Dexed::setBreathControllerRange(uint8_t range)
{
  range = constrain(range, 0, 12);

  controllers.breath.setRange(range);
}

uint8_t Dexed::getBreathControllerRange(void)
{
  return (controllers.breath.getRange());
}

void Dexed::setBreathControllerTarget(uint8_t target)
{
  target = constrain(target, 0, 7);

  controllers.breath.setTarget(target);
}

uint8_t Dexed::getBreathControllerTarget(void)
{
  return (controllers.breath.getTarget());
}

void Dexed::setAftertouchRange(uint8_t range)
{
  range = constrain(range, 0, 12);

  controllers.at.setRange(range);
}

uint8_t Dexed::getAftertouchRange(void)
{
  return (controllers.at.getRange());
}

void Dexed::setAftertouchTarget(uint8_t target)
{
  target = constrain(target, 0, 7);

  controllers.at.setTarget(target);
}

uint8_t Dexed::getAftertouchTarget(void)
{
  return (controllers.at.getTarget());
}

void Dexed::setFilterCutoff(float cutoff)
{
  fx.Cutoff = cutoff;
}

float Dexed::getFilterCutoff(void)
{
  return (fx.Cutoff);
}

void Dexed::setFilterResonance(float resonance)
{
  fx.Reso = resonance;
}

float Dexed::getFilterResonance(void)
{
  return (fx.Reso);
}

void Dexed::setGain(float gain)
{
  fx.Gain = gain;
}

float Dexed::getGain(void)
{
  return (fx.Gain);
}

void Dexed::setOPRateAll(uint8_t rate)
{
  rate = constrain(rate, 0, 99);

  for (uint8_t op = 0; op < 6; op++)
  {
    for (uint8_t step = 0; step < 4; step++)
    {
      data[(op * 21) + DEXED_OP_EG_R1 + step] = rate;
    }
  }
}

void Dexed::setOPLevelAll(uint8_t level)
{
  level = constrain(level, 0, 99);

  for (uint8_t op = 0; op < 6; op++)
  {
    for (uint8_t step = 0; step < 4; step++)
    {
      data[(op * 21) + DEXED_OP_EG_L1 + step] = level;
    }
  }
}

void Dexed::setOPRateAllModulator(uint8_t step, uint8_t rate)
{
  uint8_t op_carrier = controllers.core->get_carrier_operators(data[134]); // look for carriers

  rate = constrain(rate, 0, 99);
  step = constrain(step, 0, 3);

  for (uint8_t op = 0; op < 6; op++)
  {
    if ((op_carrier & (1 << op)) == 0)
      data[(op * 21) + DEXED_OP_EG_R1 + step] = rate;
  }
}

void Dexed::setOPLevelAllModulator(uint8_t step, uint8_t level)
{
  uint8_t op_carrier = controllers.core->get_carrier_operators(data[134]); // look for carriers

  step = constrain(step, 0, 3);
  level = constrain(level, 0, 99);

  for (uint8_t op = 0; op < 6; op++)
  {
    if ((op_carrier & (1 << op)) == 0)
      data[(op * 21) + DEXED_OP_EG_L1 + step] = level;
  }
}

void Dexed::setOPRateAllCarrier(uint8_t step, uint8_t rate)
{
  uint8_t op_carrier = controllers.core->get_carrier_operators(data[134]); // look for carriers

  rate = constrain(rate, 0, 99);
  step = constrain(step, 0, 3);

  for (uint8_t op = 0; op < 6; op++)
  {
    if ((op_carrier & (1 << op)) == 1)
      data[(op * 21) + DEXED_OP_EG_R1 + step] = rate;
  }
}

void Dexed::setOPLevelAllCarrier(uint8_t step, uint8_t level)
{
  uint8_t op_carrier = controllers.core->get_carrier_operators(data[134]); // look for carriers

  level = constrain(level, 0, 99);
  step = constrain(step, 0, 3);

  for (uint8_t op = 0; op < 6; op++)
  {
    if ((op_carrier & (1 << op)) == 1)
      data[(op * 21) + DEXED_OP_EG_L1 + step] = level;
  }
}

void Dexed::setOPRate(uint8_t op, uint8_t step, uint8_t rate)
{
  op = constrain(op, 0, 5);
  step = constrain(step, 0, 3);
  rate = constrain(rate, 0, 99);

  data[(op * 21) + DEXED_OP_EG_R1 + step] = rate;
}

uint8_t Dexed::getOPRate(uint8_t op, uint8_t step)
{
  op = constrain(op, 0, 5);
  step = constrain(step, 0, 3);

  return (data[(op * 21) + DEXED_OP_EG_R1 + step]);
}

void Dexed::setOPLevel(uint8_t op, uint8_t step, uint8_t level)
{
  op = constrain(op, 0, 5);
  step = constrain(step, 0, 3);
  level = constrain(level, 0, 99);

  data[(op * 21) + DEXED_OP_EG_L1 + step] = level;
}

uint8_t Dexed::getOPLevel(uint8_t op, uint8_t step)
{
  op = constrain(op, 0, 5);
  step = constrain(step, 0, 3);

  return (data[(op * 21) + DEXED_OP_EG_L1 + step]);
}

void Dexed::setOPKeyboardLevelScalingBreakPoint(uint8_t op, uint8_t level)
{
  op = constrain(op, 0, 5);
  level = constrain(level, 0, 99);

  data[(op * 21) + DEXED_OP_LEV_SCL_BRK_PT] = level;
}

uint8_t Dexed::getOPKeyboardLevelScalingBreakPoint(uint8_t op)
{
  op = constrain(op, 0, 5);

  return (data[(op * 21) + DEXED_OP_LEV_SCL_BRK_PT]);
}

void Dexed::setOPKeyboardLevelScalingDepthLeft(uint8_t op, uint8_t depth)
{
  op = constrain(op, 0, 5);
  depth = constrain(depth, 0, 99);

  data[(op * 21) + DEXED_OP_SCL_LEFT_DEPTH] = depth;
}

uint8_t Dexed::getOPKeyboardLevelScalingDepthLeft(uint8_t op)
{
  op = constrain(op, 0, 5);

  return (data[(op * 21) + DEXED_OP_SCL_LEFT_DEPTH]);
}

void Dexed::setOPKeyboardLevelScalingDepthRight(uint8_t op, uint8_t depth)
{
  op = constrain(op, 0, 5);
  depth = constrain(depth, 0, 99);

  data[(op * 21) + DEXED_OP_SCL_RGHT_DEPTH] = depth;
}

uint8_t Dexed::getOPKeyboardLevelScalingDepthRight(uint8_t op)
{
  op = constrain(op, 0, 5);

  return (data[(op * 21) + DEXED_OP_SCL_RGHT_DEPTH]);
}

void Dexed::setOPKeyboardLevelScalingCurveLeft(uint8_t op, uint8_t curve)
{
  op = constrain(op, 0, 5);
  curve = constrain(curve, 0, 3);

  data[(op * 21) + DEXED_OP_SCL_LEFT_CURVE] = curve;
}

uint8_t Dexed::getOPKeyboardLevelScalingCurveLeft(uint8_t op)
{
  op = constrain(op, 0, 5);

  return (data[(op * 21) + DEXED_OP_SCL_LEFT_CURVE]);
}

void Dexed::setOPKeyboardLevelScalingCurveRight(uint8_t op, uint8_t curve)
{
  op = constrain(op, 0, 5);
  curve = constrain(curve, 0, 3);

  data[(op * 21) + DEXED_OP_SCL_RGHT_CURVE] = curve;
}

uint8_t Dexed::getOPKeyboardLevelScalingCurveRight(uint8_t op)
{
  op = constrain(op, 0, 5);

  return (data[(op * 21) + DEXED_OP_SCL_RGHT_CURVE]);
}

void Dexed::setOPKeyboardRateScale(uint8_t op, uint8_t scale)
{
  op = constrain(op, 0, 5);
  scale = constrain(scale, 0, 7);

  data[(op * 21) + DEXED_OP_OSC_RATE_SCALE] = scale;
}

uint8_t Dexed::getOPKeyboardRateScale(uint8_t op)
{
  op = constrain(op, 0, 5);

  return (data[(op * 21) + DEXED_OP_OSC_RATE_SCALE]);
}

void Dexed::setOPAmpModulationSensity(uint8_t op, uint8_t sensitivity)
{
  op = constrain(op, 0, 5);
  sensitivity = constrain(sensitivity, 0, 3);

  data[(op * 21) + DEXED_OP_AMP_MOD_SENS] = sensitivity;
}

uint8_t Dexed::getOPAmpModulationSensity(uint8_t op)
{
  op = constrain(op, 0, 5);

  return (data[(op * 21) + DEXED_OP_AMP_MOD_SENS]);
}

void Dexed::setOPKeyboardVelocitySensity(uint8_t op, uint8_t sensitivity)
{
  op = constrain(op, 0, 5);
  sensitivity = constrain(sensitivity, 0, 7);

  data[(op * 21) + DEXED_OP_KEY_VEL_SENS] = sensitivity;
}

uint8_t Dexed::getOPKeyboardVelocitySensity(uint8_t op)
{
  op = constrain(op, 0, 5);

  return (data[(op * 21) + DEXED_OP_KEY_VEL_SENS]);
}

void Dexed::setOPOutputLevel(uint8_t op, uint8_t level)
{
  op = constrain(op, 0, 5);
  level = constrain(level, 0, 99);

  data[(op * 21) + DEXED_OP_OUTPUT_LEV] = level;
}

uint8_t Dexed::getOPOutputLevel(uint8_t op)
{
  op = constrain(op, 0, 5);

  return (data[(op * 21) + DEXED_OP_OUTPUT_LEV]);
}

void Dexed::setOPMode(uint8_t op, uint8_t mode)
{
  op = constrain(op, 0, 5);
  mode = constrain(mode, 0, 1);

  data[(op * 21) + DEXED_OP_OSC_MODE] = mode;
}

uint8_t Dexed::getOPMode(uint8_t op)
{
  op = constrain(op, 0, 5);

  return (data[(op * 21) + DEXED_OP_OSC_MODE]);
}

void Dexed::setOPFrequencyCoarse(uint8_t op, uint8_t frq_coarse)
{
  op = constrain(op, 0, 5);
  frq_coarse = constrain(frq_coarse, 0, 31);

  data[(op * 21) + DEXED_OP_FREQ_COARSE] = frq_coarse;
}

uint8_t Dexed::getOPFrequencyCoarse(uint8_t op)
{
  op = constrain(op, 0, 5);

  return (data[(op * 21) + DEXED_OP_FREQ_COARSE ]);
}

void Dexed::setOPFrequencyFine(uint8_t op, uint8_t frq_fine)
{
  op = constrain(op, 0, 5);
  frq_fine = constrain(frq_fine, 0, 99);

  data[(op * 21) + DEXED_OP_FREQ_FINE] = frq_fine;
}

uint8_t Dexed::getOPFrequencyFine(uint8_t op)
{
  op = constrain(op, 0, 5);

  return (data[(op * 21) + DEXED_OP_FREQ_FINE]);
}

void Dexed::setOPDetune(uint8_t op, uint8_t detune)
{
  op = constrain(op, 0, 5);
  detune = constrain(detune, 0, 14);

  data[(op * 21) + DEXED_OP_OSC_DETUNE] = detune;
}

uint8_t Dexed::getOPDetune(uint8_t op)
{
  op = constrain(op, 0, 5);

  return (data[(op * 21) + DEXED_OP_OSC_DETUNE]);
}

void Dexed::setPitchRate(uint8_t step, uint8_t rate)
{
  step = constrain(step, 0, 3);
  rate = constrain(rate, 0, 99);

  data[DEXED_VOICE_OFFSET + DEXED_PITCH_EG_R1 + step] = rate;
}

uint8_t Dexed::getPitchRate(uint8_t step)
{
  step = constrain(step, 0, 3);

  return (data[DEXED_VOICE_OFFSET + DEXED_PITCH_EG_R1 + step]);
}

void Dexed::setPitchLevel(uint8_t step, uint8_t level)
{
  step = constrain(step, 0, 3);
  level = constrain(level, 0, 99);

  data[DEXED_VOICE_OFFSET + DEXED_PITCH_EG_L1 + step] = level;
}

uint8_t Dexed::getPitchLevel(uint8_t step)
{
  step = constrain(step, 0, 3);

  return (data[DEXED_VOICE_OFFSET + DEXED_PITCH_EG_L1 + step]);
}

void Dexed::setAlgorithm(uint8_t algorithm)
{
  algorithm  = constrain(algorithm, 0, 31);

  data[DEXED_VOICE_OFFSET + DEXED_ALGORITHM] = algorithm;
}

uint8_t Dexed::getAlgorithm(void)
{
  return (data[DEXED_VOICE_OFFSET + DEXED_ALGORITHM]);
}

void Dexed::setFeedback(uint8_t feedback)
{
  feedback  = constrain(feedback, 0, 31);

  data[DEXED_VOICE_OFFSET + DEXED_FEEDBACK] = feedback;
}

uint8_t Dexed::getFeedback(void)
{
  return (data[DEXED_VOICE_OFFSET + DEXED_FEEDBACK]);
}

void Dexed::setOscillatorSync(bool sync)
{
  data[DEXED_VOICE_OFFSET + DEXED_OSC_KEY_SYNC] = sync;
}

bool Dexed::getOscillatorSync(void)
{
  return (data[DEXED_VOICE_OFFSET + DEXED_OSC_KEY_SYNC]);
}

void Dexed::setLFOSpeed(uint8_t speed)
{
  speed  = constrain(speed, 0, 99);

  data[DEXED_VOICE_OFFSET + DEXED_LFO_SPEED] = speed;
}

uint8_t Dexed::getLFOSpeed(void)
{
  return (data[DEXED_VOICE_OFFSET + DEXED_LFO_SPEED]);
}

void Dexed::setLFODelay(uint8_t delay)
{
  delay  = constrain(delay, 0, 99);

  data[DEXED_VOICE_OFFSET + DEXED_LFO_DELAY] = delay;
}

uint8_t Dexed::getLFODelay(void)
{
  return (data[DEXED_VOICE_OFFSET + DEXED_LFO_DELAY]);
}

void Dexed::setLFOPitchModulationDepth(uint8_t depth)
{
  depth = constrain(depth, 0, 99);

  data[DEXED_VOICE_OFFSET + DEXED_LFO_PITCH_MOD_DEP] = depth;
}
uint8_t Dexed::getLFOPitchModulationDepth(void)
{
  return (data[DEXED_VOICE_OFFSET + DEXED_LFO_PITCH_MOD_DEP]);
}

void Dexed::setLFOAmpModulationDepth(uint8_t depth)
{
  depth = constrain(depth, 0, 99);

  data[DEXED_VOICE_OFFSET + DEXED_LFO_AMP_MOD_DEP] = depth;
}

uint8_t Dexed::getLFOAmpModulationDepth(void)
{
  return (data[DEXED_VOICE_OFFSET + DEXED_LFO_AMP_MOD_DEP]);
}

void Dexed::setLFOSync(bool sync)
{
  data[DEXED_VOICE_OFFSET + DEXED_LFO_SYNC] = sync;
}

bool Dexed::getLFOSync(void)
{
  return (data[DEXED_VOICE_OFFSET + DEXED_LFO_SYNC]);
}

void Dexed::setLFOWaveform(uint8_t waveform)
{
  waveform = constrain(waveform, 0, 5);

  data[DEXED_VOICE_OFFSET + DEXED_LFO_WAVE] = waveform;
}

uint8_t Dexed::getLFOWaveform(void)
{
  return (data[DEXED_VOICE_OFFSET + DEXED_LFO_WAVE]);
}

void Dexed::setLFOPitchModulationSensitivity(uint8_t sensitivity)
{
  sensitivity  = constrain(sensitivity, 0, 5);

  data[DEXED_VOICE_OFFSET + DEXED_LFO_PITCH_MOD_SENS] = sensitivity;
}

uint8_t Dexed::getLFOPitchModulationSensitivity(void)
{
  return (data[DEXED_VOICE_OFFSET + DEXED_LFO_PITCH_MOD_SENS]);
}

void Dexed::setTranspose(uint8_t transpose)
{
  transpose = constrain(transpose, 0, 48);

  data[DEXED_VOICE_OFFSET + DEXED_TRANSPOSE] = transpose;
}

uint8_t Dexed::getTranspose(void)
{
  return (data[DEXED_VOICE_OFFSET + DEXED_TRANSPOSE]);
}

void Dexed::setName(char* name)
{
  strncpy(name, (char*)&data[DEXED_VOICE_OFFSET + DEXED_NAME], 10);
}

void Dexed::getName(char* buffer)
{
  strncpy((char*)&data[DEXED_VOICE_OFFSET + DEXED_NAME], buffer, 10);
  buffer[10] = '\0';
}
