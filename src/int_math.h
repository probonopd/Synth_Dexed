/*
   MicroDexed

   MicroDexed is a port of the Dexed sound engine
   (https://github.com/asb2m10/dexed) for the Teensy-3.5/3.6/4.x with audio shield.
   Dexed ist heavily based on https://github.com/google/music-synthesizer-for-android

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

//*** Integer math helpers (from https://en.wikipedia.org/wiki/Q_(number_format)) ***
inline int16_t sat16(int32_t x)
{
        if (x > 0x7FFF) return 0x7FFF;
        else if (x < -0x8000) return -0x8000;
        else return (int16_t)x;
}

inline int16_t q_add(int16_t a, int16_t b)
{
    return(sat16(a + b));
}

inline int16_t q_sub(int16_t a, int16_t b)
{
    return(sat16(a - b));
}

inline int16_t q_mul(int16_t a, int16_t b)
{
    int32_t temp;

    temp = (int32_t)a * (int32_t)b;
    temp += (1 << 14);

    return(sat16(temp >> 15));
}

inline int16_t q_div(int16_t a, int16_t b)
{
    int32_t temp = (int32_t)a << 15;

    if ((temp >= 0 && b >= 0) || (temp < 0 && b < 0)) {
        temp += (b >> 1);
    } else {
        temp -= (b >> 1);
    }
    return (int16_t)(temp / b);
}

