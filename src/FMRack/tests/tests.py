"""
MIDI Test Case Generator

This script generates a set of MIDI files that test various MIDI controllers and expressive inputs
by sweeping their values up, down, and back to center while holding a note. The generated files can be used
to verify the response of synthesizers and MIDI processing software to:

- Pitch bend (pitch wheel) (Pitch Bend message 0xE0)
- Modulation wheel (CC1)
- Breath controller (CC2)
- Foot controller (CC4)
- Channel aftertouch (Channel Pressure message 0xD0)
- Polyphonic aftertouch (Key Pressure message 0xA0))
"""


try:
    from mido import Message, MidiFile, MidiTrack, MetaMessage
except ImportError:
    raise ImportError("This script requires the 'mido' library. Install it with 'pip install mido'.")

def set_voice_data(track):
    # Use the real DX7 single voice data you provided (155 bytes)
    # Header: F0 43 00 09 20 ... 155 bytes ... CS F7
    header = [0x43, 0x00, 0x09, 0x20]
    # Replace the following with your actual 155-byte DX7 voice data
    voice_data = [
        0x5F, 0x1D, 0x14, 0x32, 0x63, 0x51, 0x52, 0x00, 0x29, 0x05, 0x13, 0x03, 0x00, 0x03, 0x00, 0x02, 0x48, 0x00, 0x01, 0x00, 0x0E, 0x5F, 0x14, 0x14, 0x3B, 0x63, 0x5F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x03, 0x01, 0x63, 0x00, 0x01, 0x00, 0x00, 0x5F, 0x45, 0x23, 0x4C, 0x58, 0x2E, 0x1B, 0x4D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x06, 0x4C, 0x00, 0x0A, 0x00, 0x07, 0x5F, 0x14, 0x14, 0x49, 0x63, 0x5F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x03, 0x02, 0x63, 0x00, 0x01, 0x00, 0x07, 0x5F, 0x32, 0x23, 0x4E, 0x63, 0x63, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x07, 0x4D, 0x00, 0x01, 0x00, 0x07, 0x60, 0x19, 0x19, 0x4D, 0x63, 0x4B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x03, 0x02, 0x63, 0x00, 0x01, 0x00, 0x0A, 0x63, 0x63, 0x63, 0x63, 0x32, 0x32, 0x32, 0x32, 0x04, 0x07, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x18, 0x45, 0x2E, 0x50, 0x49, 0x41, 0x4E, 0x4F, 0x20, 0x31, 0x41, 0x27, 0x31
    ]
    cs = (-(sum(voice_data)) & 0x7F)
    sysex_data = header + voice_data + [cs]
    track.append(Message('sysex', data=sysex_data, time=0))

