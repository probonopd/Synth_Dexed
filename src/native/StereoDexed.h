#pragma once
#include <cstdint>
#include "dexed.h"
class StereoDexed {
public:
    Dexed* dexed;
    int noteLimitLow = 0;
    int noteLimitHigh = 127;
    float stereoPosition = 0.0f;
    StereoDexed(int poly, unsigned int sampleRate) : dexed(new Dexed(poly, sampleRate)) {}
    ~StereoDexed() { delete dexed; }
    void setNoteLimitLow(int note) { noteLimitLow = note; }
    void setNoteLimitHigh(int note) { noteLimitHigh = note; }
    void setStereoPosition(float pos) { stereoPosition = pos; }
    void loadVoiceParameters(uint8_t* data) { dexed->loadVoiceParameters(data); }
    void setMasterTune(int v) { dexed->setMasterTune(v); }
    void setTranspose(int v) { dexed->setTranspose(v); }
    void setGain(float v) { dexed->setGain(v); }
    void setFilterCutoff(float v) { dexed->setFilterCutoff(v); }
    void setFilterResonance(float v) { dexed->setFilterResonance(v); }
    void setPBController(int a, int b) { dexed->setPBController(a, b); }
    void setPortamento(int a, int b, int c) { dexed->setPortamento(a, b, c); }
    void setMWController(int a, int b, int c) { dexed->setMWController(a, b, c); }
    void setFCController(int a, int b, int c) { dexed->setFCController(a, b, c); }
    void setBCController(int a, int b, int c) { dexed->setBCController(a, b, c); }
    void setATController(int a, int b, int c) { dexed->setATController(a, b, c); }
    void activate() { dexed->activate(); }
    void deactivate() { dexed->deactivate(); }
    bool getMonoMode() { return dexed->getMonoMode(); }
    void setMonoMode(bool mode) { dexed->setMonoMode(mode); }
    void setNoteRefreshMode(bool mode) { dexed->setNoteRefreshMode(mode); }
    uint8_t getMaxNotes() { return dexed->getMaxNotes(); }
    void doRefreshVoice() { dexed->doRefreshVoice(); }
    void setOPAll(uint8_t ops) { dexed->setOPAll(ops); }
    bool decodeVoice(uint8_t* data, uint8_t* encoded_data) { return dexed->decodeVoice(data, encoded_data); }
    bool encodeVoice(uint8_t* encoded_data) { return dexed->encodeVoice(encoded_data); }
    bool getVoiceData(uint8_t* data_copy) { return dexed->getVoiceData(data_copy); }
    void setVoiceDataElement(uint8_t address, uint8_t value) { dexed->setVoiceDataElement(address, value); }
    uint8_t getVoiceDataElement(uint8_t address) { return dexed->getVoiceDataElement(address); }
    void loadInitVoice() { dexed->loadInitVoice(); }
    uint8_t getNumNotesPlaying() { return dexed->getNumNotesPlaying(); }
    uint32_t getXRun() { return dexed->getXRun(); }
    uint16_t getRenderTimeMax() { return dexed->getRenderTimeMax(); }
    void resetRenderTimeMax() { dexed->resetRenderTimeMax(); }
    void ControllersRefresh() { dexed->ControllersRefresh(); }
    void setVelocityScale(uint8_t offset, uint8_t max) { dexed->setVelocityScale(offset, max); }
    void getVelocityScale(uint8_t* offset, uint8_t* max) { dexed->getVelocityScale(offset, max); }
    void setVelocityScale(uint8_t setup) { dexed->setVelocityScale(setup); }
    void setMaxNotes(uint8_t n) { dexed->setMaxNotes(n); }
    void setEngineType(uint8_t engine) { dexed->setEngineType(engine); }
    uint8_t getEngineType() { return dexed->getEngineType(); }
    FmCore* getEngineAddress() { return dexed->getEngineAddress(); }
    int16_t checkSystemExclusive(const uint8_t* sysex, const uint16_t len) { return dexed->checkSystemExclusive(sysex, len); }
    void keyup(uint8_t pitch) { dexed->keyup(pitch); }
    void keydown(uint8_t pitch, uint8_t velo) { dexed->keydown(pitch, velo); }
    void setSustain(bool sustain) { dexed->setSustain(sustain); }
    bool getSustain() { return dexed->getSustain(); }
    void setSostenuto(bool sostenuto) { dexed->setSostenuto(sostenuto); }
    bool getSostenuto() { return dexed->getSostenuto(); }
    void setHold(bool hold) { dexed->setHold(hold); }
    bool getHold() { return dexed->getHold(); }
    void panic() { dexed->panic(); }
    void notesOff() { dexed->notesOff(); }
    void resetControllers() { dexed->resetControllers(); }
    void setMasterTune(int8_t mastertune) { dexed->setMasterTune(mastertune); }
    int8_t getMasterTune() { return dexed->getMasterTune(); }
    void setPortamento(uint8_t portamento_mode, uint8_t portamento_glissando, uint8_t portamento_time) { dexed->setPortamento(portamento_mode, portamento_glissando, portamento_time); }
    void setPortamentoMode(uint8_t portamento_mode) { dexed->setPortamentoMode(portamento_mode); }
    uint8_t getPortamentoMode() { return dexed->getPortamentoMode(); }
    void setPortamentoGlissando(uint8_t portamento_glissando) { dexed->setPortamentoGlissando(portamento_glissando); }
    uint8_t getPortamentoGlissando() { return dexed->getPortamentoGlissando(); }
    void setPortamentoTime(uint8_t portamento_time) { dexed->setPortamentoTime(portamento_time); }
    uint8_t getPortamentoTime() { return dexed->getPortamentoTime(); }
    void setPBController(uint8_t pb_range, uint8_t pb_step) { dexed->setPBController(pb_range, pb_step); }
    void setMWController(uint8_t mw_range, uint8_t mw_assign, uint8_t mw_mode) { dexed->setMWController(mw_range, mw_assign, mw_mode); }
    void setFCController(uint8_t fc_range, uint8_t fc_assign, uint8_t fc_mode) { dexed->setFCController(fc_range, fc_assign, fc_mode); }
    void setBCController(uint8_t bc_range, uint8_t bc_assign, uint8_t bc_mode) { dexed->setBCController(bc_range, bc_assign, bc_mode); }
    void setATController(uint8_t at_range, uint8_t at_assign, uint8_t at_mode) { dexed->setATController(at_range, at_assign, at_mode); }
    void setModWheel(uint8_t value) { dexed->setModWheel(value); }
    uint8_t getModWheel() { return dexed->getModWheel(); }
    void setBreathController(uint8_t value) { dexed->setBreathController(value); }
    uint8_t getBreathController() { return dexed->getBreathController(); }
    void setFootController(uint8_t value) { dexed->setFootController(value); }
    uint8_t getFootController() { return dexed->getFootController(); }
    void setAftertouch(uint8_t value) { dexed->setAftertouch(value); }
    uint8_t getAftertouch() { return dexed->getAftertouch(); }
    void setPitchbend(uint8_t value1, uint8_t value2) { dexed->setPitchbend(value1, value2); }
    void setPitchbend(int16_t value) { dexed->setPitchbend(value); }
    void setPitchbend(uint16_t value) { dexed->setPitchbend(value); }
    int16_t getPitchbend() { return dexed->getPitchbend(); }
    void setPitchbendRange(uint8_t range) { dexed->setPitchbendRange(range); }
    uint8_t getPitchbendRange() { return dexed->getPitchbendRange(); }
    void setPitchbendStep(uint8_t step) { dexed->setPitchbendStep(step); }
    uint8_t getPitchbendStep() { return dexed->getPitchbendStep(); }
    void setModWheelRange(uint8_t range) { dexed->setModWheelRange(range); }
    uint8_t getModWheelRange() { return dexed->getModWheelRange(); }
    void setModWheelTarget(uint8_t target) { dexed->setModWheelTarget(target); }
    uint8_t getModWheelTarget() { return dexed->getModWheelTarget(); }
    void setFootControllerRange(uint8_t range) { dexed->setFootControllerRange(range); }
    uint8_t getFootControllerRange() { return dexed->getFootControllerRange(); }
    void setFootControllerTarget(uint8_t target) { dexed->setFootControllerTarget(target); }
    uint8_t getFootControllerTarget() { return dexed->getFootControllerTarget(); }
    void setBreathControllerRange(uint8_t range) { dexed->setBreathControllerRange(range); }
    uint8_t getBreathControllerRange() { return dexed->getBreathControllerRange(); }
    void setBreathControllerTarget(uint8_t target) { dexed->setBreathControllerTarget(target); }
    uint8_t getBreathControllerTarget() { return dexed->getBreathControllerTarget(); }
    void setAftertouchRange(uint8_t range) { dexed->setAftertouchRange(range); }
    uint8_t getAftertouchRange() { return dexed->getAftertouchRange(); }
    void setAftertouchTarget(uint8_t target) { dexed->setAftertouchTarget(target); }
    uint8_t getAftertouchTarget() { return dexed->getAftertouchTarget(); }
    bool midiDataHandler(uint8_t midiChannel, uint8_t midiState, uint8_t midiData1, uint8_t midiData2) { return dexed->midiDataHandler(midiChannel, midiState, midiData1, midiData2); }
    bool midiDataHandler(uint8_t midiChannel, uint8_t* midiData, int16_t len) { return dexed->midiDataHandler(midiChannel, midiData, len); }
    void getSamples(int16_t* buffer, uint16_t n_samples) { dexed->getSamples(buffer, n_samples); }
    void getSamplesStereo(int16_t* left, int16_t* right, int n, const uint8_t* midiNotes);
    void getSamplesStereo(int16_t* left, int16_t* right, int n);
};
