// Minimal VST3 entry point for Dexed
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstplugview.h"
#include "pluginterfaces/base/fplatform.h" // For tresult, PLUGIN_API, SMTG_OVERRIDE
#include "public.sdk/source/main/pluginfactory.h" // For factory macros, PClassInfo, kVstVersionString etc.
#include "pluginterfaces/vst/ivstevents.h" // For IEventList
#include "pluginterfaces/vst/ivstparameterchanges.h" // For IParameterChanges
#include "pluginterfaces/vst/vsttypes.h" // For SpeakerArr
#include "base/source/fstreamer.h" // For IBStream
#include <vector> // For std::vector
#include <algorithm> // For std::min, std::max

// Dexed Engine include
#include "../../dexed.h" // Assuming dexed.h is in src/

// Define FUIDs before use
// {01234567-89AB-CDEF-0123-456789ABCDEF} - Replace with actual unique IDs
Steinberg::FUID MyProcessorUID(0x01234567, 0x89ABCDEF, 0x01234567, 0x89ABCDEF);
// {89ABCDEF-0123-4567-89AB-CDEF01234567} - Replace with actual unique IDs
Steinberg::FUID MyControllerUID(0x89ABCDEF, 0x01234567, 0x89ABCDEF, 0x01234567);

// Copied from main_common.cpp
static uint8_t fmpiano_sysex[156] = {
    95, 29, 20, 50, 99, 95, 00, 00, 41, 00, 19, 00, 00, 03, 00, 06, 79, 00, 01, 00, 14,
    95, 20, 20, 50, 99, 95, 00, 00, 00, 00, 00, 00, 00, 03, 00, 00, 99, 00, 01, 00, 00,
    95, 29, 20, 50, 99, 95, 00, 00, 00, 00, 00, 00, 00, 03, 00, 06, 89, 00, 01, 00, 07,
    95, 20, 20, 50, 99, 95, 00, 00, 00, 00, 00, 00, 00, 03, 00, 02, 99, 00, 01, 00, 07,
    95, 50, 35, 78, 99, 75, 00, 00, 00, 00, 00, 00, 00, 03, 00, 07, 58, 00, 14, 00, 07,
    96, 25, 25, 67, 99, 75, 00, 00, 00, 00, 00, 00, 00, 03, 00, 02, 99, 00, 01, 00, 10,
    94, 67, 95, 60, 50, 50, 50, 50,
    04, 06, 00,
    34, 33, 00, 00, 00, 04,
    03, 24,
    70, 77, 45, 80, 73, 65, 78, 79, 00, 00
};


namespace Steinberg {
namespace Vst {


class DexedProcessor : public AudioEffect {
public:
    DexedProcessor() : dexedEngine(nullptr) { // Initialize dexedEngine to nullptr
        setControllerClass(MyControllerUID);
    }

    ~DexedProcessor() { // Destructor to clean up dexedEngine
        if (dexedEngine) {
            delete dexedEngine;
            dexedEngine = nullptr;
        }
    }

    static FUnknown* createInstance(void* /*context*/) { return (IAudioProcessor*)new DexedProcessor(); }

    tresult PLUGIN_API initialize(FUnknown* context) SMTG_OVERRIDE {
        tresult result = AudioEffect::initialize(context);
        if (result != kResultOk) {
            return result;
        }

        // Remove default audio buses that might have been created by AudioEffect's constructor
        result = Component::removeAudioBusses(); 
        if (result != kResultOk) {
            // Optional: Log or handle error
        }

        // Add a stereo audio output bus
        addAudioOutput(STR16("Stereo Out"), Steinberg::Vst::SpeakerArr::kStereo);
        
        // Initialize Dexed Engine
        // Note: processSetup.sampleRate might not be valid yet.
        // It's better to initialize/re-initialize in setupProcessing.
        // For now, let's try with a common default. It will be set correctly in setupProcessing.
        // Or, we can defer creation to the first call of setupProcessing.
        // Let's defer for robustness.
        
        return kResultOk;
    }

    tresult PLUGIN_API terminate() SMTG_OVERRIDE {
        if (dexedEngine) {
            delete dexedEngine;
            dexedEngine = nullptr;
        }
        return AudioEffect::terminate();
    }

    tresult PLUGIN_API setActive(TBool state) SMTG_OVERRIDE {
        if (dexedEngine) {
            if (state) {
                dexedEngine->activate();
            } else {
                dexedEngine->deactivate();
            }
        }
        return AudioEffect::setActive(state);
    }

