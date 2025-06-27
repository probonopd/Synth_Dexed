#include "FMRackController.h"
#include <iostream>
#include <atomic>
#include <fstream>

FMRackController::FMRackController(float sampleRate)
{
    std::lock_guard<std::mutex> lock(mutex);
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
    // Temporarily unlock mutex since setPerformance will try to acquire it
    mutex.unlock();
    setPerformance(*performance);
    mutex.lock();
}

FMRackController::~FMRackController() = default;

bool FMRackController::loadPerformanceFile(const juce::String& path)
{
    try {
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
        // Don't call rack->setPerformance directly, use our method to ensure handlers are registered
        juce::Logger::writeToLog("[FMRackController] loadPerformanceFile: calling setPerformance to ensure handlers are registered");
        std::cout << "[FMRackController] loadPerformanceFile: calling setPerformance to ensure handlers are registered" << std::endl;
        // Temporarily unlock mutex since setPerformance will try to acquire it
        mutex.unlock();
        setPerformance(*performance);
        mutex.lock();
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
        std::lock_guard<std::mutex> lock(mutex);
        if (!performance)
            performance = std::make_unique<FMRack::Performance>();
        // Only set defaults if this is a true reset
        if (performance->parts[0].midiChannel == 0) {
            performance->setDefaults(8, 1);
        }
        std::cout << "[FMRackController] setDefaultPerformance: calling setPerformance to ensure handlers are registered" << std::endl;
        // Temporarily unlock mutex since setPerformance will try to acquire it
        mutex.unlock();
        setPerformance(*performance);
        mutex.lock();
    } catch (const std::exception& e) {
        std::cout << "[FMRackController] Exception in setDefaultPerformance: " << e.what() << std::endl;
    } catch (...) {
        std::cout << "[FMRackController] Unknown exception in setDefaultPerformance" << std::endl;
    }
}

void FMRackController::setPerformance(const FMRack::Performance& perf)
{
    try {
        std::lock_guard<std::mutex> lock(mutex);
        // Only update the performance, do not change MIDI channels or module count here!
        *performance = perf;
        std::cout << "[FMRackController::setPerformance] About to call rack->setPerformance(*performance)" << std::endl;
        rack->setPerformance(*performance);
        std::cout << "[FMRackController::setPerformance] rack->setPerformance(*performance) completed" << std::endl;
        // Register single voice dump handler for each module
        auto& modules = rack->getModules();
        std::cout << "[FMRackController::setPerformance] modules.size()=" << modules.size() << std::endl;
        for (size_t i = 0; i < modules.size(); ++i) {
            auto& module = modules[i];
            std::cout << "[FMRackController::setPerformance] Registering handler for module " << i << ", module.get()=" << module.get() << std::endl;
            if (module) {
                std::cout << "[FMRackController::setPerformance] Handler registered for module " << i << std::endl;
            } else {
                std::cout << "[FMRackController::setPerformance] module " << i << " is nullptr" << std::endl;
            }
        }
        if (onModulesChanged) onModulesChanged();
    } catch (const std::exception& e) {
        std::cout << "[FMRackController] Exception in setPerformance: " << e.what() << std::endl;
    } catch (...) {
        std::cout << "[FMRackController] Unknown exception in setPerformance" << std::endl;
    }
}

FMRack::Performance* FMRackController::getPerformance()
{
    try {
        std::lock_guard<std::mutex> lock(mutex);
        return performance.get();
    } catch (const std::exception& e) {
        std::cout << "[FMRackController] Exception in getPerformance: " << e.what() << std::endl;
        return nullptr;
    } catch (...) {
        std::cout << "[FMRackController] Unknown exception in getPerformance" << std::endl;
        return nullptr;
    }
}

int FMRackController::getNumModules() const
{
    try {
        std::lock_guard<std::mutex> lock(mutex);
        return rack ? rack->getNumModules() : 0;
    } catch (const std::exception& e) {
        std::cout << "[FMRackController] Exception in getNumModules: " << e.what() << std::endl;
        return 0;
    } catch (...) {
        std::cout << "[FMRackController] Unknown exception in getNumModules" << std::endl;
        return 0;
    }
}

