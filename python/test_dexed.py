
import unittest
import ctypes
from dexed_py import Dexed

import faulthandler
faulthandler.enable()

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

class TestDexedHost(unittest.TestCase):
    @staticmethod
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

    @classmethod
    def setUpClass(cls):
        print("[INFO] Listing audio devices...")
        cls.list_audio_devices()
        device_index = 0
        print(f"[INFO] Using audio device: {device_index}")
        print("[INFO] Initializing DexedHost...")
        cls.synth = Dexed(16, 48000)
        cls.synth.loadVoiceParameters(FMPIANO_SYSEX)
        cls.synth.setGain(2.0)
        print("[INFO] Audio started.")
        # Removed sleep for speed

    @classmethod
    def tearDownClass(cls):
        print("[INFO] Tearing down DexedHost...")
        # No teardown needed for PyDexed
        print("[INFO] Teardown complete.")

    def setUp(self):
        pass  # No per-test setup needed

    def tearDown(self):
        pass  # No per-test teardown needed

    def test_note_on_off(self):
        self.synth.keydown(60, 100)
        # Removed sleep for speed
        self.synth.keyup(60)
        # Removed sleep for speed

    def test_reset_controllers(self):
        self.synth.resetControllers()

    def test_set_velocity_scale(self):
        for scale in [0, 1, 2, 127]:
            self.synth.setVelocityScale(scale)

    def test_set_mono_mode(self):
        self.synth.setMonoMode(True)
        self.synth.setMonoMode(False)

    def test_set_note_refresh_mode(self):
        self.synth.setNoteRefreshMode(True)
        self.synth.setNoteRefreshMode(False)

    def test_get_max_notes(self):
        max_notes = self.synth.getMaxNotes()
        self.assertEqual(max_notes, 16)

    def test_do_refresh_voice(self):
        self.synth.doRefreshVoice()

    def test_load_voice_parameters(self):
        self.synth.loadVoiceParameters(FMPIANO_SYSEX)
        self.synth.loadVoiceParameters(b"")  # Should not crash

    def test_set_engine_type_and_get(self):
        for t in [0, 1, 2]:
            self.synth.setEngineType(t)
            self.assertEqual(self.synth.getEngineType(), t)

    def test_load_init_voice(self):
        self.synth.loadInitVoice()

    def test_gain_edge_cases(self):
        for gain in [-1.0, 0.0, 1.0, 2.0, 10.0]:
            self.synth.setGain(gain)

    def test_multiple_start_stop_audio(self):
        # Removed: self.synth.stop_audio() and self.synth.start_audio() as they are not available in Dexed
        pass

    def test_note_on_off_edge_values(self):
        for note in [0, 127]:
            for velocity in [0, 127]:
                self.synth.keydown(note, velocity)
                self.synth.keyup(note)

    def test_all_methods_sequence(self):
        self.synth.resetControllers()
        self.synth.setVelocityScale(3)
        self.synth.setMonoMode(True)
        self.synth.setNoteRefreshMode(True)
        self.synth.doRefreshVoice()
        self.synth.setGain(1.5)
        self.synth.loadVoiceParameters(FMPIANO_SYSEX)
        self.synth.setEngineType(1)
        self.synth.getEngineType()
        self.synth.loadInitVoice()
        self.synth.keydown(60, 100)
        self.synth.keyup(60)

    def test_pitchbend_range_and_step(self):
        # Test several valid values for range and step
        for rng in [0, 2, 7, 12]:
            self.synth.setPitchbendRange(rng)
            self.assertEqual(self.synth.getPitchbendRange(), rng)
        for step in [0, 1, 6, 12]:
            self.synth.setPitchbendStep(step)
            self.assertEqual(self.synth.getPitchbendStep(), step)

    def test_mod_wheel_controller(self):
        for value in [0, 64, 127]:
            self.synth.setModWheel(value)
            self.assertEqual(self.synth.getModWheel(), value)
        for rng in [0, 6, 12]:
            self.synth.setModWheelRange(rng)
            self.assertEqual(self.synth.getModWheelRange(), rng)
        for tgt in [0, 3, 7]:
            self.synth.setModWheelTarget(tgt)
            self.assertEqual(self.synth.getModWheelTarget(), tgt)

    def test_breath_controller(self):
        for value in [0, 64, 127]:
            self.synth.setBreathController(value)
            self.assertEqual(self.synth.getBreathController(), value)
        for rng in [0, 6, 12]:
            self.synth.setBreathControllerRange(rng)
            self.assertEqual(self.synth.getBreathControllerRange(), rng)
        for tgt in [0, 3, 7]:
            self.synth.setBreathControllerTarget(tgt)
            self.assertEqual(self.synth.getBreathControllerTarget(), tgt)

    def test_aftertouch_controller(self):
        for value in [0, 64, 127]:
            self.synth.setAftertouch(value)
            self.assertEqual(self.synth.getAftertouch(), value)
        for rng in [0, 6, 12]:
            self.synth.setAftertouchRange(rng)
            self.assertEqual(self.synth.getAftertouchRange(), rng)
        for tgt in [0, 3, 7]:
            self.synth.setAftertouchTarget(tgt)
            self.assertEqual(self.synth.getAftertouchTarget(), tgt)

    def test_foot_controller(self):
        for value in [0, 64, 127]:
            self.synth.setFootController(value)
            self.assertEqual(self.synth.getFootController(), value)
        for rng in [0, 6, 12]:
            self.synth.setFootControllerRange(rng)
            self.assertEqual(self.synth.getFootControllerRange(), rng)
        for tgt in [0, 3, 7]:
            self.synth.setFootControllerTarget(tgt)
            self.assertEqual(self.synth.getFootControllerTarget(), tgt)

    def test_gain_and_compressor(self):
        # Gain
        for gain in [0.0, 0.5, 1.0, 2.0]:
            self.synth.setGain(gain)
            # getGain is now bound, so we can check it
            self.assertAlmostEqual(self.synth.getGain(), min(max(gain, 0.0), 2.0), places=2)
        # Compressor downsample
        for ds in [0, 2, 8, 255]:
            self.synth.setCompDownsample(ds)
            self.assertEqual(self.synth.getCompDownsample(), ds)
        # Compressor attack/release/ratio/knee/threshold/makeupGain
        for val in [0.1, 1.0, 10.0]:
            self.synth.setCompAttack(val)
            self.assertAlmostEqual(self.synth.getCompAttack(), val, places=2)
            self.synth.setCompRelease(val)
            self.assertAlmostEqual(self.synth.getCompRelease(), val, places=2)
            self.synth.setCompRatio(val)
            self.assertAlmostEqual(self.synth.getCompRatio(), val, places=2)
            self.synth.setCompKnee(val)
            self.assertAlmostEqual(self.synth.getCompKnee(), val, places=2)
            self.synth.setCompThreshold(val)
            self.assertAlmostEqual(self.synth.getCompThreshold(), val, places=2)
            self.synth.setCompMakeupGain(val)
            self.assertAlmostEqual(self.synth.getCompMakeupGain(), val, places=2)
        # Compressor enable
        for enable in [True, False]:
            self.synth.setCompEnable(enable)
            self.assertEqual(self.synth.getCompEnable(), enable)

    def test_op_level_and_rate(self):
        # Test set/get for OP rates and levels for a few ops and steps
        for op in [0, 3, 5]:
            for step in [0, 2, 3]:
                self.synth.setOPRate(op, step, 42)
                self.assertEqual(self.synth.getOPRate(op, step), 42)
                self.synth.setOPLevel(op, step, 99)
                self.assertEqual(self.synth.getOPLevel(op, step), 99)
        # Test set all ops
        self.synth.setOPAll(55)  # Should not throw
        self.synth.setOPRateAll(12)  # Should not throw
        self.synth.setOPLevelAll(88)  # Should not throw
        self.synth.setOPRateAllCarrier(1, 23)  # Should not throw
        self.synth.setOPLevelAllCarrier(2, 77)  # Should not throw
        self.synth.setOPRateAllModulator(3, 33)  # Should not throw
        self.synth.setOPLevelAllModulator(0, 44)  # Should not throw
        # Test set/get voice data element
        self.synth.setVoiceDataElement(10, 123)
        self.assertEqual(self.synth.getVoiceDataElement(10), 123)

    def test_op_parameter_methods(self):
        # Test OP parameter set/get for a few ops
        for op in [0, 2, 5]:
            self.synth.setOPAmpModulationSensity(op, 7)
            # Dexed clamps to 0-3, so expect 3
            self.assertEqual(self.synth.getOPAmpModulationSensity(op), 3)
            self.synth.setOPKeyboardVelocitySensity(op, 5)
            self.assertEqual(self.synth.getOPKeyboardVelocitySensity(op), 5)
            self.synth.setOPOutputLevel(op, 99)
            self.assertEqual(self.synth.getOPOutputLevel(op), 99)
            self.synth.setOPMode(op, 1)
            self.assertEqual(self.synth.getOPMode(op), 1)
            self.synth.setOPFrequencyCoarse(op, 8)
            self.assertEqual(self.synth.getOPFrequencyCoarse(op), 8)
            self.synth.setOPFrequencyFine(op, 42)
            self.assertEqual(self.synth.getOPFrequencyFine(op), 42)
            self.synth.setOPDetune(op, 3)
            self.assertEqual(self.synth.getOPDetune(op), 3)

    def test_op_keyboard_scaling_methods(self):
        for op in [0, 3, 5]:
            self.synth.setOPKeyboardLevelScalingBreakPoint(op, 60)
            self.assertEqual(self.synth.getOPKeyboardLevelScalingBreakPoint(op), 60)
            self.synth.setOPKeyboardLevelScalingDepthLeft(op, 12)
            self.assertEqual(self.synth.getOPKeyboardLevelScalingDepthLeft(op), 12)
            self.synth.setOPKeyboardLevelScalingDepthRight(op, 34)
            self.assertEqual(self.synth.getOPKeyboardLevelScalingDepthRight(op), 34)
            self.synth.setOPKeyboardLevelScalingCurveLeft(op, 1)
            self.assertEqual(self.synth.getOPKeyboardLevelScalingCurveLeft(op), 1)
            self.synth.setOPKeyboardLevelScalingCurveRight(op, 2)
            self.assertEqual(self.synth.getOPKeyboardLevelScalingCurveRight(op), 2)
            self.synth.setOPKeyboardRateScale(op, 3)
            self.assertEqual(self.synth.getOPKeyboardRateScale(op), 3)

    def test_pitch_algo_feedback_lfo(self):
        # Pitch envelope
        for step in [0, 1, 3]:
            self.synth.setPitchRate(step, 55)
            self.assertEqual(self.synth.getPitchRate(step), 55)
            self.synth.setPitchLevel(step, 77)
            self.assertEqual(self.synth.getPitchLevel(step), 77)
        # Algorithm and feedback
        for algo in [0, 7, 31]:
            self.synth.setAlgorithm(algo)
            self.assertEqual(self.synth.getAlgorithm(), algo)
        for fb in [0, 3, 7]:
            self.synth.setFeedback(fb)
            self.assertEqual(self.synth.getFeedback(), fb)
        # Oscillator sync
        for sync in [True, False]:
            self.synth.setOscillatorSync(sync)
            self.assertEqual(self.synth.getOscillatorSync(), sync)
        # LFO
        for speed in [0, 32, 99]:
            self.synth.setLFOSpeed(speed)
            self.assertEqual(self.synth.getLFOSpeed(), speed)
        for delay in [0, 10, 99]:
            self.synth.setLFODelay(delay)
            self.assertEqual(self.synth.getLFODelay(), delay)
        for depth in [0, 64, 127]:
            self.synth.setLFOPitchModulationDepth(depth)
            # Dexed clamps to 0-99, so expect min(depth, 99)
            self.assertEqual(self.synth.getLFOPitchModulationDepth(), min(depth, 99))
        for sync in [True, False]:
            self.synth.setLFOSync(sync)
            self.assertEqual(self.synth.getLFOSync(), sync)
        for wf in [0, 2, 5]:
            self.synth.setLFOWaveform(wf)
            self.assertEqual(self.synth.getLFOWaveform(), wf)
        for sens in [0, 3, 7]:
            self.synth.setLFOPitchModulationSensitivity(sens)
            # Dexed clamps to 0-5, so expect min(sens, 5)
            self.assertEqual(self.synth.getLFOPitchModulationSensitivity(), min(sens, 5))
        # Transpose
        for tr in [0, 12, 24]:
            self.synth.setTranspose(tr)
            self.assertEqual(self.synth.getTranspose(), tr)
        # Name
        name = "FMPIANO"
        self.synth.setName(name)
        returned_name = self.synth.getName()
        self.assertTrue(returned_name.startswith(name))

"""    def test_audible_sound(self):
        try:
            self.synth.resetControllers()
            self.synth.setGain(2.0)
            print("[Test] Sending programmatic MIDI note (Middle C, velocity 100) via rtmidi...")
            midi_out = rtmidi.MidiOut()
            midi_out.open_port(0)  # Use port 0 for test
            midi_out.send_message([0x90, 60, 100])  # Note on
            time.sleep(1)
            midi_out.send_message([0x80, 60, 0])    # Note off
            time.sleep(1)
            midi_out.close_port()
            print("[Test] You should have heard a short FM piano note (Middle C) for about 4 seconds.\nDid you hear this sound? (y/n): ", end="")
            user_input = input().strip().lower()
            self.assertEqual(user_input, "y", "User did not confirm hearing the expected sound.")
        except Exception as e:
            import traceback
            print("[Test] Exception occurred in test_audible_sound:")
            traceback.print_exc()
            raise
"""
if __name__ == "__main__":
    TestDexedHost.list_audio_devices()
    print("[INFO] Running DexedHost tests with audio device 0...")
    unittest.main()