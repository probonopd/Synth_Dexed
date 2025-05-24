#include "FMEngine.h"
#include <cmath>
#include <algorithm>

namespace FMRack {

FMEngine::FMEngine(float sampleRate) 
    : sampleRate(sampleRate), noteLimitLow(0), noteLimitHigh(127),
      detuneCents(0.0f), panPosition(0.0f), monoMode(false) {
    
    dexed = std::make_unique<Dexed>();
    dexed->setSampleRate(sampleRate);
}

FMEngine::~FMEngine() = default;

void FMEngine::loadPatch(const std::vector<uint8_t>& patchData) {
    if (patchData.size() >= 155) {
        dexed->loadSysexPatch(const_cast<uint8_t*>(patchData.data()));
    }
}

void FMEngine::setNoteRange(int lowNote, int highNote) {
    noteLimitLow = std::clamp(lowNote, 0, 127);
    noteLimitHigh = std::clamp(highNote, 0, 127);
}

void FMEngine::setMonoMode(bool mono) {
    monoMode = mono;
    // Configure Dexed for mono/poly mode
}

void FMEngine::setDetune(float cents) {
    detuneCents = cents;
}

void FMEngine::setPan(float pan) {
    panPosition = std::clamp(pan, -1.0f, 1.0f);
}

void FMEngine::noteOn(int note, int velocity) {
    if (isNoteInRange(note)) {
        dexed->keydown(note, velocity);
    }
}

void FMEngine::noteOff(int note) {
    if (isNoteInRange(note)) {
        dexed->keyup(note);
    }
}

void FMEngine::pitchBend(int value) {
    // Convert 14-bit pitch bend to Dexed format
    dexed->setPitchbend(value);
}

void FMEngine::controlChange(int controller, int value) {
    // Build a MIDI Control Change message: 0xB0 | channel, controller, value
    uint8_t msg[3] = { 0xB0, static_cast<uint8_t>(controller), static_cast<uint8_t>(value) };
    dexed->midiDataHandler(1, msg, 3);
}

void FMEngine::allNotesOff() {
    dexed->panic();
}

void FMEngine::processAudio(float* leftOut, float* rightOut, int numSamples) {
    // Get stereo audio from Dexed
    dexed->getSamples(numSamples, leftOut, rightOut);
    
    // Apply detune and pan
    applyDetuneAndPan(leftOut, rightOut, numSamples);
}

bool FMEngine::isNoteInRange(int note) const {
    return note >= noteLimitLow && note <= noteLimitHigh;
}

void FMEngine::applyDetuneAndPan(float* leftOut, float* rightOut, int numSamples) {
    // Apply detune (would need pitch shifting - simplified here)
    // Apply pan
    float leftGain = (1.0f - panPosition) * 0.5f + 0.5f;
    float rightGain = (1.0f + panPosition) * 0.5f + 0.5f;
    
    for (int i = 0; i < numSamples; ++i) {
        float mono = (leftOut[i] + rightOut[i]) * 0.5f;
        leftOut[i] = mono * leftGain;
        rightOut[i] = mono * rightGain;
    }
}

} // namespace FMRack
