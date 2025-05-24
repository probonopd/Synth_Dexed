#include "Module.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace FMRack {

Module::Module(float sampleRate) 
    : sampleRate_(sampleRate), midiChannel_(0), enabled_(false), volume_(1.0f), pan_(0.5f),
      detune_(0), noteShift_(0), noteLimitLow_(0), noteLimitHigh_(127), reverbSend_(0.0f),
      monoMode_(false) {
    
    tempBuffer_.resize(1024); // Buffer for Dexed output conversion
}

void Module::configureFromPerformance(const Performance::PartConfig& config) {
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
        engine->activate();
        fmEngines_.push_back(std::move(engine));
        
        // Calculate detune and pan for this voice
        if (voices == 1) {
            unisonDetune_.push_back(0.0f);
            unisonPan_.push_back(0.5f);
        } else {
            float voiceDetune = detune * (i - (voices - 1) * 0.5f);
            float voicePan = 0.5f + spread * (i / float(voices - 1) - 0.5f);
            unisonDetune_.push_back(voiceDetune);
            unisonPan_.push_back(std::clamp(voicePan, 0.0f, 1.0f));
        }
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
    uint8_t msgType = status & 0xF0;
    switch (msgType) {
        case 0x80: // Note Off
        case 0x90: // Note On
        {
            uint8_t note = data1;
            uint8_t velocity = data2;
            if (msgType == 0x90 && velocity == 0) msgType = 0x80;
            if (!isNoteInRange(note)) return;
            applyNoteShift(note);
            for (auto& engine : fmEngines_) {
                if (msgType == 0x90) {
                    std::cout << "[DEBUG] keydown(" << (int)note << ", " << (int)velocity << ")\n";
                    engine->keydown(note, velocity);
                } else {
                    std::cout << "[DEBUG] keyup(" << (int)note << ")\n";
                    engine->keyup(note);
                }
            }
            break;
        }
        case 0xB0: // Control Change
        {
            for (auto& engine : fmEngines_) {
                switch (data1) {
                    case 1:
                        std::cout << "[DEBUG] setModWheel(" << (int)data2 << ")\n";
                        engine->setModWheel(data2);
                        break;
                    case 2:
                        std::cout << "[DEBUG] setBreathController(" << (int)data2 << ")\n";
                        engine->setBreathController(data2);
                        break;
                    case 4:
                        std::cout << "[DEBUG] setFootController(" << (int)data2 << ")\n";
                        engine->setFootController(data2);
                        break;
                    case 7:
                        engine->setGain(data2 / 127.0f);
                        break;
                    case 11:
                        engine->setGain(data2 / 127.0f);
                        break;
                    case 64:
                        std::cout << "[DEBUG] setSustain(" << (data2 >= 64) << ")\n";
                        engine->setSustain(data2 >= 64);
                        break;
                    case 66:
                        std::cout << "[DEBUG] setSostenuto(" << (data2 >= 64) << ")\n";
                        engine->setSostenuto(data2 >= 64);
                        break;
                    case 123:
                        std::cout << "[DEBUG] notesOff()\n";
                        engine->notesOff();
                        break;
                    default:
                        std::cout << "[DEBUG] Unhandled CC: " << (int)data1 << " value: " << (int)data2 << std::endl;
                        break;
                }
            }
            break;
        }
        case 0xE0: // Pitch Bend
        {
            uint16_t pitchBend = (data2 << 7) | data1;
            for (auto& engine : fmEngines_) {
                std::cout << "[DEBUG] setPitchbend(" << pitchBend << ")\n";
                engine->setPitchbend(pitchBend);
            }
            break;
        }
        case 0xD0: // Channel Aftertouch
        {
            for (auto& engine : fmEngines_) {
                std::cout << "[DEBUG] setAftertouch(" << (int)data1 << ")\n";
                engine->setAftertouch(data1);
            }
            break;
        }
        default:
            std::cout << "[DEBUG] Unhandled MIDI message: status=0x" << std::hex << (int)status << std::dec << ", data1=" << (int)data1 << ", data2=" << (int)data2 << std::endl;
            break;
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
        const float scale = 1.0f / 32768.0f; // Convert int16_t to float
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
    // Forward SysEx to all FM engines (Dexed instances)
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
