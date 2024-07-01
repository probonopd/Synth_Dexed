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

#include "fx.h"
#include "int_math.h"
#include <iostream> 
#include <algorithm>

Limiter::Limiter()
{
    set(0.2,0.5,0.9,100);
}

Limiter::Limiter(float attack, float release, float threshold, int16_t delay_buffer_size)
{
    set(attack, release, threshold, delay_buffer_size);
}

Limiter::~Limiter()
{
    if(delay_line!=NULL)
        delete[] delay_line;
}

void Limiter::set(float attack, float release, float threshold, int16_t delay_buffer_size)
{
    attack=attack*0x7fff;
    release=release*0x7fff;
    delay=delay;
    threshold=threshold*0x7fff;

    reset();
}

void Limiter::reset(void)
{
    delay_index = 0;
    gain = 0x7fff;
    envelope = 0;

    if(delay_line!=NULL)
        delete[] delay_line;
    delay_line=new int16_t[delay];

    for(uint16_t i = 0; i < delay; ++i) {
        delay_line[i] = 0;
    }
}

void Limiter::enable(bool e)
{
    enabled=e;
}

void Limiter::apply(int16_t* audio, const size_t num_samples)
{
    if(enabled!=true)
        return;

    for (size_t idx = 0; idx < num_samples; ++idx)
    {
        const int16_t sample = audio[idx];
        delay_line[delay_index] = sample;
        delay_index = (delay_index + 1) % delay;

        // calculate an envelope of the signal
        envelope = std::max(abs(sample), int(q_mul(envelope, release)));

        int16_t target_gain = 0x7fff;
        if (envelope > threshold)
	{
            target_gain = q_div(threshold, envelope);
        }

        // have gain_ go towards a desired limiter gain
        gain = q_add(q_mul(gain,attack),q_mul(target_gain, q_sub(0x7fff, attack)));

        // limit the delayed signal
        audio[idx] = q_mul(delay_line[delay_index],gain);
    }
}
