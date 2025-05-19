#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/iostream.h>
#include <string> // Added for std::string
#include <cstring>
#include "dexed.h"
#include <thread>
#include <atomic>
#include <vector>
#include <windows.h>
#include <mmsystem.h>
#include <cstdint> // Added to ensure uint8_t is defined

// Ensure abi3 ABI compatibility
#define PYBIND11_USE_SMART_HOLDER_AS_DEFAULT

namespace py = pybind11;

class PyDexed {
public:
    PyDexed(unsigned char max_notes, unsigned short sample_rate)
        : synth(new Dexed(max_notes, sample_rate)) {
        // std::cout << "[DEBUG] PyDexed initialized with max_notes: " << (int)max_notes
        //           << ", sample_rate: " << sample_rate << std::endl;
    }

    ~PyDexed() { delete synth; }

    void resetControllers() { synth->resetControllers(); }
    void setVelocityScale(unsigned char scale) { synth->setVelocityScale(scale); }
    void loadVoiceParameters(py::bytes sysex) {
        std::string s = sysex;
        if (!s.empty()) {
            std::vector<unsigned char> buf(s.begin(), s.end());
            synth->loadVoiceParameters(buf.data());
            // std::cout << "[DEBUG] Voice parameters loaded with size: " << buf.size() << std::endl;
        } else {
            // std::cout << "[DEBUG] Empty sysex provided to loadVoiceParameters." << std::endl;
        }
    }
    void setGain(float gain) {
        if (gain < 0.0f) gain = 0.0f;
        if (gain > 2.0f) gain = 2.0f;
        synth->setGain(gain);
    }
    float getGain() const { return synth->getGain(); } // Added getGain()
    void keydown(unsigned char note, unsigned char velocity) {
        // py::scoped_ostream_redirect stream(std::cout, py::module_::import("sys").attr("stdout"));
        // std::cout << "[DEBUG C++] PyDexed::keydown called with note: " << (int)note
        //           << ", velocity: " << (int)velocity << std::endl;
        synth->keydown(note, velocity);
        // std::cout << "[DEBUG C++] After synth->keydown, notes playing: " << synth->getNumNotesPlaying() << std::endl;
    }
    void keyup(unsigned char note) {
        // py::scoped_ostream_redirect stream(std::cout, py::module_::import("sys").attr("stdout"));
        // std::cout << "[DEBUG C++] PyDexed::keyup called with note: " << (int)note << std::endl;
        synth->keyup(note);
        // std::cout << "[DEBUG C++] After synth->keyup, notes playing: " << synth->getNumNotesPlaying() << std::endl;
    }
    py::array_t<short> getSamples(size_t frames_requested_mono) {
        // GIL is held by default upon entry from Python now.

        if (!synth) {
            // std::cout << "[DEBUG C++] PyDexed::getSamples - Synth not available. Returning empty buffer." << std::endl;
            auto result = py::array_t<short>(frames_requested_mono);
            std::fill_n(result.mutable_data(), frames_requested_mono, (short)0);
            return result;
        }

        std::vector<short> stereo_buffer(frames_requested_mono * 2);

        {
            py::gil_scoped_release release_gil_for_synth; // Release GIL for this block

            // Core C++ synth processing - no Python API calls here.
            synth->getSamples(stereo_buffer.data(), static_cast<uint16_t>(frames_requested_mono));
            
            // GIL is automatically reacquired when 'release_gil_for_synth' goes out of scope.
        }

        // GIL is now held again.
        // Prepare mono output buffer and create Python NumPy array.
        std::vector<short> mono_output_buffer(frames_requested_mono);
        for (size_t i = 0; i < frames_requested_mono; ++i) {
            mono_output_buffer[i] = stereo_buffer[i * 2]; // Left channel
        }

        return py::array_t<short>(py::buffer_info(
            mono_output_buffer.data(),            /* Pointer to buffer */
            sizeof(short),                        /* Size of one scalar */
            py::format_descriptor<short>::format(), /* Python struct-style format descriptor */
            1,                                    /* Number of dimensions */
            { frames_requested_mono },            /* Buffer dimensions */
            { sizeof(short) }                     /* Strides (in bytes) for each index */
        ));
    }

    void activate() { 
        // std::cout << "[DEBUG C++] PyDexed::activate() called." << std::endl;
        synth->activate(); 
        // std::cout << "[DEBUG C++] PyDexed::activate() finished." << std::endl;
    }
    void deactivate() { 
        // std::cout << "[DEBUG C++] PyDexed::deactivate() called." << std::endl;
        synth->deactivate(); 
        // std::cout << "[DEBUG C++] PyDexed::deactivate() finished." << std::endl;
    }
    bool getMonoMode() const { return synth->getMonoMode(); }
    void setMonoMode(bool mode) { synth->setMonoMode(mode); }
    void setNoteRefreshMode(bool mode) { synth->setNoteRefreshMode(mode); }
    uint8_t getMaxNotes() const { return synth->getMaxNotes(); }
    void doRefreshVoice() { synth->doRefreshVoice(); }

