#define MIDI_CHANNEL 1
#define MIDI_DEVICE_DIN Serial1
//#define  USB_AUDIO_INTERFACE

#include <Audio.h>
#include "synth_dexed.h"

AudioSynthDexed dexed(4, SAMPLE_RATE);  // 4 voices max
AudioOutputI2S i2s1;
AudioControlSGTL5000 sgtl5000_1;
AudioConnection patchCord1(dexed, 0, i2s1, 0);
AudioConnection patchCord2(dexed, 0, i2s1, 1);
#if defined(USB_AUDIO_INTERFACE)
AudioOutputUSB usb1;
AudioConnection patchCord3(dexed, 0, usb1, 0);
AudioConnection patchCord4(dexed, 0, usb1, 1);
#endif

void setup() {
  AudioMemory(16);

  sgtl5000_1.enable();
  sgtl5000_1.lineOutLevel(29);
  sgtl5000_1.dacVolumeRamp();
  sgtl5000_1.dacVolume(1.0);
  sgtl5000_1.unmuteHeadphone();
  sgtl5000_1.unmuteLineout();
  sgtl5000_1.volume(0.8, 0.8);  // Headphone volume

  MIDI_DEVICE_DIN.begin(31250);
}

void loop() {
  uint8_t midi_buffer[150];
  uint8_t midi_len=0;

  while(MIDI_DEVICE_DIN.available()) {
    if(midi_len<150)
    	midi_buffer[midi_len++]=MIDI_DEVICE_DIN.read();
    else
        Serial.println("MIDI input buffer overflow!");
  }

  if(midi_len > 0)
      dexed.midiDataHandler(MIDI_CHANNEL, midi_buffer, midi_len);
}
