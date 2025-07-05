#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "FMRackController.h"
#include <cstdio> // for std::tmpnam
#include <fstream>
#include <sstream>
#include <memory>

bool debugEnabled = false; // Enable debug logging for plugin

// Global multiprocessing flag for FMRack
int multiprocessingEnabled = 1;

//==============================================================================
// Helper function to create plugin parameters
juce::AudioProcessorValueTreeState::ParameterLayout AudioPluginAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Add numModules parameter
    params.push_back(std::make_unique<juce::AudioParameterInt>("numModules", "Number of Modules", 1, 32, 1));

    // Global Effects Parameters
    params.push_back(std::make_unique<juce::AudioParameterBool>("compressorEnable", "Compressor Enable", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>("reverbEnable", "Reverb Enable", true));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("reverbSize", "Size", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("reverbHighDamp", "High Damp", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("reverbLowDamp", "Low Damp", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("reverbLowPass", "Low Pass", 0.0f, 1.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("reverbDiffusion", "Diffusion", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("reverbLevel", "Level", 0.0f, 1.0f, 0.25f));

    return { params.begin(), params.end() };
}

void AudioPluginAudioProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    if (parameterID == "numModules")
    {
        if (editorPtr)
        {
            editorPtr->numModulesChanged();
        }
        return;
    }

    if (controller && controller->getPerformance()) {
        auto& effects = controller->getPerformance()->effects;
        bool performanceChanged = false;

        if (parameterID == "compressorEnable") {
            effects.compressorEnable = newValue > 0.5f;
            performanceChanged = true;
        } else if (parameterID == "reverbEnable") {
            effects.reverbEnable = newValue > 0.5f;
            performanceChanged = true;
        } else if (parameterID == "reverbSize") {
            effects.reverbSize = static_cast<uint8_t>(newValue * 127.0f);
            performanceChanged = true;
        } else if (parameterID == "reverbHighDamp") {
            effects.reverbHighDamp = static_cast<uint8_t>(newValue * 127.0f);
            performanceChanged = true;
        } else if (parameterID == "reverbLowDamp") {
            effects.reverbLowDamp = static_cast<uint8_t>(newValue * 127.0f);
            performanceChanged = true;
        } else if (parameterID == "reverbLowPass") {
            effects.reverbLowPass = static_cast<uint8_t>(newValue * 127.0f);
            performanceChanged = true;
        } else if (parameterID == "reverbDiffusion") {
            effects.reverbDiffusion = static_cast<uint8_t>(newValue * 127.0f);
            performanceChanged = true;
        } else if (parameterID == "reverbLevel") {
            effects.reverbLevel = static_cast<uint8_t>(newValue * 127.0f);
            performanceChanged = true;
        }

        if (performanceChanged) {
            controller->setPerformance(*controller->getPerformance());
        }
    }
}

AudioPluginAudioProcessor::AudioPluginAudioProcessor()
    : AudioProcessor (BusesProperties()
        #if ! JucePlugin_IsMidiEffect
         #if ! JucePlugin_IsSynth
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
         #endif
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
        #endif
          ),
      treeState (*this, nullptr, juce::Identifier ("PluginParameters"), createParameterLayout())
{
    treeState.addParameterListener("numModules", this);
    treeState.addParameterListener("compressorEnable", this);
    treeState.addParameterListener("reverbEnable", this);
    treeState.addParameterListener("reverbSize", this);
    treeState.addParameterListener("reverbHighDamp", this);
    treeState.addParameterListener("reverbLowDamp", this);
    treeState.addParameterListener("reverbLowPass", this);
    treeState.addParameterListener("reverbDiffusion", this);
    treeState.addParameterListener("reverbLevel", this);

    try {
        // Initialize FileLogger - logs to a file in the User's Documents directory
        auto documentsDir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
        auto logFile = documentsDir.getChildFile ("AudioPluginDemoLog.txt"); 
        fileLogger = std::make_unique<juce::FileLogger>(logFile, "Log started: " + juce::Time::getCurrentTime().toString (true, true), 1024 * 1024);
        juce::Logger::setCurrentLogger (fileLogger.get());
        DBG("Constructor: FileLogger initialized. Logging to: " + logFile.getFullPathName());

        controller = std::make_unique<FMRackController>(44100.0f); // Use actual sampleRate in prepareToPlay
    } catch (const std::exception& e) {
        std::cout << "[PluginProcessor] Exception in constructor: " << e.what() << std::endl;
        juce::Logger::writeToLog(juce::String("[PluginProcessor] Exception in constructor: ") + e.what());
    } catch (...) {
        std::cout << "[PluginProcessor] Unknown exception in constructor" << std::endl;
        juce::Logger::writeToLog("[PluginProcessor] Unknown exception in constructor");
    }
}

AudioPluginAudioProcessor::~AudioPluginAudioProcessor()
{
    try {
        DBG("Destructor: Cleaning up.");
        juce::Logger::setCurrentLogger (nullptr); // Important to release the logger
        fileLogger.reset(); // Release the file logger
    } catch (const std::exception& e) {
        std::cout << "[PluginProcessor] Exception in destructor: " << e.what() << std::endl;
        juce::Logger::writeToLog(juce::String("[PluginProcessor] Exception in destructor: ") + e.what());
    } catch (...) {
        std::cout << "[PluginProcessor] Unknown exception in destructor" << std::endl;
        juce::Logger::writeToLog("[PluginProcessor] Unknown exception in destructor");
    }
}

//==============================================================================
const juce::String AudioPluginAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool AudioPluginAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool AudioPluginAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool AudioPluginAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double AudioPluginAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int AudioPluginAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int AudioPluginAudioProcessor::getCurrentProgram()
{
    return 0;
}

void AudioPluginAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String AudioPluginAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    return {};
}

void AudioPluginAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================

void AudioPluginAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    try {
        juce::Logger::writeToLog("[PluginProcessor] prepareToPlay called. sampleRate=" + juce::String(sampleRate) + ", samplesPerBlock=" + juce::String(samplesPerBlock));
        std::cout << "[PluginProcessor] prepareToPlay called. sampleRate=" << sampleRate << ", samplesPerBlock=" << samplesPerBlock << std::endl;

        bool needRecreate = false;
        if (!controller) {
            needRecreate = true;
            std::cout << "[PluginProcessor] Controller is null, will create new controller." << std::endl;
        } else {
            if (std::abs(lastSampleRate - (float)sampleRate) > 1.0f) {
                std::cout << "[PluginProcessor] Sample rate changed (old=" << lastSampleRate << ", new=" << sampleRate << "), recreating controller." << std::endl;
                needRecreate = true;
            }
        }
        lastSampleRate = (float)sampleRate;
        if (needRecreate) {
            // Use cachedPerformance if available
            std::unique_ptr<FMRack::Performance> oldPerf;
            if (cachedPerformance) {
                oldPerf = std::make_unique<FMRack::Performance>(*cachedPerformance);
                juce::Logger::writeToLog("[PluginProcessor] Using cachedPerformance for controller recreation. MIDI channels: " + juce::String(cachedPerformance->parts[0].midiChannel) + ", " + juce::String(cachedPerformance->parts[1].midiChannel) + ", ...");
            } else if (controller && controller->getPerformance()) {
                oldPerf = std::make_unique<FMRack::Performance>(*controller->getPerformance());
            }
            controller = std::make_unique<FMRackController>((float)sampleRate);
            if (oldPerf) {
                std::cout << "[PluginProcessor] Restoring previous performance after controller recreation." << std::endl;
                juce::Logger::writeToLog("[PluginProcessor] Restoring previous performance after controller recreation. MIDI channels: " + juce::String(oldPerf->parts[0].midiChannel) + ", " + juce::String(oldPerf->parts[1].midiChannel) + ", ...");
                controller->setPerformance(*oldPerf);
            }
        } else {
            if (controller && controller->getPerformance()) {
                std::cout << "[PluginProcessor] Calling setPerformance in prepareToPlay to ensure modules are created." << std::endl;
                juce::Logger::writeToLog("[PluginProcessor] prepareToPlay: setPerformance MIDI channels: " + juce::String(controller->getPerformance()->parts[0].midiChannel) + ", " + juce::String(controller->getPerformance()->parts[1].midiChannel) + ", ...");
                controller->setPerformance(*controller->getPerformance());
            }
        }
    } catch (const std::exception& e) {
        std::cout << "[PluginProcessor] Exception in prepareToPlay: " << e.what() << std::endl;
        juce::Logger::writeToLog(juce::String("[PluginProcessor] Exception in prepareToPlay: ") + e.what());
    } catch (...) {
        std::cout << "[PluginProcessor] Unknown exception in prepareToPlay" << std::endl;
        juce::Logger::writeToLog("[PluginProcessor] Unknown exception in prepareToPlay");
    }
}

void AudioPluginAudioProcessor::releaseResources()
{
    try {
        controller.reset();
    } catch (const std::exception& e) {
        std::cout << "[PluginProcessor] Exception in releaseResources: " << e.what() << std::endl;
        juce::Logger::writeToLog(juce::String("[PluginProcessor] Exception in releaseResources: ") + e.what());
    } catch (...) {
        std::cout << "[PluginProcessor] Unknown exception in releaseResources" << std::endl;
        juce::Logger::writeToLog("[PluginProcessor] Unknown exception in releaseResources");
    }
}

bool AudioPluginAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}

void AudioPluginAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& midiMessages)
{
    try {
        juce::ScopedNoDenormals noDenormals;
        process(buffer, midiMessages);
    } catch (const std::exception& e) {
        std::cout << "[PluginProcessor] Exception in processBlock: " << e.what() << std::endl;
        juce::Logger::writeToLog(juce::String("[PluginProcessor] Exception in processBlock: ") + e.what());
        buffer.clear();
    } catch (...) {
        std::cout << "[PluginProcessor] Unknown exception in processBlock" << std::endl;
        juce::Logger::writeToLog("[PluginProcessor] Unknown exception in processBlock");
        buffer.clear();
    }
}

// Add the double-precision overload
void AudioPluginAudioProcessor::processBlock (juce::AudioBuffer<double>& buffer,
                                              juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    // Create a temporary float buffer
    juce::AudioBuffer<float> floatBuffer;
    floatBuffer.setSize (buffer.getNumChannels(), buffer.getNumSamples(), false, false, true);

    // Copy double to float
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
        auto* dest = floatBuffer.getWritePointer (channel);
        const auto* src = buffer.getReadPointer (channel);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
            dest[sample] = static_cast<float>(src[sample]);
        }
    }

    process(floatBuffer, midiMessages); // Process using the float buffer

    // Copy float back to double
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
        auto* dest = buffer.getWritePointer (channel);
        const auto* src = floatBuffer.getReadPointer (channel);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
            dest[sample] = static_cast<double>(src[sample]);
        }
    }
}

