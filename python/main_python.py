#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import time
import signal
import argparse
import os
import sys
from dexed_py import DexedHost
import rtmidi
import ctypes

# Set environment variable to signal to the C++ code that we're handling MIDI in Python
os.environ["DEXED_PYTHON_HOST"] = "1"
# Enable even more debugging
os.environ["DEXED_DEBUG"] = "1"

# Create a test function that generates sound directly to verify the audio path
def test_sound_generation(synth, num_samples=1024):
    """Test function to play a simple beep through the audio system"""
    import math
    import struct
    import time
    
    print("\n*** TESTING DIRECT SOUND GENERATION ***")
    print(f"Creating programmatic MIDI note...")
    synth.keydown(60, 100)  # Note on, middle C, velocity 100
    time.sleep(1)
    
    # Get the number of notes playing in the synthesizer
    print(f"Notes currently playing: {synth.getNumNotesPlaying()}")
    time.sleep(1)
    synth.keyup(60)  # Note off, middle C
    print(f"After note release, notes playing: {synth.getNumNotesPlaying()}")
    
    # Play another programmatic MIDI note but keep it held
    print(f"Creating and holding another programmatic MIDI note...")
    synth.keydown(72, 100)  # Note on, C5, velocity 100
    time.sleep(0.5)
    print(f"Notes currently playing: {synth.getNumNotesPlaying()}")
    time.sleep(2)
    synth.keyup(72)  # Note off
    
    print("*** SOUND TEST COMPLETE ***\n")

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
    channel = msg[0] & 0x0F
    note = msg[1]
    velocity = msg[2] if len(msg) > 2 else 0
    synth = data['synth']
    
    # Enhanced MIDI debugging
    print(f"[MIDI] Received message: status=0x{status:02x}, channel={channel+1}, note={note}, velocity={velocity}")
    
    if status == 0x90 and velocity > 0:  # Note On
        print(f"[MIDI] Forwarded: note_on channel={channel+1}, note={note}, velocity={velocity}")
        synth.keydown(note, velocity)
        print(f"[MIDI] After keydown call, notes playing: {synth.getNumNotesPlaying()}")
    elif status == 0x80 or (status == 0x90 and velocity == 0):  # Note Off
        print(f"[MIDI] Forwarded: note_off channel={channel+1}, note={note}")
        synth.keyup(note)
        print(f"[MIDI] After keyup call, notes playing: {synth.getNumNotesPlaying()}")
    
    # Check the synth engine state after processing MIDI
    try:
        # This helps us verify the synth engine is responding to MIDI input
        print(f"[MIDI] Current gain: {synth.getGain()}")
    except Exception as e:
        print(f"[ERROR] Exception checking synth state: {e}")

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
    parser.add_argument('--debug', action='store_true', help='Enable extra debugging')
    args = parser.parse_args()

    list_audio_devices()
    print(f"[INFO] Using audio device: {args.audio_device}")
    if args.sine:
        print("[WARN] Sine test mode is not implemented in the Python host. Running synth mode.")

    print("[INFO] Initializing DexedHost...")
    synth = DexedHost(16, 48000, args.audio_device)
    print("[INFO] Setting voice parameters...")
    synth.loadVoiceParameters(FMPIANO_SYSEX)
    
    # Ensure we have good gain value
    print(f"[INFO] Setting gain to 2.0 (current: {synth.getGain() if hasattr(synth, 'getGain') else 'unknown'})")
    synth.setGain(2.0)
    
    # Try to force some settings that might help with sound generation
    print("[INFO] Adjusting synth settings...")
    synth.setTranspose(36)  # Match old working example
    synth.setMonoMode(False)  # Make sure synth is in polyphonic mode

    # Enable pitch bend and mod wheel by default
    synth.setPitchbendRange(2)    # ±2 semitones for pitch bend
    synth.setModWheelRange(99)    # Full range for mod wheel
    synth.setModWheelTarget(1)    # 1=pitch, 2=amp, 4=EG, or combinations
    
    print("[INFO] Starting audio...")
    synth.start_audio()
    print("[INFO] Audio started.")

    # Run the sound test before MIDI setup
    test_sound_generation(synth)
    
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

    print("[INFO] Setting up debug MIDI output...")
    try:
        midi_out = rtmidi.MidiOut()
        midi_out.open_port(0)  # or the correct port index
        print("[INFO] Sending programmatic MIDI note...")
        midi_out.send_message([0x90, 60, 100])  # Note on, channel 1, middle C, velocity 100
        time.sleep(1)
        midi_out.send_message([0x80, 60, 0])    # Note off, channel 1, middle C
        print("[INFO] Programmatic MIDI note sent.")
    except Exception as e:
        print(f"[ERROR] Error with MIDI output: {e}")
        midi_out = None

    signal.signal(signal.SIGINT, signal_handler)
    print("[INFO] Synth_Dexed running. Press Ctrl+C to quit.")
    
    # Try another direct sound test every 5 seconds
    test_counter = 0
    
    try:
        while running:
            time.sleep(0.1)
            test_counter += 1
            if test_counter >= 50:  # Approximately every 5 seconds
                test_counter = 0
                if args.debug:
                    # Play a test note directly through the synth
                    print("\n[DEBUG] Playing test note directly through synth...")
                    synth.keydown(48, 100)  # C2, loud
                    time.sleep(0.5)
                    synth.keyup(48)
    finally:
        print("[INFO] Shutting down...")
        if midi_in:
            midi_in.close_port()
        if hasattr(midi_out, 'close_port') and midi_out is not None:
            midi_out.close_port()
        print("[INFO] Stopping audio...")
        synth.stop_audio()
        print("[INFO] Audio stopped. Script finished.")

if __name__ == "__main__":
    main()
