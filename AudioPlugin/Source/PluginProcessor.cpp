#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "FMRackController.h"
#include "MidiPlaybackEngine.h"
#include <cstdio> // for std::tmpnam
#include <cstring> // for std::memset
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
        logToGui("prepareToPlay called. sampleRate=" + juce::String(sampleRate) + ", samplesPerBlock=" + juce::String(samplesPerBlock));
        juce::Logger::writeToLog("[PluginProcessor] prepareToPlay called. sampleRate=" + juce::String(sampleRate) + ", samplesPerBlock=" + juce::String(samplesPerBlock));

        // Pre-allocate audio buffers to avoid memory allocation in audio thread
        // Allocate extra capacity for safety (some hosts may exceed samplesPerBlock)
        int bufferCapacity = samplesPerBlock * 2;
        if (bufferCapacity > (int)audioBufferLeft.size()) {
            audioBufferLeft.resize(bufferCapacity);
            audioBufferRight.resize(bufferCapacity);
        }
        lastPreparedBlockSize = samplesPerBlock;

        bool needRecreate = false;
        if (!controller) {
            needRecreate = true;
        } else {
            if (std::abs(lastSampleRate - (float)sampleRate) > 1.0f) {
                needRecreate = true;
            }
        }
        lastSampleRate = (float)sampleRate;
        if (needRecreate) {
            // Use cachedPerformance if available
            std::unique_ptr<FMRack::Performance> oldPerf;
            if (cachedPerformance) {
                oldPerf = std::make_unique<FMRack::Performance>(*cachedPerformance);
                juce::Logger::writeToLog("[PluginProcessor] Using cachedPerformance for controller recreation.");
            } else if (controller && controller->getPerformance()) {
                oldPerf = std::make_unique<FMRack::Performance>(*controller->getPerformance());
            }
            controller = std::make_unique<FMRackController>((float)sampleRate);
            if (oldPerf) {
                juce::Logger::writeToLog("[PluginProcessor] Restoring previous performance after controller recreation.");
                controller->setPerformance(*oldPerf);
            }
        } else {
            if (controller && controller->getPerformance()) {
                juce::Logger::writeToLog("[PluginProcessor] prepareToPlay: ensuring modules are created.");
                controller->setPerformance(*controller->getPerformance());
            }
        }
    } catch (const std::exception& e) {
        juce::Logger::writeToLog(juce::String("[PluginProcessor] Exception in prepareToPlay: ") + e.what());
    } catch (...) {
        juce::Logger::writeToLog("[PluginProcessor] Unknown exception in prepareToPlay");
    }
}

