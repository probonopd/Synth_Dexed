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
    
#pragma once

#include <stdlib.h>
#include <stdint.h>

class Limiter
{
  public:
    Limiter(float attack, float release, float threshold, int16_t delay_buffer_size);
    Limiter();
    ~Limiter();

    // Global methods
    void apply(int16_t* audio, const size_t num_samples);
    void set(float attack, float release, float threshold, int16_t delay_buffer_size);
    void enable(bool e);

  protected:
    void reset(void);
    bool enabled=true;
    int16_t attack;
    int16_t release;
    int16_t threshold;
    uint16_t delay;
    int16_t* delay_line=NULL;
    uint16_t delay_index;
    int16_t envelope;
    int16_t gain;
};
