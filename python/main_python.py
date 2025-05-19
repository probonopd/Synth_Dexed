#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import time
import signal
import argparse
from dexed_py import DexedHost
import rtmidi
import ctypes

FMPIANO_SYSEX = bytes([
    95, 29, 20, 50, 99, 95, 0, 0, 41, 0, 19, 0, 0, 3, 0, 6, 79, 0, 1, 0, 14,
    95, 20, 20, 50, 99, 95, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 99, 0, 1, 0, 0,
    95, 29, 20, 50, 99, 95, 0, 0, 0, 0, 0, 0, 0, 3, 0, 6, 89, 0, 1, 0, 7,
    95, 20, 20, 50, 99, 95, 0, 0, 0, 0, 0, 0, 0, 3, 0, 2, 99, 0, 1, 0, 7,
    95, 50, 35, 78, 99, 75, 0, 0, 0, 0, 0, 0, 0, 3, 0, 7, 58, 0, 14, 0, 7,
    96, 25, 25, 67, 99, 75, 0, 0, 0, 0, 0, 0, 0, 3, 0, 2, 99, 0, 1, 0, 10,
    94, 67, 95, 60, 50, 50, 50, 50,
    4, 6, 0,
    34, 33, 0, 0, 0, 4,
    3, 24,
    70, 77, 45, 80, 73, 65, 78, 79, 0, 0
])

running = True

def signal_handler(sig, frame):
    global running
    print("\n[INFO] Caught Ctrl+C, exiting...")
    running = False

def midi_callback(event, data):
    msg, deltatime = event
    status = msg[0] & 0xF0
    note = msg[1]
    velocity = msg[2] if len(msg) > 2 else 0
    synth = data['synth']
    if status == 0x90 and velocity > 0:
        synth.keydown(note, velocity)
    elif status == 0x80 or (status == 0x90 and velocity == 0):
        synth.keyup(note)

def list_audio_devices():
    waveOutGetNumDevs = ctypes.windll.winmm.waveOutGetNumDevs
    waveOutGetDevCaps = ctypes.windll.winmm.waveOutGetDevCapsW
    class WAVEOUTCAPS(ctypes.Structure):
        _fields_ = [
            ("wMid", ctypes.c_ushort),
            ("wPid", ctypes.c_ushort),
            ("vDriverVersion", ctypes.c_uint),
            ("szPname", ctypes.c_wchar * 32),
            ("dwFormats", ctypes.c_uint),
            ("wChannels", ctypes.c_ushort),
            ("wReserved1", ctypes.c_ushort),
            ("dwSupport", ctypes.c_uint),
        ]
    num_devs = waveOutGetNumDevs()
    print("Available audio output devices:")
    for i in range(num_devs):
        caps = WAVEOUTCAPS()
        waveOutGetDevCaps(i, ctypes.byref(caps), ctypes.sizeof(caps))
        print(f"  [{i}] {caps.szPname}")

def main():
    global running
    parser = argparse.ArgumentParser(description="Dexed Python Host (main_win.cpp replacement)")
    parser.add_argument('--audio-device', '-a', type=int, default=0, help='Audio device index (matches main_win.cpp)')
    parser.add_argument('--sine', action='store_true', help='Sine test mode (not implemented in Python host)')
    parser.add_argument('--synth', action='store_true', help='Use synth (default)')
    parser.add_argument('--midi-device', '-m', type=int, default=0, help='MIDI device index')
    args = parser.parse_args()

    list_audio_devices()
    print(f"[INFO] Using audio device: {args.audio_device}")
    if args.sine:
        print("[WARN] Sine test mode is not implemented in the Python host. Running synth mode.")

    print("[INFO] Initializing DexedHost...")
    synth = DexedHost(16, 48000, args.audio_device)
    synth.loadVoiceParameters(FMPIANO_SYSEX)
    synth.setGain(2.0)
    synth.start_audio()
    print("[INFO] Audio started.")

    # MIDI setup
    midi_in = rtmidi.MidiIn()
    available_ports = midi_in.get_ports()
    if available_ports:
        print("Available MIDI input devices:")
        for i, port in enumerate(available_ports):
            print(f"  [{i}] {port}")
        midi_port = args.midi_device if args.midi_device < len(available_ports) else 0
        print(f"[INFO] Opening MIDI port: {available_ports[midi_port]}")
        midi_in.open_port(midi_port)
        midi_in.set_callback(midi_callback, {'synth': synth})
        print("[INFO] MIDI input started.")
    else:
        print("[WARN] No MIDI input devices found. MIDI will be disabled.")
        midi_in = None

    midi_out = rtmidi.MidiOut()
    midi_out.open_port(0)  # or the correct port index
    print("Sending programmatic MIDI note...")
    midi_out.send_message([0x90, 60, 100])  # Note on, channel 1, middle C, velocity 100
    time.sleep(1)
    midi_out.send_message([0x80, 60, 0])    # Note off, channel 1, middle C
    time.sleep(1)

    signal.signal(signal.SIGINT, signal_handler)
    print("[INFO] Synth_Dexed running. Press Ctrl+C to quit.")

    try:
        while running:
            time.sleep(0.1)
    finally:
        print("[INFO] Shutting down...")
        if midi_in:
            midi_in.close_port()
        synth.stop_audio()
        print("[INFO] Audio stopped. Script finished.")

if __name__ == "__main__":
    main()