    // Exposed additional methods from dexed.h
    py::bytes getVoiceData() {
        uint8_t data_copy[155];
        synth->getVoiceData(data_copy);
        return py::bytes(reinterpret_cast<const char *>(data_copy), 155);
    }

    void setVoiceDataElement(uint8_t address, uint8_t value) {
        synth->setVoiceDataElement(address, value);
    }

    uint8_t getVoiceDataElement(uint8_t address) {
        return synth->getVoiceDataElement(address);
    }

    void loadInitVoice() { synth->loadInitVoice(); }
    int getNumNotesPlaying() { return synth->getNumNotesPlaying(); }
    double getXRun() { return synth->getXRun(); }
    double getRenderTimeMax() { return synth->getRenderTimeMax(); }
    void resetRenderTimeMax() { synth->resetRenderTimeMax(); }
    void ControllersRefresh() { synth->ControllersRefresh(); }
    void setEngineType(int type) { synth->setEngineType(type); }
    int getEngineType() { return synth->getEngineType(); }
    bool checkSystemExclusive(py::bytes sysex) {
        std::string s = sysex;
        std::vector<uint8_t> buf(s.begin(), s.end());
        return synth->checkSystemExclusive(buf.data(), static_cast<uint16_t>(buf.size()));
    }

    void debugSynthState() {
        // py::scoped_ostream_redirect stream(std::cout, py::module_::import("sys").attr("stdout"));
        std::cout << "[DEBUG C++] Synth state: " << std::endl;
        std::cout << "  Max notes (C++): " << static_cast<int>(synth->getMaxNotes()) << std::endl;
        std::cout << "  Mono mode (C++): " << (synth->getMonoMode() ? "Enabled" : "Disabled") << std::endl;
        int notesPlayingVal = synth->getNumNotesPlaying(); // Get the value
        std::cout << "  Notes playing (C++) (int): " << notesPlayingVal << std::endl; // Print the value
        std::cout << "  Render time max (C++): " << synth->getRenderTimeMax() << " ms" << std::endl;
        std::cout << "  XRun (C++): " << synth->getXRun() << std::endl;
    }

    void debugVoiceParameters() {
        uint8_t voiceData[155];
        synth->getVoiceData(voiceData);
        std::cout << "[DEBUG] Voice parameters: ";
        for (int i = 0; i < 155; ++i) {
            std::cout << (int)voiceData[i] << " ";
        }
        std::cout << std::endl;
    }

    void fillSamples(py::array_t<short, py::array::c_style> out_buffer) {
        py::buffer_info buf = out_buffer.request();
        if (buf.ndim != 2 || buf.shape[1] != 2 || buf.itemsize != sizeof(short)) {
            throw std::runtime_error("Output buffer must be shape (frames, 2), dtype int16");
        }
        size_t frames = buf.shape[0];
        short* data = static_cast<short*>(buf.ptr);

        if (!synth) {
            std::memset(data, 0, frames * 2 * sizeof(short));
            return;
        }
        {
            py::gil_scoped_release release;
            synth->getSamples(data, static_cast<uint16_t>(frames)); // Fills interleaved stereo
        }
    }

    // New methods to match dexed API:
    void setSustain(bool sustain) { synth->setSustain(sustain); }
    bool getSustain() const { return synth->getSustain(); }
    void setSostenuto(bool sostenuto) { synth->setSostenuto(sostenuto); }
    bool getSostenuto() const { return synth->getSostenuto(); }
    void panic() { synth->panic(); }
    void notesOff() { synth->notesOff(); }
    void setMasterTune(int8_t tune) { synth->setMasterTune(tune); }
    int8_t getMasterTune() const { return synth->getMasterTune(); }
    void setPortamento(uint8_t mode, uint8_t glissando, uint8_t time) {
        synth->setPortamento(mode, glissando, time);
    }
    void setPortamentoMode(int mode) { synth->setPortamentoMode(mode); }
    int getPortamentoMode() const { return synth->getPortamentoMode(); }
    void setPortamentoGlissando(bool glissando) { synth->setPortamentoGlissando(glissando); }
    bool getPortamentoGlissando() const { return synth->getPortamentoGlissando(); }
    void setPortamentoTime(uint8_t time) { synth->setPortamentoTime(time); }
    uint8_t getPortamentoTime() const { return synth->getPortamentoTime(); }
    void setPitchbendRange(uint8_t range) { synth->setPitchbendRange(range); }
    uint8_t getPitchbendRange() const { return synth->getPitchbendRange(); }
    void setPitchbendStep(uint8_t step) { synth->setPitchbendStep(step); }
    uint8_t getPitchbendStep() const { return synth->getPitchbendStep(); }

    // --- Begin: Forward all required methods to synth for Python bindings ---
    // Controller and parameter methods
    void setAftertouch(uint8_t value) { synth->setAftertouch(value); }
    uint8_t getAftertouch() const { return synth->getAftertouch(); }
    void setAftertouchRange(uint8_t range) { synth->setAftertouchRange(range); }
    uint8_t getAftertouchRange() const { return synth->getAftertouchRange(); }
    void setAftertouchTarget(uint8_t target) { synth->setAftertouchTarget(target); }
    uint8_t getAftertouchTarget() const { return synth->getAftertouchTarget(); }