// Add the templated process implementation
template <typename FloatType>
void AudioPluginAudioProcessor::process (juce::AudioBuffer<FloatType>& buffer, juce::MidiBuffer& midiMessages) {
    try {
        // juce::Logger::writeToLog("Process block. Samples: " + juce::String(buffer.getNumSamples()) + ", MIDI events: " + juce::String(midiMessages.getNumEvents()));
        auto totalNumOutputChannels = getTotalNumOutputChannels();
        auto numSamples = buffer.getNumSamples();
        for (auto i = getTotalNumInputChannels(); i < totalNumOutputChannels; ++i)
            buffer.clear (i, 0, numSamples);

        // Route MIDI to controller
        if (controller) {
            for (const auto metadata : midiMessages) {
                const auto msg = metadata.getMessage();
                std::cout << "[PluginProcessor] MIDI event: status=0x" << std::hex << (int)msg.getRawData()[0] << ", size=" << std::dec << msg.getRawDataSize() << std::endl;
                if (msg.isSysEx()) {
                    std::cout << "[PluginProcessor] SysEx event: size=" << msg.getSysExDataSize() << std::endl;
                    if (msg.getSysExDataSize() > 0) {
                        std::cout << "[PluginProcessor] SysEx first 16 bytes: ";
                        const uint8_t* d = msg.getSysExData();
                        for (size_t i = 0; i < std::min<size_t>(16, msg.getSysExDataSize()); ++i) std::cout << std::hex << (int)d[i] << " ";
                        std::cout << std::dec << std::endl;
                        std::cout << "[PluginProcessor] SysEx last 8 bytes: ";
                        for (size_t i = (msg.getSysExDataSize() > 8 ? msg.getSysExDataSize() - 8 : 0); i < msg.getSysExDataSize(); ++i) std::cout << std::hex << (int)d[i] << " ";
                        std::cout << std::dec << std::endl;
                    }
                }
                if (msg.isNoteOn() || msg.isNoteOff() || msg.isController() || msg.isPitchWheel()) {
                    controller->processMidiMessage(msg.getRawData()[0],
                                                  msg.getRawDataSize() > 1 ? msg.getRawData()[1] : 0,
                                                  msg.getRawDataSize() > 2 ? msg.getRawData()[2] : 0);
                }
                // --- Handle incoming SysEx: DX7 single voice dump (163 bytes) ---
                if (msg.isSysEx() && msg.getSysExDataSize() == 163) {
                    const uint8_t* data = msg.getSysExData();
                    if (data[0] == 0xF0 && data[1] == 0x43 && data[5] == 0x1B) {
                        std::cout << "[PluginProcessor] Detected DX7 single voice dump (163 bytes), forwarding to controller->onSingleVoiceDumpReceived" << std::endl;
                        std::vector<uint8_t> sysexData(data, data + 163);
                        controller->onSingleVoiceDumpReceived(sysexData);
                    }
                }
            }
        }
        // Render audio from controller
        if (controller) {
            std::vector<float> left(numSamples, 0.0f), right(numSamples, 0.0f);
            try {
                controller->processAudio(left.data(), right.data(), numSamples);
            } catch (const std::exception& e) {
                std::cout << "[PluginProcessor] Exception in controller->processAudio: " << e.what() << std::endl;
                juce::Logger::writeToLog(juce::String("[PluginProcessor] Exception in controller->processAudio: ") + e.what());
                std::fill(left.begin(), left.end(), 0.0f);
                std::fill(right.begin(), right.end(), 0.0f);
            } catch (...) {
                std::cout << "[PluginProcessor] Unknown exception in controller->processAudio" << std::endl;
                juce::Logger::writeToLog("[PluginProcessor] Unknown exception in controller->processAudio");
                std::fill(left.begin(), left.end(), 0.0f);
                std::fill(right.begin(), right.end(), 0.0f);
            }
            for (int ch = 0; ch < totalNumOutputChannels; ++ch) {
                FloatType* out = buffer.getWritePointer(ch);
                const float* src = (ch == 0) ? left.data() : right.data();
                for (int i = 0; i < numSamples; ++i) {
                    out[i] = src[i];
                }
            }
        } else {
            buffer.clear();
        }
    } catch (const std::exception& e) {
        std::cout << "[PluginProcessor] Exception in process<>: " << e.what() << std::endl;
        juce::Logger::writeToLog(juce::String("[PluginProcessor] Exception in process<>: ") + e.what());
        buffer.clear();
    } catch (...) {
        std::cout << "[PluginProcessor] Unknown exception in process<>" << std::endl;
        juce::Logger::writeToLog("[PluginProcessor] Unknown exception in process<>");
        buffer.clear();
    }
}

