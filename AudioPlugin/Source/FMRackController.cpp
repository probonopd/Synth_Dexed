#include "FMRackController.h"
#include <iostream>

FMRackController::FMRackController(float sampleRate)
{
    std::lock_guard<std::mutex> lock(mutex);
    rack = std::make_unique<FMRack::Rack>(sampleRate);
    performance = std::make_unique<FMRack::Performance>();
    rack->setPerformance(*performance);
}

FMRackController::~FMRackController() = default;

bool FMRackController::loadPerformanceFile(const juce::String& path)
{
    try {
        std::lock_guard<std::mutex> lock(mutex);
        if (!performance)
            performance = std::make_unique<FMRack::Performance>();
        if (!performance->loadFromFile(path.toStdString())) {
            std::cout << "[FMRackController] Performance loadFromFile failed: " << path << std::endl;
            return false;
        }
        rack->setPerformance(*performance);
        std::cout << "[FMRackController] Performance loaded and rack configured from: " << path << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cout << "[FMRackController] Exception in loadPerformanceFile: " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cout << "[FMRackController] Unknown exception in loadPerformanceFile" << std::endl;
        return false;
    }
}

void FMRackController::setDefaultPerformance()
{
    try {
        std::lock_guard<std::mutex> lock(mutex);
        if (!performance)
            performance = std::make_unique<FMRack::Performance>();
        performance->setDefaults(8, 1);
        rack->setPerformance(*performance);
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
        *performance = perf;
        rack->setPerformance(*performance);
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
            rack->setPerformance(*performance);
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
            rack->setPerformance(*performance);
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
        rack->setPerformance(*performance);
    }
}

void FMRackController::setNumModules(int num)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (performance) {
        for (int i = 0; i < 16; ++i)
            performance->parts[i].midiChannel = (i < num) ? (i + 1) : 0;
        if (rack) rack->setPerformance(*performance);
    }
    if (onModulesChanged) onModulesChanged();
}

std::mutex& FMRackController::getMutex() { return mutex; }
