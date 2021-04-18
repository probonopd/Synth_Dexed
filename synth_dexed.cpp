#include "synth_dexed.h"
/*
   Copyright (C) 2015-2017 Pascal Gauthier.

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

   The code is based on ppplay https://github.com/stohrendorf/ppplay and opl3
   math documentation :
   https://github.com/gtaylormb/opl3_fpga/blob/master/docs/opl3math/opl3math.pdf

*/

const int32_t __attribute__ ((aligned(16))) zeros[_N_] = {0};

static const uint16_t NEGATIVE_BIT = 0x8000;
static const uint16_t ENV_BITDEPTH = 14;

static const uint16_t SINLOG_BITDEPTH = 10;
static const uint16_t SINLOG_TABLESIZE = 1 << SINLOG_BITDEPTH;
static uint16_t sinLogTable[SINLOG_TABLESIZE];

static const uint16_t SINEXP_BITDEPTH = 10;
static const uint16_t SINEXP_TABLESIZE = 1 << SINEXP_BITDEPTH;
static uint16_t sinExpTable[SINEXP_TABLESIZE];

const uint16_t ENV_MAX = 1 << ENV_BITDEPTH;

static inline uint16_t sinLog(uint16_t phi) {
  const uint16_t SINLOG_TABLEFILTER = SINLOG_TABLESIZE - 1;
  const uint16_t index = (phi & SINLOG_TABLEFILTER);

  switch ( ( phi & (SINLOG_TABLESIZE * 3) ) ) {
    case 0:
      return sinLogTable[index];
    case SINLOG_TABLESIZE:
      return sinLogTable[index ^ SINLOG_TABLEFILTER];
    case SINLOG_TABLESIZE * 2 :
      return sinLogTable[index] | NEGATIVE_BIT;
    default:
      return sinLogTable[index ^ SINLOG_TABLEFILTER] | NEGATIVE_BIT;
  }
}

EngineMkI::EngineMkI() {
  float bitReso = SINLOG_TABLESIZE;

  for (int i = 0; i < SINLOG_TABLESIZE; i++) {
    //float x1 = sin(((0.5+i)/bitReso) * M_PI/2.0);
    float x1 = SIN_FUNC(((0.5 + i) / bitReso) * M_PI / 2.0);
    sinLogTable[i] = round(-1024 * log2(x1));
  }

  bitReso = SINEXP_TABLESIZE;
  for (int i = 0; i < SINEXP_TABLESIZE; i++) {
    float x1 = (pow(2, float(i) / bitReso) - 1) * 4096;
    sinExpTable[i] = round(x1);
  }

#ifdef MKIDEBUG
  char buffer[4096];
  int pos = 0;

  for (int i = 0; i < SINLOG_TABLESIZE; i++) {
    pos += sprintf(buffer + pos, "%d ", sinLogTable[i]);
    if ( pos > 90 ) {
      buffer[0] = 0;
      pos = 0;
    }
  }
  buffer[0] = 0;
  pos = 0;
  for (int i = 0; i < SINEXP_TABLESIZE; i++) {
    pos += sprintf(buffer + pos, "%d ", sinExpTable[i]);
    if ( pos > 90 ) {
      buffer[0] = 0;
      pos = 0;
    }
  }
#endif
}

inline int32_t mkiSin(int32_t phase, uint16_t env) {
  uint16_t expVal = sinLog(phase >> (22 - SINLOG_BITDEPTH)) + (env);
  //int16_t expValShow = expVal;

  const bool isSigned = expVal & NEGATIVE_BIT;
  expVal &= ~NEGATIVE_BIT;

  const uint16_t SINEXP_FILTER = 0x3FF;
  uint16_t result = 4096 + sinExpTable[( expVal & SINEXP_FILTER ) ^ SINEXP_FILTER];

  //uint16_t resultB4 = result;
  result >>= ( expVal >> 10 ); // exp

#ifdef MKIDEBUG
  if ( ( time(NULL) % 5 ) == 0 ) {
    if ( expValShow < 0 ) {
      expValShow = (expValShow + 0x7FFF) * -1;
    }
  }
#endif

  if ( isSigned )
    return (-result - 1) << 13;
  else
    return result << 13;
}

void EngineMkI::compute(int32_t *output, const int32_t *input,
                        int32_t phase0, int32_t freq,
                        int32_t gain1, int32_t gain2, bool add) {
  int32_t dgain = (gain2 - gain1 + (_N_ >> 1)) >> LG_N;
  int32_t gain = gain1;
  int32_t phase = phase0;
  const int32_t *adder = add ? output : zeros;

  for (int i = 0; i < _N_; i++) {
    gain += dgain;
    int32_t y = mkiSin((phase + input[i]), gain);
    output[i] = y + adder[i];
    phase += freq;
  }

}

void EngineMkI::compute_pure(int32_t *output, int32_t phase0, int32_t freq,
                             int32_t gain1, int32_t gain2, bool add) {
  int32_t dgain = (gain2 - gain1 + (_N_ >> 1)) >> LG_N;
  int32_t gain = gain1;
  int32_t phase = phase0;
  const int32_t *adder = add ? output : zeros;

  for (int i = 0; i < _N_; i++) {
    gain += dgain;
    int32_t y = mkiSin(phase , gain);
    output[i] = y + adder[i];
    phase += freq;
  }
}

void EngineMkI::compute_fb(int32_t *output, int32_t phase0, int32_t freq,
                           int32_t gain1, int32_t gain2,
                           int32_t *fb_buf, int fb_shift, bool add) {
  int32_t dgain = (gain2 - gain1 + (_N_ >> 1)) >> LG_N;
  int32_t gain = gain1;
  int32_t phase = phase0;
  const int32_t *adder = add ? output : zeros;
  int32_t y0 = fb_buf[0];
  int32_t y = fb_buf[1];

  for (int i = 0; i < _N_; i++) {
    gain += dgain;
    int32_t scaled_fb = (y0 + y) >> (fb_shift + 1);
    y0 = y;
    y = mkiSin((phase + scaled_fb), gain);
    output[i] = y + adder[i];
    phase += freq;
  }

  fb_buf[0] = y0;
  fb_buf[1] = y;
}

// exclusively used for ALGO 6 with feedback
void EngineMkI::compute_fb2(int32_t *output, FmOpParams *parms, int32_t gain01, int32_t gain02, int32_t *fb_buf, int fb_shift) {
  int32_t dgain[2];
  int32_t gain[2];
  int32_t phase[2];
  int32_t y0 = fb_buf[0];
  int32_t y = fb_buf[1];

  phase[0] = parms[0].phase;
  phase[1] = parms[1].phase;

  parms[1].gain_out = (ENV_MAX - (parms[1].level_in >> (28 - ENV_BITDEPTH)));

  gain[0] = gain01;
  gain[1] = parms[1].gain_out == 0 ? (ENV_MAX - 1) : parms[1].gain_out;

  dgain[0] = (gain02 - gain01 + (_N_ >> 1)) >> LG_N;
  dgain[1] = (parms[1].gain_out - (parms[1].gain_out == 0 ? (ENV_MAX - 1) : parms[1].gain_out));

  for (int i = 0; i < _N_; i++) {
    int32_t scaled_fb = (y0 + y) >> (fb_shift + 1);

    // op 0
    gain[0] += dgain[0];
    y0 = y;
    y = mkiSin(phase[0] + scaled_fb, gain[0]);
    phase[0] += parms[0].freq;

    // op 1
    gain[1] += dgain[1];
    y = mkiSin(phase[1] + y, gain[1]);
    phase[1] += parms[1].freq;

    output[i] = y;
  }
  fb_buf[0] = y0;
  fb_buf[1] = y;
}

// exclusively used for ALGO 4 with feedback
void EngineMkI::compute_fb3(int32_t *output, FmOpParams *parms, int32_t gain01, int32_t gain02, int32_t *fb_buf, int fb_shift) {
  int32_t dgain[3];
  int32_t gain[3];
  int32_t phase[3];
  int32_t y0 = fb_buf[0];
  int32_t y = fb_buf[1];

  phase[0] = parms[0].phase;
  phase[1] = parms[1].phase;
  phase[2] = parms[2].phase;

  parms[1].gain_out = (ENV_MAX - (parms[1].level_in >> (28 - ENV_BITDEPTH)));
  parms[2].gain_out = (ENV_MAX - (parms[2].level_in >> (28 - ENV_BITDEPTH)));

  gain[0] = gain01;
  gain[1] = parms[1].gain_out == 0 ? (ENV_MAX - 1) : parms[1].gain_out;
  gain[2] = parms[2].gain_out == 0 ? (ENV_MAX - 1) : parms[2].gain_out;

  dgain[0] = (gain02 - gain01 + (_N_ >> 1)) >> LG_N;
  dgain[1] = (parms[1].gain_out - (parms[1].gain_out == 0 ? (ENV_MAX - 1) : parms[1].gain_out));
  dgain[2] = (parms[2].gain_out - (parms[2].gain_out == 0 ? (ENV_MAX - 1) : parms[2].gain_out));


  for (int i = 0; i < _N_; i++) {
    int32_t scaled_fb = (y0 + y) >> (fb_shift + 1);

    // op 0
    gain[0] += dgain[0];
    y0 = y;
    y = mkiSin(phase[0] + scaled_fb, gain[0]);
    phase[0] += parms[0].freq;

    // op 1
    gain[1] += dgain[1];
    y = mkiSin(phase[1] + y, gain[1]);
    phase[1] += parms[1].freq;

    // op 2
    gain[2] += dgain[2];
    y = mkiSin(phase[2] + y, gain[2]);
    phase[2] += parms[2].freq;

    output[i] = y;
  }
  fb_buf[0] = y0;
  fb_buf[1] = y;
}

