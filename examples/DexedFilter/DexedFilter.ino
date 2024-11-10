#define MIDI_CHANNEL 1
//#define  USB_AUDIO_INTERFACE

#include <USBHost_t36.h>
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
USBHost usb_host;
MIDIDevice midi_usb(usb_host);

void setup() {

  Serial.begin(115200);

  AudioMemory(16);

  sgtl5000_1.enable();
  sgtl5000_1.lineOutLevel(29);
  sgtl5000_1.dacVolumeRamp();
  sgtl5000_1.dacVolume(1.0);
  sgtl5000_1.unmuteHeadphone();
  sgtl5000_1.unmuteLineout();
  sgtl5000_1.volume(0.8, 0.8);  // Headphone volume

  midi_usb.setHandleNoteOn(NoteOn);
  midi_usb.setHandleNoteOff(NoteOff);

  midi_usb.begin();

  dexed.setFilterCutoffFrequency(2500.0);
  //dexed.setFilterResonance(0.3);

  Serial.println("<START>");
  Serial.printf("Res: %f, Cut: %f\n", dexed.getFilterResonance(), dexed.getFilterCutoffFrequency());

  dexed.keydown(48, 100);
  delay(1000);
  dexed.keyup(48);
}

void loop() {
  static bool c_sign;
  static bool r_sign;
  static uint32_t timer=0;

  usb_host.Task();
  midi_usb.read();

  if (dexed.getFilterCutoffFrequency() >= 15000.0)
    c_sign = false;
  else if (dexed.getFilterCutoffFrequency() <= 0.0)
    c_sign = true;

  /*if (dexed.getFilterResonance() >= 1.0)
    r_sign = false;
  else if (dexed.getFilterResonance() <= 0.0)
    r_sign = true;*/

  if (c_sign)
    dexed.setFilterCutoffFrequency(dexed.getFilterCutoffFrequency() + 10.0);
  else
    dexed.setFilterCutoffFrequency(dexed.getFilterCutoffFrequency() - 10. + 0);

  /*if (r_sign)
    dexed.setFilterResonance(dexed.getFilterResonance() + 0.001);
  else
    dexed.setFilterResonance(dexed.getFilterResonance() - 0.001);*/

  if (millis() - timer > 1000) {
    timer = millis();
    Serial.printf("Res: %f, Cut: %f\n", dexed.getFilterResonance(), dexed.getFilterCutoffFrequency());
  }
}

void NoteOn(byte channel, byte note, byte velocity) {
  bool b;

  Serial.println("NOTE ON");
  b = dexed.midiDataHandler(MIDI_CHANNEL, 0x90 | (channel - 1), note, velocity);

  Serial.printf("b=%d\n", b);
}

void NoteOff(byte channel, byte note, byte velocity) {
  bool b;

  Serial.println("NOTE OFF");
  b = dexed.midiDataHandler(MIDI_CHANNEL, 0x80 | (channel - 1), note, velocity);

  Serial.printf("b=%d\n", b);
}