    void setBreathController(uint8_t value) { synth->setBreathController(value); }
    uint8_t getBreathController() const { return synth->getBreathController(); }
    void setBreathControllerRange(uint8_t range) { synth->setBreathControllerRange(range); }
    uint8_t getBreathControllerRange() const { return synth->getBreathControllerRange(); }
    void setBreathControllerTarget(uint8_t target) { synth->setBreathControllerTarget(target); }
    uint8_t getBreathControllerTarget() const { return synth->getBreathControllerTarget(); }

    void setFootController(uint8_t value) { synth->setFootController(value); }
    uint8_t getFootController() const { return synth->getFootController(); }
    void setFootControllerRange(uint8_t range) { synth->setFootControllerRange(range); }
    uint8_t getFootControllerRange() const { return synth->getFootControllerRange(); }
    void setFootControllerTarget(uint8_t target) { synth->setFootControllerTarget(target); }
    uint8_t getFootControllerTarget() const { return synth->getFootControllerTarget(); }

    void setModWheel(uint8_t value) { synth->setModWheel(value); }
    uint8_t getModWheel() const { return synth->getModWheel(); }
    void setModWheelRange(uint8_t range) { synth->setModWheelRange(range); }
    uint8_t getModWheelRange() const { return synth->getModWheelRange(); }
    void setModWheelTarget(uint8_t target) { synth->setModWheelTarget(target); }
    uint8_t getModWheelTarget() const { return synth->getModWheelTarget(); }
    void setOPRate(uint8_t op, uint8_t step, uint8_t rate) { synth->setOPRate(op, step, rate); }
    uint8_t getOPRate(uint8_t op, uint8_t step) const { return synth->getOPRate(op, step); }
    void setOPLevel(uint8_t op, uint8_t step, uint8_t level) { synth->setOPLevel(op, step, level); }
    uint8_t getOPLevel(uint8_t op, uint8_t step) const { return synth->getOPLevel(op, step); }
    void setOPAll(uint8_t ops) { synth->setOPAll(ops); }
    void setOPRateAll(uint8_t rate) { synth->setOPRateAll(rate); }
    void setOPLevelAll(uint8_t level) { synth->setOPLevelAll(level); }
    void setOPRateAllCarrier(uint8_t step, uint8_t rate) { synth->setOPRateAllCarrier(step, rate); }
    void setOPLevelAllCarrier(uint8_t step, uint8_t level) { synth->setOPLevelAllCarrier(step, level); }
    void setOPRateAllModulator(uint8_t step, uint8_t rate) { synth->setOPRateAllModulator(step, rate); }
    void setOPLevelAllModulator(uint8_t step, uint8_t level) { synth->setOPLevelAllModulator(step, level); }
    void setPBController(uint8_t pb_range, uint8_t pb_step) { synth->setPBController(pb_range, pb_step); }
    void setMWController(uint8_t mw_range, uint8_t mw_assign, uint8_t mw_mode) { synth->setMWController(mw_range, mw_assign, mw_mode); }
    void setFCController(uint8_t fc_range, uint8_t fc_assign, uint8_t fc_mode) { synth->setFCController(fc_range, fc_assign, fc_mode); }
    void setBCController(uint8_t bc_range, uint8_t bc_assign, uint8_t bc_mode) { synth->setBCController(bc_range, bc_assign, bc_mode); }
    void setATController(uint8_t at_range, uint8_t at_assign, uint8_t at_mode) { synth->setATController(at_range, at_assign, at_mode); }
    void setPitchbend(uint8_t value1, uint8_t value2) { synth->setPitchbend(value1, value2); }
    void setOPAmpModulationSensity(uint8_t op, uint8_t value) { synth->setOPAmpModulationSensity(op, value); }
    uint8_t getOPAmpModulationSensity(uint8_t op) const { return synth->getOPAmpModulationSensity(op); }
    void setOPKeyboardVelocitySensity(uint8_t op, uint8_t value) { synth->setOPKeyboardVelocitySensity(op, value); }
    uint8_t getOPKeyboardVelocitySensity(uint8_t op) const { return synth->getOPKeyboardVelocitySensity(op); }
    void setOPOutputLevel(uint8_t op, uint8_t value) { synth->setOPOutputLevel(op, value); }
    uint8_t getOPOutputLevel(uint8_t op) const { return synth->getOPOutputLevel(op); }
    void setOPMode(uint8_t op, uint8_t value) { synth->setOPMode(op, value); }
    uint8_t getOPMode(uint8_t op) const { return synth->getOPMode(op); }
    void setOPFrequencyCoarse(uint8_t op, uint8_t value) { synth->setOPFrequencyCoarse(op, value); }
    uint8_t getOPFrequencyCoarse(uint8_t op) const { return synth->getOPFrequencyCoarse(op); }
    void setOPFrequencyFine(uint8_t op, uint8_t value) { synth->setOPFrequencyFine(op, value); }
    uint8_t getOPFrequencyFine(uint8_t op) const { return synth->getOPFrequencyFine(op); }
    void setOPDetune(uint8_t op, uint8_t value) { synth->setOPDetune(op, value); }
    uint8_t getOPDetune(uint8_t op) const { return synth->getOPDetune(op); }
    void setOPKeyboardLevelScalingBreakPoint(uint8_t op, uint8_t value) { synth->setOPKeyboardLevelScalingBreakPoint(op, value); }
    uint8_t getOPKeyboardLevelScalingBreakPoint(uint8_t op) const { return synth->getOPKeyboardLevelScalingBreakPoint(op); }
    void setOPKeyboardLevelScalingDepthLeft(uint8_t op, uint8_t value) { synth->setOPKeyboardLevelScalingDepthLeft(op, value); }
    uint8_t getOPKeyboardLevelScalingDepthLeft(uint8_t op) const { return synth->getOPKeyboardLevelScalingDepthLeft(op); }
    void setOPKeyboardLevelScalingDepthRight(uint8_t op, uint8_t value) { synth->setOPKeyboardLevelScalingDepthRight(op, value); }
    uint8_t getOPKeyboardLevelScalingDepthRight(uint8_t op) const { return synth->getOPKeyboardLevelScalingDepthRight(op); }
    void setOPKeyboardLevelScalingCurveLeft(uint8_t op, uint8_t value) { synth->setOPKeyboardLevelScalingCurveLeft(op, value); }
    uint8_t getOPKeyboardLevelScalingCurveLeft(uint8_t op) const { return synth->getOPKeyboardLevelScalingCurveLeft(op); }
    void setOPKeyboardLevelScalingCurveRight(uint8_t op, uint8_t value) { synth->setOPKeyboardLevelScalingCurveRight(op, value); }
    uint8_t getOPKeyboardLevelScalingCurveRight(uint8_t op) const { return synth->getOPKeyboardLevelScalingCurveRight(op); }
    void setOPKeyboardRateScale(uint8_t op, uint8_t value) { synth->setOPKeyboardRateScale(op, value); }
    uint8_t getOPKeyboardRateScale(uint8_t op) const { return synth->getOPKeyboardRateScale(op); }
    void setPitchRate(uint8_t step, uint8_t value) { synth->setPitchRate(step, value); }
    uint8_t getPitchRate(uint8_t step) const { return synth->getPitchRate(step); }
    void setPitchLevel(uint8_t step, uint8_t value) { synth->setPitchLevel(step, value); }
    uint8_t getPitchLevel(uint8_t step) const { return synth->getPitchLevel(step); }
    void setAlgorithm(uint8_t value) { synth->setAlgorithm(value); }
    uint8_t getAlgorithm() const { return synth->getAlgorithm(); }
    void setFeedback(uint8_t value) { synth->setFeedback(value); }
    uint8_t getFeedback() const { return synth->getFeedback(); }
    void setOscillatorSync(bool value) { synth->setOscillatorSync(value); }
    bool getOscillatorSync() const { return synth->getOscillatorSync(); }
    void setLFOSpeed(uint8_t value) { synth->setLFOSpeed(value); }
    uint8_t getLFOSpeed() const { return synth->getLFOSpeed(); }
    void setLFODelay(uint8_t value) { synth->setLFODelay(value); }
    uint8_t getLFODelay() const { return synth->getLFODelay(); }
    void setLFOPitchModulationDepth(uint8_t value) { synth->setLFOPitchModulationDepth(value); }
    uint8_t getLFOPitchModulationDepth() const { return synth->getLFOPitchModulationDepth(); }
    void setLFOSync(bool value) { synth->setLFOSync(value); }
    bool getLFOSync() const { return synth->getLFOSync(); }
    void setLFOWaveform(uint8_t value) { synth->setLFOWaveform(value); }
    uint8_t getLFOWaveform() const { return synth->getLFOWaveform(); }
    void setLFOPitchModulationSensitivity(uint8_t value) { synth->setLFOPitchModulationSensitivity(value); }
    uint8_t getLFOPitchModulationSensitivity() const { return synth->getLFOPitchModulationSensitivity(); }
    void setTranspose(int8_t value) { synth->setTranspose(value); }
    int8_t getTranspose() const { return synth->getTranspose(); }
    void setName(const std::string& name) {
        char buf[11] = {0};
        size_t len = name.copy(buf, sizeof(buf) - 1);
        buf[len] = '\0';
        synth->setName(buf);
    }
    std::string getName() const {
        char buf[11] = {0};
        synth->getName(buf);
        return std::string(buf);
    }