void EngineMkI::render(int32_t *output, FmOpParams *params, int algorithm, int32_t *fb_buf, int32_t feedback_shift) {
  const int kLevelThresh = ENV_MAX - 100;
  FmAlgorithm alg = algorithms[algorithm];
  bool has_contents[3] = { true, false, false };
  bool fb_on = feedback_shift < 16;

  switch (algorithm) {
    case 3 : case 5 :
      if ( fb_on )
        alg.ops[0] = 0xc4;
  }

  for (int op = 0; op < 6; op++) {
    int flags = alg.ops[op];
    bool add = (flags & OUT_BUS_ADD) != 0;
    FmOpParams &param = params[op];
    int inbus = (flags >> 4) & 3;
    int outbus = flags & 3;
    int32_t *outptr = (outbus == 0) ? output : buf_[outbus - 1].get();
    int32_t gain1 = param.gain_out == 0 ? (ENV_MAX - 1) : param.gain_out;
    int32_t gain2 = ENV_MAX - (param.level_in >> (28 - ENV_BITDEPTH));
    param.gain_out = gain2;

    if (gain1 <= kLevelThresh || gain2 <= kLevelThresh) {

      if (!has_contents[outbus]) {
        add = false;
      }

      if (inbus == 0 || !has_contents[inbus]) {
        // PG: this is my 'dirty' implementation of FB for 2 and 3 operators...
        if ((flags & 0xc0) == 0xc0 && fb_on) {
          switch ( algorithm ) {
            // three operator feedback, process exception for ALGO 4
            case 3 :
              compute_fb3(outptr, params, gain1, gain2, fb_buf, min((feedback_shift + 2), 16));
              params[1].phase += params[1].freq << LG_N; // hack, we already processed op-5 - op-4
              params[2].phase += params[2].freq << LG_N; // yuk yuk
              op += 2; // ignore the 2 other operators
              break;
            // two operator feedback, process exception for ALGO 6
            case 5 :
              compute_fb2(outptr, params, gain1, gain2, fb_buf, min((feedback_shift + 2), 16));
              params[1].phase += params[1].freq << LG_N;  // yuk, hack, we already processed op-5
              op++; // ignore next operator;
              break;
            default:
              // one operator feedback, normal proces
              compute_fb(outptr, param.phase, param.freq, gain1, gain2, fb_buf, feedback_shift, add);
              break;
          }
        } else {
          compute_pure(outptr, param.phase, param.freq, gain1, gain2, add);
        }
      } else {
        compute(outptr, buf_[inbus - 1].get(), param.phase, param.freq, gain1, gain2, add);
      }

      has_contents[outbus] = true;
    } else if (!add) {
      has_contents[outbus] = false;
    }
    param.phase += param.freq << LG_N;
  }
}
/*
   Copyright (C) 2014  Pascal Gauthier.
   Copyright (C) 2012  Steffen Ohrendorf <steffen.ohrendorf@gmx.de>

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

   Original Java Code: Copyright (C) 2008 Robson Cozendey <robson@cozendey.com>

   Some code based on forum posts in: http://forums.submarine.org.uk/phpBB/viewforum.php?f=9,
   Copyright (C) 2010-2013 by carbon14 and opl3

*/

uint16_t SignBit = 0x8000;

sinLogTable[] = {
  2137, 1731, 1543, 1419, 1326, 1252, 1190, 1137, 1091, 1050, 1013, 979, 949, 920, 894, 869,
  846, 825, 804, 785, 767, 749, 732, 717, 701, 687, 672, 659, 646, 633, 621, 609,
  598, 587, 576, 566, 556, 546, 536, 527, 518, 509, 501, 492, 484, 476, 468, 461,
  453, 446, 439, 432, 425, 418, 411, 405, 399, 392, 386, 380, 375, 369, 363, 358,
  352, 347, 341, 336, 331, 326, 321, 316, 311, 307, 302, 297, 293, 289, 284, 280,
  276, 271, 267, 263, 259, 255, 251, 248, 244, 240, 236, 233, 229, 226, 222, 219,
  215, 212, 209, 205, 202, 199, 196, 193, 190, 187, 184, 181, 178, 175, 172, 169,
  167, 164, 161, 159, 156, 153, 151, 148, 146, 143, 141, 138, 136, 134, 131, 129,
  127, 125, 122, 120, 118, 116, 114, 112, 110, 108, 106, 104, 102, 100, 98, 96,
  94, 92, 91, 89, 87, 85, 83, 82, 80, 78, 77, 75, 74, 72, 70, 69,
  67, 66, 64, 63, 62, 60, 59, 57, 56, 55, 53, 52, 51, 49, 48, 47,
  46, 45, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32, 31, 30,
  29, 28, 27, 26, 25, 24, 23, 23, 22, 21, 20, 20, 19, 18, 17, 17,
  16, 15, 15, 14, 13, 13, 12, 12, 11, 10, 10, 9, 9, 8, 8, 7,
  7, 7, 6, 6, 5, 5, 5, 4, 4, 4, 3, 3, 3, 2, 2, 2,
  2, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0
};

uint16_t sinExpTable[256] = {
  0, 3, 6, 8, 11, 14, 17, 20, 22, 25, 28, 31, 34, 37, 40, 42,
  45, 48, 51, 54, 57, 60, 63, 66, 69, 72, 75, 78, 81, 84, 87, 90,
  93, 96, 99, 102, 105, 108, 111, 114, 117, 120, 123, 126, 130, 133, 136, 139,
  142, 145, 148, 152, 155, 158, 161, 164, 168, 171, 174, 177, 181, 184, 187, 190,
  194, 197, 200, 204, 207, 210, 214, 217, 220, 224, 227, 231, 234, 237, 241, 244,
  248, 251, 255, 258, 262, 265, 268, 272, 276, 279, 283, 286, 290, 293, 297, 300,
  304, 308, 311, 315, 318, 322, 326, 329, 333, 337, 340, 344, 348, 352, 355, 359,
  363, 367, 370, 374, 378, 382, 385, 389, 393, 397, 401, 405, 409, 412, 416, 420,
  424, 428, 432, 436, 440, 444, 448, 452, 456, 460, 464, 468, 472, 476, 480, 484,
  488, 492, 496, 501, 505, 509, 513, 517, 521, 526, 530, 534, 538, 542, 547, 551,
  555, 560, 564, 568, 572, 577, 581, 585, 590, 594, 599, 603, 607, 612, 616, 621,
  625, 630, 634, 639, 643, 648, 652, 657, 661, 666, 670, 675, 680, 684, 689, 693,
  698, 703, 708, 712, 717, 722, 726, 731, 736, 741, 745, 750, 755, 760, 765, 770,
  774, 779, 784, 789, 794, 799, 804, 809, 814, 819, 824, 829, 834, 839, 844, 849,
  854, 859, 864, 869, 874, 880, 885, 890, 895, 900, 906, 911, 916, 921, 927, 932,
  937, 942, 948, 953, 959, 964, 969, 975, 980, 986, 991, 996, 1002, 1007, 1013, 1018
};

inline uint16_t sinLog(uint16_t phi) {
  const uint8_t index = (phi & 0xff);

  switch ( ( phi & 0x0300 ) ) {
    case 0x0000:
      // rising quarter wave  Shape A
      return sinLogTable[index];
    case 0x0100:
      // falling quarter wave  Shape B
      return sinLogTable[index ^ 0xFF];
    case 0x0200:
      // rising quarter wave -ve  Shape C
      return sinLogTable[index] | SignBit;
    default:
      // falling quarter wave -ve  Shape D
      return sinLogTable[index ^ 0xFF] | SignBit;
  }
}

// 16 env units are ~3dB and halve the output
/**
   @brief OPL Sine Wave calculation
   @param[in] phase Wave phase (0..1023)
   @param[in] env Envelope value (0..511)
   @warning @a env will not be checked for correct values.
*/
inline int16_t oplSin( uint16_t phase, uint16_t env ) {
  uint16_t expVal = sinLog(phase) + (env << 3);
  const bool isSigned = expVal & SignBit;

  expVal &= ~SignBit;
  // expVal: 0..2137+511*8 = 0..6225
  // result: 0..1018+1024
  uint32_t result = 0x0400 + sinExpTable[( expVal & 0xff ) ^ 0xFF];
  result <<= 1;
  result >>= ( expVal >> 8 ); // exp

  if ( isSigned ) {
    // -1 for one's complement
    return -result - 1;
  } else {
    return result;
  }
}

void EngineOpl::compute(int32_t *output, const int32_t *input, int32_t phase0, int32_t freq, int32_t gain1, int32_t gain2, bool add) {
  int32_t dgain = (gain2 - gain1 + (_N_ >> 1)) >> LG_N;
  int32_t gain = gain1;
  int32_t phase = phase0;
  const int32_t *adder = add ? output : zeros;

  for (int i = 0; i < _N_; i++) {
    gain += dgain;
    int32_t y = oplSin((phase + input[i]) >> 14, gain);
    output[i] = (y << 14) + adder[i];
    phase += freq;
  }

}
void EngineOpl::compute_pure(int32_t *output, int32_t phase0, int32_t freq, int32_t gain1, int32_t gain2, bool add) {
  int32_t dgain = (gain2 - gain1 + (_N_ >> 1)) >> LG_N;
  int32_t gain = gain1;
  int32_t phase = phase0;
  const int32_t *adder = add ? output : zeros;

  for (int i = 0; i < _N_; i++) {
    gain += dgain;
    int32_t y = oplSin(phase >> 14, gain);
    output[i] = (y << 14) + adder[i];
    phase += freq;
  }
}

void EngineOpl::compute_fb(int32_t *output, int32_t phase0, int32_t freq,
                           int32_t gain1, int32_t gain2,
                           int32_t *fb_buf, int fb_shift, bool add) {
  int32_t dgain = (gain2 - gain1 + (_N_ >> 1)) >> LG_N;
  int32_t gain = gain1;
  int32_t phase = phase0;
  const int32_t *adder = add ? output : zeros;
  int32_t y0 = fb_buf[0];
  int32_t y = fb_buf[1];

  for (int i = 0; i < _N_; i++) {
    gain += dgain;
    int32_t scaled_fb = (y0 + y) >> (fb_shift + 1);
    y0 = y;
    y = oplSin((phase + scaled_fb) >> 14, gain) << 14;
    output[i] = y + adder[i];
    phase += freq;
  }

  fb_buf[0] = y0;
  fb_buf[1] = y;
}


