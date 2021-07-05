#include <Audio.h>
#include "config.h"
#include "synth_dexed.h"

AudioSynthDexed          dexed(SAMPLE_RATE);
AudioOutputI2S           i2s1;
AudioControlSGTL5000     sgtl5000_1;
AudioConnection          patchCord1(dexed, 0, i2s1, 0);
AudioConnection          patchCord2(dexed, 0, i2s1, 1);

void setup()
{
  AudioMemory(32);

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
  dexed.keydown(60, 100);
  delay(100);
  dexed.keydown(64, 100);
  delay(100);
  dexed.keydown(67, 100);
  delay(100);
  dexed.keydown(72, 100);
  delay(2000);

  Serial.println("Key-Up");
  dexed.keyup(60);
  dexed.keyup(64);
  dexed.keyup(67);
  dexed.keyup(72);
  delay(2000);
}
