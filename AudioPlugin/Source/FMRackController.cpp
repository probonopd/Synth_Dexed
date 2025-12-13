#include "FMRackController.h"
#include <iostream>
#include <atomic>
#include <fstream>
#include <cstring> // for std::memset

FMRackController::FMRackController(float sampleRate)
{
    // Initialize without locking - we're in the constructor so no other thread can access us
    rack = std::make_unique<FMRack::Rack>(sampleRate);
    performance = std::make_unique<FMRack::Performance>();
    // Only set defaults if this is the very first initialization and no user config exists
    bool allChannelsZero = true;
    for (int i = 0; i < 16; ++i) {
        if (performance->parts[i].midiChannel != 0) {
            allChannelsZero = false;
            break;
        }
    }
    if (allChannelsZero) {
        performance->setDefaults(8, 1);
    }
    std::cout << "[FMRackController] Constructor: calling setPerformance to ensure handlers are registered" << std::endl;
    // setPerformance now properly locks the mutex internally
    setPerformance(*performance);
}

FMRackController::~FMRackController() = default;

bool FMRackController::loadPerformanceFile(const juce::String& path)
{
    try {
        FMRack::Performance loadedPerformance;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!performance)
                performance = std::make_unique<FMRack::Performance>();
            juce::Logger::writeToLog("[FMRackController] Attempting to load performance file: " + path);
            std::cout << "[FMRackController] Attempting to load performance file: " << path << std::endl;
            // Extra logging: check file existence and permissions
            juce::File juceFile(path);
            juce::Logger::writeToLog("[FMRackController] File exists: " + juce::String(juceFile.exists() ? "yes" : "no") + ", readable: " + juce::String(juceFile.hasReadAccess() ? "yes" : "no"));
            std::cout << "[FMRackController] File exists: " << (juceFile.exists() ? "yes" : "no") << ", readable: " << (juceFile.hasReadAccess() ? "yes" : "no") << std::endl;
            // Log current working directory
            juce::File cwd = juce::File::getCurrentWorkingDirectory();
            juce::Logger::writeToLog("[FMRackController] Current working directory: " + cwd.getFullPathName());
            std::cout << "[FMRackController] Current working directory: " << cwd.getFullPathName() << std::endl;
            if (!performance->loadFromFile(path.toStdString())) {
                juce::Logger::writeToLog("[FMRackController] Performance loadFromFile failed: " + path);
                std::cout << "[FMRackController] Performance loadFromFile failed: " << path << std::endl;
                // Try to open the file directly for debug
                std::ifstream testFile(path.toStdString());
                if (!testFile.is_open()) {
                    juce::Logger::writeToLog("[FMRackController] DEBUG: Could not open file with std::ifstream: " + path);
                    std::cout << "[FMRackController] DEBUG: Could not open file with std::ifstream: " << path << std::endl;
                } else {
                    juce::Logger::writeToLog("[FMRackController] DEBUG: File opened with std::ifstream: " + path);
                    std::cout << "[FMRackController] DEBUG: File opened with std::ifstream: " << path << std::endl;
                }
                return false;
            }
            // Make a copy of the loaded performance to use outside the lock
            loadedPerformance = *performance;
        }
        // Call setPerformance outside the lock scope - it will acquire the lock internally
        juce::Logger::writeToLog("[FMRackController] loadPerformanceFile: calling setPerformance to ensure handlers are registered");
        std::cout << "[FMRackController] loadPerformanceFile: calling setPerformance to ensure handlers are registered" << std::endl;
        setPerformance(loadedPerformance);
        juce::Logger::writeToLog("[FMRackController] Performance loaded and rack configured from: " + path);
        std::cout << "[FMRackController] Performance loaded and rack configured from: " << path << std::endl;
        return true;
    } catch (const std::exception& e) {
        juce::Logger::writeToLog("[FMRackController] Exception in loadPerformanceFile: " + juce::String(e.what()));
        std::cout << "[FMRackController] Exception in loadPerformanceFile: " << e.what() << std::endl;
        return false;
    } catch (...) {
        juce::Logger::writeToLog("[FMRackController] Unknown exception in loadPerformanceFile");
        std::cout << "[FMRackController] Unknown exception in loadPerformanceFile" << std::endl;
        return false;
    }
}