void EngineOpl::render(int32_t *output, FmOpParams *params, int algorithm, int32_t *fb_buf, int32_t feedback_shift) {
  const int kLevelThresh = 507;  // really ????
  const FmAlgorithm alg = algorithms[algorithm];
  bool has_contents[3] = { true, false, false };
  for (int op = 0; op < 6; op++) {
    int flags = alg.ops[op];
    bool add = (flags & OUT_BUS_ADD) != 0;
    FmOpParams &param = params[op];
    int inbus = (flags >> 4) & 3;
    int outbus = flags & 3;
    int32_t *outptr = (outbus == 0) ? output : buf_[outbus - 1].get();
    int32_t gain1 = param.gain_out == 0 ? 511 : param.gain_out;
    int32_t gain2 = 512 - (param.level_in >> 19);
    param.gain_out = gain2;

    if (gain1 <= kLevelThresh || gain2 <= kLevelThresh) {
      if (!has_contents[outbus]) {
        add = false;
      }
      if (inbus == 0 || !has_contents[inbus]) {
        // todo: more than one op in a feedback loop
        if ((flags & 0xc0) == 0xc0 && feedback_shift < 16) {
          // cout << op << " fb " << inbus << outbus << add << endl;
          compute_fb(outptr, param.phase, param.freq,
                     gain1, gain2,
                     fb_buf, feedback_shift, add);
        } else {
          // cout << op << " pure " << inbus << outbus << add << endl;
          compute_pure(outptr, param.phase, param.freq,
                       gain1, gain2, add);
        }
      } else {
        // cout << op << " normal " << inbus << outbus << " " << param.freq << add << endl;
        compute(outptr, buf_[inbus - 1].get(),
                param.phase, param.freq, gain1, gain2, add);
      }
      has_contents[outbus] = true;
    } else if (!add) {
      has_contents[outbus] = false;
    }
    param.phase += param.freq << LG_N;
  }
}
/**

   Copyright (c) 2013-2014 Pascal Gauthier.
   Copyright (c) 2013-2014 Filatov Vadim.

   Filter taken from the Obxd project :
     https://github.com/2DaT/Obxd

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

const float dc = 1e-18;

inline static float tptpc(float& state, float inp, float cutoff) {
  float v = (inp - state) * cutoff / (1 + cutoff);
  float res = v + state;
  state = res + v;
  return res;
}

inline static float tptlpupw(float & state , float inp , float cutoff , float srInv) {
  cutoff = (cutoff * srInv) * M_PI;
  float v = (inp - state) * cutoff / (1 + cutoff);
  float res = v + state;
  state = res + v;
  return res;
}

//static float linsc(float param,const float min,const float max) {
//    return (param) * (max - min) + min;
//}

static float logsc(float param, const float min, const float max, const float rolloff = 19.0f) {
  return ((EXP_FUNC(param * LOG_FUNC(rolloff + 1)) - 1.0f) / (rolloff)) * (max - min) + min;
}

PluginFx::PluginFx() {
  Cutoff = 1.0;
  Reso = 0.0;
  Gain = 1.0;
}

void PluginFx::init(int sr) {
  mm = 0;
  s1 = s2 = s3 = s4 = c = d = 0;
  R24 = 0;

  mmch = (int)(mm * 3);
  mmt = mm * 3 - mmch;

  sampleRate = sr;
  sampleRateInv = 1 / sampleRate;
#if defined(ARM_SQRT_FUNC)
  float rcrate; ARM_SQRT_FUNC(44000 / sampleRate, &rcrate);
#else
  float rcrate = SQRT_FUNC((44000 / sampleRate));
#endif
  rcor24 = (970.0 / 44000) * rcrate;
  rcor24Inv = 1 / rcor24;

  bright = tanf((sampleRate * 0.5f - 10) * M_PI * sampleRateInv);

  R = 1;
  rcor = (480.0 / 44000) * rcrate;
  rcorInv = 1 / rcor;
  bandPassSw = false;

  pCutoff = -1;
  pReso = -1;

  dc_r = 1.0 - (126.0 / sr);
  dc_id = 0;
  dc_od = 0;
}

inline float PluginFx::NR24(float sample, float g, float lpc) {
  float ml = 1 / (1 + g);
  float S = (lpc * (lpc * (lpc * s1 + s2) + s3) + s4) * ml;
  float G = lpc * lpc * lpc * lpc;
  float y = (sample - R24 * S) / (1 + R24 * G);
  return y + 1e-8;
};

inline float PluginFx::NR(float sample, float g) {
  float y = ((sample - R * s1 * 2 - g * s1  - s2) / (1 + g * (2 * R + g))) + dc;
  return y;
}

void PluginFx::process(float *work, int sampleSize) {
  // very basic DC filter
  float t_fd = work[0];
  work[0] = work[0] - dc_id + dc_r * dc_od;
  dc_id = t_fd;
  for (int i = 1; i < sampleSize; i++) {
    t_fd = work[i];
    work[i] = work[i] - dc_id + dc_r * work[i - 1];
    dc_id = t_fd;
  }

  dc_od = work[sampleSize - 1];

  // Gain
  if (Gain == 0.0)
  {
    for (int i = 0; i < sampleSize; i++ )
      work[i] = 0.0;
  }
  else if ( Gain != 1.0)
  {
    for (int i = 0; i < sampleSize; i++ )
      work[i] *= Gain;
  }

#ifdef USE_FX
  // don't apply the LPF if the cutoff is to maximum
  if ( Cutoff == 1.0 )
    return;

  if ( Cutoff != pCutoff || Reso != pReso ) {
    rReso = (0.991 - logsc(1 - Reso, 0, 0.991));
    R24 = 3.5 * rReso;

    float cutoffNorm = logsc(Cutoff, 60, 19000);
    rCutoff = (float)tanf(cutoffNorm * sampleRateInv * M_PI);

    pCutoff = Cutoff;
    pReso = Reso;

    R = 1 - rReso;
  }

  // THIS IS MY FAVORITE 4POLE OBXd filter

  // maybe smooth this value
  float g = rCutoff;
  float lpc = g / (1 + g);

  for (int i = 0; i < sampleSize; i++ ) {
    float s = work[i];
    s = s - 0.45 * tptlpupw(c, s, 15, sampleRateInv);
    s = tptpc(d, s, bright);

    float y0 = NR24(s, g, lpc);

    //first low pass in cascade
    float v = (y0 - s1) * lpc;
    float res = v + s1;
    s1 = res + v;

    //damping
    s1 = atanf(s1 * rcor24) * rcor24Inv;
    float y1 = res;
    float y2 = tptpc(s2, y1, g);
    float y3 = tptpc(s3, y2, g);
    float y4 = tptpc(s4, y3, g);
    float mc = 0.0;

    switch (mmch) {
      case 0:
        mc = ((1 - mmt) * y4 + (mmt) * y3);
        break;
      case 1:
        mc = ((1 - mmt) * y3 + (mmt) * y2);
        break;
      case 2:
        mc = ((1 - mmt) * y2 + (mmt) * y1);
        break;
      case 3:
        mc = y1;
        break;
    }

    //half volume comp
    work[i] = mc * (1 + R24 * 0.45);
  }
#endif
}

/*

  // THIS IS THE 2POLE FILTER

  for(int i=0; i < sampleSize; i++ ) {
  float s = work[i];
  s = s - 0.45*tptlpupw(c,s,15,sampleRateInv);
  s = tptpc(d,s,bright);

  //float v = ((sample- R * s1*2 - g2*s1 - s2)/(1+ R*g1*2 + g1*g2));
  float v = NR(s,g);
  float y1 = v*g + s1;
  //damping
  s1 = atanf(s1 * rcor) * rcorInv;

  float y2 = y1*g + s2;
  s2 = y2 + y1*g;

  float mc;
  if(!bandPassSw)
  mc = (1-mm)*y2 + (mm)*v;
  else
  {

  mc =2 * ( mm < 0.5 ?
  ((0.5 - mm) * y2 + (mm) * y1):
  ((1-mm) * y1 + (mm-0.5) * v)
  );
  }

  work[i] = mc;
  }

*/

float PluginFx::getGain(void)
{
  return (Gain);
}
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

const int FEEDBACK_BITDEPTH = 8;

int32_t midinote_to_logfreq(int midinote) {
  //const int32_t base = 50857777;  // (1 << 24) * (log(440) / log(2) - 69/12)
  const int32_t base = 50857777;  // (1 << 24) * (LOG_FUNC(440) / LOG_FUNC(2) - 69/12)
  const int32_t step = (1 << 24) / 12;
  return base + step * midinote;
}

int32_t logfreq_round2semi(int freq) {
  const int base = 50857777;  // (1 << 24) * (log(440) / log(2) - 69/12)
  const int step = (1 << 24) / 12;
  const int rem = (freq - base) % step;
  return freq - rem;
}

const int32_t coarsemul[] = {
  -16777216, 0, 16777216, 26591258, 33554432, 38955489, 43368474, 47099600,
  50331648, 53182516, 55732705, 58039632, 60145690, 62083076, 63876816,
  65546747, 67108864, 68576247, 69959732, 71268397, 72509921, 73690858,
  74816848, 75892776, 76922906, 77910978, 78860292, 79773775, 80654032,
  81503396, 82323963, 83117622
};

int32_t osc_freq(int midinote, int mode, int coarse, int fine, int detune) {
  // TODO: pitch randomization
  int32_t logfreq;
  if (mode == 0) {
    logfreq = midinote_to_logfreq(midinote);
    // could use more precision, closer enough for now. those numbers comes from my DX7
    //FRAC_NUM detuneRatio = 0.0209 * exp(-0.396 * (((float)logfreq) / (1 << 24))) / 7;
    FRAC_NUM detuneRatio = 0.0209 * EXP_FUNC(-0.396 * (((float)logfreq) / (1 << 24))) / 7;
    logfreq += detuneRatio * logfreq * (detune - 7);

    logfreq += coarsemul[coarse & 31];
    if (fine) {
      // (1 << 24) / log(2)
      //logfreq += (int32_t)floor(24204406.323123 * log(1 + 0.01 * fine) + 0.5);
      logfreq += (int32_t)floor(24204406.323123 * LOG_FUNC(1 + 0.01 * fine) + 0.5);
    }

    // // This was measured at 7.213Hz per count at 9600Hz, but the exact
    // // value is somewhat dependent on midinote. Close enough for now.
    // //logfreq += 12606 * (detune -7);
  } else {
    // ((1 << 24) * log(10) / log(2) * .01) << 3
    logfreq = (4458616 * ((coarse & 3) * 100 + fine)) >> 3;
    logfreq += detune > 7 ? 13457 * (detune - 7) : 0;
  }
  return logfreq;
}

