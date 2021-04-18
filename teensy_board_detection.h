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


// Teensy-4.x
#if defined(__IMXRT1062__) || defined (ARDUINO_TEENSY40) || defined (ARDUINO_TEENSY41)
#define TEENSY4
#if defined (ARDUINO_TEENSY40)
#define TEENSY4_0
#elif defined (ARDUINO_TEENSY41)
#define TEENSY4_1
#endif
#endif

// Teensy-3.6
#if defined(__MK66FX1M0__)
#  define TEENSY3_6
#endif

// Teensy-3.5
#if defined (__MK64FX512__)
#define TEENSY3_5
#endif

#endif
