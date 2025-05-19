# Synth_Dexed

Synth_Dexed is a port of the Dexed sound engine
(https://github.com/asb2m10/dexed) as library for the Teensy-3.5/3.6/4.x with
an audio shield. Dexed ist heavily based on
https://github.com/google/music-synthesizer-for-android .
Synth_Dexed ist SYSEX compatible with a famous 6-OP FM synth and is used in MicroDexed (https://codeberg.org/dcoredump/MicroDexed).

## License
MicroDexed is licensed on the GPL v3. The msfa component (acronym for music synthesizer for android, see https://github.com/google/music-synthesizer-for-android) stays on the Apache 2.0 license to able to collaborate between projects.

## Credits & thanks

* Dexed engine by Pascal Gauthier (asb2m10)
* DX Synth engine (as part of Dexed): Raph Levien and the msfa team
* PPPlay : Great OPL3 implementation, with documented code :D
* Thierry Pottier: for extreme testing, discussing about different options, images and many good suggestions for UI handling
* Lars Pelz: Testing and documentation
* Mark (@positionhigh) for testing

## Usage

See example sketch.

## API

    AudioSynthDexed(uint8_t max_notes, uint16_t sample_rate);

    // Global methods
    void activate(void);
    void deactivate(void);
    bool getMonoMode(void);
    void setMonoMode(bool mode);
    void setNoteRefreshMode(bool mode);
    uint8_t getMaxNotes(void);
    void doRefreshVoice(void);
    void setOPAll(uint8_t ops);
    bool decodeVoice(uint8_t* data, uint8_t* encoded_data);
    bool encodeVoice(uint8_t* encoded_data);
    bool getVoiceData(uint8_t* data_copy);
    void setVoiceDataElement(uint8_t address, uint8_t value);
    uint8_t getVoiceDataElement(uint8_t address);
    void loadInitVoice(void);
    void loadVoiceParameters(uint8_t* data);
    uint8_t getNumNotesPlaying(void);
    uint32_t getXRun(void);
    uint16_t getRenderTimeMax(void);
    void resetRenderTimeMax(void);
    void ControllersRefresh(void);
    void setVelocityScale(uint8_t offset, uint8_t max);
    void getVelocityScale(uint8_t* offset, uint8_t* max);
    void setVelocityScale(uint8_t setup);
    void setMaxNotes(uint8_t n);
    void setEngineType(uint8_t engine);
    uint8_t getEngineType(void);
    FmCore* getEngineAddress(void);
    int16_t checkSystemExclusive(const uint8_t* sysex, const uint16_t len);

    // Sound methods
    void keyup(uint8_t pitch);
    void keydown(uint8_t pitch, uint8_t velo);
    void setSustain(bool sustain);
    bool getSustain(void);
    void setSostenuto(bool sostenuto);
    bool getSostenuto(void);
    void panic(void);
    void notesOff(void);
    void resetControllers(void);
    void setMasterTune(int8_t mastertune);
    int8_t getMasterTune(void);
    void setPortamento(uint8_t portamento_mode, uint8_t portamento_glissando, uint8_t portamento_time);
    void setPortamentoMode(uint8_t portamento_mode);
    uint8_t getPortamentoMode(void);
    void setPortamentoGlissando(uint8_t portamento_glissando);
    uint8_t getPortamentoGlissando(void);
    void setPortamentoTime(uint8_t portamento_time);
    uint8_t getPortamentoTime(void);
    void setPBController(uint8_t pb_range, uint8_t pb_step);
    void setMWController(uint8_t mw_range, uint8_t mw_assign, uint8_t mw_mode);
    void setFCController(uint8_t fc_range, uint8_t fc_assign, uint8_t fc_mode);
    void setBCController(uint8_t bc_range, uint8_t bc_assign, uint8_t bc_mode);
    void setATController(uint8_t at_range, uint8_t at_assign, uint8_t at_mode);
    void setModWheel(uint8_t value);
    uint8_t getModWheel(void);
    void setBreathController(uint8_t value);
    uint8_t getBreathController(void);
    void setFootController(uint8_t value);
    uint8_t getFootController(void);
    void setAftertouch(uint8_t value);
    uint8_t getAftertouch(void);
    void setPitchbend(uint8_t value1, uint8_t value2);
    void setPitchbend(int16_t value);
    void setPitchbend(uint16_t value);
    int16_t getPitchbend(void);
    void setPitchbendRange(uint8_t range);
    uint8_t getPitchbendRange(void);
    void setPitchbendStep(uint8_t step);
    uint8_t getPitchbendStep(void);
    void setModWheelRange(uint8_t range);
    uint8_t getModWheelRange(void);
    void setModWheelTarget(uint8_t target);
    uint8_t getModWheelTarget(void);
    void setFootControllerRange(uint8_t range);
    uint8_t getFootControllerRange(void);
    void setFootControllerTarget(uint8_t target);
    uint8_t getFootControllerTarget(void);
    void setBreathControllerRange(uint8_t range);
    uint8_t getBreathControllerRange(void);
    void setBreathControllerTarget(uint8_t target);
    uint8_t getBreathControllerTarget(void);
    void setAftertouchRange(uint8_t range);
    uint8_t getAftertouchRange(void);
    void setAftertouchTarget(uint8_t target);
    uint8_t getAftertouchTarget(void);
    void setGain(float gain);
    float getGain(void);

    void setCompDownsample(uint8_t _downSample);
    uint8_t getCompDownsample(void);
    float getCompAttack(void);
    void setCompAttack(float _attack);
    float getCompAttack(void);
    void setCompRelease(float _release);
    float getCompRelease(void);
    void setCompRatio(float _ratio);
    float getCompRatio(void);
    void setCompKnee(float _knee);
    float getCompKnee(void);
    void setCompThreshold(float _thresh);
    float getCompThreshold(void);
    void setCompMakeupGain(float _makeupGain);
    float getCompMakeupGain(void);
    void setCompEnable(bool _enable);
    bool getCompEnable(void);

    // Voice configuration methods
    void setOPRateAll(uint8_t rate);
    void setOPLevelAll(uint8_t level);
    void setOPRateAllCarrier(uint8_t step, uint8_t rate);
    void setOPLevelAllCarrier(uint8_t step, uint8_t level);
    void setOPRateAllModulator(uint8_t step, uint8_t rate);
    void setOPLevelAllModulator(uint8_t step, uint8_t level);
    void setOPRate(uint8_t op, uint8_t step, uint8_t rate);
    uint8_t getOPRate(uint8_t op, uint8_t step);
    void setOPLevel(uint8_t op, uint8_t step, uint8_t level);
    uint8_t getOPLevel(uint8_t op, uint8_t step);
    void setOPKeyboardLevelScalingBreakPoint(uint8_t op, uint8_t level);
    uint8_t getOPKeyboardLevelScalingBreakPoint(uint8_t op);
    void setOPKeyboardLevelScalingDepthLeft(uint8_t op, uint8_t depth);
    uint8_t getOPKeyboardLevelScalingDepthLeft(uint8_t op);
    void setOPKeyboardLevelScalingDepthRight(uint8_t op, uint8_t depth);
    uint8_t getOPKeyboardLevelScalingDepthRight(uint8_t op);
    void setOPKeyboardLevelScalingCurveLeft(uint8_t op, uint8_t curve);
    uint8_t getOPKeyboardLevelScalingCurveLeft(uint8_t op);
    void setOPKeyboardLevelScalingCurveRight(uint8_t op, uint8_t curve);
    uint8_t getOPKeyboardLevelScalingCurveRight(uint8_t op);
    void setOPKeyboardRateScale(uint8_t op, uint8_t scale);
    uint8_t getOPKeyboardRateScale(uint8_t op);
    void setOPAmpModulationSensity(uint8_t op, uint8_t sensitivity);
    uint8_t getOPAmpModulationSensity(uint8_t op);
    void setOPKeyboardVelocitySensity(uint8_t op, uint8_t sensitivity);
    uint8_t getOPKeyboardVelocitySensity(uint8_t op);
    void setOPOutputLevel(uint8_t op, uint8_t level);
    uint8_t getOPOutputLevel(uint8_t op);
    void setOPMode(uint8_t op, uint8_t mode);
    uint8_t getOPMode(uint8_t op);
    void setOPFrequencyCoarse(uint8_t op, uint8_t frq_coarse);
    uint8_t getOPFrequencyCoarse(uint8_t op);
    void setOPFrequencyFine(uint8_t op, uint8_t frq_fine);
    uint8_t getOPFrequencyFine(uint8_t op);
    void setOPDetune(uint8_t op, uint8_t detune);
    uint8_t getOPDetune(uint8_t op);
    void setPitchRate(uint8_t step, uint8_t rate);
    uint8_t getPitchRate(uint8_t step);
    void setPitchLevel(uint8_t step, uint8_t level);
    uint8_t getPitchLevel(uint8_t step);
    void setAlgorithm(uint8_t algorithm);
    uint8_t getAlgorithm(void);
    void setFeedback(uint8_t feedback);
    uint8_t getFeedback(void);
    void setOscillatorSync(bool sync);
    bool getOscillatorSync(void);
    void setLFOSpeed(uint8_t speed);
    uint8_t getLFOSpeed(void);
    void setLFODelay(uint8_t delay);
    uint8_t getLFODelay(void);
    void setLFOPitchModulationDepth(uint8_t depth);
    uint8_t getLFOPitchModulationDepth(void);
    void setLFOSync(bool sync);
    bool getLFOSync(void);
    void setLFOWaveform(uint8_t waveform);
    uint8_t getLFOWaveform(void);
    void setLFOPitchModulationSensitivity(uint8_t sensitivity);
    uint8_t getLFOPitchModulationSensitivity(void);
    void setTranspose(uint8_t transpose);
    uint8_t getTranspose(void);
    void setName(char* name);
    void getName(char* buffer);

## Native

### Windows

```
winget install cmake
mkdir build
cd build
& "C:\Program Files\CMake\bin\cmake.exe" ..
# -- Building for: Visual Studio 17 2022
# -- Selecting Windows SDK version 10.0.22621.0 to target Windows 10.0.22631.
# -- The CXX compiler identification is MSVC 19.43.34809.0
# (...)
# -- Build files have been written to: C:/Users/User/Development/Synth_Dexed/build
& "C:\Program Files\CMake\bin\cmake.exe"  --build . --config Release
# Release\Synth_Dexed.exe
```

Python bindings:

```
cd python
pip install -r requirements.txt
python setup.py clean --all # When needed
python setup.py build; python setup.py install; python -m unittest -v test_dexed.py
```

### Linux

```
sudo apt-get update
sudo apt-get -y install cmake
mkdir build
cd build
cmake ..
cmake --build . --config Release
# ./Synth_Dexed
```

### macOS

```
brew install cmake --formula
mkdir build
cd build
cmake ..
cmake --build . --config Release
# ./Synth_Dexed
```