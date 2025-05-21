import argparse
import ctypes
import os
import sys
import time
import mido

# Argument parsing to match C++ exe
parser = argparse.ArgumentParser(description="Python drop-in replacement for Synth_Dexed_exe.exe")
parser.add_argument('-a', '--audio-device', type=int, default=0, help='Select audio device index (default: 0)')
parser.add_argument('-m', '--midi-device', type=int, default=0, help='Select MIDI input device index (default: 0)')
parser.add_argument('--sample-rate', type=int, default=48000, help='Set sample rate (default: 48000)')
parser.add_argument('--buffer-frames', type=int, default=1024, help='Set audio buffer size in frames (default: 1024)')
parser.add_argument('--num-buffers', type=int, default=4, help='Set number of audio buffers (default: 4)')
parser.add_argument('--sine', action='store_true', help='Output test sine wave instead of synth')
parser.add_argument('--synth', action='store_true', help='Use synth (default)')
parser.add_argument('--unison-voices', type=int, default=1, help='Set number of unison voices (1-4, default: 1)')
parser.add_argument('--unison-spread', type=int, default=0, help='Set unison spread (0-99, default: 0)')
parser.add_argument('--unison-detune', type=int, default=0, help='Set unison detune (cents, default: 0)')
parser.add_argument('--debug', action='store_true', help='Enable debug output')
parser.add_argument('--no-auto-notes', action='store_true', help='Disable automatic test notes', default=True)
args = parser.parse_args()

# Set environment variables to control the C++ side behavior
os.environ["DEXED_PYTHON_HOST"] = "1"  # Tell C++ we're handling the MIDI
os.environ["DEXED_NO_AUTO_NOTES"] = "1"  # Disable automatic test notes
if args.debug:
    os.environ["DEXED_DEBUG"] = "1"
    
# Enable verbose MIDI debugging
VERBOSE_MIDI_DEBUG = False  # Set to False to reduce console spam

# Path to the DLL
DLL_PATH = os.path.abspath(os.path.join(os.path.dirname(__file__), 'build', 'Release', 'Synth_Dexed.dll'))
print(f"[PYTHON] Loading DLL from: {DLL_PATH}")

# Load the DLL
synth_dll = ctypes.CDLL(DLL_PATH)

# Define types
synth_handle = ctypes.c_void_p

# Set up function signatures
synth_dll.synth_create.argtypes = [ctypes.c_uint32, ctypes.c_uint8]
synth_dll.synth_create.restype = synth_handle
synth_dll.synth_destroy.argtypes = [synth_handle]
synth_dll.synth_destroy.restype = None
synth_dll.synth_send_midi.argtypes = [synth_handle, ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint32]
synth_dll.synth_send_midi.restype = None
synth_dll.synth_start_audio.argtypes = [synth_handle]
synth_dll.synth_start_audio.restype = None
synth_dll.synth_stop_audio.argtypes = [synth_handle]
synth_dll.synth_stop_audio.restype = None
synth_dll.synth_load_init_voice.argtypes = [synth_handle]
synth_dll.synth_load_init_voice.restype = None
synth_dll.synth_load_epiano_patch.argtypes = [synth_handle]
synth_dll.synth_load_epiano_patch.restype = None


# Print available MIDI input ports
input_names = mido.get_input_names()
print("Available MIDI input ports:")
for idx, name in enumerate(input_names):
    print(f"  [{idx}] {name}")

if not input_names:
    print("No MIDI input ports found. Exiting.")
    sys.exit(1)

# Select MIDI input port
midi_index = args.midi_device
if midi_index < 0 or midi_index >= len(input_names):
    print(f"Invalid MIDI device index {midi_index}. Exiting.")
    sys.exit(1)
input_name = input_names[midi_index]

# Synth options
sample_rate = args.sample_rate
max_notes = 16  # Not exposed as CLI option in C++

# Create synth instance
handle = synth_dll.synth_create(sample_rate, max_notes)
print(f"[PYTHON] Created synth instance with handle: {handle}")
synth_dll.synth_load_init_voice(handle)
print("[PYTHON] Loaded init voice")
synth_dll.synth_load_epiano_patch(handle)
print("[PYTHON] Loaded epiano patch")

