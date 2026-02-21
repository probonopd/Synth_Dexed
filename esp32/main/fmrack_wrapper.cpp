/*
 * FMRack ESP32-S3 Port - Engine Wrapper Implementation
 *
 * Bridges the ESP32-S3 platform code to the C++ FMRack engine.
 * All FMRack C++ objects are managed here.
 *
 * Key adaptations for ESP32-S3:
 * - Rack and modules are allocated in PSRAM via placement new
 * - Multiprocessing enabled (ESP32-S3 is dual-core Xtensa LX7)
 * - std::filesystem calls are replaced with POSIX file ops
 * - 8 modules by default (dual-core + 240 MHz headroom)
 */

#include "fmrack_wrapper.h"
#include "esp32_config.h"

// FMRack engine headers
#include "Rack.h"
#include "Module.h"
#include "Performance.h"
#include "VoiceData.h"
#include "Debug.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <memory>
#include <vector>
#include <string>
#include <cstring>

static const char *TAG = "fmrack_engine";

// Global debug flag (required by FMRack Debug.h)
bool debugEnabled = false;

// Multiprocessing flag (required by FMRack Debug.h)
// Disabled: the engine's internal std::thread-based multiprocessing is not
// suitable for FreeRTOS.  We achieve dual-core operation by pinning the
// audio task to core 1 and protocol tasks to core 0 instead.
int multiprocessingEnabled = 0;

// The FMRack engine instance
static FMRack::Rack *s_rack = nullptr;

// Mutex for thread-safe MIDI message handling
static SemaphoreHandle_t s_midi_mutex = NULL;

// Performance directory for program changes
static std::string s_performance_dir;

