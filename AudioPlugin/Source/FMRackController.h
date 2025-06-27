#pragma once

#include <juce_core/juce_core.h>
#include <memory>
#include <mutex>
#include "../src/FMRack/Rack.h"
#include "../src/FMRack/Performance.h"

// FMRackController mediates all access between the JUCE UI and the FMRack backend.
// It exposes high-level methods for UI interaction and ensures thread safety.
class FMRackController {
public:
    FMRackController(float sampleRate);
    ~FMRackController();

    // Performance management
    bool loadPerformanceFile(const juce::String& path);
    bool savePerformanceFile(const juce::String& path);
    void setDefaultPerformance();
    void setPerformance(const FMRack::Performance& perf);
    FMRack::Performance* getPerformance();

    // Module access
    int getNumModules() const;
    void setNumModules(int num);
    const std::vector<std::unique_ptr<FMRack::Module>>& getModules() const;
    FMRack::Module* getModule(int index);

    // Metering: get output levels for a module (returns average, sets l/r)
    float getModuleOutputLevels(int moduleIndex, float& l, float& r);
    // Metering: get both pre-gain and post-gain levels for a module
    void getModuleOutputLevelsExtended(int moduleIndex, float& l, float& r, float& lPre, float& rPre);

    // MIDI and audio
    void processMidiMessage(uint8_t status, uint8_t data1, uint8_t data2);
    void processAudio(float* leftOut, float* rightOut, int numSamples);

    // Effects
    void setReverbEnabled(bool enabled);
    void setReverbLevel(float level);
    void setReverbSize(float size);

    // Set a Dexed parameter (address, value)
    void setDexedParam(uint8_t address, uint8_t value);

    // Request a DX7 single voice dump from the given MIDI channel (1-16)
    void requestSingleVoiceDump(int midiChannel);
    // This function should be called by the MIDI/SysEx receive path when a single voice dump is received
    void onSingleVoiceDumpReceived(const std::vector<uint8_t>& data);

    // Voice data management
    void setPartVoiceData(int partIndex, const std::vector<uint8_t>& voiceData);

    // Thread safety
    std::mutex& getMutex();
    FMRack::Rack* getRack() const { return rack.get(); }

    // UI update callback
    std::function<void()> onModulesChanged;

private:
    std::unique_ptr<FMRack::Rack> rack;
    std::unique_ptr<FMRack::Performance> performance;
    mutable std::mutex mutex;
};
