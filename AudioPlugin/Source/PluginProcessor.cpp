#include "PluginProcessor.h"
#include "PluginEditor.h"

bool debugEnabled = false; // Enable debug logging for plugin

// Global multiprocessing flag for FMRack
int multiprocessingEnabled = 1;

//==============================================================================
// Helper function to create plugin parameters
juce::AudioProcessorValueTreeState::ParameterLayout AudioPluginAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // TODO: Add your plugin parameters here. For example:
    // params.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "gain", 1 }, "Gain", 0.0f, 1.0f, 0.5f));
    // params.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "mode", 1 }, "Mode", juce::StringArray { "Option1", "Option2" }, 0));

    return { params.begin(), params.end() };
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
    // Initialize FileLogger - logs to a file in the User's Documents directory
    auto documentsDir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    auto logFile = documentsDir.getChildFile ("AudioPluginDemoLog.txt"); 
    fileLogger = std::make_unique<juce::FileLogger>(logFile, "Log started: " + juce::Time::getCurrentTime().toString (true, true), 1024 * 1024);
    juce::Logger::setCurrentLogger (fileLogger.get());
    DBG("Constructor: FileLogger initialized. Logging to: " + logFile.getFullPathName());

    performance = std::make_unique<FMRack::Performance>();
    rack = std::make_unique<FMRack::Rack>(44100.0f); // Use actual sampleRate in prepareToPlay
}

AudioPluginAudioProcessor::~AudioPluginAudioProcessor()
{
    DBG("Destructor: Cleaning up.");
    juce::Logger::setCurrentLogger (nullptr); // Important to release the logger
    fileLogger.reset(); // Release the file logger
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
    juce::Logger::writeToLog("AudioPluginAudioProcessor::prepareToPlay called. Sample rate: " + juce::String(sampleRate));
    juce::ignoreUnused (samplesPerBlock);
    rack = std::make_unique<FMRack::Rack>(static_cast<float>(sampleRate));
    if (performance)
        rack->setPerformance(*performance);
}

void AudioPluginAudioProcessor::releaseResources()
{
    juce::Logger::writeToLog("AudioPluginAudioProcessor::releaseResources called.");
    rack.reset();
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
    juce::Logger::writeToLog("Process block. Samples: " + juce::String(buffer.getNumSamples()) + ", MIDI events: " + juce::String(midiMessages.getNumEvents()));
    auto totalNumOutputChannels = getTotalNumOutputChannels();
    auto numSamples = buffer.getNumSamples();
    for (auto i = getTotalNumInputChannels(); i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, numSamples);

    // Route MIDI to rack and log to GUI
    if (rack) {
        for (const auto metadata : midiMessages) {
            const auto msg = metadata.getMessage();
            if (msg.isNoteOn() || msg.isNoteOff() || msg.isController() || msg.isPitchWheel()) {
                rack->processMidiMessage(msg.getRawData()[0],
                                        msg.getRawDataSize() > 1 ? msg.getRawData()[1] : 0,
                                        msg.getRawDataSize() > 2 ? msg.getRawData()[2] : 0);
            }
        }
    }
    // Render audio from rack
    if (rack) {
        std::vector<float> left(numSamples, 0.0f), right(numSamples, 0.0f);
        rack->processAudio(left.data(), right.data(), numSamples);
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
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
    juce::ignoreUnused (destData);
}

void AudioPluginAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
    juce::ignoreUnused (data, sizeInBytes);
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioPluginAudioProcessor();
}

bool AudioPluginAudioProcessor::loadPerformanceFile(const juce::String& path)
{
    if (!performance)
        performance = std::make_unique<FMRack::Performance>();
    if (!performance->loadFromFile(path.toStdString())) {
        logToGui("Performance loadFromFile failed: " + path);
        return false;
    }
    if (!rack)
        rack = std::make_unique<FMRack::Rack>(44100.0f); // Use actual sampleRate if available
    rack->setPerformance(*performance);
    logToGui("Performance loaded and rack configured from: " + path);
    return true;
}