bool FMRackController::savePerformanceFile(const juce::String& path)
{
    try {
        std::lock_guard<std::mutex> lock(mutex);
        if (!performance)
            return false;
        // --- Sync Dexed engine state back to performance before saving ---
        if (rack) {
            const auto& modules = rack->getModules();
            for (size_t i = 0; i < modules.size() && i < performance->parts.size(); ++i) {
                auto* module = modules[i].get();
                if (module) {
                    auto* dexed = module->getDexedEngine();
                    if (dexed) {
                        uint8_t data[155] = {0};
                        if (dexed->getVoiceData(data)) {
                            // Copy to performance part's voiceData (ensure correct size)
                            auto& partVoiceData = performance->parts[i].voiceData;
                            size_t sz = std::min<size_t>(partVoiceData.size(), 155);
                            std::copy_n(data, sz, partVoiceData.begin());
                            // Also copy the voice name (bytes 145-154) from Dexed engine (already updated by UI)
                            for (int n = 0; n < 10; ++n) partVoiceData[145 + n] = data[145 + n];
                        }
                    }
                }
            }
        }
        // --- End sync ---
        return performance->saveToFile(path.toStdString());
    } catch (const std::exception& e) {
        juce::Logger::writeToLog("[FMRackController] Exception in savePerformanceFile: " + juce::String(e.what()));
        return false;
    } catch (...) {
        juce::Logger::writeToLog("[FMRackController] Unknown exception in savePerformanceFile");
        return false;
    }
}

void FMRackController::setDefaultPerformance()
{
    try {
        FMRack::Performance defaultPerf;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!performance)
                performance = std::make_unique<FMRack::Performance>();
            // Only set defaults if this is a true reset
            if (performance->parts[0].midiChannel == 0) {
                performance->setDefaults(8, 1);
            }
            // Make a copy of the performance to use outside the lock
            defaultPerf = *performance;
        }
        std::cout << "[FMRackController] setDefaultPerformance: calling setPerformance to ensure handlers are registered" << std::endl;
        // Call setPerformance outside the lock scope - it will acquire the lock internally
        setPerformance(defaultPerf);
    } catch (const std::exception& e) {
        std::cout << "[FMRackController] Exception in setDefaultPerformance: " << e.what() << std::endl;
    } catch (...) {
        std::cout << "[FMRackController] Unknown exception in setDefaultPerformance" << std::endl;
    }
}

void FMRackController::setPerformance(const FMRack::Performance& newPerformance) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        *performance = newPerformance;
        if (rack) {
            rack->setPerformance(newPerformance);
        }
    }
    // Call UI callback outside the lock to avoid potential deadlocks
    if (onModulesChanged) {
        onModulesChanged();
    }
}

FMRack::Performance* FMRackController::getPerformance()
{
    // Note: This returns a raw pointer after releasing the lock - modifications
    // should go through setPerformance() for thread safety
    std::lock_guard<std::mutex> lock(mutex);
    return performance.get();
}

int FMRackController::getNumModules() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return rack ? rack->getNumModules() : 0;
}

const std::vector<std::unique_ptr<FMRack::Module>>& FMRackController::getModules() const
{
    // Note: This returns a reference after releasing the lock - caller must use getMutex() 
    // to hold the lock while accessing the returned reference
    std::lock_guard<std::mutex> lock(mutex);
    static std::vector<std::unique_ptr<FMRack::Module>> empty;
    if (!rack) return empty;
    return rack->getModules();
}

FMRack::Module* FMRackController::getModule(int index)
{
    // Note: This returns a raw pointer after releasing the lock - caller must use getMutex()
    // to hold the lock while using the returned pointer
    std::lock_guard<std::mutex> lock(mutex);
    if (!rack) return nullptr;
    const auto& modules = rack->getModules();
    if (index < 0 || index >= (int)modules.size()) return nullptr;
    return modules[index].get();
}

void FMRackController::processMidiMessage(uint8_t status, uint8_t data1, uint8_t data2)
{
    // No try-catch in real-time audio path - exceptions shouldn't happen here
    // and catching them adds overhead and can cause priority inversion
    std::lock_guard<std::mutex> lock(mutex);
    if (rack)
        rack->processMidiMessage(status, data1, data2);
}

void FMRackController::processAudio(float* leftOut, float* rightOut, int numSamples)
{
    // No try-catch in real-time audio path - exceptions shouldn't happen here
    std::lock_guard<std::mutex> lock(mutex);
    if (rack) {
        rack->processAudio(leftOut, rightOut, numSamples);
    } else {
        // Clear output if no rack
        if (leftOut) std::memset(leftOut, 0, numSamples * sizeof(float));
        if (rightOut) std::memset(rightOut, 0, numSamples * sizeof(float));
    }
}