# Helper functions for MIDI handling
def debug_midi_message(message):
    """Print detailed debug information about a MIDI message."""
    if not VERBOSE_MIDI_DEBUG:
        return
    
    if message.type == 'note_on':
        print(f"[MIDI DEBUG] Note ON: note={message.note}, velocity={message.velocity}, channel={message.channel}")
    elif message.type == 'note_off':
        print(f"[MIDI DEBUG] Note OFF: note={message.note}, velocity={message.velocity}, channel={message.channel}")
    elif message.type == 'control_change':
        print(f"[MIDI DEBUG] Control Change: control={message.control}, value={message.value}, channel={message.channel}")
    elif message.type == 'program_change':
        print(f"[MIDI DEBUG] Program Change: program={message.program}, channel={message.channel}")
    elif message.type == 'pitchwheel':
        print(f"[MIDI DEBUG] Pitch Wheel: pitch={message.pitch}, channel={message.channel}")
    elif message.type == 'sysex':
        print(f"[MIDI DEBUG] SysEx: data={message.data}, size={len(message.data)}")
    else:
        print(f"[MIDI DEBUG] Other message type: {message}")
    
    # Also print the raw bytes
    bytes_data = message.bytes()
    print(f"[MIDI DEBUG] Raw bytes: {' '.join([f'{b:02x}' for b in bytes_data])}")

# Start audio
print("[PYTHON] Starting audio...")
synth_dll.synth_start_audio(handle)
print("[PYTHON] Audio started")

# Send a single loud test note at startup to verify sound is working
def send_test_note(handle):
    """Send a test note to verify the MIDI and synth are working."""
    print("[PYTHON] Sending test note (C4, velocity 100)")
    # Note On, channel 1, note C4 (60), velocity 100
    note_on = [0x90, 60, 127]  # Max velocity for test note
    note_on_array = (ctypes.c_uint8 * len(note_on))(*note_on)
    synth_dll.synth_send_midi(handle, note_on_array, len(note_on))
    
    # Wait 0.5 seconds
    time.sleep(0.5)
    
    # Note Off, channel 1, note C4 (60), velocity 0
    note_off = [0x80, 60, 0]
    note_off_array = (ctypes.c_uint8 * len(note_off))(*note_off)
    synth_dll.synth_send_midi(handle, note_off_array, len(note_off))
    print("[PYTHON] Test note complete")

# Wait a moment for audio to initialize
time.sleep(0.5)
send_test_note(handle)

print(f"[PYTHON] Listening for MIDI on: {input_name}")
if args.debug:
    print(f"[DEBUG] sample_rate={sample_rate}, buffer_frames={args.buffer_frames}, num_buffers={args.num_buffers}, unison_voices={args.unison_voices}, unison_spread={args.unison_spread}, unison_detune={args.unison_detune}, sine={args.sine}, synth={args.synth}")

try:
    with mido.open_input(input_name) as inport:
        print(f"[PYTHON] Successfully opened MIDI input port: {input_name}")
        print("[PYTHON] Entering MIDI processing loop. Press keys on your MIDI device.")
        no_message_counter = 0
        last_notes = {}  # To track active notes for debugging
        
        while True:
            messages_received_this_iteration = False
            for msg in inport.iter_pending():
                messages_received_this_iteration = True
                no_message_counter = 0  # Reset counter if message received
                
                # Debug log the message (only if VERBOSE_MIDI_DEBUG is True)
                debug_midi_message(msg)
                
                # Track notes for minimal debugging
                if msg.type == 'note_on' and msg.velocity > 0:
                    last_notes[msg.note] = msg.velocity
                    print(f"▶ Note ON: {msg.note} (velocity: {msg.velocity})")
                elif msg.type == 'note_off' or (msg.type == 'note_on' and msg.velocity == 0):
                    if msg.note in last_notes:
                        del last_notes[msg.note]
                    print(f"◼ Note OFF: {msg.note}")
                
                # Forward to synth - boost velocity for better audibility
                midi_bytes = msg.bytes()

                midi_array = (ctypes.c_uint8 * len(midi_bytes))(*midi_bytes)
                synth_dll.synth_send_midi(handle, midi_array, len(midi_bytes))
            
            if not messages_received_this_iteration:
                no_message_counter += 1
                if no_message_counter % 300 == 0:  # Log every 3 seconds (300 * 0.01s)
                    print(f"[PYTHON] No MIDI messages in the last {no_message_counter * 0.01:.1f} seconds.")
                    if last_notes:
                        print(f"[PYTHON] Currently active notes: {list(last_notes.keys())}")

            time.sleep(0.01)
except KeyboardInterrupt:
    print("[PYTHON] Keyboard interrupt, exiting.")
finally:
    print("[PYTHON] Done, stopping audio.")
    synth_dll.synth_stop_audio(handle)
    synth_dll.synth_destroy(handle)