const uint8_t velocity_data[64] = {
  0, 70, 86, 97, 106, 114, 121, 126, 132, 138, 142, 148, 152, 156, 160, 163,
  166, 170, 173, 174, 178, 181, 184, 186, 189, 190, 194, 196, 198, 200, 202,
  205, 206, 209, 211, 214, 216, 218, 220, 222, 224, 225, 227, 229, 230, 232,
  233, 235, 237, 238, 240, 241, 242, 243, 244, 246, 246, 248, 249, 250, 251,
  252, 253, 254
};

// See "velocity" section of notes. Returns velocity delta in microsteps.
int ScaleVelocity(int velocity, int sensitivity) {
  int clamped_vel = max(0, min(127, velocity));
  int vel_value = velocity_data[clamped_vel >> 1] - 239;
  int scaled_vel = ((sensitivity * vel_value + 7) >> 3) << 4;
  return scaled_vel;
}

int ScaleRate(int midinote, int sensitivity) {
  int x = min(31, max(0, midinote / 3 - 7));
  int qratedelta = (sensitivity * x) >> 3;
#ifdef SUPER_PRECISE
  int rem = x & 7;
  if (sensitivity == 3 && rem == 3) {
    qratedelta -= 1;
  } else if (sensitivity == 7 && rem > 0 && rem < 4) {
    qratedelta += 1;
  }
#endif
  return qratedelta;
}

const uint8_t exp_scale_data[] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 14, 16, 19, 23, 27, 33, 39, 47, 56, 66,
  80, 94, 110, 126, 142, 158, 174, 190, 206, 222, 238, 250
};

int ScaleCurve(int group, int depth, int curve) {
  int scale;
  if (curve == 0 || curve == 3) {
    // linear
    scale = (group * depth * 329) >> 12;
  } else {
    // exponential
    int n_scale_data = sizeof(exp_scale_data);
    int raw_exp = exp_scale_data[min(group, n_scale_data - 1)];
    scale = (raw_exp * depth * 329) >> 15;
  }
  if (curve < 2) {
    scale = -scale;
  }
  return scale;
}

int ScaleLevel(int midinote, int break_pt, int left_depth, int right_depth,
               int left_curve, int right_curve) {
  int offset = midinote - break_pt - 17;
  if (offset >= 0) {
    return ScaleCurve((offset + 1) / 3, right_depth, right_curve);
  } else {
    return ScaleCurve(-(offset - 1) / 3, left_depth, left_curve);
  }
}

static const uint8_t pitchmodsenstab[] = {
  0, 10, 20, 33, 55, 92, 153, 255
};

// 0, 66, 109, 255
static const uint32_t ampmodsenstab[] = {
  0, 4342338, 7171437, 16777216
};

Dx7Note::Dx7Note() {
  for (int op = 0; op < 6; op++) {
    params_[op].phase = 0;
    params_[op].gain_out = 0;
  }
}

//void Dx7Note::init(const uint8_t patch[156], int midinote, int velocity) {
void Dx7Note::init(const uint8_t patch[156], int midinote, int velocity, int srcnote, int porta, const Controllers *ctrls) {
  int rates[4];
  int levels[4];
  for (int op = 0; op < 6; op++) {
    int off = op * 21;
    for (int i = 0; i < 4; i++) {
      rates[i] = patch[off + i];
      levels[i] = patch[off + 4 + i];
    }
    int outlevel = patch[off + 16];
    outlevel = Env::scaleoutlevel(outlevel);
    int level_scaling = ScaleLevel(midinote, patch[off + 8], patch[off + 9],
                                   patch[off + 10], patch[off + 11], patch[off + 12]);
    outlevel += level_scaling;
    outlevel = min(127, outlevel);
    outlevel = outlevel << 5;
    outlevel += ScaleVelocity(velocity, patch[off + 15]);
    outlevel = max(0, outlevel);
    int rate_scaling = ScaleRate(midinote, patch[off + 13]);
    env_[op].init(rates, levels, outlevel, rate_scaling);

    int mode = patch[off + 17];
    int coarse = patch[off + 18];
    int fine = patch[off + 19];
    int detune = patch[off + 20];
    int32_t freq = osc_freq(midinote, mode, coarse, fine, detune);
    opMode[op] = mode;
    basepitch_[op] = freq;
    porta_curpitch_[op] = freq;
    ampmodsens_[op] = ampmodsenstab[patch[off + 14] & 3];

    if (porta >= 0)
      porta_curpitch_[op] = osc_freq(srcnote, mode, coarse, fine, detune);
  }
  for (int i = 0; i < 4; i++) {
    rates[i] = patch[126 + i];
    levels[i] = patch[130 + i];
  }
  pitchenv_.set(rates, levels);
  algorithm_ = patch[134];
  int feedback = patch[135];
  fb_shift_ = feedback != 0 ? FEEDBACK_BITDEPTH - feedback : 16;
  pitchmoddepth_ = (patch[139] * 165) >> 6;
  pitchmodsens_ = pitchmodsenstab[patch[143] & 7];
  ampmoddepth_ = (patch[140] * 165) >> 6;
  porta_rateindex_ = (porta < 128) ? porta : 127;
  porta_gliss_ = ctrls->values_[kControllerPortamentoGlissando];
}

void Dx7Note::compute(int32_t *buf, int32_t lfo_val, int32_t lfo_delay, const Controllers *ctrls) {
  // ==== PITCH ====
  uint32_t pmd = pitchmoddepth_ * lfo_delay;  // Q32
  int32_t senslfo = pitchmodsens_ * (lfo_val - (1 << 23));
  int32_t pmod_1 = (((int64_t) pmd) * (int64_t) senslfo) >> 39;
  pmod_1 = abs(pmod_1);
  int32_t pmod_2 = (int32_t)(((int64_t)ctrls->pitch_mod * (int64_t)senslfo) >> 14);
  pmod_2 = abs(pmod_2);
  int32_t pitch_mod = max(pmod_1, pmod_2);
  pitch_mod = pitchenv_.getsample() + (pitch_mod * (senslfo < 0 ? -1 : 1));

  // ---- PITCH BEND ----
  int pitchbend = ctrls->values_[kControllerPitch];
  int32_t pb = (pitchbend - 0x2000);
  if (pb != 0) {
    if (ctrls->values_[kControllerPitchStep] == 0) {
      pb = ((float) (pb << 11)) * ((float) ctrls->values_[kControllerPitchRange]) / 12.0;
    } else {
      int stp = 12 / ctrls->values_[kControllerPitchStep];
      pb = pb * stp / 8191;
      pb = (pb * (8191 / stp)) << 11;
    }
  }
  int32_t pitch_base = pb + ctrls->masterTune;
  pitch_mod += pitch_base;

  // ==== AMP MOD ====
  lfo_val = (1 << 24) - lfo_val;
  uint32_t amod_1 = (uint32_t)(((int64_t) ampmoddepth_ * (int64_t) lfo_delay) >> 8); // Q24 :D
  amod_1 = (uint32_t)(((int64_t) amod_1 * (int64_t) lfo_val) >> 24);
  uint32_t amod_2 = (uint32_t)(((int64_t) ctrls->amp_mod * (int64_t) lfo_val) >> 7); // Q?? :|
  uint32_t amd_mod = max(amod_1, amod_2);

  // ==== EG AMP MOD ====
  uint32_t amod_3 = (ctrls->eg_mod + 1) << 17;
  amd_mod = max((1 << 24) - amod_3, amd_mod);

  // ==== OP RENDER ====
  for (int op = 0; op < 6; op++) {
    // if ( ctrls->opSwitch[op] == '0' )  {
    if (!(ctrls->opSwitch & (1 << op)))  {
      env_[op].getsample(); // advance the envelop even if it is not playing
      params_[op].level_in = 0;
    } else {
      //int32_t gain = pow(2, 10 + level * (1.0 / (1 << 24)));

      int32_t basepitch = basepitch_[op];

      if ( opMode[op] )
        params_[op].freq = Freqlut::lookup(basepitch + pitch_base);
      else {
        if ( porta_rateindex_ >= 0 ) {
          basepitch = porta_curpitch_[op];
          if ( porta_gliss_ )
            basepitch = logfreq_round2semi(basepitch);
        }
        params_[op].freq = Freqlut::lookup(basepitch + pitch_mod);
      }

      int32_t level = env_[op].getsample();
      if (ampmodsens_[op] != 0) {
        uint32_t sensamp = (uint32_t)(((uint64_t) amd_mod) * ((uint64_t) ampmodsens_[op]) >> 24);

        // TODO: mehhh.. this needs some real tuning.
        //uint32_t pt = exp(((float)sensamp) / 262144 * 0.07 + 12.2);
        uint32_t pt = EXP_FUNC(((float)sensamp) / 262144 * 0.07 + 12.2);
        uint32_t ldiff = (uint32_t)(((uint64_t)level) * (((uint64_t)pt << 4)) >> 28);
        level -= ldiff;
      }
      params_[op].level_in = level;
    }
  }

  // ==== PORTAMENTO ====
  int porta = porta_rateindex_;
  if ( porta >= 0 ) {
    int32_t rate = Porta::rates[porta];
    for (int op = 0; op < 6; op++) {
      int32_t cur = porta_curpitch_[op];
      int32_t dst = basepitch_[op];

      bool going_up = cur < dst;
      int32_t newpitch = cur + (going_up ? +rate : -rate);

      if ( going_up ? (cur > dst) : (cur < dst) )
        newpitch = dst;

      porta_curpitch_[op] = newpitch;
    }
  }

  ctrls->core->render(buf, params_, algorithm_, fb_buf_, fb_shift_);
}

void Dx7Note::keyup() {
  for (int op = 0; op < 6; op++) {
    env_[op].keydown(false);
  }
  pitchenv_.keydown(false);
}