    tresult PLUGIN_API process(ProcessData& data) SMTG_OVERRIDE {
        if (!dexedEngine) { // If engine not initialized, output silence
            if (data.outputs && data.outputs[0].numChannels > 0) {
                 data.outputs[0].silenceFlags = ((1ULL << data.outputs[0].numChannels) - 1);
            } else if (data.outputs) {
                 data.outputs[0].silenceFlags = 0;
            }
            return kResultOk;
        }

        // 1. Handle MIDI events
        IEventList* eventList = data.inputEvents;
        if (eventList) {
            int32 numEvents = eventList->getEventCount();
            for (int32 i = 0; i < numEvents; ++i) {
                Event event;
                if (eventList->getEvent(i, event) == kResultOk) {
                    uint8_t midiBytes[3]; 
                    int16_t midiBytesLen = 0;
                    uint8_t channel = 0; 

                    switch (event.type) {
                        case Event::kNoteOnEvent:
                            channel = static_cast<uint8_t>(event.noteOn.channel);
                            midiBytes[0] = 0x90 | channel;
                            midiBytes[1] = static_cast<uint8_t>(event.noteOn.pitch);
                            midiBytes[2] = static_cast<uint8_t>(event.noteOn.velocity * 127.f + 0.5f);
                            midiBytesLen = 3;
                            if (dexedEngine) dexedEngine->midiDataHandler(channel, midiBytes, midiBytesLen);
                            break;

                        case Event::kNoteOffEvent:
                            channel = static_cast<uint8_t>(event.noteOff.channel);
                            midiBytes[0] = 0x80 | channel;
                            midiBytes[1] = static_cast<uint8_t>(event.noteOff.pitch);
                            midiBytes[2] = static_cast<uint8_t>(event.noteOff.velocity * 127.f + 0.5f); // Dexed midiDataHandler handles this
                            midiBytesLen = 3;
                            if (dexedEngine) dexedEngine->midiDataHandler(channel, midiBytes, midiBytesLen);
                            break;

                        case Event::kPolyPressureEvent: // Polyphonic Aftertouch
                            channel = static_cast<uint8_t>(event.polyPressure.channel);
                            midiBytes[0] = 0xA0 | channel;
                            midiBytes[1] = static_cast<uint8_t>(event.polyPressure.pitch);
                            midiBytes[2] = static_cast<uint8_t>(event.polyPressure.pressure * 127.f + 0.5f);
                            midiBytesLen = 3;
                            if (dexedEngine) dexedEngine->midiDataHandler(channel, midiBytes, midiBytesLen);
                            break;

                        case Event::kDataEvent: // For SysEx
                            if (event.data.type == DataEvent::kMidiSysEx) {
                                if (dexedEngine) dexedEngine->checkSystemExclusive(event.data.bytes, event.data.size);
                            }
                            break;
                        default:
                            // Other event types not handled for now
                            break;
                    }
                }
            }
        }
        
        // TODO: Handle incoming parameter changes (e.g. for MIDI CCs mapped to parameters)
        // IParameterChanges* paramChanges = data.inputParameterChanges;
        // if (paramChanges) { ... }


        // 2. Process audio
        if (data.numSamples > 0) {
            if (data.outputs && data.outputs[0].numChannels >= 2 && data.outputs[0].channelBuffers32) {
                // Ensure processing buffer is large enough
                if (mProcessingBuffer.size() < static_cast<size_t>(data.numSamples)) {
                    mProcessingBuffer.resize(data.numSamples);
                }

                dexedEngine->getSamples(mProcessingBuffer.data(), data.numSamples);
                
                float* outL = data.outputs[0].channelBuffers32[0];
                float* outR = data.outputs[0].channelBuffers32[1];

                for (int32 i = 0; i < data.numSamples; ++i) {
                   float sampleFloat = static_cast<float>(mProcessingBuffer[i]) / 32768.0f;
                   outL[i] = sampleFloat;
                   outR[i] = sampleFloat; // Output mono signal to both channels
                }
                data.outputs[0].silenceFlags = 0; 
            } else {
                 if (data.outputs && data.outputs[0].numChannels > 0) {
                    data.outputs[0].silenceFlags = ((1ULL << data.outputs[0].numChannels) - 1);
                 } else if (data.outputs) {
                    data.outputs[0].silenceFlags = 0;
                 }
            }
        } else { // if data.numSamples == 0, still process MIDI and params
             if (data.outputs && data.outputs[0].numChannels > 0) {
                data.outputs[0].silenceFlags = ((1ULL << data.outputs[0].numChannels) - 1);
             } else if (data.outputs) {
                data.outputs[0].silenceFlags = 0;
             }
        }
        return kResultOk;
    }