    // --- End: Operator and synth parameter methods ---

private:
    Dexed* synth;
    float gain_ = 1.0f; // Added gain_ declaration
};

class DexedHost {
private:
    static const int NUM_BUFFERS = 4;
    Dexed* synth;
    std::atomic<bool> running;
    std::mutex audio_mutex;
#ifdef _WIN32
    HWAVEOUT hWaveOut;
    int audio_device;
    std::thread audio_thread;
    WAVEHDR waveHeaders[NUM_BUFFERS];
    std::vector<std::vector<short>> audioBuffers;
#endif
public:
    DexedHost(uint8_t max_notes, uint16_t sample_rate, int audio_device = 0)
        : synth(new Dexed(max_notes, sample_rate)), running(false)
#ifdef _WIN32
        , hWaveOut(nullptr), audio_device(audio_device), audioBuffers(NUM_BUFFERS)
#endif
    {
#ifdef _WIN32
        // Prepare audio buffers
        for (int i = 0; i < NUM_BUFFERS; ++i) {
            audioBuffers[i].resize(2048 * 2); // stereo
            waveHeaders[i] = {};
            waveHeaders[i].lpData = reinterpret_cast<LPSTR>(audioBuffers[i].data());
            waveHeaders[i].dwBufferLength = 2048 * 2 * sizeof(short);
        }
#endif
    }