void Dx7Note::update(const uint8_t patch[156], int midinote, int velocity, int porta, const Controllers *ctrls) {
  int rates[4];
  int levels[4];
  for (int op = 0; op < 6; op++) {
    int off = op * 21;
    int mode = patch[off + 17];
    int coarse = patch[off + 18];
    int fine = patch[off + 19];
    int detune = patch[off + 20];
    int32_t freq = osc_freq(midinote, mode, coarse, fine, detune);
    basepitch_[op] = freq;
    porta_curpitch_[op] = freq;
    ampmodsens_[op] = ampmodsenstab[patch[off + 14] & 3];
    opMode[op] = mode;

    for (int i = 0; i < 4; i++) {
      rates[i] = patch[off + i];
      levels[i] = patch[off + 4 + i];
    }
    int outlevel = patch[off + 16];
    outlevel = Env::scaleoutlevel(outlevel);
    int level_scaling = ScaleLevel(midinote, patch[off + 8], patch[off + 9],
                                   patch[off + 10], patch[off + 11], patch[off + 12]);
    outlevel += level_scaling;
    outlevel = min(127, outlevel);
    outlevel = outlevel << 5;
    outlevel += ScaleVelocity(velocity, patch[off + 15]);
    outlevel = max(0, outlevel);
    int rate_scaling = ScaleRate(midinote, patch[off + 13]);
    env_[op].update(rates, levels, outlevel, rate_scaling);
  }
  algorithm_ = patch[134];
  int feedback = patch[135];
  fb_shift_ = feedback != 0 ? FEEDBACK_BITDEPTH - feedback : 16;
  pitchmoddepth_ = (patch[139] * 165) >> 6;
  pitchmodsens_ = pitchmodsenstab[patch[143] & 7];
  ampmoddepth_ = (patch[140] * 165) >> 6;
  porta_rateindex_ = (porta < 128) ? porta : 127;
  porta_gliss_ = ctrls->values_[kControllerPortamentoGlissando];

}

void Dx7Note::peekVoiceStatus(VoiceStatus &status) {
  for (int i = 0; i < 6; i++) {
    status.amp[i] = Exp2::lookup(params_[i].level_in - (14 * (1 << 24)));
    env_[i].getPosition(&status.ampStep[i]);
  }
  pitchenv_.getPosition(&status.pitchStep);
}

/**
   Used in monophonic mode to transfer voice state from different notes
*/
void Dx7Note::transferState(Dx7Note &src) {
  for (int i = 0; i < 6; i++) {
    env_[i].transfer(src.env_[i]);
    params_[i].gain_out = src.params_[i].gain_out;
    params_[i].phase = src.params_[i].phase;
  }
}

void Dx7Note::transferSignal(Dx7Note &src) {
  for (int i = 0; i < 6; i++) {
    params_[i].gain_out = src.params_[i].gain_out;
    params_[i].phase = src.params_[i].phase;
  }
}

void Dx7Note::transferPortamento(Dx7Note &src) {
  for (int i = 0; i < 6; i++) {
    porta_curpitch_[i] = src.porta_curpitch_[i];
  }
}

void Dx7Note::oscSync() {
  for (int i = 0; i < 6; i++) {
    params_[i].gain_out = 0;
    params_[i].phase = 0;
  }
}
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

//using namespace std;

uint32_t Env::sr_multiplier = (1 << 24);

const int levellut[] = {
  0, 5, 9, 13, 17, 20, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41, 42, 43, 45, 46
};

#ifdef ACCURATE_ENVELOPE
const int statics[] = {
  1764000, 1764000, 1411200, 1411200, 1190700, 1014300, 992250,
  882000, 705600, 705600, 584325, 507150, 502740, 441000, 418950,
  352800, 308700, 286650, 253575, 220500, 220500, 176400, 145530,
  145530, 125685, 110250, 110250, 88200, 88200, 74970, 61740,
  61740, 55125, 48510, 44100, 37485, 31311, 30870, 27562, 27562,
  22050, 18522, 17640, 15435, 14112, 13230, 11025, 9261, 9261, 7717,
  6615, 6615, 5512, 5512, 4410, 3969, 3969, 3439, 2866, 2690, 2249,
  1984, 1896, 1808, 1411, 1367, 1234, 1146, 926, 837, 837, 705,
  573, 573, 529, 441, 441
  // and so on, I stopped measuring after R=76 (needs to be double-checked anyway)
};
#endif

void Env::init_sr(double sampleRate) {
  sr_multiplier = (44100.0 / sampleRate) * (1 << 24);
}

void Env::init(const int r[4], const int l[4], int ol, int rate_scaling) {
  for (int i = 0; i < 4; i++) {
    rates_[i] = r[i];
    levels_[i] = l[i];
  }
  outlevel_ = ol;
  rate_scaling_ = rate_scaling;
  level_ = 0;
  down_ = true;
  advance(0);
}

int32_t Env::getsample() {
#ifdef ACCURATE_ENVELOPE
  if (staticcount_) {
    staticcount_ -= _N_;
    if (staticcount_ <= 0) {
      staticcount_ = 0;
      advance(ix_ + 1);
    }
  }
#endif

  if (ix_ < 3 || ((ix_ < 4) && !down_)) {
    if (staticcount_) {
      ;
    }
    else if (rising_) {
      const int jumptarget = 1716;
      if (level_ < (jumptarget << 16)) {
        level_ = jumptarget << 16;
      }
      level_ += (((17 << 24) - level_) >> 24) * inc_;
      // TODO: should probably be more accurate when inc is large
      if (level_ >= targetlevel_) {
        level_ = targetlevel_;
        advance(ix_ + 1);
      }
    }
    else {  // !rising
      level_ -= inc_;
      if (level_ <= targetlevel_) {
        level_ = targetlevel_;
        advance(ix_ + 1);
      }
    }
  }
  // TODO: this would be a good place to set level to 0 when under threshold
  return level_;
}

void Env::keydown(bool d) {
  if (down_ != d) {
    down_ = d;
    advance(d ? 0 : 3);
  }
}

int Env::scaleoutlevel(int outlevel) {
  return outlevel >= 20 ? 28 + outlevel : levellut[outlevel];
}

void Env::advance(int newix) {
  ix_ = newix;
  if (ix_ < 4) {
    int newlevel = levels_[ix_];
    int actuallevel = scaleoutlevel(newlevel) >> 1;
    actuallevel = (actuallevel << 6) + outlevel_ - 4256;
    actuallevel = actuallevel < 16 ? 16 : actuallevel;
    // level here is same as Java impl
    targetlevel_ = actuallevel << 16;
    rising_ = (targetlevel_ > level_);

    // rate
    int qrate = (rates_[ix_] * 41) >> 6;
    qrate += rate_scaling_;
    qrate = min(qrate, 63);

#ifdef ACCURATE_ENVELOPE
    if (targetlevel_ == level_ || (ix_ == 0 && newlevel == 0)) {
      // approximate number of samples at 44.100 kHz to achieve the time
      // empirically gathered using 2 TF1s, could probably use some double-checking
      // and cleanup, but it's pretty close for now.
      int staticrate = rates_[ix_];
      staticrate += rate_scaling_; // needs to be checked, as well, but seems correct
      staticrate = min(staticrate, 99);
      staticcount_ = staticrate < 77 ? statics[staticrate] : 20 * (99 - staticrate);
      if (staticrate < 77 && (ix_ == 0 && newlevel == 0)) {
        staticcount_ /= 20; // attack is scaled faster
      }
      staticcount_ = (int)(((int64_t)staticcount_ * (int64_t)sr_multiplier) >> 24);
    }
    else {
      staticcount_ = 0;
    }
#endif
    inc_ = (4 + (qrate & 3)) << (2 + LG_N + (qrate >> 2));
    // meh, this should be fixed elsewhere
    inc_ = (int)(((int64_t)inc_ * (int64_t)sr_multiplier) >> 24);
  }
}

void Env::update(const int r[4], const int l[4], int ol, int rate_scaling) {
  for (int i = 0; i < 4; i++) {
    rates_[i] = r[i];
    levels_[i] = l[i];
  }
  outlevel_ = ol;
  rate_scaling_ = rate_scaling;
  if ( down_ ) {
    // for now we simply reset ourselves at level 3
    int newlevel = levels_[2];
    int actuallevel = scaleoutlevel(newlevel) >> 1;
    actuallevel = (actuallevel << 6) - 4256;
    actuallevel = actuallevel < 16 ? 16 : actuallevel;
    targetlevel_ = actuallevel << 16;
    advance(2);
  }
}

void Env::getPosition(char *step) {
  *step = ix_;
}