void AudioPluginAudioProcessor::releaseResources()
{
    logToGui("releaseResources called.");
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
    logToGui("isBusesLayoutSupported called.");
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
    // No try-catch in real-time audio path - exceptions shouldn't happen here
    // and catching them adds overhead. If there's a bug, let it crash cleanly
    // so we can debug it rather than silently failing.
    juce::ScopedNoDenormals noDenormals;
    process(buffer, midiMessages);
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
    auto totalNumOutputChannels = getTotalNumOutputChannels();
    auto numSamples = buffer.getNumSamples();
    
    // Clear output channels that don't have input
    for (auto i = getTotalNumInputChannels(); i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, numSamples);

    // Safety check: ensure we have a controller
    if (!controller) {
        buffer.clear();
        return;
    }

    // Route MIDI to controller (no std::cout in audio thread!)
    for (const auto metadata : midiMessages) {
        const auto msg = metadata.getMessage();
        if (msg.isNoteOn() || msg.isNoteOff() || msg.isController() || msg.isPitchWheel() || msg.isChannelPressure() || msg.isAftertouch()) {
            controller->processMidiMessage(msg.getRawData()[0],
                                          msg.getRawDataSize() > 1 ? msg.getRawData()[1] : 0,
                                          msg.getRawDataSize() > 2 ? msg.getRawData()[2] : 0);
        }
        
        // Handle incoming SysEx messages
        if (msg.isSysEx()) {
            const uint8_t* data = msg.getSysExData();
            const int sysexSize = msg.getSysExDataSize();
            
            // DX7 Parameter Change: F0 43 1n pp vv F7 (6 bytes total, getSysExData excludes F0/F7)
            // So we get: 43 1n pp vv (4 bytes)
            if (sysexSize == 4 && data[0] == 0x43 && (data[1] & 0xF0) == 0x10) {
                // Extract channel, parameter, and value
                uint8_t channel = (data[1] & 0x0F) + 1;  // Convert 0-15 to 1-16
                uint8_t param = data[2];
                uint8_t value = data[3] & 0x7F;
                
                // Apply to all modules on this MIDI channel
                // (In a real scenario, you might want to route to specific modules based on channel)
                for (int i = 0; i < controller->getNumModules(); i++) {
                    auto* module = controller->getModule(i);
                    if (module && module->getMIDIChannel() == channel) {
                        controller->setDexedParamForModule(i, param, value);
                    }
                }
            }
            // DX7 single voice dump: F0 43 0n 00 01 1B <155 voice bytes> <checksum> F7
            // getSysExData() excludes F0/F7, so we check: 43 0n 00 01 1B... (161 bytes)
            else if (sysexSize == 161 && data[0] == 0x43 && (data[1] & 0xF0) == 0x00 && 
                     data[2] == 0x00 && data[3] == 0x01 && data[4] == 0x1B) {
                // Extract MIDI channel from second byte
                uint8_t channel = (data[1] & 0x0F) + 1;
                
                // Apply to all modules on this MIDI channel
                for (int i = 0; i < controller->getNumModules(); i++) {
                    auto* module = controller->getModule(i);
                    if (module && module->getMIDIChannel() == channel) {
                        // Voice data starts at byte 5 (after header)
                        controller->applyVoiceDumpToModule(i, data + 5, 156);
                    }
                }
            }
        }
    }

    // Render audio using pre-allocated buffers
    // Ensure buffers are large enough (should be handled in prepareToPlay, but check for safety)
    if (numSamples > (int)audioBufferLeft.size()) {
        // This shouldn't happen, but handle it gracefully without allocating
        buffer.clear();
        return;
    }

    // Clear the pre-allocated buffers (use memset for speed)
    std::memset(audioBufferLeft.data(), 0, numSamples * sizeof(float));
    std::memset(audioBufferRight.data(), 0, numSamples * sizeof(float));

    // Process audio
    controller->processAudio(audioBufferLeft.data(), audioBufferRight.data(), numSamples);

    // Inject MIDI playback events before flushing output queue
    midiPlaybackEngine.fillMidiBuffer(midiMessages, getSampleRate(), numSamples);

    // Flush any queued MIDI output messages (e.g., parameter changes to external DX7)
    // Always flush MIDI output regardless of build flag (the plugin is configured with NEEDS_MIDI_OUTPUT TRUE)
    controller->flushMidiOutputQueue(midiMessages);

    // Copy to output buffer
    for (int ch = 0; ch < totalNumOutputChannels; ++ch) {
        FloatType* out = buffer.getWritePointer(ch);
        const float* src = (ch == 0) ? audioBufferLeft.data() : audioBufferRight.data();
        for (int i = 0; i < numSamples; ++i) {
            out[i] = static_cast<FloatType>(src[i]);
        }
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

void AudioPluginAudioProcessor::logToGui(const juce::String& message) const {
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
        auto tempFile = juce::File::createTempFile("FMRackState.ini");
        const auto tempPath = tempFile.getFullPathName().toStdString();
        controller->getPerformance()->saveToFile(tempPath);
        std::ifstream file(tempPath, std::ios::binary);
        if (file)
        {
            std::ostringstream oss;
            oss << file.rdbuf();
            std::string iniString = oss.str();
            juce::Logger::writeToLog("[PluginProcessor] getStateInformation: saving INI string:\n" + juce::String(iniString.c_str()));
            juce::MemoryOutputStream stream(destData, false);
            stream.writeString(iniString);
        }
        tempFile.deleteFile();
    }
}

void AudioPluginAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (controller && data && sizeInBytes > 0)
    {
        juce::Logger::writeToLog("[PluginProcessor] setStateInformation called");
        juce::MemoryInputStream stream(data, static_cast<size_t>(sizeInBytes), false);
        juce::String iniString = stream.readString();
        auto tempFile = juce::File::createTempFile("FMRackState.ini");
        const auto tempPath = tempFile.getFullPathName().toStdString();
        {
            std::ofstream file(tempPath, std::ios::binary);
            file << iniString.toStdString();
        }
        controller->getPerformance()->loadFromFile(tempPath);
        tempFile.deleteFile();
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
        // Sync parameters from loaded performance
        syncParametersFromPerformance();
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
    {
        bool ok = controller->loadPerformanceFile(path);
        if (ok) syncParametersFromPerformance();
        return ok;
    }
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
            juce::Logger::writeToLog("[PluginProcessor] setNumModules: all MIDI channels zero, assigning same channel to all");
            // Find the MIDI channel to use - default to 1
            uint8_t channel = 1;
            for (int i = 0; i < 16; ++i) {
                controller->getPerformance()->parts[i].midiChannel = (i < num) ? channel : 0;
            }
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
            controller->getPerformance()->parts[i].unisonVoices = static_cast<uint8_t>(num);
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

// Helper: Sync ValueTreeState parameters from the current performance's effects
void AudioPluginAudioProcessor::syncParametersFromPerformance()
{
    if (!controller || !controller->getPerformance()) return;
    const auto& effects = controller->getPerformance()->effects;
    auto setBool = [this](const char* param, bool value) {
        if (auto* p = treeState.getParameter(param))
            p->setValueNotifyingHost(value ? 1.0f : 0.0f);
    };
    auto setFloat = [this](const char* param, uint8_t value) {
        if (auto* p = treeState.getParameter(param))
            p->setValueNotifyingHost((float)value / 127.0f);
    };
    setBool("compressorEnable", effects.compressorEnable);
    setBool("reverbEnable", effects.reverbEnable);
    setFloat("reverbSize", effects.reverbSize);
    setFloat("reverbHighDamp", effects.reverbHighDamp);
    setFloat("reverbLowDamp", effects.reverbLowDamp);
    setFloat("reverbLowPass", effects.reverbLowPass);
    setFloat("reverbDiffusion", effects.reverbDiffusion);
    setFloat("reverbLevel", effects.reverbLevel);
}

//==============================================================================
// MIDI Playback

void AudioPluginAudioProcessor::loadMidiForPlayback(const juce::MidiFile& midiFile) {
    midiPlaybackEngine.loadMidiFile(midiFile);
}

void AudioPluginAudioProcessor::startMidiPlayback() {
    midiPlaybackEngine.play();
}

void AudioPluginAudioProcessor::stopMidiPlayback() {
    midiPlaybackEngine.stop();
}

bool AudioPluginAudioProcessor::isMidiPlaying() const {
    return midiPlaybackEngine.isPlaying();
}