//==============================================================================
bool AudioPluginAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* AudioPluginAudioProcessor::createEditor()
{
    return new AudioPluginAudioProcessorEditor (*this);
}

void AudioPluginAudioProcessor::setEditorPointer(AudioPluginAudioProcessorEditor* editor) {
    editorPtr = editor;
}

void AudioPluginAudioProcessor::logToGui(const juce::String& message) {
    if (editorPtr) {
        juce::MessageManager::callAsync([this, msg = message]() {
            if (editorPtr) editorPtr->appendLogMessage(msg);
        });
    }
}

//==============================================================================
void AudioPluginAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (controller && controller->getPerformance())
    {
        // Use a temporary file to save the performance as INI
        char tmpPath[L_tmpnam];
        std::tmpnam(tmpPath);
        controller->getPerformance()->saveToFile(tmpPath);
        std::ifstream file(tmpPath, std::ios::binary);
        if (file)
        {
            std::ostringstream oss;
            oss << file.rdbuf();
            std::string iniString = oss.str();
            juce::Logger::writeToLog("[PluginProcessor] getStateInformation: saving INI string:\n" + juce::String(iniString.c_str()));
            juce::MemoryOutputStream stream(destData, false);
            stream.writeString(iniString);
        }
        std::remove(tmpPath);
    }
}

void AudioPluginAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (controller && data && sizeInBytes > 0)
    {
        juce::Logger::writeToLog("[PluginProcessor] setStateInformation called");
        juce::MemoryInputStream stream(data, static_cast<size_t>(sizeInBytes), false);
        juce::String iniString = stream.readString();
        char tmpPath[L_tmpnam];
        std::tmpnam(tmpPath);
        {
            std::ofstream file(tmpPath, std::ios::binary);
            file << iniString.toStdString();
        }
        controller->getPerformance()->loadFromFile(tmpPath);
        std::remove(tmpPath);
        // Cache the loaded performance for later controller recreation
        if (!cachedPerformance) cachedPerformance = std::make_unique<FMRack::Performance>();
        *cachedPerformance = *controller->getPerformance();
        // Log all MIDI channels after loading
        juce::String midiChannels;
        for (int i = 0; i < 8; ++i) midiChannels += juce::String(cachedPerformance->parts[i].midiChannel) + " ";
        juce::Logger::writeToLog("[PluginProcessor] setStateInformation: cachedPerformance MIDI channels: " + midiChannels);
        midiChannels = "";
        for (int i = 0; i < 8; ++i) midiChannels += juce::String(controller->getPerformance()->parts[i].midiChannel) + " ";
        juce::Logger::writeToLog("[PluginProcessor] setStateInformation: controller->getPerformance() MIDI channels: " + midiChannels);
        controller->setPerformance(*controller->getPerformance());
        // Suppress UI-triggered module count changes after loading state
        if (editorPtr && editorPtr->getRackAccordion()) {
            editorPtr->getRackAccordion()->suppressNumModulesSync(true);
            editorPtr->getRackAccordion()->updatePanels();
            editorPtr->getRackAccordion()->suppressNumModulesSync(false);
        }
    }
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioPluginAudioProcessor();
}

bool AudioPluginAudioProcessor::loadPerformanceFile(const juce::String& path)
{
    if (controller)
        return controller->loadPerformanceFile(path);
    return false;
}

bool AudioPluginAudioProcessor::savePerformanceFile(const juce::String& path)
{
    if (controller)
        return controller->savePerformanceFile(path);
    return false;
}

void AudioPluginAudioProcessor::setNumModules(int num) {
    juce::Logger::writeToLog("[PluginProcessor] setNumModules called with num=" + juce::String(num));
    // WARNING: This should only be called in direct response to user action (e.g. UI slider),
    // NEVER after loading a performance or restoring state, or you will overwrite MIDI channels.
    jassert(controller && controller->getPerformance());
    if (controller && controller->getPerformance()) {
        // Only set MIDI channels if all are currently zero (no modules active)
        bool allZero = true;
        for (int i = 0; i < 16; ++i) {
            if (controller->getPerformance()->parts[i].midiChannel != 0) {
                allZero = false;
                break;
            }
        }
        if (allZero) {
            juce::Logger::writeToLog("[PluginProcessor] setNumModules: all MIDI channels zero, assigning 1..N");
            for (int i = 0; i < 16; ++i)
                controller->getPerformance()->parts[i].midiChannel = (i < num) ? (i + 1) : 0;
            controller->setPerformance(*controller->getPerformance());
        } else {
            juce::Logger::writeToLog("[PluginProcessor] setNumModules: NOT all MIDI channels zero, skipping assignment");
            jassertfalse; // This should never be called after state restore or performance load!
        }
        // Log MIDI channels after setNumModules
        juce::String midiChannels;
        for (int i = 0; i < 8; ++i) midiChannels += juce::String(controller->getPerformance()->parts[i].midiChannel) + " ";
        juce::Logger::writeToLog("[PluginProcessor] setNumModules: MIDI channels after: " + midiChannels);
    }
}
void AudioPluginAudioProcessor::setUnisonVoices(int num) {
    if (controller && controller->getPerformance()) {
        for (int i = 0; i < 16; ++i)
            controller->getPerformance()->parts[i].unisonVoices = num;
        controller->setPerformance(*controller->getPerformance());
    }
}
void AudioPluginAudioProcessor::setUnisonDetune(float detune) {
    if (controller && controller->getPerformance()) {
        for (int i = 0; i < 16; ++i)
            controller->getPerformance()->parts[i].unisonDetune = detune;
        controller->setPerformance(*controller->getPerformance());
    }
}
void AudioPluginAudioProcessor::setUnisonPan(float pan) {
    if (controller && controller->getPerformance()) {
        for (int i = 0; i < 16; ++i)
            controller->getPerformance()->parts[i].unisonSpread = pan;
        controller->setPerformance(*controller->getPerformance());
    }
}
FMRack::Rack* AudioPluginAudioProcessor::getRack() const {
    return controller ? controller->getRack() : nullptr;
}

FMRack::Performance* AudioPluginAudioProcessor::getPerformance() const {
    return controller ? controller->getPerformance() : nullptr;
}