void FMRackController::setReverbEnabled(bool enabled)
{
    try {
        FMRack::Performance perfCopy;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (rack && performance) {
                performance->effects.reverbEnable = enabled;
                perfCopy = *performance;
            } else {
                return;
            }
        }
        // Call setPerformance outside the lock scope - it will acquire the lock internally
        std::cout << "[FMRackController] setReverbEnabled: calling setPerformance to ensure handlers are registered" << std::endl;
        setPerformance(perfCopy);
    } catch (const std::exception& e) {
        std::cout << "[FMRackController] Exception in setReverbEnabled: " << e.what() << std::endl;
    } catch (...) {
        std::cout << "[FMRackController] Unknown exception in setReverbEnabled" << std::endl;
    }
}

void FMRackController::setReverbLevel(float level)
{
    try {
        FMRack::Performance perfCopy;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (rack && performance) {
                performance->effects.reverbLevel = static_cast<uint8_t>(level * 127.0f);
                perfCopy = *performance;
            } else {
                return;
            }
        }
        // Call setPerformance outside the lock scope - it will acquire the lock internally
        std::cout << "[FMRackController] setReverbLevel: calling setPerformance to ensure handlers are registered" << std::endl;
        setPerformance(perfCopy);
    } catch (const std::exception& e) {
        std::cout << "[FMRackController] Exception in setReverbLevel: " << e.what() << std::endl;
    } catch (...) {
        std::cout << "[FMRackController] Unknown exception in setReverbLevel" << std::endl;
    }
}

void FMRackController::setReverbSize(float size)
{
    FMRack::Performance perfCopy;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (rack && performance) {
            performance->effects.reverbSize = static_cast<uint8_t>(size * 127.0f);
            perfCopy = *performance;
        } else {
            return;
        }
    }
    // Call setPerformance outside the lock scope - it will acquire the lock internally
    std::cout << "[FMRackController] setReverbSize: calling setPerformance to ensure handlers are registered" << std::endl;
    setPerformance(perfCopy);
}

void FMRackController::setNumModules(int num)
{
    FMRack::Performance perfCopy;
    bool hasPerformance = false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (performance) {
            // Find the MIDI channel of the last active part to use for new parts
            uint8_t lastActiveChannel = 1; // Default to 1
            for (int j = 0; j < 16; ++j) {
                if (performance->parts[j].midiChannel != 0) {
                    lastActiveChannel = performance->parts[j].midiChannel;
                }
            }
            for (int i = 0; i < 16; ++i) {
                performance->parts[i].midiChannel = (i < num) ? lastActiveChannel : 0;
            }
            perfCopy = *performance;
            hasPerformance = true;
        }
    }
    
    // Call setPerformance outside the lock scope - it will acquire the lock internally
    if (hasPerformance) {
        setPerformance(perfCopy);
    }
    
    // Call UI callback outside any lock
    if (onModulesChanged) {
        onModulesChanged();
    }
}

void FMRackController::setDexedParam(uint8_t address, uint8_t value)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (!rack) return;
    const auto& modules = rack->getModules();
    if (modules.empty()) return;
    auto* dexed = modules[0]->getDexedEngine();
    if (!dexed) return;
    dexed->setVoiceDataElement(address, value);
    dexed->doRefreshVoice();
}

uint8_t FMRackController::getDexedParamForModule(int moduleIndex, uint8_t address) const
{
    std::lock_guard<std::mutex> lock(mutex);
    if (!rack) return 0;
    const auto& modules = rack->getModules();
    if (moduleIndex < 0 || moduleIndex >= static_cast<int>(modules.size())) return 0;
    auto* dexed = modules[moduleIndex]->getDexedEngine();
    if (!dexed) return 0;
    return dexed->getVoiceDataElement(address);
}

void FMRackController::setDexedParamForModule(int moduleIndex, uint8_t address, uint8_t value)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (!rack) return;
    const auto& modules = rack->getModules();
    if (moduleIndex < 0 || moduleIndex >= static_cast<int>(modules.size())) return;
    auto* dexed = modules[moduleIndex]->getDexedEngine();
    if (!dexed) return;
    dexed->setVoiceDataElement(address, value);
    dexed->doRefreshVoice();
}

juce::String FMRackController::getVoiceNameForModule(int moduleIndex) const
{
    std::lock_guard<std::mutex> lock(mutex);
    if (!rack) return {};
    const auto& modules = rack->getModules();
    if (moduleIndex < 0 || moduleIndex >= static_cast<int>(modules.size())) return {};
    auto* dexed = modules[moduleIndex]->getDexedEngine();
    if (!dexed) return {};
    
    // Voice name is at bytes 145-154 (10 bytes)
    char nameBytes[11] = {0};
    for (int i = 0; i < 10; ++i) {
        nameBytes[i] = static_cast<char>(dexed->getVoiceDataElement(static_cast<uint8_t>(145 + i)));
    }
    return juce::String(nameBytes).trim();
}