void Env::transfer(Env &src) {
  for (int i = 0; i < 4; i++) {
    rates_[i] = src.rates_[i];
    levels_[i] = src.levels_[i];
  }
  outlevel_ = src.outlevel_;
  rate_scaling_ = src.rate_scaling_;
  level_ = src.level_;
  targetlevel_ = src.targetlevel_;
  rising_ = src.rising_;
  ix_ = src.ix_;
  down_ = src.down_;
#ifdef ACCURATE_ENVELOPE
  staticcount_ = src.staticcount_;
#endif
  inc_ = src.inc_;
}
/*
 * Copyright 2013 Google Inc.
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *      http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifdef _MSC_VER
#define exp2(arg) pow(2.0, arg)
#endif

int32_t exp2tab[EXP2_N_SAMPLES << 1];

void Exp2::init() {
  FRAC_NUM inc = exp2(1.0 / EXP2_N_SAMPLES);
  FRAC_NUM y = 1 << 30;
  for (int i = 0; i < EXP2_N_SAMPLES; i++) {
    exp2tab[(i << 1) + 1] = (int32_t)floor(y + 0.5);
    y *= inc;
  }
  for (int i = 0; i < EXP2_N_SAMPLES - 1; i++) {
    exp2tab[i << 1] = exp2tab[(i << 1) + 3] - exp2tab[(i << 1) + 1];
  }
  exp2tab[(EXP2_N_SAMPLES << 1) - 2] = (1U << 31) - exp2tab[(EXP2_N_SAMPLES << 1) - 1];
}

int32_t tanhtab[TANH_N_SAMPLES << 1];

static FRAC_NUM dtanh(FRAC_NUM y) {
  return 1 - y * y;
}

void Tanh::init() {
  FRAC_NUM step = 4.0 / TANH_N_SAMPLES;
  FRAC_NUM y = 0;
  for (int i = 0; i < TANH_N_SAMPLES; i++) {
    tanhtab[(i << 1) + 1] = (1 << 24) * y + 0.5;
    //printf("%d\n", tanhtab[(i << 1) + 1]);
    // Use a basic 4th order Runge-Kutte to compute tanh from its
    // differential equation.
    FRAC_NUM k1 = dtanh(y);
    FRAC_NUM k2 = dtanh(y + 0.5 * step * k1);
    FRAC_NUM k3 = dtanh(y + 0.5 * step * k2);
    FRAC_NUM k4 = dtanh(y + step * k3);
    FRAC_NUM dy = (step / 6) * (k1 + k4 + 2 * (k2 + k3));
    y += dy;
  }
  for (int i = 0; i < TANH_N_SAMPLES - 1; i++) {
    tanhtab[i << 1] = tanhtab[(i << 1) + 3] - tanhtab[(i << 1) + 1];
  }
  int32_t lasty = (1 << 24) * y + 0.5;
  tanhtab[(TANH_N_SAMPLES << 1) - 2] = lasty - tanhtab[(TANH_N_SAMPLES << 1) - 1];
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

//using namespace std;

const FmAlgorithm FmCore::algorithms[32] = {
  { { 0xc1, 0x11, 0x11, 0x14, 0x01, 0x14 } }, // 1
  { { 0x01, 0x11, 0x11, 0x14, 0xc1, 0x14 } }, // 2
  { { 0xc1, 0x11, 0x14, 0x01, 0x11, 0x14 } }, // 3
  { { 0xc1, 0x11, 0x94, 0x01, 0x11, 0x14 } }, // 4
  { { 0xc1, 0x14, 0x01, 0x14, 0x01, 0x14 } }, // 5
  { { 0xc1, 0x94, 0x01, 0x14, 0x01, 0x14 } }, // 6
  { { 0xc1, 0x11, 0x05, 0x14, 0x01, 0x14 } }, // 7
  { { 0x01, 0x11, 0xc5, 0x14, 0x01, 0x14 } }, // 8
  { { 0x01, 0x11, 0x05, 0x14, 0xc1, 0x14 } }, // 9
  { { 0x01, 0x05, 0x14, 0xc1, 0x11, 0x14 } }, // 10
  { { 0xc1, 0x05, 0x14, 0x01, 0x11, 0x14 } }, // 11
  { { 0x01, 0x05, 0x05, 0x14, 0xc1, 0x14 } }, // 12
  { { 0xc1, 0x05, 0x05, 0x14, 0x01, 0x14 } }, // 13
  { { 0xc1, 0x05, 0x11, 0x14, 0x01, 0x14 } }, // 14
  { { 0x01, 0x05, 0x11, 0x14, 0xc1, 0x14 } }, // 15
  { { 0xc1, 0x11, 0x02, 0x25, 0x05, 0x14 } }, // 16
  { { 0x01, 0x11, 0x02, 0x25, 0xc5, 0x14 } }, // 17
  { { 0x01, 0x11, 0x11, 0xc5, 0x05, 0x14 } }, // 18
  { { 0xc1, 0x14, 0x14, 0x01, 0x11, 0x14 } }, // 19
  { { 0x01, 0x05, 0x14, 0xc1, 0x14, 0x14 } }, // 20
  { { 0x01, 0x14, 0x14, 0xc1, 0x14, 0x14 } }, // 21
  { { 0xc1, 0x14, 0x14, 0x14, 0x01, 0x14 } }, // 22
  { { 0xc1, 0x14, 0x14, 0x01, 0x14, 0x04 } }, // 23
  { { 0xc1, 0x14, 0x14, 0x14, 0x04, 0x04 } }, // 24
  { { 0xc1, 0x14, 0x14, 0x04, 0x04, 0x04 } }, // 25
  { { 0xc1, 0x05, 0x14, 0x01, 0x14, 0x04 } }, // 26
  { { 0x01, 0x05, 0x14, 0xc1, 0x14, 0x04 } }, // 27
  { { 0x04, 0xc1, 0x11, 0x14, 0x01, 0x14 } }, // 28
  { { 0xc1, 0x14, 0x01, 0x14, 0x04, 0x04 } }, // 29
  { { 0x04, 0xc1, 0x11, 0x14, 0x04, 0x04 } }, // 30
  { { 0xc1, 0x14, 0x04, 0x04, 0x04, 0x04 } }, // 31
  { { 0xc4, 0x04, 0x04, 0x04, 0x04, 0x04 } }, // 32
};

int n_out(const FmAlgorithm &alg) {
  int count = 0;
  for (int i = 0; i < 6; i++) {
    if ((alg.ops[i] & 7) == OUT_BUS_ADD) count++;
  }
  return count;
}

uint8_t FmCore::get_carrier_operators(uint8_t algorithm)
{
  uint8_t op_out = 0;
  FmAlgorithm alg = algorithms[algorithm];

  for (uint8_t i = 0; i < 6; i++)
  {
    if ((alg.ops[i]&OUT_BUS_ADD) == OUT_BUS_ADD)
      op_out |= 1 << i;
  }

  return op_out;
}

void FmCore::dump() {
#ifdef VERBOSE
  for (int i = 0; i < 32; i++) {
    cout << (i + 1) << ":";
    const FmAlgorithm &alg = algorithms[i];
    for (int j = 0; j < 6; j++) {
      int flags = alg.ops[j];
      cout << " ";
      if (flags & FB_IN) cout << "[";
      cout << (flags & IN_BUS_ONE ? "1" : flags & IN_BUS_TWO ? "2" : "0") << "->";
      cout << (flags & OUT_BUS_ONE ? "1" : flags & OUT_BUS_TWO ? "2" : "0");
      if (flags & OUT_BUS_ADD) cout << "+";
      //cout << alg.ops[j].in << "->" << alg.ops[j].out;
      if (flags & FB_OUT) cout << "]";
    }
    cout << " " << n_out(alg);
    cout << endl;
  }
#endif
}

void FmCore::render(int32_t *output, FmOpParams *params, int algorithm, int32_t *fb_buf, int feedback_shift) {
  const int kLevelThresh = 1120;
  const FmAlgorithm alg = algorithms[algorithm];
  bool has_contents[3] = { true, false, false };
  for (int op = 0; op < 6; op++) {
    int flags = alg.ops[op];
    bool add = (flags & OUT_BUS_ADD) != 0;
    FmOpParams &param = params[op];
    int inbus = (flags >> 4) & 3;
    int outbus = flags & 3;
    int32_t *outptr = (outbus == 0) ? output : buf_[outbus - 1].get();
    int32_t gain1 = param.gain_out;
    int32_t gain2 = Exp2::lookup(param.level_in - (14 * (1 << 24)));
    param.gain_out = gain2;

    if (gain1 >= kLevelThresh || gain2 >= kLevelThresh) {
      if (!has_contents[outbus]) {
        add = false;
      }
      if (inbus == 0 || !has_contents[inbus]) {
        // todo: more than one op in a feedback loop
        if ((flags & 0xc0) == 0xc0 && feedback_shift < 16) {
          // cout << op << " fb " << inbus << outbus << add << endl;
          FmOpKernel::compute_fb(outptr, param.phase, param.freq,
                                 gain1, gain2,
                                 fb_buf, feedback_shift, add);
        } else {
          // cout << op << " pure " << inbus << outbus << add << endl;
          FmOpKernel::compute_pure(outptr, param.phase, param.freq,
                                   gain1, gain2, add);
        }
      } else {
        // cout << op << " normal " << inbus << outbus << " " << param.freq << add << endl;
        FmOpKernel::compute(outptr, buf_[inbus - 1].get(),
                            param.phase, param.freq, gain1, gain2, add);
      }
      has_contents[outbus] = true;
    } else if (!add) {
      has_contents[outbus] = false;
    }
    param.phase += param.freq << LG_N;
  }
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

#ifdef HAVE_NEON
static bool hasNeon() {
  return true;
  return (android_getCpuFeatures() & ANDROID_CPU_ARM_FEATURE_NEON) != 0;
}

extern "C"
void neon_fm_kernel(const int *in, const int *busin, int *out, int count,
                    int32_t phase0, int32_t freq, int32_t gain1, int32_t dgain);

#else
static bool hasNeon() {
  return false;
}
#endif

void FmOpKernel::compute(int32_t *output, const int32_t *input,
                         int32_t phase0, int32_t freq,
                         int32_t gain1, int32_t gain2, bool add) {
  int32_t dgain = (gain2 - gain1 + (_N_ >> 1)) >> LG_N;
  int32_t gain = gain1;
  int32_t phase = phase0;
  if (hasNeon()) {
#ifdef HAVE_NEON
    neon_fm_kernel(input, add ? output : zeros, output, _N_,
                   phase0, freq, gain, dgain);
#endif
  } else {
    if (add) {
      for (int i = 0; i < _N_; i++) {
        gain += dgain;
        int32_t y = Sin::lookup(phase + input[i]);
        int32_t y1 = ((int64_t)y * (int64_t)gain) >> 24;
        output[i] += y1;
        phase += freq;
      }
    } else {
      for (int i = 0; i < _N_; i++) {
        gain += dgain;
        int32_t y = Sin::lookup(phase + input[i]);
        int32_t y1 = ((int64_t)y * (int64_t)gain) >> 24;
        output[i] = y1;
        phase += freq;
      }
    }
  }
}

void FmOpKernel::compute_pure(int32_t *output, int32_t phase0, int32_t freq,
                              int32_t gain1, int32_t gain2, bool add) {
  int32_t dgain = (gain2 - gain1 + (_N_ >> 1)) >> LG_N;
  int32_t gain = gain1;
  int32_t phase = phase0;
  if (hasNeon()) {
#ifdef HAVE_NEON
    neon_fm_kernel(zeros, add ? output : zeros, output, _N_,
                   phase0, freq, gain, dgain);
#endif
  } else {
    if (add) {
      for (int i = 0; i < _N_; i++) {
        gain += dgain;
        int32_t y = Sin::lookup(phase);
        int32_t y1 = ((int64_t)y * (int64_t)gain) >> 24;
        output[i] += y1;
        phase += freq;
      }
    } else {
      for (int i = 0; i < _N_; i++) {
        gain += dgain;
        int32_t y = Sin::lookup(phase);
        int32_t y1 = ((int64_t)y * (int64_t)gain) >> 24;
        output[i] = y1;
        phase += freq;
      }
    }
  }
}

#define noDOUBLE_ACCURACY
#define HIGH_ACCURACY

void FmOpKernel::compute_fb(int32_t *output, int32_t phase0, int32_t freq,
                            int32_t gain1, int32_t gain2,
                            int32_t *fb_buf, int fb_shift, bool add) {
  int32_t dgain = (gain2 - gain1 + (_N_ >> 1)) >> LG_N;
  int32_t gain = gain1;
  int32_t phase = phase0;
  int32_t y0 = fb_buf[0];
  int32_t y = fb_buf[1];
  if (add) {
    for (int i = 0; i < _N_; i++) {
      gain += dgain;
      int32_t scaled_fb = (y0 + y) >> (fb_shift + 1);
      y0 = y;
      y = Sin::lookup(phase + scaled_fb);
      y = ((int64_t)y * (int64_t)gain) >> 24;
      output[i] += y;
      phase += freq;
    }
  } else {
    for (int i = 0; i < _N_; i++) {
      gain += dgain;
      int32_t scaled_fb = (y0 + y) >> (fb_shift + 1);
      y0 = y;
      y = Sin::lookup(phase + scaled_fb);
      y = ((int64_t)y * (int64_t)gain) >> 24;
      output[i] = y;
      phase += freq;
    }
  }
  fb_buf[0] = y0;
  fb_buf[1] = y;
}

////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////

// Experimental sine wave generators below
#if 0
// Results: accuracy 64.3 mean, 170 worst case
// high accuracy: 5.0 mean, 49 worst case
void FmOpKernel::compute_pure(int32_t *output, int32_t phase0, int32_t freq,
                              int32_t gain1, int32_t gain2, bool add) {
  int32_t dgain = (gain2 - gain1 + (_N_ >> 1)) >> LG_N;
  int32_t gain = gain1;
  int32_t phase = phase0;
#ifdef HIGH_ACCURACY
  int32_t u = Sin::compute10(phase << 6);
  u = ((int64_t)u * gain) >> 30;
  int32_t v = Sin::compute10((phase << 6) + (1 << 28));  // quarter cycle
  v = ((int64_t)v * gain) >> 30;
  int32_t s = Sin::compute10(freq << 6);
  int32_t c = Sin::compute10((freq << 6) + (1 << 28));
#else
  int32_t u = Sin::compute(phase);
  u = ((int64_t)u * gain) >> 24;
  int32_t v = Sin::compute(phase + (1 << 22));  // quarter cycle
  v = ((int64_t)v * gain) >> 24;
  int32_t s = Sin::compute(freq) << 6;
  int32_t c = Sin::compute(freq + (1 << 22)) << 6;
#endif
  for (int i = 0; i < _N_; i++) {
    output[i] = u;
    int32_t t = ((int64_t)v * (int64_t)c - (int64_t)u * (int64_t)s) >> 30;
    u = ((int64_t)u * (int64_t)c + (int64_t)v * (int64_t)s) >> 30;
    v = t;
  }
}
#endif

#if 0
// Results: accuracy 392.3 mean, 15190 worst case (near freq = 0.5)
// for freq < 0.25, 275.2 mean, 716 worst
// high accuracy: 57.4 mean, 7559 worst
//  freq < 0.25: 17.9 mean, 78 worst
void FmOpKernel::compute_pure(int32_t *output, int32_t phase0, int32_t freq,
                              int32_t gain1, int32_t gain2, bool add) {
  int32_t dgain = (gain2 - gain1 + (_N_ >> 1)) >> LG_N;
  int32_t gain = gain1;
  int32_t phase = phase0;
#ifdef HIGH_ACCURACY
  int32_t u = floor(gain * sin(phase * (M_PI / (1 << 23))) + 0.5);
  int32_t v = floor(gain * cos((phase - freq * 0.5) * (M_PI / (1 << 23))) + 0.5);
  int32_t a = floor((1 << 25) * sin(freq * (M_PI / (1 << 24))) + 0.5);
#else
  int32_t u = Sin::compute(phase);
  u = ((int64_t)u * gain) >> 24;
  int32_t v = Sin::compute(phase + (1 << 22) - (freq >> 1));
  v = ((int64_t)v * gain) >> 24;
  int32_t a = Sin::compute(freq >> 1) << 1;
#endif
  for (int i = 0; i < _N_; i++) {
    output[i] = u;
    v -= ((int64_t)a * (int64_t)u) >> 24;
    u += ((int64_t)a * (int64_t)v) >> 24;
  }
}
#endif

#if 0
// Results: accuracy 370.0 mean, 15480 worst case (near freq = 0.5)
// with FRAC_NUM accuracy initialization: mean 1.55, worst 58 (near freq = 0)
// with high accuracy: mean 4.2, worst 292 (near freq = 0.5)
void FmOpKernel::compute_pure(int32_t *output, int32_t phase0, int32_t freq,
                              int32_t gain1, int32_t gain2, bool add) {
  int32_t dgain = (gain2 - gain1 + (_N_ >> 1)) >> LG_N;
  int32_t gain = gain1;
  int32_t phase = phase0;
#ifdef DOUBLE_ACCURACY
  int32_t u = floor((1 << 30) * sin(phase * (M_PI / (1 << 23))) + 0.5);
  FRAC_NUM a_d = sin(freq * (M_PI / (1 << 24)));
  int32_t v = floor((1LL << 31) * a_d * cos((phase - freq * 0.5) *
                    (M_PI / (1 << 23))) + 0.5);
  int32_t aa = floor((1LL << 31) * a_d * a_d + 0.5);
#else
#ifdef HIGH_ACCURACY
  int32_t u = Sin::compute10(phase << 6);
  int32_t v = Sin::compute10((phase << 6) + (1 << 28) - (freq << 5));
  int32_t a = Sin::compute10(freq << 5);
  v = ((int64_t)v * (int64_t)a) >> 29;
  int32_t aa = ((int64_t)a * (int64_t)a) >> 29;
#else
  int32_t u = Sin::compute(phase) << 6;
  int32_t v = Sin::compute(phase + (1 << 22) - (freq >> 1));
  int32_t a = Sin::compute(freq >> 1);
  v = ((int64_t)v * (int64_t)a) >> 17;
  int32_t aa = ((int64_t)a * (int64_t)a) >> 17;
#endif
#endif

  if (aa < 0) aa = (1 << 31) - 1;
  for (int i = 0; i < _N_; i++) {
    gain += dgain;
    output[i] = ((int64_t)u * (int64_t)gain) >> 30;
    v -= ((int64_t)aa * (int64_t)u) >> 29;
    u += v;
  }
}
#endif

#if 0
// Results:: accuracy 112.3 mean, 4262 worst (near freq = 0.5)
// high accuracy 2.9 mean, 143 worst
void FmOpKernel::compute_pure(int32_t *output, int32_t phase0, int32_t freq,
                              int32_t gain1, int32_t gain2, bool add) {
  int32_t dgain = (gain2 - gain1 + (_N_ >> 1)) >> LG_N;
  int32_t gain = gain1;
  int32_t phase = phase0;
#ifdef HIGH_ACCURACY
  int32_t u = Sin::compute10(phase << 6);
  int32_t lastu = Sin::compute10((phase - freq) << 6);
  int32_t a = Sin::compute10((freq << 6) + (1 << 28)) << 1;
#else
  int32_t u = Sin::compute(phase) << 6;
  int32_t lastu = Sin::compute(phase - freq) << 6;
  int32_t a = Sin::compute(freq + (1 << 22)) << 7;
#endif
  if (a < 0 && freq < 256) a = (1 << 31) - 1;
  if (a > 0 && freq > 0x7fff00) a = -(1 << 31);
  for (int i = 0; i < _N_; i++) {
    gain += dgain;
    output[i] = ((int64_t)u * (int64_t)gain) >> 30;
    //output[i] = u;
    int32_t newu = (((int64_t)u * (int64_t)a) >> 30) - lastu;
    lastu = u;
    u = newu;
  }
}
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

// Resolve frequency signal (1.0 in Q24 format = 1 octave) to phase delta.

// The LUT is just a global, and we'll need the init function to be called before
// use.

#define LG_N_SAMPLES 10
#define N_SAMPLES (1 << LG_N_SAMPLES)
#define SAMPLE_SHIFT (24 - LG_N_SAMPLES)

#define MAX_LOGFREQ_INT 20

int32_t lut[N_SAMPLES + 1];

void Freqlut::init(FRAC_NUM sample_rate) {
  FRAC_NUM y = (1LL << (24 + MAX_LOGFREQ_INT)) / sample_rate;
  FRAC_NUM inc = pow(2, 1.0 / N_SAMPLES);
  for (int i = 0; i < N_SAMPLES + 1; i++) {
    lut[i] = (int32_t)floor(y + 0.5);
    y *= inc;
  }
}

// Note: if logfreq is more than 20.0, the results will be inaccurate. However,
// that will be many times the Nyquist rate.
int32_t Freqlut::lookup(int32_t logfreq) {
  int ix = (logfreq & 0xffffff) >> SAMPLE_SHIFT;

  int32_t y0 = lut[ix];
  int32_t y1 = lut[ix + 1];
  int lowbits = logfreq & ((1 << SAMPLE_SHIFT) - 1);
  int32_t y = y0 + ((((int64_t)(y1 - y0) * (int64_t)lowbits)) >> SAMPLE_SHIFT);
  int hibits = logfreq >> 24;
  return y >> (MAX_LOGFREQ_INT - hibits);
}
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

uint32_t Lfo::unit_;

void Lfo::init(FRAC_NUM sample_rate) {
  // constant is 1 << 32 / 15.5s / 11
  Lfo::unit_ = (int32_t)(_N_ * 25190424 / sample_rate + 0.5);
}

void Lfo::reset(const uint8_t params[6]) {
  int rate = params[0];  // 0..99
  int sr = rate == 0 ? 1 : (165 * rate) >> 6;
  sr *= sr < 160 ? 11 : (11 + ((sr - 160) >> 4));
  delta_ = unit_ * sr;
  int a = 99 - params[1];  // LFO delay
  if (a == 99) {
    delayinc_ = ~0u;
    delayinc2_ = ~0u;
  } else {
    a = (16 + (a & 15)) << (1 + (a >> 4));
    delayinc_ = unit_ * a;
    a &= 0xff80;
    a = max(0x80, a);
    delayinc2_ = unit_ * a;
  }
  waveform_ = params[5];
  sync_ = params[4] != 0;
}

int32_t Lfo::getsample() {
  phase_ += delta_;
  int32_t x;
  switch (waveform_) {
    case 0:  // triangle
      x = phase_ >> 7;
      x ^= -(phase_ >> 31);
      x &= (1 << 24) - 1;
      return x;
    case 1:  // sawtooth down
      return (~phase_ ^ (1U << 31)) >> 8;
    case 2:  // sawtooth up
      return (phase_ ^ (1U << 31)) >> 8;
    case 3:  // square
      return ((~phase_) >> 7) & (1 << 24);
    case 4:  // sine
      return (1 << 23) + (Sin::lookup(phase_ >> 8) >> 1);
    case 5:  // s&h
      if (phase_ < delta_) {
        randstate_ = (randstate_ * 179 + 17) & 0xff;
      }
      x = randstate_ ^ 0x80;
      return (x + 1) << 16;
  }
  return 1 << 23;
}

int32_t Lfo::getdelay() {
  uint32_t delta = delaystate_ < (1U << 31) ? delayinc_ : delayinc2_;
  uint64_t d = ((uint64_t)delaystate_) + delta;
  if (d > ~0u) {
    return 1 << 24;
  }
  delaystate_ = d;
  if (d < (1U << 31)) {
    return 0;
  } else {
    return (d >> 7) & ((1 << 24) - 1);
  }
}

void Lfo::keydown() {
  if (sync_) {
    phase_ = (1U << 31) - 1;
  }
  delaystate_ = 0;
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

int PitchEnv::unit_;

void PitchEnv::init(FRAC_NUM sample_rate) {
  unit_ = _N_ * (1 << 24) / (21.3 * sample_rate) + 0.5;
}

const uint8_t pitchenv_rate[] = {
  1, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12,
  12, 13, 13, 14, 14, 15, 16, 16, 17, 18, 18, 19, 20, 21, 22, 23, 24,
  25, 26, 27, 28, 30, 31, 33, 34, 36, 37, 38, 39, 41, 42, 44, 46, 47,
  49, 51, 53, 54, 56, 58, 60, 62, 64, 66, 68, 70, 72, 74, 76, 79, 82,
  85, 88, 91, 94, 98, 102, 106, 110, 115, 120, 125, 130, 135, 141, 147,
  153, 159, 165, 171, 178, 185, 193, 202, 211, 232, 243, 254, 255
};

const int8_t pitchenv_tab[] = {
  -128, -116, -104, -95, -85, -76, -68, -61, -56, -52, -49, -46, -43,
  -41, -39, -37, -35, -33, -32, -31, -30, -29, -28, -27, -26, -25, -24,
  -23, -22, -21, -20, -19, -18, -17, -16, -15, -14, -13, -12, -11, -10,
  -9, -8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
  11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,
  28, 29, 30, 31, 32, 33, 34, 35, 38, 40, 43, 46, 49, 53, 58, 65, 73,
  82, 92, 103, 115, 127
};

void PitchEnv::set(const int r[4], const int l[4]) {
  for (int i = 0; i < 4; i++) {
    rates_[i] = r[i];
    levels_[i] = l[i];
  }
  level_ = pitchenv_tab[l[3]] << 19;
  down_ = true;
  advance(0);
}

int32_t PitchEnv::getsample() {
  if (ix_ < 3 || ((ix_ < 4) && !down_)) {
    if (rising_) {
      level_ += inc_;
      if (level_ >= targetlevel_) {
        level_ = targetlevel_;
        advance(ix_ + 1);
      }
    } else {  // !rising
      level_ -= inc_;
      if (level_ <= targetlevel_) {
        level_ = targetlevel_;
        advance(ix_ + 1);
      }
    }
  }
  return level_;
}

void PitchEnv::keydown(bool d) {
  if (down_ != d) {
    down_ = d;
    advance(d ? 0 : 3);
  }
}

void PitchEnv::advance(int newix) {
  ix_ = newix;
  if (ix_ < 4) {
    int newlevel = levels_[ix_];
    targetlevel_ = pitchenv_tab[newlevel] << 19;
    rising_ = (targetlevel_ > level_);
    inc_ = pitchenv_rate[rates_[ix_]] * unit_;
  }
}

void PitchEnv::getPosition(char *step) {
  *step = ix_;
}
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

void Porta::init_sr(double sampleRate)
{
  // compute portamento for CC 7-bit range

  for (unsigned int i = 0; i < 128; ++i) {
    // number of semitones travelled
    double sps = 350.0 * pow(2.0, -0.062 * i);  // per second
    double spf = sps / sampleRate;              // per frame
    double spp = spf * _N_;                       // per period
    const int step = (1 << 24) / 12;
    rates[i] = (int32_t)(0.5f + step * spp);    // to pitch units
  }
}

int32_t Porta::rates[128];
/*
 * Copyright 2012 Google Inc.
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *      http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define R (1 << 29)

#ifdef SIN_DELTA
int32_t sintab[SIN_N_SAMPLES << 1];
#else
int32_t sintab[SIN_N_SAMPLES + 1];
#endif

void Sin::init() {
  FRAC_NUM dphase = 2 * M_PI / SIN_N_SAMPLES;
  //int32_t c = (int32_t)floor(cos(dphase) * (1 << 30) + 0.5);
  int32_t c = (int32_t)floor(COS_FUNC(dphase) * (1 << 30) + 0.5);
  //int32_t s = (int32_t)floor(sin(dphase) * (1 << 30) + 0.5);
  int32_t s = (int32_t)floor(SIN_FUNC(dphase) * (1 << 30) + 0.5);
  int32_t u = 1 << 30;
  int32_t v = 0;
  for (int i = 0; i < SIN_N_SAMPLES / 2; i++) {
#ifdef SIN_DELTA
    sintab[(i << 1) + 1] = (v + 32) >> 6;
    sintab[((i + SIN_N_SAMPLES / 2) << 1) + 1] = -((v + 32) >> 6);
#else
    sintab[i] = (v + 32) >> 6;
    sintab[i + SIN_N_SAMPLES / 2] = -((v + 32) >> 6);
#endif
    int32_t t = ((int64_t)u * (int64_t)s + (int64_t)v * (int64_t)c + R) >> 30;
    u = ((int64_t)u * (int64_t)c - (int64_t)v * (int64_t)s + R) >> 30;
    v = t;
  }
#ifdef SIN_DELTA
  for (int i = 0; i < SIN_N_SAMPLES - 1; i++) {
    sintab[i << 1] = sintab[(i << 1) + 3] - sintab[(i << 1) + 1];
  }
  sintab[(SIN_N_SAMPLES << 1) - 2] = -sintab[(SIN_N_SAMPLES << 1) - 1];
#else
  sintab[SIN_N_SAMPLES] = 0;
#endif
}

#ifndef SIN_INLINE
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


#if 0
// The following is an implementation designed not to use any lookup tables,
// based on the following implementation by Basile Graf:
// http://www.rossbencina.com/static/code/sinusoids/even_polynomial_sin_approximation.txt

#define C0 (1 << 24)
#define C1 (331121857 >> 2)
#define C2 (1084885537 >> 4)
#define C3 (1310449902 >> 6)

int32_t Sin::compute(int32_t phase) {
  int32_t x = (phase & ((1 << 23) - 1)) - (1 << 22);
  int32_t x2 = ((int64_t)x * (int64_t)x) >> 22;
  int32_t x4 = ((int64_t)x2 * (int64_t)x2) >> 24;
  int32_t x6 = ((int64_t)x2 * (int64_t)x4) >> 24;
  int32_t y = C0 -
    (((int64_t)C1 * (int64_t)x2) >> 24) +
    (((int64_t)C2 * (int64_t)x4) >> 24) -
    (((int64_t)C3 * (int64_t)x6) >> 24);
  y ^= -((phase >> 23) & 1);
  return y;
}
#endif

#if 1
// coefficients are Chebyshev polynomial, computed by compute_cos_poly.py
#define C8_0 16777216
#define C8_2 -331168742
#define C8_4 1089453524
#define C8_6 -1430910663
#define C8_8 950108533

int32_t Sin::compute(int32_t phase) {
  int32_t x = (phase & ((1 << 23) - 1)) - (1 << 22);
  int32_t x2 = ((int64_t)x * (int64_t)x) >> 16;
  int32_t y = (((((((((((((int64_t)C8_8
    * (int64_t)x2) >> 32) + C8_6)
    * (int64_t)x2) >> 32) + C8_4)
    * (int64_t)x2) >> 32) + C8_2)
    * (int64_t)x2) >> 32) + C8_0);
  y ^= -((phase >> 23) & 1);
  return y;
}
#endif

#define C10_0 (1 << 30)
#define C10_2 -1324675874  // scaled * 4
#define C10_4 1089501821
#define C10_6 -1433689867
#define C10_8 1009356886
#define C10_10 -421101352
int32_t Sin::compute10(int32_t phase) {
  int32_t x = (phase & ((1 << 29) - 1)) - (1 << 28);
  int32_t x2 = ((int64_t)x * (int64_t)x) >> 26;
  int32_t y = ((((((((((((((((int64_t)C10_10
    * (int64_t)x2) >> 34) + C10_8)
    * (int64_t)x2) >> 34) + C10_6)
    * (int64_t)x2) >> 34) + C10_4)
    * (int64_t)x2) >> 32) + C10_2)
    * (int64_t)x2) >> 30) + C10_0);
  y ^= -((phase >> 29) & 1);
  return y;
}