const std::vector<std::unique_ptr<FMRack::Module>>& FMRackController::getModules() const
{
    try {
        std::lock_guard<std::mutex> lock(mutex);
        return rack->getModules();
    } catch (const std::exception& e) {
        std::cout << "[FMRackController] Exception in getModules: " << e.what() << std::endl;
        static std::vector<std::unique_ptr<FMRack::Module>> empty;
        return empty;
    } catch (...) {
        std::cout << "[FMRackController] Unknown exception in getModules" << std::endl;
        static std::vector<std::unique_ptr<FMRack::Module>> empty;
        return empty;
    }
}

FMRack::Module* FMRackController::getModule(int index)
{
    try {
        std::lock_guard<std::mutex> lock(mutex);
        if (!rack) return nullptr;
        const auto& modules = rack->getModules();
        if (index < 0 || index >= (int)modules.size()) return nullptr;
        return modules[index].get();
    } catch (const std::exception& e) {
        std::cout << "[FMRackController] Exception in getModule: " << e.what() << std::endl;
        return nullptr;
    } catch (...) {
        std::cout << "[FMRackController] Unknown exception in getModule" << std::endl;
        return nullptr;
    }
}

void FMRackController::processMidiMessage(uint8_t status, uint8_t data1, uint8_t data2)
{
    try {
        std::lock_guard<std::mutex> lock(mutex);
        if (rack)
            rack->processMidiMessage(status, data1, data2);
    } catch (const std::exception& e) {
        std::cout << "[FMRackController] Exception in processMidiMessage: " << e.what() << std::endl;
    } catch (...) {
        std::cout << "[FMRackController] Unknown exception in processMidiMessage" << std::endl;
    }
}

void FMRackController::processAudio(float* leftOut, float* rightOut, int numSamples)
{
    try {
        std::lock_guard<std::mutex> lock(mutex);
        if (rack)
            rack->processAudio(leftOut, rightOut, numSamples);
    } catch (const std::exception& e) {
        std::cout << "[FMRackController] Exception in processAudio: " << e.what() << std::endl;
        if (leftOut) std::fill(leftOut, leftOut + numSamples, 0.0f);
        if (rightOut) std::fill(rightOut, rightOut + numSamples, 0.0f);
    } catch (...) {
        std::cout << "[FMRackController] Unknown exception in processAudio" << std::endl;
        if (leftOut) std::fill(leftOut, leftOut + numSamples, 0.0f);
        if (rightOut) std::fill(rightOut, rightOut + numSamples, 0.0f);
    }
}

void FMRackController::setReverbEnabled(bool enabled)
{
    try {
        std::lock_guard<std::mutex> lock(mutex);
        if (rack && performance) {
            performance->effects.reverbEnable = enabled;
            // Don't call rack->setPerformance directly, use our method to ensure handlers are registered
            std::cout << "[FMRackController] setReverbEnabled: calling setPerformance to ensure handlers are registered" << std::endl;
            // Temporarily unlock mutex since setPerformance will try to acquire it
            mutex.unlock();
            setPerformance(*performance);
            mutex.lock();
        }
    } catch (const std::exception& e) {
        std::cout << "[FMRackController] Exception in setReverbEnabled: " << e.what() << std::endl;
    } catch (...) {
        std::cout << "[FMRackController] Unknown exception in setReverbEnabled" << std::endl;
    }
}

void FMRackController::setReverbLevel(float level)
{
    try {
        std::lock_guard<std::mutex> lock(mutex);
        if (rack && performance) {
            performance->effects.reverbLevel = static_cast<uint8_t>(level * 127.0f);
            // Don't call rack->setPerformance directly, use our method to ensure handlers are registered
            std::cout << "[FMRackController] setReverbLevel: calling setPerformance to ensure handlers are registered" << std::endl;
            // Temporarily unlock mutex since setPerformance will try to acquire it
            mutex.unlock();
            setPerformance(*performance);
            mutex.lock();
        }
    } catch (const std::exception& e) {
        std::cout << "[FMRackController] Exception in setReverbLevel: " << e.what() << std::endl;
    } catch (...) {
        std::cout << "[FMRackController] Unknown exception in setReverbLevel" << std::endl;
    }
}