void FMRackController::setVoiceNameForModule(int moduleIndex, const juce::String& name)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (!rack) return;
    const auto& modules = rack->getModules();
    if (moduleIndex < 0 || moduleIndex >= static_cast<int>(modules.size())) return;
    auto* dexed = modules[moduleIndex]->getDexedEngine();
    if (!dexed) return;
    
    // Pad name to 10 chars with spaces
    juce::String paddedName = name.substring(0, 10);
    while (paddedName.length() < 10) paddedName += " ";
    
    for (int i = 0; i < 10; ++i) {
        dexed->setVoiceDataElement(static_cast<uint8_t>(145 + i), 
                                   static_cast<uint8_t>(static_cast<unsigned char>(paddedName[i])));
    }
}

Dexed* FMRackController::getDexedEngineForModule(int moduleIndex)
{
    // Note: Caller MUST hold getMutex() lock while using the returned pointer!
    // This is inherently unsafe but provided for compatibility with existing code.
    if (!rack) return nullptr;
    const auto& modules = rack->getModules();
    if (moduleIndex < 0 || moduleIndex >= static_cast<int>(modules.size())) return nullptr;
    return modules[moduleIndex]->getDexedEngine();
}

void FMRackController::requestSingleVoiceDump(int midiChannel) {
    std::cout << "[FMRackController] requestSingleVoiceDump(" << midiChannel << ") called" << std::endl;
    uint8_t n = static_cast<uint8_t>((midiChannel - 1) & 0x0F);
    std::vector<uint8_t> sysex = {0xF0, 0x43, static_cast<uint8_t>(0x20 | n), 0x00, 0x7F, 0xF7};
    std::cout << "[FMRackController] requestSingleVoiceDump: constructed sysex: ";
    for (auto b : sysex) std::cout << std::hex << (int)b << " ";
    std::cout << std::dec << std::endl;
    std::lock_guard<std::mutex> lock(mutex);
    if (rack) {
        std::cout << "[FMRackController] requestSingleVoiceDump: calling rack->routeSysexToModules with sysex.size()=" << sysex.size() << ", midiChannel=" << midiChannel << std::endl;
        rack->routeSysexToModules(sysex.data(), static_cast<int>(sysex.size()), static_cast<uint8_t>(midiChannel));
    } else {
        std::cout << "[FMRackController] requestSingleVoiceDump: rack is nullptr" << std::endl;
    }
}

void FMRackController::onSingleVoiceDumpReceived(const uint8_t* data, int len) {
    // NOTE: All std::cout logging removed - this function is called from audio thread
    // Handle incoming MIDI/SysEx here
    (void)data;
    (void)len;
}

void FMRackController::setPartVoiceData(int partIndex, const std::vector<uint8_t>& voiceData) {
    try {
        std::lock_guard<std::mutex> lock(mutex);
        if (performance && partIndex >= 0 && partIndex < 16) {
            performance->setPartVoiceData(partIndex, voiceData);
            // Update the rack with the new performance
            if (rack) {
                rack->setPerformance(*performance);
            }
            juce::Logger::writeToLog("[FMRackController] Voice data set for part " + juce::String(partIndex));
        }
    } catch (const std::exception& e) {
        juce::Logger::writeToLog("[FMRackController] Exception in setPartVoiceData: " + juce::String(e.what()));
    } catch (...) {
        juce::Logger::writeToLog("[FMRackController] Unknown exception in setPartVoiceData");
    }
}

float FMRackController::getModuleOutputLevels(int moduleIndex, float& l, float& r)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (!rack) return 0.0f;
    const auto& modules = rack->getModules();
    if (moduleIndex < 0 || moduleIndex >= (int)modules.size()) return 0.0f;
    modules[moduleIndex]->getOutputLevels(l, r);
    return (l + r) * 0.5f;
}

void FMRackController::getModuleOutputLevelsExtended(int moduleIndex, float& l, float& r, float& lPre, float& rPre)
{
    std::lock_guard<std::mutex> lock(mutex);
    l = r = lPre = rPre = 0.0f;
    if (!rack) return;
    const auto& modules = rack->getModules();
    if (moduleIndex < 0 || moduleIndex >= (int)modules.size()) return;
    modules[moduleIndex]->getOutputLevels(l, r, lPre, rPre);
}

std::mutex& FMRackController::getMutex() { return mutex; }
