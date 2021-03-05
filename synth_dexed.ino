#include <Audio.h>
#include "synth_dexed.h"

AudioSourceMicroDexed    dexed(SAMPLE_RATE);
AudioOutputI2S           i2s1;
AudioControlSGTL5000     sgtl5000_1;
AudioConnection          patchCord1(dexed, 0, i2s1, 0);
AudioConnection          patchCord2(dexed, 0, i2s1, 1);

void setup()
{
  
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
  dexed.keydown(100, 100);
  delay(1000);
  Serial.println("Key-Up");
  dexed.keyup(100);
  delay(1000);
}
