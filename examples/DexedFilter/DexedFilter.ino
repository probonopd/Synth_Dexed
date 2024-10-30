#define MIDI_CHANNEL 1
#define MIDI_DEVICE_DIN Serial1
//#define  USB_AUDIO_INTERFACE
#define MIDI_BUFFER_SIZE 255

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

  dexed.setFilterCutoff(5000.0);
  dexed.setFilterResonance(0.3);
}

void loop() {
  static bool c_sign;
  static bool r_sign;
  uint8_t midi_buffer[MIDI_BUFFER_SIZE];
  uint8_t midi_len=0;

  if (dexed.getFilterCutoff() >= 15000.0)
    c_sign = false;
  else if (dexed.getFilterCutoff() <= 0.0)
    c_sign = true;
    
  if (dexed.getFilterResonance() >= 1.0)
    r_sign = false;
  else if (dexed.getFilterResonance() <= 0.0)
    r_sign = true;

  if (c_sign)
    dexed.setFilterCutoff(dexed.getFilterCutoff() + 10.0);
  else
    dexed.setFilterCutoff(dexed.getFilterCutoff() - 10.0);

  if (r_sign)
    dexed.setFilterResonance(dexed.getFilterResonance() + 0.001);
  else
    dexed.setFilterResonance(dexed.getFilterResonance() - 0.001);

  while(MIDI_DEVICE_DIN.available()) {
    if(midi_len<MIDI_BUFFER_SIZE)
    	midi_buffer[midi_len++]=MIDI_DEVICE_DIN.read();
    else
        Serial.println("MIDI input buffer overflow!");
  }

  if(midi_len > 0)
      dexed.midiDataHandler(MIDI_CHANNEL, midi_buffer, midi_len);
}