def sweep(track, msg_type, steps, duration, note=None, control=None):
    # Upward sweep
    for i in range(steps):
        val = int(127 * i / (steps - 1))
        kwargs = {'value': val, 'time': duration // (steps * 3)}
        if note is not None:
            kwargs['note'] = note
        if control is not None:
            kwargs['control'] = control
        track.append(Message(msg_type, **kwargs))
    # Downward sweep
    for i in range(steps):
        val = int(127 - (127 * i / (steps - 1)))
        kwargs = {'value': val, 'time': duration // (steps * 3)}
        if note is not None:
            kwargs['note'] = note
        if control is not None:
            kwargs['control'] = control
        track.append(Message(msg_type, **kwargs))
    # Back to center
    for i in range(steps):
        val = int(0 + (64 * i / (steps - 1)))
        kwargs = {'value': val, 'time': duration // (steps * 3)}
        if note is not None:
            kwargs['note'] = note
        if control is not None:
            kwargs['control'] = control
        track.append(Message(msg_type, **kwargs))

def create_sweep_midi(filename, sweep_type, note=60, velocity=100, steps=32, duration=480):
    mid = MidiFile()
    track = MidiTrack()
    mid.tracks.append(track)
    track.append(Message('note_on', note=note, velocity=velocity, time=0))
    if sweep_type == 'pitchwheel':
        # Pitch bend is special: value is -8192..8191
        for i in range(steps):
            bend = int(8192 + (8191 * i / (steps - 1)))
            track.append(Message('pitchwheel', pitch=bend - 8192, time=duration // (steps * 3)))
        for i in range(steps):
            bend = int(16383 - (16383 * i / (steps - 1)))
            track.append(Message('pitchwheel', pitch=bend - 8192, time=duration // (steps * 3)))
        for i in range(steps):
            bend = int(0 + (8192 * i / (steps - 1)))
            track.append(Message('pitchwheel', pitch=bend - 8192, time=duration // (steps * 3)))
        track.append(Message('note_off', note=note, velocity=0, time=120))
        track.append(Message('pitchwheel', pitch=0, time=0))
    elif sweep_type == 'modwheel':
        sweep(track, 'control_change', steps, duration, control=1)
        track.append(Message('note_off', note=note, velocity=0, time=120))
        track.append(Message('control_change', control=1, value=0, time=0))
    elif sweep_type == 'aftertouch':
        sweep(track, 'aftertouch', steps, duration)
        track.append(Message('note_off', note=note, velocity=0, time=120))
        track.append(Message('aftertouch', value=0, time=0))
    elif sweep_type == 'polytouch':
        sweep(track, 'polytouch', steps, duration, note=note)
        track.append(Message('note_off', note=note, velocity=0, time=120))
        track.append(Message('polytouch', note=note, value=0, time=0))
    elif sweep_type == 'breath':
        sweep(track, 'control_change', steps, duration, control=2)
        track.append(Message('note_off', note=note, velocity=0, time=120))
        track.append(Message('control_change', control=2, value=0, time=0))
    elif sweep_type == 'foot':
        sweep(track, 'control_change', steps, duration, control=4)
        track.append(Message('note_off', note=note, velocity=0, time=120))
        track.append(Message('control_change', control=4, value=0, time=0))
    mid.save(filename)

create_sweep_midi('pitchbend_sweep.mid', 'pitchwheel')
create_sweep_midi('modwheel_sweep.mid', 'modwheel')
create_sweep_midi('channel_aftertouch_sweep.mid', 'aftertouch')
create_sweep_midi('poly_aftertouch_sweep.mid', 'polytouch')
create_sweep_midi('breath_controller_sweep.mid', 'breath')
create_sweep_midi('foot_controller_sweep.mid', 'foot')

##########################################################################################
# Systematic DX7 Feature Test Suite
# Each test is numbered and described. The generated MIDI files will exercise a wide range of DX7 features.
# Listen for the described effect in each test to verify correct operation.

# 1. Basic Note Test
# You should hear a single note (Middle C, MIDI note 60) played at medium velocity. The sound should be clear, with no modulation or effects. This verifies that the synth produces sound and that the default voice is loaded correctly. There should be no pitch bend, vibrato, or other modulation.
def test_01_basic_note(track):
    # Starts at tick: 0 in overall file
    track.append(Message('note_on', note=60, velocity=100, time=0))
    track.append(Message('note_off', note=60, velocity=0, time=480))
    return 480

# 2. Velocity Sensitivity Test
# You should hear the same note played twice: first softly (low velocity), then loudly (high velocity). The DX7's velocity sensitivity should cause the timbre and/or volume to change between the two notes. The soft note may sound duller or quieter, while the loud note should be brighter or more percussive, depending on the voice.
def test_02_velocity_sensitivity(track):
    # Starts at tick: 480 in overall file
    track.append(Message('note_on', note=60, velocity=20, time=0))
    track.append(Message('note_off', note=60, velocity=0, time=480))
    track.append(Message('note_on', note=60, velocity=120, time=240))
    track.append(Message('note_off', note=60, velocity=0, time=480))
    return 480+240+480

# 3. Pitch Bend Range Test
# You should hear a note bend smoothly up and down by the maximum pitch bend range set in the voice. The pitch should rise smoothly to its highest point, then fall smoothly to its lowest, then return to center. The bend should be continuous, not stepped, and the range should match the voice's pitch bend setting (often +/- 2 semitones by default).
def test_03_pitch_bend(track):
    track.append(Message('note_on', note=60, velocity=100, time=0))
    for i in range(32):
        bend = int(8192 + (8191 * i / 31))
        track.append(Message('pitchwheel', pitch=bend - 8192, time=30))
    for i in range(32):
        bend = int(16383 - (16383 * i / 31))
        track.append(Message('pitchwheel', pitch=bend - 8192, time=30))
    for i in range(32):
        bend = int(0 + (8192 * i / 31))
        track.append(Message('pitchwheel', pitch=bend - 8192, time=30))
    track.append(Message('note_off', note=60, velocity=0, time=120))
    track.append(Message('pitchwheel', pitch=0, time=0))
    return 480 + 3*32*30 + 120

# 4. Modulation Wheel (Vibrato) Test
# You should hear vibrato (pitch modulation) or tremolo (amplitude modulation) increase as the modulation wheel (CC1) is moved up, and decrease as it is moved down. The vibrato should be slow and subtle at first, becoming deeper and more obvious as the wheel reaches its maximum. When the wheel returns to zero, the vibrato should disappear.
def test_04_mod_wheel(track):
    track.append(Message('note_on', note=60, velocity=100, time=0))
    for i in range(32):
        mod = int(127 * i / 31)
        track.append(Message('control_change', control=1, value=mod, time=30))
    for i in range(32):
        mod = int(127 - (127 * i / 31))
        track.append(Message('control_change', control=1, value=mod, time=30))
    track.append(Message('note_off', note=60, velocity=0, time=120))
    track.append(Message('control_change', control=1, value=0, time=0))
    return 480 + 2*32*30 + 120

# 5. Channel Aftertouch Test
# You should hear a change in timbre, volume, or modulation as channel aftertouch (channel pressure) is applied and released. Pressing harder on the keyboard (or simulating this via MIDI) should cause the sound to become brighter, louder, or more modulated, depending on the voice's aftertouch routing. Releasing aftertouch should return the sound to normal.
def test_05_channel_aftertouch(track):
    track.append(Message('note_on', note=60, velocity=100, time=0))
    for i in range(32):
        val = int(127 * i / 31)
        track.append(Message('aftertouch', value=val, time=30))
    for i in range(32):
        val = int(127 - (127 * i / 31))
        track.append(Message('aftertouch', value=val, time=30))
    track.append(Message('note_off', note=60, velocity=0, time=120))
    track.append(Message('aftertouch', value=0, time=0))
    return 480 + 2*32*30 + 120

# 6. Polyphonic Aftertouch Test
# You should hear a change only on the pressed note as polyphonic aftertouch is applied. If the voice supports poly aftertouch, pressing harder on one key should affect only that note, not others. This is rare on the original DX7 but supported in some emulations and later models.
def test_06_poly_aftertouch(track):
    track.append(Message('note_on', note=60, velocity=100, time=0))
    for i in range(32):
        val = int(127 * i / 31)
        track.append(Message('polytouch', note=60, value=val, time=30))
    for i in range(32):
        val = int(127 - (127 * i / 31))
        track.append(Message('polytouch', note=60, value=val, time=30))
    track.append(Message('note_off', note=60, velocity=0, time=120))
    track.append(Message('polytouch', note=60, value=0, time=0))
    return 480 + 2*32*30 + 120

# 7. Breath Controller Test (CC2)
# You should hear a change in timbre, volume, or modulation as the breath controller (CC2) is swept up and down. Blowing harder (or increasing CC2 value) should make the sound brighter, louder, or more modulated, depending on the voice's breath controller assignment. Releasing the controller should return the sound to normal.
def test_07_breath_controller(track):
    track.append(Message('note_on', note=60, velocity=100, time=0))
    for i in range(32):
        val = int(127 * i / 31)
        track.append(Message('control_change', control=2, value=val, time=30))
    for i in range(32):
        val = int(127 - (127 * i / 31))
        track.append(Message('control_change', control=2, value=val, time=30))
    track.append(Message('note_off', note=60, velocity=0, time=120))
    track.append(Message('control_change', control=2, value=0, time=0))
    return 480 + 2*32*30 + 120

# 8. Foot Controller Test (CC4)
# You should hear a change in timbre, volume, or modulation as the foot controller (CC4) is swept up and down. Pressing the pedal (or increasing CC4 value) should affect the sound according to the voice's foot controller assignment. Releasing the pedal should return the sound to normal.
def test_08_foot_controller(track):
    track.append(Message('note_on', note=60, velocity=100, time=0))
    for i in range(32):
        val = int(127 * i / 31)
        track.append(Message('control_change', control=4, value=val, time=30))
    for i in range(32):
        val = int(127 - (127 * i / 31))
        track.append(Message('control_change', control=4, value=val, time=30))
    track.append(Message('note_off', note=60, velocity=0, time=120))
    track.append(Message('control_change', control=4, value=0, time=0))
    return 480 + 2*32*30 + 120

# 9. Portamento Test (CC5)
# You should hear a smooth glide (portamento) between two notes. The first note plays, then the second note glides up or down from the first, rather than starting immediately at the new pitch. The speed of the glide is determined by the portamento time setting. Portamento only works in mono mode on the DX7, so the test switches to mono mode before playing.
def test_09_portamento(track):
    mid = MidiFile()
    track = MidiTrack()
    mid.tracks.append(track)
    # Switch to mono mode
    track.append(Message('control_change', control=126, value=0, time=0))
    # Enable portamento
    track.append(Message('control_change', control=65, value=127, time=0))
    # Set portamento time
    track.append(Message('control_change', control=5, value=64, time=0))
    # Play first note
    track.append(Message('note_on', note=60, velocity=100, time=0))
    track.append(Message('note_off', note=60, velocity=0, time=480))
    # Play second note (should glide)
    track.append(Message('note_on', note=72, velocity=100, time=0))
    track.append(Message('note_off', note=72, velocity=0, time=480))
    # Disable portamento
    track.append(Message('control_change', control=65, value=0, time=0))
    # Switch back to poly mode
    track.append(Message('control_change', control=127, value=0, time=0))
    return 480 + 2*480 + 240

# 10. Program Change Test
# You should hear the same note played with two different timbres, as the program (voice) changes between two voicees. The sound should be noticeably different between the two notes, demonstrating that program change messages are working and that the synth can switch voicees via MIDI.
def test_10_program_change(track):
    mid = MidiFile()
    track = MidiTrack()
    mid.tracks.append(track)
    # Program 1
    track.append(Message('program_change', program=0, time=0))
    track.append(Message('note_on', note=60, velocity=100, time=0))
    track.append(Message('note_off', note=60, velocity=0, time=480))
    # Program 2
    track.append(Message('program_change', program=1, time=0))
    track.append(Message('note_on', note=60, velocity=100, time=0))
    track.append(Message('note_off', note=60, velocity=0, time=480))
    return 2*480

# 11. Expression Pedal (CC11) Test
# You should hear a change in volume or timbre as the expression pedal is swept up and down. The sound should get louder or more intense as the pedal is pressed, and softer as it is released. This tests the synth's response to the expression pedal controller.
def test_11_expression_pedal(track):
    track.append(Message('note_on', note=60, velocity=100, time=0))
    for i in range(32):
        val = int(127 * i / 31)
        track.append(Message('control_change', control=11, value=val, time=30))
    for i in range(32):
        val = int(127 - (127 * i / 31))
        track.append(Message('control_change', control=11, value=val, time=30))
    track.append(Message('note_off', note=60, velocity=0, time=120))
    track.append(Message('control_change', control=11, value=0, time=0))
    return 480 + 2*32*30 + 120

# 12. Sustain Pedal (CC64) Test
# You should hear the note sustain (continue to sound) after the key is released, as long as the sustain pedal (CC64) is held down. When the pedal is released, the note should stop. This tests the synth's sustain pedal response and release envelope behavior.
def test_12_sustain_pedal(track):
    # Press note
    track.append(Message('note_on', note=60, velocity=100, time=0))
    # Press sustain pedal
    track.append(Message('control_change', control=64, value=127, time=120))
    # Release note (should sustain)
    track.append(Message('note_off', note=60, velocity=0, time=240))
    # Release sustain pedal (should stop)
    track.append(Message('control_change', control=64, value=0, time=480))
    return 240 + 480 + 480

# 13. All Notes Off (CC123) Test
# You should hear a chord (three notes) start, then all notes should stop immediately when the All Notes Off message (CC123) is sent, even if no note-off messages are sent. This tests the synth's ability to respond to the All Notes Off controller and silence all voices.
def test_13_all_notes_off(track):
    # Play a chord
    for n in [60, 64, 67]:
        track.append(Message('note_on', note=n, velocity=100, time=0))
    # Hold for a while
    track.append(Message('control_change', control=123, value=0, time=480))
    # (No note_off needed, should be silent)
    return 480

# 14. Mono/Poly Mode Test (CC126/127)
# You should hear only one note at a time in mono mode (even if two notes are played), and chords in poly mode (both notes sound together). The test switches between mono and poly modes and plays two notes in each mode to demonstrate the difference.
def test_14_mono_poly_mode(track):
    # Set mono mode
    track.append(Message('control_change', control=126, value=0, time=0))
    # Play two notes (should only hear one)
    track.append(Message('note_on', note=60, velocity=100, time=0))
    track.append(Message('note_on', note=64, velocity=100, time=120))
    track.append(Message('note_off', note=60, velocity=0, time=240))
    track.append(Message('note_off', note=64, velocity=0, time=240))
    # Set poly mode
    track.append(Message('control_change', control=127, value=0, time=120))
    # Play two notes (should hear both)
    track.append(Message('note_on', note=60, velocity=100, time=0))
    track.append(Message('note_on', note=64, velocity=100, time=0))
    track.append(Message('note_off', note=60, velocity=0, time=480))
    track.append(Message('note_off', note=64, velocity=0, time=0))
    return 240 + 240 + 480

# 15. Volume (CC7) Test
# You should hear the overall volume increase and decrease as the volume controller (CC7) is swept up and down. The sound should get louder as the controller increases, and softer as it decreases. This tests the synth's response to MIDI volume changes.
def test_15_volume_cc7(track):
    track.append(Message('note_on', note=60, velocity=100, time=0))
    for i in range(32):
        val = int(127 * i / 31)
        track.append(Message('control_change', control=7, value=val, time=30))
    for i in range(32):
        val = int(127 - (127 * i / 31))
        track.append(Message('control_change', control=7, value=val, time=30))
    track.append(Message('note_off', note=60, velocity=0, time=120))
    track.append(Message('control_change', control=7, value=100, time=0))
    return 480 + 2*32*30 + 120

# 16. Pan (CC10) Test (if supported by synth)
# You should hear the sound move from the left speaker to the right and back as the pan controller (CC10) is swept. If the synth or voice does not support panning, there may be no audible effect. This tests the synth's stereo panning response.
def test_16_pan_cc10(track):
    track.append(Message('note_on', note=60, velocity=100, time=0))
    for i in range(32):
        val = int(127 * i / 31)
        track.append(Message('control_change', control=10, value=val, time=30))
    for i in range(32):
        val = int(127 - (127 * i / 31))
        track.append(Message('control_change', control=10, value=val, time=30))
    track.append(Message('note_off', note=60, velocity=0, time=120))
    track.append(Message('control_change', control=10, value=64, time=0))
    return 480 + 2*32*30 + 120

# 17. Data Entry (CC6) and Parameter Change (NRPN/RPN) Test
# You should hear a change in pitch bend range (or another parameter) if the synth supports data entry via CC6 and RPN/NRPN. The note should bend further than normal, demonstrating that the parameter change was received. This is rare on the original DX7 but supported in some emulations.
def test_17_data_entry(track):
    mid = MidiFile()
    track = MidiTrack()
    mid.tracks.append(track)
    # Set RPN to pitch bend range (0,0)
    track.append(Message('control_change', control=101, value=0, time=0))
    track.append(Message('control_change', control=100, value=0, time=0))
    # Data entry to set pitch bend range to 12 semitones
    track.append(Message('control_change', control=6, value=12, time=0))
    # Play note and bend
    track.append(Message('note_on', note=60, velocity=100, time=0))
    for i in range(32):
        bend = int(8192 + (8191 * i / 31))
        track.append(Message('pitchwheel', pitch=bend - 8192, time=30))
    track.append(Message('note_off', note=60, velocity=0, time=120))
    # Reset RPN
    track.append(Message('control_change', control=101, value=127, time=0))
    track.append(Message('control_change', control=100, value=127, time=0))
    return 480 + 32*30 + 120

def test_18_bank_select(track):
    mid = MidiFile()
    track = MidiTrack()
    mid.tracks.append(track)
    # Bank 0, program 0
    track.append(Message('control_change', control=0, value=0, time=0))
    track.append(Message('control_change', control=32, value=0, time=0))
    track.append(Message('program_change', program=0, time=0))
    track.append(Message('note_on', note=60, velocity=100, time=0))
    track.append(Message('note_off', note=60, velocity=0, time=480))
    # Bank 1, program 1
    track.append(Message('control_change', control=0, value=1, time=0))
    track.append(Message('control_change', control=32, value=0, time=0))
    track.append(Message('program_change', program=1, time=0))
    track.append(Message('note_on', note=60, velocity=100, time=0))
    track.append(Message('note_off', note=60, velocity=0, time=480))
    return 2*480

def test_19_detune_transpose(track):
    mid = MidiFile()
    track = MidiTrack()
    mid.tracks.append(track)
    # Program 0 (normal)
    track.append(Message('program_change', program=0, time=0))
    track.append(Message('note_on', note=60, velocity=100, time=0))
    track.append(Message('note_off', note=60, velocity=0, time=480))
    # Program 2 (should be detuned or transposed voice)
    track.append(Message('program_change', program=2, time=0))
    track.append(Message('note_on', note=60, velocity=100, time=0))
    track.append(Message('note_off', note=60, velocity=0, time=480))
    return 2*480

def test_20_key_range(track):
    # Play a sweep from C2 to C7
    for n in range(36, 96, 4):
        track.append(Message('note_on', note=n, velocity=100, time=60))
        track.append(Message('note_off', note=n, velocity=0, time=120))
    return 60 + 120*(15-9)

def test_21_algorithm_change(track):
    mid = MidiFile()
    track = MidiTrack()
    mid.tracks.append(track)
    for prog in [0, 1, 2]:
        track.append(Message('program_change', program=prog, time=0))
        track.append(Message('note_on', note=60, velocity=100, time=0))
        track.append(Message('note_off', note=60, velocity=0, time=480))
    return 3*480

def test_22_operator_on_off(track):
    mid = MidiFile()
    track = MidiTrack()
    mid.tracks.append(track)
    for prog in [0, 1, 2]:
        track.append(Message('program_change', program=prog, time=0))
        track.append(Message('note_on', note=60, velocity=100, time=0))
        track.append(Message('note_off', note=60, velocity=0, time=480))
    return 3*480

def test_23_eg_response(track):
    mid = MidiFile()
    track = MidiTrack()
    mid.tracks.append(track)
    for prog in [0, 1, 2]:
        track.append(Message('program_change', program=prog, time=0))
        track.append(Message('note_on', note=60, velocity=100, time=0))
        track.append(Message('note_off', note=60, velocity=0, time=960))
    return 3*960

def test_24_lfo_speed_delay(track):
    mid = MidiFile()
    track = MidiTrack()
    mid.tracks.append(track)
    for prog in [0, 1, 2]:
        track.append(Message('program_change', program=prog, time=0))
        track.append(Message('note_on', note=60, velocity=100, time=0))
        track.append(Message('note_off', note=60, velocity=0, time=960))
    return 3*960

def test_25_pitch_envelope(track):
    mid = MidiFile()
    track = MidiTrack()
    mid.tracks.append(track)
    for prog in [0, 1, 2]:
        track.append(Message('program_change', program=prog, time=0))
        track.append(Message('note_on', note=60, velocity=100, time=0))
        track.append(Message('note_off', note=60, velocity=0, time=960))
    return 3*960

def test_26_key_scaling(track):
    mid = MidiFile()
    track = MidiTrack()
    mid.tracks.append(track)
    track.append(Message('program_change', program=0, time=0))
    for n in range(36, 96, 6):
        track.append(Message('note_on', note=n, velocity=100, time=60))
        track.append(Message('note_off', note=n, velocity=0, time=180))
    return 60 + 180*(15-6)

def test_27_velocity_sensitivity_ops(track):
    mid = MidiFile()
    track = MidiTrack()
    mid.tracks.append(track)
    track.append(Message('program_change', program=0, time=0))
    # Soft
    track.append(Message('note_on', note=60, velocity=20, time=0))
    track.append(Message('note_off', note=60, velocity=0, time=480))
    # Loud
    track.append(Message('note_on', note=60, velocity=120, time=240))
    track.append(Message('note_off', note=60, velocity=0, time=480))
    return 480 + 240 + 480

def test_28_sustain_with_release_eg(track):
    mid = MidiFile()
    track = MidiTrack()
    mid.tracks.append(track)
    track.append(Message('program_change', program=0, time=0))
    track.append(Message('note_on', note=60, velocity=100, time=0))
    track.append(Message('control_change', control=64, value=127, time=240))
    track.append(Message('note_off', note=60, velocity=0, time=480))
    track.append(Message('control_change', control=64, value=0, time=960))
    return 480 + 240 + 960

def test_29_midi_channel(track):
    mid = MidiFile()
    track = MidiTrack()
    mid.tracks.append(track)
    # Channel 1
    track.append(Message('note_on', note=60, velocity=100, time=0, channel=0))
    track.append(Message('note_off', note=60, velocity=0, time=480, channel=0))
    # Channel 2
    track.append(Message('note_on', note=62, velocity=100, time=0, channel=1))
    track.append(Message('note_off', note=62, velocity=0, time=480, channel=1))
    return 480 + 480

def test_31_glissando(track):
    # Placeholder for glissando test
    # Starts at tick: (calculated in main function)
    # Add your actual test implementation here
    track.append(Message('note_on', note=60, velocity=100, time=0))
    track.append(Message('note_off', note=60, velocity=0, time=480))
    return 480

def test_32_pitch_env_depth(track):
    # Placeholder for pitch envelope depth test
    track.append(Message('note_on', note=60, velocity=100, time=0))
    track.append(Message('note_off', note=60, velocity=0, time=960))
    return 960

def test_33_lfo_waveform(track):
    # Placeholder for LFO waveform test
    for _ in range(3):
        track.append(Message('note_on', note=60, velocity=100, time=0))
        track.append(Message('note_off', note=60, velocity=0, time=960))
    return 3*960

def test_34_mod_sensitivity(track):
    # Placeholder for modulation sensitivity test
    for _ in range(2):
        track.append(Message('note_on', note=60, velocity=100, time=0))
        for i in range(32):
            mod = int(127 * i / 31)
            track.append(Message('control_change', control=1, value=mod, time=30))
        for i in range(32):
            mod = int(127 - (127 * i / 31))
            track.append(Message('control_change', control=1, value=mod, time=30))
        track.append(Message('note_off', note=60, velocity=0, time=120))
        track.append(Message('control_change', control=1, value=0, time=0))
    return 2*(480 + 2*32*30 + 120)

def test_35_operator_output_level(track):
    # Placeholder for operator output level test
    for _ in range(2):
        track.append(Message('note_on', note=60, velocity=100, time=0))
        track.append(Message('note_off', note=60, velocity=0, time=480))
    return 2*480

def test_36_lfo_key_sync(track):
    # Placeholder for LFO key sync test
    for _ in range(2):
        for n in [60, 64, 67]:
            track.append(Message('note_on', note=n, velocity=100, time=0))
            track.append(Message('note_off', note=n, velocity=0, time=480))
    return 2*3*480

def test_37_pitch_bend_range(track):
    # Placeholder for pitch bend range test
    for _ in range(2):
        track.append(Message('note_on', note=60, velocity=100, time=0))
        for i in range(32):
            bend = int(8192 + (8191 * i / 31))
            track.append(Message('pitchwheel', pitch=bend - 8192, time=30))
        for i in range(32):
            bend = int(16383 - (16383 * i / 31))
            track.append(Message('pitchwheel', pitch=bend - 8192, time=30))
        track.append(Message('note_off', note=60, velocity=0, time=120))
        track.append(Message('pitchwheel', pitch=0, time=0))
    return 2*(480 + 2*32*30 + 120)

def test_38_sustain_pedal_polarity(track):
    # Placeholder for sustain pedal polarity test
    # Pedal up, then down
    track.append(Message('control_change', control=64, value=0, time=0))
    track.append(Message('note_on', note=60, velocity=100, time=0))
    track.append(Message('control_change', control=64, value=127, time=120))
    track.append(Message('note_off', note=60, velocity=0, time=240))
    track.append(Message('control_change', control=64, value=0, time=480))
    # Pedal down, then up
    track.append(Message('control_change', control=64, value=127, time=0))
    track.append(Message('note_on', note=62, velocity=100, time=0))
    track.append(Message('control_change', control=64, value=0, time=120))
    track.append(Message('note_off', note=62, velocity=0, time=240))
    return 240+480+240+480

# Function to create the overall MIDI file

def create_all_tests_midi(filename='dx7test_all.mid'):
    mid = MidiFile()
    track = MidiTrack()
    set_voice_data(track)
    mid.tracks.append(track)
    tests = [
        test_01_basic_note,
        test_02_velocity_sensitivity,
        test_03_pitch_bend,
        test_04_mod_wheel,
        test_05_channel_aftertouch,
        test_06_poly_aftertouch,
        test_07_breath_controller,
        test_08_foot_controller,
        test_09_portamento,
        test_10_program_change,
        test_11_expression_pedal,
        test_12_sustain_pedal,
        test_13_all_notes_off,
        test_14_mono_poly_mode,
        test_15_volume_cc7,
        test_16_pan_cc10,
        test_17_data_entry,
        test_18_bank_select,
        test_19_detune_transpose,
        test_20_key_range,
        test_21_algorithm_change,
        test_22_operator_on_off,
        test_23_eg_response,
        test_24_lfo_speed_delay,
        test_25_pitch_envelope,
        test_26_key_scaling,
        test_27_velocity_sensitivity_ops,
        test_28_sustain_with_release_eg,
        test_29_midi_channel,
        test_31_glissando,
        test_32_pitch_env_depth,
        test_33_lfo_waveform,
        test_34_mod_sensitivity,
        test_35_operator_output_level,
        test_36_lfo_key_sync,
        test_37_pitch_bend_range,
        test_38_sustain_pedal_polarity
    ]
    tick = 0
    for i, test in enumerate(tests, 1):
        # Insert a marker for the start of each test using the function name
        track.append(MetaMessage('marker', text=f'{test.__name__}', time=0))
        duration = test(track)
        tick += duration
        # Insert a 1 second pause (assuming 480 ticks per quarter note, 120 BPM, 1 quarter note = 0.5 sec)
        # 1 second = 960 ticks at 120 BPM
        track.append(Message('note_off', note=0, velocity=0, time=960))
        tick += 960
        # Insert a gap (e.g., 240 ticks) between tests
        track.append(Message('note_off', note=0, velocity=0, time=240))
        tick += 240
    mid.save(filename)

# Call to generate the overall MIDI file
create_all_tests_midi()

# --- DX7 SysEx Check ---
from mido import MidiFile as MF

def is_dx7_voice_sysex(msg):
    # DX7 single voice parameter change: F0 43 1n 08 00 ... F7 (n = channel)
    # DX7 bulk dump: F0 43 00 09 20 ... F7
    if msg.type == 'sysex' and msg.data:
        data = msg.data
        # Check for single voice or bulk dump header
        if (len(data) >= 6 and data[0] == 0x43 and (
            (data[1] & 0xF0) == 0x10 and data[2] == 0x4C) # Used in this script
            ):
            # This is not a standard DX7 header (should be 08 for single, 09 for bulk)
            return False, 'Header 0x4C is not standard for DX7 voice data. Should be 0x08 (single) or 0x09 (bulk).'
        if (len(data) >= 6 and data[0] == 0x43 and data[2] in (0x08, 0x09)):
            return True, 'DX7 voice data sysex detected.'
    return False, 'No DX7 voice data sysex detected.'

mid = MF('dx7test_all.mid')
dx7_sysex_found = False
for i, track in enumerate(mid.tracks):
    for msg in track:
        if msg.type == 'sysex':
            ok, info = is_dx7_voice_sysex(msg)
            if ok:
                print(f"Track {i}: {info}")
                dx7_sysex_found = True
            else:
                print(f"Track {i}: Warning: {info}")
if not dx7_sysex_found:
    print("No valid DX7 voice data sysex found in the generated MIDI file.")