    tresult PLUGIN_API setupProcessing(ProcessSetup& newSetup) SMTG_OVERRIDE {
        if ((newSetup.processMode != kRealtime && newSetup.processMode != kPrefetch && newSetup.processMode != kOffline) ||
            newSetup.symbolicSampleSize != kSample32) {
            return kResultFalse; 
        }

        // If engine exists and sample rate changed, re-create it
        if (dexedEngine && dexedEngine->getEngineAddress() && processSetup.sampleRate != newSetup.sampleRate) {
            delete dexedEngine;
            dexedEngine = nullptr;
        }

        if (!dexedEngine) {
            dexedEngine = new Dexed(16, newSetup.sampleRate); // 16 voices
            if (dexedEngine) {
                dexedEngine->activate(); // Activate the engine
                dexedEngine->resetControllers();
                dexedEngine->loadVoiceParameters(fmpiano_sysex); // Load default patch
                dexedEngine->setGain(1.5f); // Set a reasonable gain
            } else {
                return kResultFalse; // Failed to create engine
            }
        }
        
        // Resize processing buffer based on max samples per block
        mProcessingBuffer.resize(newSetup.maxSamplesPerBlock);

        return AudioEffect::setupProcessing(newSetup);
    }
    
    tresult PLUGIN_API setState(IBStream* state) SMTG_OVERRIDE {
        // TODO: Implement saving plugin state (current patch, controller values etc.)
        // Example:
        // FUnknownPtr<IBStreamer> streamer(state);
        // if (streamer && dexedEngine) {
        //     uint8_t voiceData[NUM_VOICE_PARAMETERS];
        //     if (dexedEngine->getVoiceData(voiceData)) {
        //          streamer->write(voiceData, NUM_VOICE_PARAMETERS);
        //     }
        //     // ... save other states like gain, master tune etc.
        // }
        return kNotImplemented;
    }

    tresult PLUGIN_API getState(IBStream* state) SMTG_OVERRIDE {
        // TODO: Implement loading plugin state
        // Example:
        // FUnknownPtr<IBStreamer> streamer(state);
        // if (streamer && dexedEngine) {
        //     uint8_t voiceData[NUM_VOICE_PARAMETERS];
        //     if (streamer->read(voiceData, NUM_VOICE_PARAMETERS) == kResultOk) {
        //          dexedEngine->loadVoiceParameters(voiceData);
        //     }
        //     // ... load other states
        // }
        return kNotImplemented;
    }


private:
    Dexed* dexedEngine; // Instance of your synth engine
    std::vector<int16_t> mProcessingBuffer; // Buffer for int16_t samples from Dexed
};

class DexedController : public EditController {
public:
    DexedController() {}
    static FUnknown* createInstance(void* /*context*/) { return (IEditController*)new DexedController(); }
    
    // --- IEditController methods ---
    tresult PLUGIN_API initialize (FUnknown* context) SMTG_OVERRIDE {
        tresult result = EditController::initialize(context);
        if (result == kResultOk) {
            // TODO: Create parameters here
            // parameters.addParameter(STR16("Parameter 1"), ...);
            // Example: Master Gain parameter
            // Parameter* gainParam = new RangeParameter(STR16("Master Gain"), kParamMasterGain, STR16("dB"),
            //                                           0.0, 1.0, 0.707, 0, ParameterInfo::kCanAutomate);
            // parameters.addParameter(gainParam);

            // Example: Sustain Pedal parameter (as a switch)
            // Parameter* sustainParam = new BoolParameter(STR16("Sustain"), kParamSustain, 0, ParameterInfo::kCanAutomate);
            // parameters.addParameter(sustainParam);
        }
        return result;
    }

    // TODO: Implement other IEditController methods:
    // getParameterCount, getParameterInfo, getParamStringByValue, getParamValueByString,
    // setParamNormalized, getParamNormalized, setComponentState, getState, setState, etc.
    // createView (if you have a custom UI)
};

} // namespace Vst
} // namespace Steinberg


BEGIN_FACTORY_DEF ("Dexed",
                   "https://github.com/asb2m10/dexed",
                   "support@dexed.com")
    DEF_CLASS2 (INLINE_UID_FROM_FUID(MyProcessorUID),
                PClassInfo::kManyInstances,
                kVstAudioEffectClass,
                "DexedVST3",
                Steinberg::Vst::kDistributable,
                "Instrument|Synth",
                "1.0.0",
                kVstVersionString,
                Steinberg::Vst::DexedProcessor::createInstance)
    DEF_CLASS2 (INLINE_UID_FROM_FUID(MyControllerUID),
                PClassInfo::kManyInstances,
                kVstComponentControllerClass,
                "DexedVST3Controller",
                0, 
                "", 
                "1.0.0",
                kVstVersionString,
                Steinberg::Vst::DexedController::createInstance)
END_FACTORY