    ~DexedHost() {
        std::cout << "[C++] DexedHost destructor: calling stop_audio()" << std::endl;
        stop_audio();
        std::lock_guard<std::mutex> lock(audio_mutex);
        std::cout << "[C++] DexedHost destructor: deleting synth" << std::endl;
        delete synth;
        synth = nullptr;
        std::cout << "[C++] DexedHost destructor: done" << std::endl;
    }

    void start_audio() {
#ifdef _WIN32
        if (running) return;
        WAVEFORMATEX wfx = {};
        wfx.wFormatTag = WAVE_FORMAT_PCM;
        wfx.nChannels = 2;
        wfx.nSamplesPerSec = 48000;
        wfx.wBitsPerSample = 16;
        wfx.nBlockAlign = wfx.nChannels * wfx.wBitsPerSample / 8;
        wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
        wfx.cbSize = 0;
        if (waveOutOpen(&hWaveOut, audio_device, &wfx, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
            hWaveOut = nullptr;
            return;
        }
        for (int i = 0; i < NUM_BUFFERS; ++i) {
            waveOutPrepareHeader(hWaveOut, &waveHeaders[i], sizeof(WAVEHDR));
        }
        running = true;
        audio_thread = std::thread(&DexedHost::audio_loop, this);
#endif
    }

    void stop_audio() {
#ifdef _WIN32
        std::cout << "[C++] DexedHost::stop_audio: begin" << std::endl;
        running = false;
        if (audio_thread.joinable()) {
            std::cout << "[C++] DexedHost::stop_audio: joining audio_thread" << std::endl;
            audio_thread.join(); // Wait for thread to finish
        }
        std::lock_guard<std::mutex> lock(audio_mutex);
        if (hWaveOut) {
            waveOutReset(hWaveOut);
            for (int i = 0; i < NUM_BUFFERS; ++i)
                waveOutUnprepareHeader(hWaveOut, &waveHeaders[i], sizeof(WAVEHDR));
            waveOutClose(hWaveOut);
            hWaveOut = nullptr;
        }
        std::cout << "[C++] DexedHost::stop_audio: end" << std::endl;
#endif
    }

    void keydown(uint8_t note, uint8_t velocity) { synth->keydown(note, velocity); }
    void keyup(uint8_t note) { synth->keyup(note); }
    void setGain(float gain) { synth->setGain(gain); }
    void resetControllers() { synth->resetControllers(); }
    void setVelocityScale(unsigned char scale) { synth->setVelocityScale(scale); }
    void setMonoMode(bool mode) { synth->setMonoMode(mode); }
    void setNoteRefreshMode(bool mode) { synth->setNoteRefreshMode(mode); }
    uint8_t getMaxNotes() const { return synth->getMaxNotes(); }
    void doRefreshVoice() { synth->doRefreshVoice(); }
    void loadVoiceParameters(py::bytes sysex) {
        std::string s = sysex;
        if (!s.empty()) synth->loadVoiceParameters(reinterpret_cast<uint8_t*>(s.data()));
    }
    void setEngineType(int type) { synth->setEngineType(type); }
    int getEngineType() { return synth->getEngineType(); }
    void loadInitVoice() { synth->loadInitVoice(); }

#ifdef _WIN32
private:
    void audio_loop() {
        try {
            std::vector<int16_t> monoBuffer(2048); // Use the same size as your class buffer
            int bufferIndex = 0;
            while (true) {
                std::lock_guard<std::mutex> lock(audio_mutex);
                if (!running || !synth) {
                    std::cout << "[C++] DexedHost::audio_loop: exiting thread (early)" << std::endl;
                    return;
                }
                WAVEHDR& hdr = waveHeaders[bufferIndex];
                // Wait for buffer to be done (WHDR_INQUEUE cleared)
                while (hdr.dwFlags & WHDR_INQUEUE) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    if (!running) return;
                }
                synth->getSamples(monoBuffer.data(), 2048);
                for (int i = 0; i < 2048; ++i) {
                    audioBuffers[bufferIndex][2*i] = monoBuffer[i];
                    audioBuffers[bufferIndex][2*i+1] = monoBuffer[i];
                }
                waveOutWrite(hWaveOut, &hdr, sizeof(WAVEHDR));
                bufferIndex = (bufferIndex + 1) % NUM_BUFFERS;
            }
            std::cout << "[C++] DexedHost::audio_loop: thread finished" << std::endl;
        } catch (const std::exception& ex) {
            std::cerr << "[C++] DexedHost::audio_loop: exception: " << ex.what() << std::endl;
        } catch (...) {
            std::cerr << "[C++] DexedHost::audio_loop: unknown exception" << std::endl;
        }
    }
#endif
};