void FMRackController::setReverbSize(float size)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (rack && performance) {
        performance->effects.reverbSize = static_cast<uint8_t>(size * 127.0f);
        // Don't call rack->setPerformance directly, use our method to ensure handlers are registered
        std::cout << "[FMRackController] setReverbSize: calling setPerformance to ensure handlers are registered" << std::endl;
        // Temporarily unlock mutex since setPerformance will try to acquire it
        mutex.unlock();
        setPerformance(*performance);
        mutex.lock();
    }
}

void FMRackController::setNumModules(int num)
{
    std::cout << "[FMRackController] setNumModules(" << num << ") called" << std::endl;
    if (performance) {
        std::cout << "[FMRackController] setNumModules: performance exists, updating MIDI channels" << std::endl;
        for (int i = 0; i < 16; ++i) {
            performance->parts[i].midiChannel = (i < num) ? (i + 1) : 0;
        }
        // Debug: print all MIDI channels after setting
        std::cout << "[FMRackController] setNumModules: MIDI channels after update: ";
        for (int i = 0; i < 16; ++i) {
            std::cout << performance->parts[i].midiChannel << " ";
        }
        std::cout << std::endl;
        // Use our own setPerformance method which properly registers handlers
        std::cout << "[FMRackController] setNumModules: calling FMRackController::setPerformance to ensure handlers are registered" << std::endl;
        setPerformance(*performance);
    } else {
        std::cout << "[FMRackController] setNumModules: performance is nullptr" << std::endl;
    }
    if (onModulesChanged) {
        std::cout << "[FMRackController] setNumModules: calling onModulesChanged()" << std::endl;
        onModulesChanged();
    }
}

void FMRackController::setDexedParam(uint8_t address, uint8_t value)
{
    std::cout << "[FMRackController] setDexedParam(" << static_cast<int>(address) << ", " << static_cast<int>(value) << ") called" << std::endl;
    std::lock_guard<std::mutex> lock(mutex);
    if (!rack) { std::cout << "[FMRackController] setDexedParam: rack is nullptr" << std::endl; return; }
    const auto& modules = rack->getModules();
    std::cout << "[FMRackController] setDexedParam: modules.size()=" << modules.size() << std::endl;
    if (modules.empty()) { std::cout << "[FMRackController] setDexedParam: modules is empty" << std::endl; return; }
    auto* dexed = modules[0]->getDexedEngine();
    if (!dexed) { std::cout << "[FMRackController] setDexedParam: dexed is nullptr" << std::endl; return; }
    std::cout << "[FMRackController] setDexedParam: calling dexed->setVoiceDataElement(" << (int)address << ", " << (int)value << ") and doRefreshVoice" << std::endl;
    dexed->setVoiceDataElement(address, value);
    dexed->doRefreshVoice();
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
        rack->routeSysexToModules(sysex.data(), (int)sysex.size(), midiChannel);
    } else {
        std::cout << "[FMRackController] requestSingleVoiceDump: rack is nullptr" << std::endl;
    }
}

void FMRackController::onSingleVoiceDumpReceived(const std::vector<uint8_t>& data) {
    std::cout << "[FMRackController] onSingleVoiceDumpReceived called, data size: " << data.size() << std::endl;
    if (!data.empty()) {
        std::cout << "[FMRackController] onSingleVoiceDumpReceived: first 32 bytes: ";
        for (size_t i = 0; i < std::min<size_t>(32, data.size()); ++i) std::cout << std::hex << (int)data[i] << " ";
        std::cout << std::dec << std::endl;
        std::cout << "[FMRackController] onSingleVoiceDumpReceived: last 8 bytes: ";
        for (size_t i = (data.size() > 8 ? data.size() - 8 : 0); i < data.size(); ++i) std::cout << std::hex << (int)data[i] << " ";
        std::cout << std::dec << std::endl;
    }
    // Always handle incoming MIDI/SysEx here, no callback indirection
    // ...existing MIDI/SysEx handling logic...
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
