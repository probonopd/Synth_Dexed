#include <Audio.h>
#include "config.h"
#if defined(USE_OPEN_AUDIO_LIB)
#include "OpenAudio_ArduinoLibrary.h"
#endif
#include "synth_dexed.h"

#if !defined(USE_OPEN_AUDIO_LIB)
AudioSynthDexed          dexed(SAMPLE_RATE);
AudioOutputI2S           i2s1;
AudioControlSGTL5000     sgtl5000_1;
AudioConnection          patchCord1(dexed, 0, i2s1, 0);
AudioConnection          patchCord2(dexed, 0, i2s1, 1);
#else
AudioSynthDexed_F32      dexed(SAMPLE_RATE);
AudioOutputI2S           i2s1;
AudioControlSGTL5000     sgtl5000_1;
AudioConvert_F32toI16    convert_f32toi16;
AudioConnection_F32      patchCordF0(dexed, 0, convert_f32toi16, 0);
AudioConnection          patchCordI0(convert_f32toi16, 0, i2s1, 0);
AudioConnection          patchCordI1(convert_f32toi16, 0, i2s1, 1);
#endif

void setup()
{
#if !defined(USE_OPEN_AUDIO_LIB)
  AudioMemory(32);
#else
  AudioMemory_F32(32);
  AudioMemory(32);
#endif

  sgtl5000_1.enable();
  sgtl5000_1.lineOutLevel(29);
  sgtl5000_1.dacVolumeRamp();
  sgtl5000_1.dacVolume(1.0);
  sgtl5000_1.unmuteHeadphone();
  sgtl5000_1.unmuteLineout();
  sgtl5000_1.volume(0.8, 0.8); // Headphone volume
}

void loop()
{
  Serial.println("Key-Down");
  dexed.keydown(50, 100);
  delay(1000);
  Serial.println("Key-Up");
  dexed.keyup(50);
  delay(5000);
}
