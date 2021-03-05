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

extern config_t configuration;

Dexed::Dexed(int rate)
{
  uint8_t i;

  Exp2::init();
  Tanh::init();
  Sin::init();

  Freqlut::init(rate);
  Lfo::init(rate);
  PitchEnv::init(rate);
  Env::init_sr(rate);
  Porta::init_sr(rate);
  fx.init(rate);

  engineMkI = new EngineMkI;
  engineOpl = new EngineOpl;
  engineMsfa = new FmCore;

  for (i = 0; i < MAX_ACTIVE_NOTES; i++)
  {
    voices[i].dx7_note = new Dx7Note;
    voices[i].keydown = false;
    voices[i].sustained = false;
    voices[i].live = false;
    voices[i].key_pressed_timer = 0;
  }

  max_notes = MAX_NOTES;
  currentNote = 0;
  resetControllers();
  controllers.masterTune = 0;
  controllers.opSwitch = 0x3f; // enable all operators
  //controllers.opSwitch=0x00;
  lastKeyDown = -1;
  vuSignal = 0.0;

  lfo.reset(data + 137);

  setMonoMode(false);

  sustain = false;

  setEngineType(DEXED_ENGINE);
}

Dexed::~Dexed()
{
  currentNote = -1;

  for (uint8_t note = 0; note < MAX_ACTIVE_NOTES; note++)
    delete voices[note].dx7_note;

  delete(engineMsfa);
  delete(engineOpl);
  delete(engineMkI);
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
  float s;
  const double decayFactor = 0.99992;

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

  // mild compression
  for (i = 0; i < n_samples; i++)
  {
    s = abs(sumbuf[i]);
    if (s > vuSignal)
      vuSignal = s;
    else if (vuSignal > 0.001f)
      vuSignal *= decayFactor;
    else
      vuSignal = 0;
  }

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

void Dexed::setOPs(uint8_t ops)
{
  controllers.opSwitch = ops;
}

uint8_t Dexed::getEngineType() {
  return engineType;
}

void Dexed::setEngineType(uint8_t tp) {
  if (engineType == tp)
    return;

  switch (tp)  {
    case DEXED_ENGINE_MARKI:
      controllers.core = engineMkI;
      break;
    case DEXED_ENGINE_OPL:
      controllers.core = engineOpl;
      break;
    default:
      controllers.core = engineMsfa;
      tp = DEXED_ENGINE_MODERN;
      break;
  }
  engineType = tp;
  panic();
  controllers.refresh();
}

bool Dexed::isMonoMode(void) {
  return monoMode;
}

void Dexed::setMonoMode(bool mode) {
  if (monoMode == mode)
    return;

  //panic();
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
  for (uint8_t i = 0; i < MAX_ACTIVE_NOTES; i++)
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
  for (uint8_t i = 0; i < MAX_ACTIVE_NOTES; i++) {
    if (voices[i].live == true) {
      voices[i].keydown = false;
      voices[i].live = false;
    }
  }
}

void Dexed::setMaxNotes(uint8_t n) {
  if (n <= MAX_ACTIVE_NOTES)
  {
    notesOff();
    max_notes = n;
    //panic();
    controllers.refresh();
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

bool Dexed::decodeVoice(uint8_t* encoded_data, uint8_t* new_data)
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

bool Dexed::loadVoiceParameters(uint8_t* new_data)
{
  char dexed_voice_name[11];

  panic();

  memcpy(&data, new_data, 155);

  doRefreshVoice();
  //activate();

  strncpy(dexed_voice_name, (char *)&new_data[145], sizeof(dexed_voice_name) - 1);
  dexed_voice_name[10] = '\0';
#ifdef DEBUG
  Serial.print(F("Voice ["));
  Serial.print(dexed_voice_name);
  Serial.println(F("] loaded."));
#endif

  return (true);
}

void Dexed::setPBController(uint8_t pb_range, uint8_t pb_step)
{
#ifdef DEBUG
  Serial.println(F("Dexed::setPBController"));
#endif

  pb_range = constrain(pb_range, PB_RANGE_MIN, PB_RANGE_MAX);
  pb_step = constrain(pb_step, PB_STEP_MIN, PB_STEP_MAX);

  controllers.values_[kControllerPitchRange] = pb_range;
  controllers.values_[kControllerPitchStep] = pb_step;

  controllers.refresh();
}

void Dexed::setMWController(uint8_t mw_range, uint8_t mw_assign, uint8_t mw_mode)
{
#ifdef DEBUG
  Serial.println(F("Dexed::setMWController"));
#endif

  mw_range = constrain(mw_range, MW_RANGE_MIN, MW_RANGE_MAX);
  mw_assign = constrain(mw_assign, MW_ASSIGN_MIN, MW_ASSIGN_MAX);
  mw_mode = constrain(mw_mode, MW_MODE_MIN, MW_MODE_MAX);

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

  fc_range = constrain(fc_range, FC_RANGE_MIN, FC_RANGE_MAX);
  fc_assign = constrain(fc_assign, FC_ASSIGN_MIN, FC_ASSIGN_MAX);
  fc_mode = constrain(fc_mode, FC_MODE_MIN, FC_MODE_MAX);

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

  bc_range = constrain(bc_range, BC_RANGE_MIN, BC_RANGE_MAX);
  bc_assign = constrain(bc_assign, BC_ASSIGN_MIN, BC_ASSIGN_MAX);
  bc_mode = constrain(bc_mode, BC_MODE_MIN, BC_MODE_MAX);

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

  at_range = constrain(at_range, AT_RANGE_MIN, AT_RANGE_MAX);
  at_assign = constrain(at_assign, AT_ASSIGN_MIN, AT_ASSIGN_MAX);
  at_mode = constrain(at_mode, AT_MODE_MIN, AT_MODE_MAX);

  controllers.at.setRange(at_range);
  controllers.at.setTarget(at_assign);
  controllers.at.setMode(at_mode);

  controllers.refresh();
}

void Dexed::setPortamentoMode(uint8_t portamento_mode, uint8_t portamento_glissando, uint8_t portamento_time)
{
  controllers.portamento_cc = portamento_time;
  controllers.portamento_enable_cc = portamento_mode > 63;

  if (portamento_time > 0)
    controllers.portamento_enable_cc = true;
  else
    controllers.portamento_enable_cc = false;

  controllers.values_[kControllerPortamentoGlissando] = portamento_glissando;

  controllers.refresh();
}

/*
  // https://www.musicdsp.org/en/latest/Effects/169-compressor.html#
  void compress
  (
  float*  wav_in,     // signal
  int     n,          // N samples
  double  threshold,  // threshold (percents)
  double  slope,      // slope angle (percents)
  int     sr,         // sample rate (smp/sec)
  double  tla,        // lookahead  (ms)
  double  twnd,       // window time (ms)
  double  tatt,       // attack time  (ms)
  double  trel        // release time (ms)
  )
  {
  typedef float   stereodata[2];
  stereodata*     wav = (stereodata*) wav_in; // our stereo signal
  threshold *= 0.01;          // threshold to unity (0...1)
  slope *= 0.01;              // slope to unity
  tla *= 1e-3;                // lookahead time to seconds
  twnd *= 1e-3;               // window time to seconds
  tatt *= 1e-3;               // attack time to seconds
  trel *= 1e-3;               // release time to seconds

  // attack and release "per sample decay"
  double  att = (tatt == 0.0) ? (0.0) : exp (-1.0 / (sr * tatt));
  double  rel = (trel == 0.0) ? (0.0) : exp (-1.0 / (sr * trel));

  // envelope
  double  env = 0.0;

  // sample offset to lookahead wnd start
  int     lhsmp = (int) (sr * tla);

  // samples count in lookahead window
  int     nrms = (int) (sr * twnd);

  // for each sample...
  for (int i = 0; i < n; ++i)
  {
    // now compute RMS
    double  summ = 0;

    // for each sample in window
    for (int j = 0; j < nrms; ++j)
    {
      int     lki = i + j + lhsmp;
      double  smp;

      // if we in bounds of signal?
      // if so, convert to mono
      if (lki < n)
        smp = 0.5 * wav[lki][0] + 0.5 * wav[lki][1];
      else
        smp = 0.0;      // if we out of bounds we just get zero in smp

      summ += smp * smp;  // square em..
    }

    double  rms = sqrt (summ / nrms);   // root-mean-square

    // dynamic selection: attack or release?
    double  theta = rms > env ? att : rel;

    // smoothing with capacitor, envelope extraction...
    // here be aware of pIV denormal numbers glitch
    env = (1.0 - theta) * rms + theta * env;

    // the very easy hard knee 1:N compressor
    double  gain = 1.0;
    if (env > threshold)
      gain = gain - (env - threshold) * slope;

    // result - two hard kneed compressed channels...
    float  leftchannel = wav[i][0] * gain;
    float  rightchannel = wav[i][1] * gain;
  }
  }
*/