int fmrack_init(int sample_rate, int num_modules)
{
    ESP_LOGI(TAG, "Initializing FMRack engine...");
    ESP_LOGI(TAG, "  Sample rate: %d Hz", sample_rate);
    ESP_LOGI(TAG, "  Modules: %d", num_modules);

    // Create MIDI mutex
    s_midi_mutex = xSemaphoreCreateMutex();
    if (!s_midi_mutex) {
        ESP_LOGE(TAG, "Failed to create MIDI mutex");
        return -1;
    }

    // Allocate Rack in PSRAM
    void *rack_mem = heap_caps_malloc(sizeof(FMRack::Rack), MALLOC_CAP_SPIRAM);
    if (!rack_mem) {
        ESP_LOGW(TAG, "PSRAM allocation failed for Rack, using internal memory");
        rack_mem = malloc(sizeof(FMRack::Rack));
        if (!rack_mem) {
            ESP_LOGE(TAG, "Failed to allocate Rack");
            return -1;
        }
    }

    // Construct Rack in-place
    s_rack = new (rack_mem) FMRack::Rack(static_cast<float>(sample_rate));

    if (!s_rack->isInitialized()) {
        ESP_LOGE(TAG, "Rack initialization failed");
        s_rack->~Rack();
        heap_caps_free(rack_mem);
        s_rack = nullptr;
        return -1;
    }

    // Set up default performance with configured number of modules
    FMRack::Performance perf;
    perf.setDefaults(num_modules, 1);  // 1 unison voice per module (ESP32 CPU limit)
    for (int i = 0; i < 16; i++) {
        if (i < num_modules) {
            perf.parts[i].midiChannel = static_cast<uint8_t>(i + 1);
            perf.parts[i].volume = 100;
            perf.parts[i].unisonVoices = 1;  // No unison on ESP32 to save CPU
            // Dexed uses voiceData[155] as the operator-enable bitmask (OPE).
            // The default Performance init voice leaves this at 0, which disables all ops -> silence.
            perf.parts[i].voiceData[155] = 0x3F;
        } else {
            perf.parts[i].midiChannel = 0;
        }
    }
    s_rack->setPerformance(perf);

    ESP_LOGI(TAG, "FMRack engine initialized with %d modules", num_modules);
    ESP_LOGI(TAG, "  Free heap: %lu bytes (internal: %lu, PSRAM: %lu)",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    return 0;
}

void fmrack_deinit(void)
{
    if (s_rack) {
        s_rack->~Rack();
        heap_caps_free(s_rack);
        s_rack = nullptr;
    }

    if (s_midi_mutex) {
        vSemaphoreDelete(s_midi_mutex);
        s_midi_mutex = NULL;
    }

    ESP_LOGI(TAG, "FMRack engine deinitialized");
}

int fmrack_load_performance(const char *path)
{
    if (!s_rack || !path) return -1;

    ESP_LOGI(TAG, "Loading performance: %s", path);

    if (s_rack->loadPerformance(std::string(path))) {
        // Store the directory for program changes
        std::string pathStr(path);
        size_t lastSlash = pathStr.find_last_of('/');
        if (lastSlash != std::string::npos) {
            s_performance_dir = pathStr.substr(0, lastSlash);
        }
        ESP_LOGI(TAG, "Performance loaded: %d enabled parts",
                 s_rack->getEnabledPartCount());
        return 0;
    }

    ESP_LOGE(TAG, "Failed to load performance: %s", path);
    return -1;
}

// Flag for one-shot audio diagnostic after note events
static volatile bool s_diag_next_render = false;

void fmrack_process_audio(float *left_out, float *right_out, int num_samples)
{
    if (s_rack) {
        s_rack->processAudio(left_out, right_out, num_samples);

        // One-shot diagnostic triggered by note events
        bool do_diag = s_diag_next_render;
        if (do_diag) s_diag_next_render = false;

        // Periodic diagnostic: check if engine produces any non-zero samples
        static uint32_t diag_count = 0;
        if (++diag_count >= 375) { // ~every 2s at 48kHz/256
            diag_count = 0;
            do_diag = true;
        }

        if (do_diag) {
            float maxL = 0, maxR = 0;
            int nonzero = 0;
            for (int i = 0; i < num_samples; i++) {
                float al = left_out[i] < 0 ? -left_out[i] : left_out[i];
                float ar = right_out[i] < 0 ? -right_out[i] : right_out[i];
                if (al > maxL) maxL = al;
                if (ar > maxR) maxR = ar;
                if (al > 0.0f || ar > 0.0f) nonzero++;
            }
            int voices = s_rack->getActiveVoices();
            int parts = s_rack->getEnabledPartCount();
            int nmods = s_rack->getNumModules();
            ESP_LOGI(TAG, "RENDER: peak L=%.6f R=%.6f nz=%d/%d voices=%d parts=%d mods=%d",
                     maxL, maxR, nonzero, num_samples, voices, parts, nmods);

            // Check each module's individual output via captureModuleOutput
            float *modL = (float *)alloca(num_samples * sizeof(float));
            float *modR = (float *)alloca(num_samples * sizeof(float));
            for (int m = 0; m < nmods && m < 4; m++) {
                if (s_rack->captureModuleOutput(m, modL, modR, num_samples)) {
                    float mxL = 0, mxR = 0;
                    for (int i = 0; i < num_samples; i++) {
                        float al = modL[i] < 0 ? -modL[i] : modL[i];
                        float ar = modR[i] < 0 ? -modR[i] : modR[i];
                        if (al > mxL) mxL = al;
                        if (ar > mxR) mxR = ar;
                    }
                    if (mxL > 0 || mxR > 0 || voices > 0) {
                        ESP_LOGI(TAG, "  MOD[%d]: peak L=%.6f R=%.6f", m, mxL, mxR);
                    }
                }
            }

            // Direct Dexed engine test on module 0 (bypasses Module::processAudio)
            if (voices > 0 && nmods > 0) {
                const auto& modules = s_rack->getModules();
                if (!modules.empty()) {
                    auto* mod0 = modules[0].get();
                    Dexed* dex0 = mod0->getDexedEngine(0);
                    if (dex0) {
                        int liveCount = 0;
                        for (int i = 0; i < dex0->getMaxNotes(); i++) {
                            if (dex0->voices[i].live) liveCount++;
                        }
                        ESP_LOGI(TAG, "  DEXED[0]: live=%d engineType=%d gain=%.4f ch=%d enabled=%d",
                                 liveCount, dex0->getEngineType(), dex0->getGain(),
                                 mod0->getMIDIChannel(), mod0->isActive() ? 1 : 0);

                        // Directly render from the engine
                        int16_t testbuf[64];
                        memset(testbuf, 0, sizeof(testbuf));
                        dex0->getSamples(testbuf, 64);
                        int16_t tmax = 0;
                        int tnz = 0;
                        for (int i = 0; i < 64; i++) {
                            int16_t av = testbuf[i] < 0 ? -testbuf[i] : testbuf[i];
                            if (av > tmax) tmax = av;
                            if (testbuf[i] != 0) tnz++;
                        }
                        ESP_LOGI(TAG, "  DEXED[0] direct render: max=%d nz=%d/64 first4: %d %d %d %d",
                                 (int)tmax, tnz,
                                 (int)testbuf[0], (int)testbuf[1], (int)testbuf[2], (int)testbuf[3]);
                    }
                }
            }
        }
    } else {
        memset(left_out, 0, num_samples * sizeof(float));
        memset(right_out, 0, num_samples * sizeof(float));
    }
}

void fmrack_handle_midi(uint8_t status, uint8_t data1, uint8_t data2)
{
    if (!s_rack) {
        ESP_LOGW(TAG, "fmrack_handle_midi: rack not initialized!");
        return;
    }

    // Check for program change
    if ((status & 0xF0) == 0xC0 && !s_performance_dir.empty()) {
        fmrack_handle_program_change(data1, s_performance_dir.c_str());
        return;
    }

    uint8_t msgType = status & 0xF0;
    uint8_t channel = (status & 0x0F) + 1;

    // Log note on/off for debugging
    if (msgType == 0x90 && data2 > 0) {
        ESP_LOGI(TAG, ">> Note ON  ch=%d note=%d vel=%d", channel, data1, data2);
    } else if (msgType == 0x80 || (msgType == 0x90 && data2 == 0)) {
        ESP_LOGI(TAG, ">> Note OFF ch=%d note=%d", channel, data1);
    }

    // Thread-safe MIDI routing
    if (xSemaphoreTake(s_midi_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        s_rack->processMidiMessage(status, data1, data2);
        xSemaphoreGive(s_midi_mutex);

        // Log voice count after note events
        if (msgType == 0x90 || msgType == 0x80) {
            int voices = s_rack->getActiveVoices();
            ESP_LOGI(TAG, "   Active voices: %d", voices);
            // Trigger one-shot audio diagnostic on next render
            if (msgType == 0x90 && data2 > 0) {
                s_diag_next_render = true;
            }
        }
    } else {
        ESP_LOGW(TAG, "MIDI mutex timeout! Dropping: %02X %02X %02X", status, data1, data2);
    }
}

void fmrack_handle_sysex(const uint8_t *data, int len, uint8_t channel)
{
    if (!s_rack || !data || len < 2) return;

    if (xSemaphoreTake(s_midi_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        s_rack->routeSysexToModules(data, len, channel);
        xSemaphoreGive(s_midi_mutex);
    }
}

void fmrack_handle_program_change(int program, const char *dir)
{
    if (!s_rack || !dir) return;

    if (xSemaphoreTake(s_midi_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        s_rack->handleProgramChange(program, std::string(dir));
        xSemaphoreGive(s_midi_mutex);
    }
}

int fmrack_get_active_voices(void)
{
    return s_rack ? s_rack->getActiveVoices() : 0;
}

int fmrack_get_enabled_parts(void)
{
    return s_rack ? s_rack->getEnabledPartCount() : 0;
}

bool fmrack_is_initialized(void)
{
    return s_rack != nullptr && s_rack->isInitialized();
}

// ================================================================
// Standalone Dexed engine test — bypasses Rack/Module entirely.
// Creates a fresh Dexed instance, sends it a note, renders audio,
// and logs whether the engine produces non-zero samples.
// ================================================================
#include "dexed.h"

static void test_render(Dexed *dex, const char *label)
{
    int16_t buf[256];
    memset(buf, 0, sizeof(buf));
    dex->getSamples(buf, 256);

    int16_t maxVal = 0;
    int nonzero = 0;
    for (int i = 0; i < 256; i++) {
        int16_t abs_val = buf[i] < 0 ? -buf[i] : buf[i];
        if (abs_val > maxVal) maxVal = abs_val;
        if (buf[i] != 0) nonzero++;
    }
    ESP_LOGI(TAG, "TEST [%s]: max=%d, nonzero=%d/256, first8: %d %d %d %d %d %d %d %d",
             label, (int)maxVal, nonzero,
             (int)buf[0], (int)buf[1], (int)buf[2], (int)buf[3],
             (int)buf[4], (int)buf[5], (int)buf[6], (int)buf[7]);
}

void fmrack_test_dexed_standalone(void)
{
    ESP_LOGI(TAG, "=== Standalone Dexed Engine Test ===");

    uint8_t voice[156] = {
        99,99,99,99, 99,99,99,0, 39,0,0,0,0,0,0,0,  0,0,1,0,7, // OP6 (silent)
        99,99,99,99, 99,99,99,0, 39,0,0,0,0,0,0,0,  0,0,1,0,7, // OP5 (silent)
        99,99,99,99, 99,99,99,0, 39,0,0,0,0,0,0,0,  0,0,1,0,7, // OP4 (silent)
        99,99,99,99, 99,99,99,0, 39,0,0,0,0,0,0,0,  0,0,1,0,7, // OP3 (silent)
        99,99,99,99, 99,99,99,0, 39,0,0,0,0,0,0,0,  0,0,1,0,7, // OP2 (silent)
        99,99,99,99, 99,99,99,0, 39,0,0,0,0,0,0,0, 99,0,1,0,7, // OP1 (output=99)
        99,99,99,99, 50,50,50,50,                                // pitch EG
        0, 0, 1,                                                  // algo=0, fb=0, sync=1
        35, 0, 0, 0, 1, 0,                                       // LFO
        3, 24,                                                    // pitch_mod_sens, transpose
        73,78,73,84,32,86,79,73,67,69,                            // "INIT VOICE"
        63                                                        // OPE: all ops enabled
    };

    // ---- Test A: Minimal (like before — works) ----
    {
        Dexed *dex = new Dexed(16, 48000);
        dex->loadVoiceParameters(voice);
        dex->setEngineType(0); // MSFA
        dex->setGain(1.0f);

        uint8_t noteOn[3] = {0x90, 60, 100};
        dex->midiDataHandler(1, noteOn, 3);
        test_render(dex, "A-minimal");
        delete dex;
    }

    // ---- Test B: Replicate EXACT Module::configureFromPerformance sequence ----
    {
        Dexed *dex = new Dexed(16, 48000);

        // Step 1: loadVoiceParameters (calls panic + memcpy data)
        dex->loadVoiceParameters(voice);

        // Step 2: setMonoMode
        dex->setMonoMode(false);

        // Step 3: setPortamento
        dex->setPortamento(0, 0, 0);

        // Step 4: Controller assignments (matching Performance defaults)
        dex->setModWheelRange(99);
        dex->setModWheelTarget(1);
        dex->setFootControllerRange(99);
        dex->setFootControllerTarget(0);
        dex->setBreathControllerRange(99);
        dex->setBreathControllerTarget(0);
        dex->setAftertouchRange(99);
        dex->setAftertouchTarget(0);
        dex->setPitchbendRange(2);
        dex->setPitchbendStep(0);
        dex->setPortamentoMode(0);
        dex->setPortamentoGlissando(0);
        dex->setPortamentoTime(0);
        dex->setMonoMode(false);
        // Note: setMasterTune skipped (unison detune = 0 for single voice)

        // Step 5: setVelocityScale
        dex->setVelocityScale(64);

        // Step 6: setMaxNotes (calls panic!)
        dex->setMaxNotes(16);

        // Step 7: setEngineType (calls panic!)
        dex->setEngineType(0); // MSFA

        // Step 8: setGain — THIS IS THE SUSPECT LINE
        // In Module: engine->setGain(static_cast<int16_t>(config.gain))
        // config.gain = 1.0f, static_cast<int16_t>(1.0f) = 1
        // Then setGain(float) receives 1.0f (int promoted)
        dex->setGain(static_cast<int16_t>(1.0f));  // Same as Module does
        ESP_LOGI(TAG, "TEST B: gain after setGain(int16_t(1.0f)) = %.4f", dex->getGain());

        // Step 9: setSustain, setSostenuto, setHold
        dex->setSustain(false);
        dex->setSostenuto(false);
        dex->setHold(false);

        // Step 10: setNoteRefreshMode
        dex->setNoteRefreshMode(false);

        // Step 11: setFilterCutoff, setFilterResonance
        dex->setFilterCutoff(127.0f);
        dex->setFilterResonance(0.0f);

        ESP_LOGI(TAG, "TEST B: engineType=%d, gain=%.4f, maxNotes=%d",
                 dex->getEngineType(), dex->getGain(), dex->getMaxNotes());

        // Send note
        uint8_t noteOn[3] = {0x90, 60, 100};
        dex->midiDataHandler(1, noteOn, 3);

        int liveCount = 0;
        for (int i = 0; i < 16; i++) {
            if (dex->voices[i].live) liveCount++;
        }
        ESP_LOGI(TAG, "TEST B: live=%d", liveCount);

        test_render(dex, "B-full-config");
        delete dex;
    }

    // ---- Test C: Same as B, but setGain(1.0f) without int16_t cast ----
    {
        Dexed *dex = new Dexed(16, 48000);
        dex->loadVoiceParameters(voice);
        dex->setMonoMode(false);
        dex->setPortamento(0, 0, 0);
        dex->setVelocityScale(64);
        dex->setMaxNotes(16);
        dex->setEngineType(0);
        dex->setGain(1.0f);                        // Direct float (no int16_t cast)
        dex->setSustain(false);
        dex->setSostenuto(false);
        dex->setHold(false);
        dex->setNoteRefreshMode(false);
        ESP_LOGI(TAG, "TEST C: gain=%.4f", dex->getGain());

        uint8_t noteOn[3] = {0x90, 60, 100};
        dex->midiDataHandler(1, noteOn, 3);
        test_render(dex, "C-float-gain");
        delete dex;
    }

    // ---- Test D: Create a Module directly (bypasses Rack) ----
    {
        ESP_LOGI(TAG, "TEST D: Creating Module directly...");
        FMRack::Performance perf;
        perf.setDefaults(4, 1);
        // Override same as fmrack_init does
        perf.parts[0].midiChannel = 1;
        perf.parts[0].volume = 100;
        perf.parts[0].unisonVoices = 1;
        auto config = perf.getPartConfig(0);

        auto mod = std::make_unique<FMRack::Module>(48000.0f, config);
        ESP_LOGI(TAG, "TEST D: Module created, ch=%d, enabled=%d",
                 mod->getMIDIChannel(), mod->isActive() ? 1 : 0);

        // Send Note On via Module's processMidiMessage
        mod->processMidiMessage(0x90, 60, 100);
        ESP_LOGI(TAG, "TEST D: After NoteOn, isActive=%d", mod->isActive() ? 1 : 0);

        // Check internal Dexed state
        Dexed* dexD = mod->getDexedEngine(0);
        if (dexD) {
            int liveD = 0;
            for (int i = 0; i < dexD->getMaxNotes(); i++) {
                if (dexD->voices[i].live) liveD++;
            }
            ESP_LOGI(TAG, "TEST D: Dexed live=%d engineType=%d gain=%.4f maxNotes=%d",
                     liveD, dexD->getEngineType(), dexD->getGain(), dexD->getMaxNotes());
        }

        // Render via Module::processAudio (avoid large stack allocations)
        std::vector<float> dL(256, 0.0f);
        std::vector<float> dR(256, 0.0f);
        std::vector<float> dRL(256, 0.0f);
        std::vector<float> dRR(256, 0.0f);
        mod->processAudio(dL.data(), dR.data(), dRL.data(), dRR.data(), 256);

        float dmxL = 0, dmxR = 0;
        for (int i = 0; i < 256; i++) {
            float al = dL[i] < 0 ? -dL[i] : dL[i];
            float ar = dR[i] < 0 ? -dR[i] : dR[i];
            if (al > dmxL) dmxL = al;
            if (ar > dmxR) dmxR = ar;
        }
        ESP_LOGI(TAG, "TEST D: Module render peak L=%.6f R=%.6f", dmxL, dmxR);

        // Also render directly from the Dexed engine
        if (dexD) {
            int16_t dbuf[64];
            memset(dbuf, 0, sizeof(dbuf));
            dexD->getSamples(dbuf, 64);
            int16_t dtmax = 0;
            for (int i = 0; i < 64; i++) {
                int16_t av = dbuf[i] < 0 ? -dbuf[i] : dbuf[i];
                if (av > dtmax) dtmax = av;
            }
            ESP_LOGI(TAG, "TEST D: Dexed direct render after Module: max=%d first4: %d %d %d %d",
                     (int)dtmax, (int)dbuf[0], (int)dbuf[1], (int)dbuf[2], (int)dbuf[3]);
        }
    }

    ESP_LOGI(TAG, "=== End Standalone Dexed Test ===");
}
