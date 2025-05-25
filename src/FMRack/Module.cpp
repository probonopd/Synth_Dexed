#include "Module.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstring>

// Debug macro/flag for debug output (no main.h include needed)
extern bool debugEnabled;
#define DEBUG_PRINT(x) do { if (debugEnabled) { std::cout << x << std::endl; } } while(0)

namespace FMRack {

Module::Module(float sampleRate) 
    : sampleRate_(sampleRate), midiChannel_(0), enabled_(false), volume_(1.0f), pan_(0.5f),
      detune_(0), noteShift_(0), noteLimitLow_(0), noteLimitHigh_(127), reverbSend_(0.0f),
      monoMode_(false) {
    
    tempBuffer_.resize(1024); // Buffer for Dexed output conversion
}

void Module::configureFromPerformance(const Performance::PartConfig& config) {
    partConfig_ = const_cast<Performance::PartConfig*>(&config);
    midiChannel_ = config.midiChannel;
    enabled_ = (midiChannel_ > 0);
    
    if (!enabled_) return;

    std::cout << "    Configuring module parameters...\n";
    
    // Set module parameters
    volume_ = config.volume / 127.0f;
    pan_ = config.pan / 127.0f;
    detune_ = config.detune;
    noteShift_ = config.noteShift;
    noteLimitLow_ = config.noteLimitLow;
    noteLimitHigh_ = config.noteLimitHigh;
    reverbSend_ = config.reverbSend / 127.0f;
    monoMode_ = (config.monoMode != 0);
    
    // Controller assignments
    modulationWheelRange_ = config.modulationWheelRange;
    modulationWheelTarget_ = config.modulationWheelTarget;
    footControlRange_ = config.footControlRange;
    footControlTarget_ = config.footControlTarget;
    breathControlRange_ = config.breathControlRange;
    breathControlTarget_ = config.breathControlTarget;
    aftertouchRange_ = config.aftertouchRange;
    aftertouchTarget_ = config.aftertouchTarget;
    
    // Portamento settings
    portamentoMode_ = config.portamentoMode;
    portamentoGlissando_ = config.portamentoGlissando;
    portamentoTime_ = config.portamentoTime;
    
    // Set up unison
    std::cout << "    Setting up unison configuration...\n";
    setupUnison(config.unisonVoices, config.unisonDetune, config.unisonSpread);
    
    // Configure each FM engine
    std::cout << "    Loading voice data into " << fmEngines_.size() << " FM engine(s)...\n";
    for (size_t i = 0; i < fmEngines_.size(); ++i) {
        auto& engine = fmEngines_[i];
        engine->loadVoiceParameters(const_cast<uint8_t*>(config.voiceData.data()));
        engine->setMonoMode(monoMode_);
        engine->setPortamento(portamentoMode_, portamentoGlissando_, portamentoTime_);
        
        if (fmEngines_.size() > 1) {
            std::cout << "      Engine " << (i + 1) << ": detune " << unisonDetune_[i] 
                      << " cents, pan " << (unisonPan_[i] * 100.0f) << "%\n";
        }
    }
    
    std::cout << "    Module configuration complete.\n";
}

void Module::setupUnison(uint8_t voices, float detune, float spread) {
    voices = std::clamp(voices, static_cast<uint8_t>(1), static_cast<uint8_t>(4));
    
    // Clear existing engines
    if (!fmEngines_.empty()) {
        std::cout << "      Removing " << fmEngines_.size() << " existing FM engine(s)\n";
    }
    fmEngines_.clear();
    unisonDetune_.clear();
    unisonPan_.clear();
    
    std::cout << "      Creating " << static_cast<int>(voices) << " FM engine(s)\n";
    
    // Create engines with detuning and panning
    for (uint8_t i = 0; i < voices; ++i) {
        auto engine = std::make_unique<Dexed>(16, static_cast<uint16_t>(sampleRate_));
        
        // Calculate detune and pan for this voice
        float voiceDetune = 0.0f;
        float voicePan = 0.5f;
        if (voices == 1) {
            voiceDetune = 0.0f;
            voicePan = 0.5f;
        } else {
            voiceDetune = detune * (i - (voices - 1) * 0.5f);
            voicePan = 0.5f + spread * (i / float(voices - 1) - 0.5f);
            voicePan = std::clamp(voicePan, 0.0f, 1.0f);
        }
        // Apply detune to Dexed engine (in cents)
        engine->setMasterTune(static_cast<int8_t>(voiceDetune));
        fmEngines_.push_back(std::move(engine));
        unisonDetune_.push_back(voiceDetune);
        unisonPan_.push_back(voicePan);
    }
}

void Module::setMIDIChannel(uint8_t channel) { midiChannel_ = channel; }
uint8_t Module::getMIDIChannel() const { return midiChannel_; }

bool Module::isNoteInRange(uint8_t note) const {
    return (note >= noteLimitLow_) && (note <= noteLimitHigh_);
}

void Module::applyNoteShift(uint8_t& note) const {
    int shiftedNote = static_cast<int>(note) + noteShift_;
    note = static_cast<uint8_t>(std::clamp(shiftedNote, 0, 127));
}

void Module::processMidiMessage(uint8_t status, uint8_t data1, uint8_t data2) {
    if (!enabled_) return;
    DEBUG_PRINT("[DEBUG] Module::processMidiMessage: status=0x" << std::hex << (int)status << std::dec << ", data1=" << (int)data1 << ", data2=" << (int)data2);
    uint8_t midiData[3] = {status, data1, data2};
    uint8_t msgChannel = (status & 0x0F) + 1; // 1-based channel for Dexed
    for (auto& engine : fmEngines_) {
        DEBUG_PRINT("[DEBUG] midiDataHandler: channel=" << (int)msgChannel << ", status=0x" << std::hex << (int)status << std::dec << ", data1=" << (int)data1 << ", data2=" << (int)data2);
        engine->midiDataHandler(msgChannel, midiData, 3);
    }
}

void Module::processAudio(float* leftOut, float* rightOut, float* reverbSendLeft, float* reverbSendRight, int numSamples) {
    if (!enabled_ || fmEngines_.empty()) {
        std::fill(leftOut, leftOut + numSamples, 0.0f);
        std::fill(rightOut, rightOut + numSamples, 0.0f);
        std::fill(reverbSendLeft, reverbSendLeft + numSamples, 0.0f);
        std::fill(reverbSendRight, reverbSendRight + numSamples, 0.0f);
        return;
    }
    
    // Clear output buffers
    std::fill(leftOut, leftOut + numSamples, 0.0f);
    std::fill(rightOut, rightOut + numSamples, 0.0f);
    
    // Ensure temp buffer is large enough
    if (tempBuffer_.size() < static_cast<size_t>(numSamples)) {
        tempBuffer_.resize(numSamples);
    }
    
    // Process each unison voice
    for (size_t voice = 0; voice < fmEngines_.size(); ++voice) {
        // Get mono samples from Dexed (int16_t format)
        fmEngines_[voice]->getSamples(tempBuffer_.data(), numSamples);
        
        // Apply voice-specific detune/pan
        float voicePan = unisonPan_[voice];
        float leftGain = std::sqrt(1.0f - voicePan);
        float rightGain = std::sqrt(voicePan);
        
        // Convert int16_t to float and apply panning
        const float scale = 1.0f / 3000.0f; // Convert int16_t to float; FIXME: 3000 is a rough scale factor for Dexed output
        for (int i = 0; i < numSamples; ++i) {
            float sample = tempBuffer_[i] * scale;
            leftOut[i] += sample * leftGain;
            rightOut[i] += sample * rightGain;
        }
    }
    
    // Apply module-level volume and panning
    float moduleLeftGain = std::sqrt(1.0f - pan_) * volume_;
    float moduleRightGain = std::sqrt(pan_) * volume_;
    
    for (int i = 0; i < numSamples; ++i) {
        float left = leftOut[i] * moduleLeftGain;
        float right = rightOut[i] * moduleRightGain;
        
        leftOut[i] = left;
        rightOut[i] = right;
        
        // Calculate reverb send
        reverbSendLeft[i] = left * reverbSend_;
        reverbSendRight[i] = right * reverbSend_;
    }
}

void Module::processSysex(const uint8_t* data, int len) {
    // Only handle Yamaha/Dexed engine-specific SysEx here.
    // All MiniDexed performance parameter SysEx is now handled in Performance.
    // Yamaha SysEx: F0 43 1n 01 1B vv F7 (operator ON/OFF mask)
    if (len == 7 && data[0] == 0xF0 && data[1] == 0x43 && data[3] == 0x01 && data[4] == 0x1B && data[6] == 0xF7) {
        uint8_t opMask = data[5];
        std::cout << "[SYSEX] Operator ON/OFF mask received: 0x" << std::hex << (int)opMask << std::dec << std::endl;
        for (auto& engine : fmEngines_) {
            engine->setOPAll(opMask);
        }
        return;
    }
    // Forward all other SysEx to all FM engines (Dexed instances)
    for (auto& engine : fmEngines_) {
        if (engine) {
            engine->midiDataHandler(midiChannel_, const_cast<uint8_t*>(data), len);
        }
    }
}

bool Module::isActive() const {
    if (!enabled_) return false;
    
    for (const auto& engine : fmEngines_) {
        if (engine->getNumNotesPlaying() > 0) {
            return true;
        }
    }
    return false;
}

void Module::panic() {
    for (auto& engine : fmEngines_) {
        engine->panic();
    }
}

} // namespace FMRack
