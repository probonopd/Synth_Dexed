#define MIDI_CHANNEL 1
#define MIDI_DEVICE_USB_HOST
#define MIDI_DEVICE_DIN Serial1

#include <Audio.h>
#include <MIDI.h>
#include "synth_dexed.h"
#if defined(MIDI_DEVICE_USB_HOST)
#include <USBHost_t36.h>
#endif

AudioSynthDexed dexed(4, SAMPLE_RATE);  // 4 voices max
AudioOutputI2S i2s1;
AudioControlSGTL5000 sgtl5000_1;
AudioConnection patchCord1(dexed, 0, i2s1, 0);
AudioConnection patchCord2(dexed, 0, i2s1, 1);
#ifdef AUDIO_INTERFACE
AudioOutputUSB usb1;
AudioConnection patchCord3(dexed, 0, usb1, 0);
AudioConnection patchCord4(dexed, 0, usb1, 1);
#endif

MIDI_CREATE_INSTANCE(HardwareSerial, MIDI_DEVICE_DIN, midi_serial);
#if defined(MIDI_DEVICE_USB_HOST)
USBHost usb_host;
#endif
#ifdef MIDI_DEVICE_USB_HOST
MIDIDevice midi_usb(usb_host);
#endif

void setup() {
  AudioMemory(32);

  sgtl5000_1.enable();
  sgtl5000_1.lineOutLevel(29);
  sgtl5000_1.dacVolumeRamp();
  sgtl5000_1.dacVolume(1.0);
  sgtl5000_1.unmuteHeadphone();
  sgtl5000_1.unmuteLineout();
  sgtl5000_1.volume(0.8, 0.8);  // Headphone volume

  midi_serial.begin(MIDI_CHANNEL_OMNI);
#if defined(MIDI_DEVICE_USB_HOST)
  usb_host.begin();
#endif
  dexed.setTranspose(36);
}

void loop() {
  int8_t type;
  int8_t midi_data[3];
  int16_t SysExLength;

  for (uint8_t i = 0; i < 2; i++) {
    if (i == 0) {
      midi_serial.read();

      type = midi_serial.getType();
      midi_data[0] = midi_serial.getChannel();
      midi_data[1] = midi_serial.getData1();
      midi_data[2] = midi_serial.getData2();

      if (type != midi::SystemExclusive) {
        dexed.midiDataHandler(midi_data[0], midi_data, 3);
      } else {
        SysExLength = midi_data[1] + midi_data[2] * 256;
        dexed.midiDataHandler(midi_data[0], midi_serial.getSysExArray(), SysExLength);
      }
    } else {
      midi_usb.read();

      type = midi_usb.getType();
      midi_data[0] = midi_usb.getChannel();
      midi_data[1] = midi_usb.getData1();
      midi_data[2] = midi_usb.getData2();

      if (type != midi::SystemExclusive) {
        dexed.midiDataHandler(midi_data[0], midi_data, 3);
      } else {
        SysExLength = midi_data[1] + midi_data[2] * 256;
        dexed.midiDataHandler(midi_data[0], midi_usb.getSysExArray(), SysExLength);
      }
    }
  }
}
