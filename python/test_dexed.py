import mido
import sounddevice as sd
import numpy as np
from dexed_py import Dexed
import time
import threading

# Initialize Dexed with 4 max notes and a sample rate of 44100
synth = Dexed(4, 44100)

print(dir(synth))

# FM Piano voice from main_win.cpp
fmpiano_sysex = bytes([
    95, 29, 20, 50, 99, 95, 0, 0, 41, 0, 19, 0, 0, 3, 0, 6, 79, 0, 1, 0, 14,  # OP6
    95, 20, 20, 50, 99, 95, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 99, 0, 1, 0, 0,  # OP5
    95, 29, 20, 50, 99, 95, 0, 0, 0, 0, 0, 0, 0, 3, 0, 6, 89, 0, 1, 0, 7,  # OP4
    95, 20, 20, 50, 99, 95, 0, 0, 0, 0, 0, 0, 0, 3, 0, 2, 99, 0, 1, 0, 7,  # OP3
    95, 50, 35, 78, 99, 75, 0, 0, 0, 0, 0, 0, 0, 3, 0, 7, 58, 0, 14, 0, 7,  # OP2
    96, 25, 25, 67, 99, 75, 0, 0, 0, 0, 0, 0, 0, 3, 0, 2, 99, 0, 1, 0, 10,  # OP1
    94, 67, 95, 60, 50, 50, 50, 50,  # pitch EG rates, pitch EG levels
    4, 6, 0,  # algorithm, feedback, osc sync
    34, 33, 0, 0, 0, 4,  # lfo speed, lfo delay, lfo pitch_mod_depth, lfo_amp_mod_depth, lfo_sync, lfo_waveform
    3, 24,  # pitch_mod_sensitivity, transpose
    70, 77, 45, 80, 73, 65, 78, 79, 0, 0  # name "FM PIANO"
])

# Load the FM Piano voice
synth.loadVoiceParameters(fmpiano_sysex)

# Set gain for better audibility
synth.setGain(10.0)

def play_note_thread(note, velocity, duration):
    """Play a single note for a specified duration in a separate thread."""
    synth.keydown(note, velocity)
    time.sleep(duration)
    synth.keyup(note)

# Define a chord (C major: C, E, G) with durations
chord_notes = [(60, 100, 1.0), (64, 100, 1.0), (67, 100, 1.0)]  # (note, velocity, duration)

# Play the chord using threads
threads = []
for note, velocity, duration in chord_notes:
    thread = threading.Thread(target=play_note_thread, args=(note, velocity, duration))
    threads.append(thread)
    thread.start()

# Wait for all threads to finish
for thread in threads:
    thread.join()

print("Chord played with threading.")

# Stream audio samples for 2 seconds
def audio_callback(outdata, frames, time, status):
    if status:
        print(status)
    samples = synth.getSamples(frames)
    outdata[:] = np.array(samples, dtype=np.int16).reshape(-1, 1)

print("Playing chord...")
with sd.OutputStream(samplerate=44100, channels=1, dtype='int16', callback=audio_callback):
    time.sleep(2)

# Release the chord (only pass the note number to keyup)
for note, _, _ in chord_notes:
    synth.keyup(note)

print("Chord released.")

# Fetch and display some audio sample data
samples = synth.getSamples(10)  # Fetch 10 audio samples
print("Sample data:", samples)