PYBIND11_MODULE(dexed_py, m) {
    py::class_<PyDexed>(m, "Dexed")
        .def(py::init<unsigned char, unsigned short>())
        .def("resetControllers", &PyDexed::resetControllers)
        .def("setVelocityScale", &PyDexed::setVelocityScale)
        .def("loadVoiceParameters", &PyDexed::loadVoiceParameters)
        .def("setGain", &PyDexed::setGain)
        .def("getGain", &PyDexed::getGain)
        .def("keydown", &PyDexed::keydown, py::call_guard<py::gil_scoped_release>())
        .def("keyup", &PyDexed::keyup, py::call_guard<py::gil_scoped_release>())
        .def("getSamples", &PyDexed::getSamples) // Removed py::call_guard here
        .def("activate", &PyDexed::activate)
        .def("deactivate", &PyDexed::deactivate)
        .def("getMonoMode", &PyDexed::getMonoMode)
        .def("setMonoMode", &PyDexed::setMonoMode)
        .def("setNoteRefreshMode", &PyDexed::setNoteRefreshMode)
        .def("getMaxNotes", &PyDexed::getMaxNotes)
        .def("doRefreshVoice", &PyDexed::doRefreshVoice)
        .def("getVoiceData", [](PyDexed& self) { return self.getVoiceData(); })
        .def("setVoiceDataElement", &PyDexed::setVoiceDataElement)
        .def("getVoiceDataElement", &PyDexed::getVoiceDataElement)
        .def("loadInitVoice", &PyDexed::loadInitVoice)
        .def("getNumNotesPlaying", &PyDexed::getNumNotesPlaying)
        .def("getXRun", &PyDexed::getXRun)
        .def("getRenderTimeMax", &PyDexed::getRenderTimeMax)
        .def("resetRenderTimeMax", &PyDexed::resetRenderTimeMax)
        .def("ControllersRefresh", &PyDexed::ControllersRefresh)
        .def("setEngineType", &PyDexed::setEngineType)
        .def("getEngineType", &PyDexed::getEngineType)
        .def("checkSystemExclusive", &PyDexed::checkSystemExclusive)
        .def("debugSynthState", &PyDexed::debugSynthState)
        .def("debugVoiceParameters", &PyDexed::debugVoiceParameters)
        .def("fillSamples", &PyDexed::fillSamples)
        .def("setSustain", &PyDexed::setSustain)
        .def("getSustain", &PyDexed::getSustain)
        .def("setSostenuto", &PyDexed::setSostenuto)
        .def("getSostenuto", &PyDexed::getSostenuto)
        .def("panic", &PyDexed::panic)
        .def("notesOff", &PyDexed::notesOff)
        .def("setMasterTune", &PyDexed::setMasterTune)
        .def("getMasterTune", &PyDexed::getMasterTune)
        .def("setPortamento", &PyDexed::setPortamento,
             py::arg("mode"), py::arg("glissando"), py::arg("time"))
        .def("setPortamentoMode", &PyDexed::setPortamentoMode)
        .def("getPortamentoMode", &PyDexed::getPortamentoMode)
        .def("setPortamentoGlissando", &PyDexed::setPortamentoGlissando)
        .def("getPortamentoGlissando", &PyDexed::getPortamentoGlissando)
        .def("setPortamentoTime", &PyDexed::setPortamentoTime)
        .def("getPortamentoTime", &PyDexed::getPortamentoTime)
        .def("setPitchbendRange", &PyDexed::setPitchbendRange)
        .def("getPitchbendRange", &PyDexed::getPitchbendRange)
        .def("setPitchbendStep", &PyDexed::setPitchbendStep)
        .def("getPitchbendStep", &PyDexed::getPitchbendStep)
        .def("setOPAmpModulationSensity", &PyDexed::setOPAmpModulationSensity)
        .def("getOPAmpModulationSensity", [](const PyDexed& self, uint8_t op) { return static_cast<int>(self.getOPAmpModulationSensity(op)); })
        .def("setOPKeyboardVelocitySensity", &PyDexed::setOPKeyboardVelocitySensity)
        .def("getOPKeyboardVelocitySensity", [](const PyDexed& self, uint8_t op) { return static_cast<int>(self.getOPKeyboardVelocitySensity(op)); })
        .def("setOPOutputLevel", &PyDexed::setOPOutputLevel)
        .def("getOPOutputLevel", [](const PyDexed& self, uint8_t op) { return static_cast<int>(self.getOPOutputLevel(op)); })
        .def("setOPMode", &PyDexed::setOPMode)
        .def("getOPMode", [](const PyDexed& self, uint8_t op) { return static_cast<int>(self.getOPMode(op)); })
        .def("setOPFrequencyCoarse", &PyDexed::setOPFrequencyCoarse)
        .def("getOPFrequencyCoarse", [](const PyDexed& self, uint8_t op) { return static_cast<int>(self.getOPFrequencyCoarse(op)); })
        .def("setOPFrequencyFine", &PyDexed::setOPFrequencyFine)
        .def("getOPFrequencyFine", [](const PyDexed& self, uint8_t op) { return static_cast<int>(self.getOPFrequencyFine(op)); })
        .def("setOPDetune", &PyDexed::setOPDetune)
        .def("getOPDetune", [](const PyDexed& self, uint8_t op) { return static_cast<int>(self.getOPDetune(op)); })
        .def("setOPKeyboardLevelScalingBreakPoint", &PyDexed::setOPKeyboardLevelScalingBreakPoint)
        .def("getOPKeyboardLevelScalingBreakPoint", [](const PyDexed& self, uint8_t op) { return static_cast<int>(self.getOPKeyboardLevelScalingBreakPoint(op)); })
        .def("setOPKeyboardLevelScalingDepthLeft", &PyDexed::setOPKeyboardLevelScalingDepthLeft)
        .def("getOPKeyboardLevelScalingDepthLeft", [](const PyDexed& self, uint8_t op) { return static_cast<int>(self.getOPKeyboardLevelScalingDepthLeft(op)); })
        .def("setOPKeyboardLevelScalingDepthRight", &PyDexed::setOPKeyboardLevelScalingDepthRight)
        .def("getOPKeyboardLevelScalingDepthRight", [](const PyDexed& self, uint8_t op) { return static_cast<int>(self.getOPKeyboardLevelScalingDepthRight(op)); })
        .def("setOPKeyboardLevelScalingCurveLeft", &PyDexed::setOPKeyboardLevelScalingCurveLeft)
        .def("getOPKeyboardLevelScalingCurveLeft", [](const PyDexed& self, uint8_t op) { return static_cast<int>(self.getOPKeyboardLevelScalingCurveLeft(op)); })
        .def("setOPKeyboardLevelScalingCurveRight", &PyDexed::setOPKeyboardLevelScalingCurveRight)
        .def("getOPKeyboardLevelScalingCurveRight", [](const PyDexed& self, uint8_t op) { return static_cast<int>(self.getOPKeyboardLevelScalingCurveRight(op)); })
        .def("setOPKeyboardRateScale", &PyDexed::setOPKeyboardRateScale)
        .def("getOPKeyboardRateScale", [](const PyDexed& self, uint8_t op) { return static_cast<int>(self.getOPKeyboardRateScale(op)); })
        .def("setPitchRate", &PyDexed::setPitchRate)
        .def("getPitchRate", [](const PyDexed& self, uint8_t step) { return static_cast<int>(self.getPitchRate(step)); })
        .def("setPitchLevel", &PyDexed::setPitchLevel)
        .def("getPitchLevel", [](const PyDexed& self, uint8_t step) { return static_cast<int>(self.getPitchLevel(step)); })
        .def("setAlgorithm", [](PyDexed &self, uint8_t algorithm) { self.setAlgorithm(algorithm); }, py::arg("algorithm"))
        .def("getAlgorithm", [](const PyDexed &self) { return static_cast<int>(self.getAlgorithm()); })
        .def("setFeedback", &PyDexed::setFeedback)
        .def("getFeedback", [](const PyDexed& self) { return static_cast<int>(self.getFeedback()); })
        .def("setOscillatorSync", &PyDexed::setOscillatorSync)
        .def("getOscillatorSync", &PyDexed::getOscillatorSync)
        .def("setLFOSpeed", &PyDexed::setLFOSpeed)
        .def("getLFOSpeed", [](const PyDexed& self) { return static_cast<int>(self.getLFOSpeed()); })
        .def("setLFODelay", &PyDexed::setLFODelay)
        .def("getLFODelay", [](const PyDexed& self) { return static_cast<int>(self.getLFODelay()); })
        .def("setLFOPitchModulationDepth", &PyDexed::setLFOPitchModulationDepth)
        .def("getLFOPitchModulationDepth", [](const PyDexed& self) { return static_cast<int>(self.getLFOPitchModulationDepth()); })
        .def("setLFOSync", &PyDexed::setLFOSync)
        .def("getLFOSync", &PyDexed::getLFOSync)
        .def("setLFOWaveform", &PyDexed::setLFOWaveform)
        .def("getLFOWaveform", [](const PyDexed& self) { return static_cast<int>(self.getLFOWaveform()); })
        .def("setLFOPitchModulationSensitivity", &PyDexed::setLFOPitchModulationSensitivity)
        .def("getLFOPitchModulationSensitivity", [](const PyDexed& self) { return static_cast<int>(self.getLFOPitchModulationSensitivity()); })
        .def("setTranspose", &PyDexed::setTranspose)
        .def("getTranspose", [](const PyDexed& self) { return static_cast<int>(self.getTranspose()); })
        .def("setName", &PyDexed::setName)
        .def("getName", &PyDexed::getName, "Gets the voice name") // New binding
        .def("setAftertouch", &PyDexed::setAftertouch)
        .def("getAftertouch", &PyDexed::getAftertouch)
        .def("setAftertouchRange", &PyDexed::setAftertouchRange)
        .def("getAftertouchRange", &PyDexed::getAftertouchRange)
        .def("setAftertouchTarget", &PyDexed::setAftertouchTarget)
        .def("getAftertouchTarget", &PyDexed::getAftertouchTarget)
        .def("setBreathController", &PyDexed::setBreathController)
        .def("getBreathController", &PyDexed::getBreathController)
        .def("setBreathControllerRange", &PyDexed::setBreathControllerRange)
        .def("getBreathControllerRange", &PyDexed::getBreathControllerRange)
        .def("setBreathControllerTarget", &PyDexed::setBreathControllerTarget)
        .def("getBreathControllerTarget", &PyDexed::getBreathControllerTarget)
        .def("setFootController", &PyDexed::setFootController)
        .def("getFootController", &PyDexed::getFootController)
        .def("setFootControllerRange", &PyDexed::setFootControllerRange)
        .def("getFootControllerRange", &PyDexed::getFootControllerRange)
        .def("setFootControllerTarget", &PyDexed::setFootControllerTarget)
        .def("getFootControllerTarget", &PyDexed::getFootControllerTarget)
        .def("setModWheel", &PyDexed::setModWheel)
        .def("getModWheel", &PyDexed::getModWheel)
        .def("setModWheelRange", &PyDexed::setModWheelRange)
        .def("getModWheelRange", &PyDexed::getModWheelRange)
        .def("setModWheelTarget", &PyDexed::setModWheelTarget)
        .def("getModWheelTarget", &PyDexed::getModWheelTarget)
        .def("setOPRate", &PyDexed::setOPRate)
        .def("getOPRate", &PyDexed::getOPRate)
        .def("setOPLevel", &PyDexed::setOPLevel)
        .def("getOPLevel", &PyDexed::getOPLevel)
        .def("setOPAll", &PyDexed::setOPAll)
        .def("setOPRateAll", &PyDexed::setOPRateAll)
        .def("setOPLevelAll", &PyDexed::setOPLevelAll)
        .def("setOPRateAllCarrier", &PyDexed::setOPRateAllCarrier)
        .def("setOPLevelAllCarrier", &PyDexed::setOPLevelAllCarrier)
        .def("setOPRateAllModulator", &PyDexed::setOPRateAllModulator)
        .def("setOPLevelAllModulator", &PyDexed::setOPLevelAllModulator)
        .def("setPBController", &PyDexed::setPBController)
        .def("setMWController", &PyDexed::setMWController)
        .def("setFCController", &PyDexed::setFCController)
        .def("setBCController", &PyDexed::setBCController)
        .def("setATController", &PyDexed::setATController)
        ;

    py::class_<DexedHost>(m, "DexedHost")
        .def(py::init<uint8_t, uint16_t, int>(), py::arg("max_notes"), py::arg("sample_rate"), py::arg("audio_device") = 0)
        .def("start_audio", &DexedHost::start_audio)
        .def("stop_audio", &DexedHost::stop_audio)
        .def("keydown", &DexedHost::keydown)
        .def("keyup", &DexedHost::keyup)
        .def("setGain", &DexedHost::setGain)
        .def("resetControllers", &DexedHost::resetControllers)
        .def("setVelocityScale", &DexedHost::setVelocityScale)
        .def("setMonoMode", &DexedHost::setMonoMode)
        .def("setNoteRefreshMode", &DexedHost::setNoteRefreshMode)
        .def("getMaxNotes", &DexedHost::getMaxNotes)
        .def("doRefreshVoice", &DexedHost::doRefreshVoice)
        .def("loadVoiceParameters", &DexedHost::loadVoiceParameters)
        .def("setEngineType", &DexedHost::setEngineType)
        .def("getEngineType", &DexedHost::getEngineType)
        .def("loadInitVoice", &DexedHost::loadInitVoice)
        ;

#ifdef USE_COMPRESSOR
    m.def("setCompDownsample", &PyDexed::setCompDownsample);
    m.def("getCompDownsample", &PyDexed::getCompDownsample);
    m.def("setCompAttack", &PyDexed::setCompAttack);
    m.def("getCompAttack", &PyDexed::getCompAttack);
    m.def("setCompRelease", &PyDexed::setCompRelease);
    m.def("getCompRelease", &PyDexed::getCompRelease);
    m.def("setCompRatio", &PyDexed::setCompRatio);
    m.def("getCompRatio", &PyDexed::getCompRatio);
    m.def("setCompKnee", &PyDexed::setCompKnee);
    m.def("getCompKnee", &PyDexed::getCompKnee);
    m.def("setCompThreshold", &PyDexed::setCompThreshold);
    m.def("getCompThreshold", &PyDexed::getCompThreshold);
    m.def("setCompMakeupGain", &PyDexed::setCompMakeupGain);
    m.def("getCompMakeupGain", &PyDexed::getCompMakeupGain);
    m.def("setCompEnable", &PyDexed::setCompEnable);
    m.def("getCompEnable", &PyDexed::getCompEnable);
#endif
}