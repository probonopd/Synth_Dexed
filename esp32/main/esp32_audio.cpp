/*
 * FMRack ESP32-S3 Port - Audio Output (I2S) Implementation
 *
 * Architecture: decoupled render + write pipeline
 *
 *   audio_render_task  (Core 1, MAX priority, IRAM_ATTR)
 *       Renders Dexed FM audio into a pre-render ring buffer as fast as
 *       possible — independently of I2S DMA timing.
 *
 *   audio_write_task   (Core 1, MAX-1 priority)
 *       Drains the pre-render ring buffer to the I2S DMA one block at a
 *       time.  Blocks only on DMA descriptor availability (I2S ISR).
 *
 * The pre-render ring (FMRACK_AUDIO_PRERENDER_BLOCKS blocks of internal SRAM)
 * decouples these two tasks so that any transient stall in either path is
 * absorbed without an audible glitch.
 *
 * Root-cause fix for the USB crackle:
 *   sizeof(Dexed) >= 16 KB (inline render scratch buffer).  With
 *   CONFIG_SPIRAM_USE_MALLOC=y, plain malloc() sends it to PSRAM.  PSRAM and
 *   Flash share the SPI0 bus on ESP32-S3, so USB lib code executing from Flash
 *   stalls PSRAM reads by the render task → overruns → crackle.  Dexed is now
 *   explicitly allocated in internal SRAM in dexed_raw.cpp.
 */

#include "esp32_audio.h"
#include "esp32_config.h"
#include "dexed_raw.h"
#include "esp32_usb_audio.h"
#include "AudioEffectSymphonic.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/portmacro.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"  // for timing diagnostics
#include "esp_pm.h"

#include <string.h>
#include <math.h>

static const char *TAG = "fmrack_audio";

// I2S channel handle
static i2s_chan_handle_t s_tx_handle = NULL;

// Task handles
static TaskHandle_t s_audio_task_handle  = NULL;  // render task
static TaskHandle_t s_audio_write_handle = NULL;  // write task
static volatile bool s_audio_running = false;

// Low-priority stats task (prints diagnostics gathered in the audio thread)
static TaskHandle_t s_audio_stats_task_handle = NULL;

// Mono int16 render buffer (internal SRAM).  The render task writes here,
// then expands to stereo into the pre-render ring.
static int16_t *s_mono_buffer = NULL;

// SPX90 Symphonic effect (mono-in / stereo-out tri-chorus)
static FMRack::AudioEffectSymphonic *s_symphonic = nullptr;

// Float working buffers for symphonic effect processing.
// DRAM_ATTR: force internal SRAM to avoid PSRAM bus contention with USB/Flash.
static DRAM_ATTR float s_mono_float[FMRACK_BUFFER_SIZE];
static DRAM_ATTR float s_left_float[FMRACK_BUFFER_SIZE];
static DRAM_ATTR float s_right_float[FMRACK_BUFFER_SIZE];

// -------------------------------------------------------------------------
// Pre-render ring buffer
//
// Sits between the render task (producer) and the write task (consumer).
// Allocated as a static DRAM array so it is guaranteed to be in internal
// SRAM regardless of SPIRAM_USE_MALLOC settings.
// -------------------------------------------------------------------------
#define PRERENDER_RING_BLOCKS  FMRACK_AUDIO_PRERENDER_BLOCKS

// stereo interleaved int16 — DRAM_ATTR forces internal SRAM placement
static DRAM_ATTR int16_t s_pre_ring[PRERENDER_RING_BLOCKS][FMRACK_BUFFER_SIZE * 2];
static volatile int s_pre_ring_write = 0;  // next slot for render task
static volatile int s_pre_ring_read  = 0;  // next slot for write task

// Counting semaphores: free_sem counts empty slots, data_sem counts filled slots
static SemaphoreHandle_t s_pre_ring_free_sem = NULL;
static SemaphoreHandle_t s_pre_ring_data_sem = NULL;

// -------------------------------------------------------------------------
// Audio task stacks in internal SRAM to prevent audio crackle (works)
//
// With CONFIG_SPIRAM_USE_MALLOC=y and SPIRAM_MALLOC_ALWAYSINTERNAL=4096,
// xTaskCreatePinnedToCore allocates stacks > 4 KB in PSRAM.  The render-task
// stack is 8 KB, so without this workaround it ends up in PSRAM.
//
// When a USB hub + keyboard are attached, the USB host library runs code from
// flash on Core 0.  Both that flash fetch and Core 1's PSRAM stack accesses
// share the SPI0/SPI1 bus, serialising them.  Core 1's render task stalls
// while Core 0 fetches USB library pages → render overruns → I2S underruns →
// audible glitches identical to the Dexed PSRAM issue fixed in dexed_raw.cpp.
//
// Fix: use xTaskCreateStaticPinnedToCore with DRAM_ATTR stacks so both stacks
// are always in internal SRAM, eliminating cross-core SPI0 bus contention.
// -------------------------------------------------------------------------
static DRAM_ATTR StackType_t s_render_stack[AUDIO_TASK_STACK_SIZE / sizeof(StackType_t)];
static DRAM_ATTR StaticTask_t s_render_tcb;
static DRAM_ATTR StackType_t s_write_stack[AUDIO_WRITE_TASK_STACK_SIZE / sizeof(StackType_t)];
static DRAM_ATTR StaticTask_t s_write_tcb;

// Power management locks (only effective if CONFIG_PM_ENABLE is enabled)
static esp_pm_lock_handle_t s_pm_lock_cpu = NULL;
static esp_pm_lock_handle_t s_pm_lock_no_ls = NULL;
static bool s_pm_supported = false;
static bool s_pm_locked = false;

// -----------------------------------------------------------------------------
// Audio RT diagnostics (updated in audio thread, printed elsewhere)
// -----------------------------------------------------------------------------

typedef struct {
    uint64_t blocks;
    uint64_t render_us_total;
    uint32_t render_us_max;
    uint32_t render_overruns;

    uint64_t write_calls;
    uint64_t write_us_total;
    uint32_t write_us_max;
    uint32_t write_errors;
    uint32_t write_zero_bytes;
    uint32_t write_partial_calls;
    uint32_t write_partial_bytes;

    uint32_t loop_dt_us_max;
    uint32_t loop_dt_over_2p;

    // Audio data diagnostics
    uint32_t clip_count;         // samples at INT16_MAX or INT16_MIN
    int16_t  peak_sample;        // max |sample| seen (resets each stats period)

    // I2S ISR-side counters
    uint64_t isr_on_sent;
    uint32_t isr_on_send_q_ovf;
} audio_rt_stats_t;

static audio_rt_stats_t s_rt_stats = {};
static portMUX_TYPE s_rt_stats_mux = portMUX_INITIALIZER_UNLOCKED;

// I2S event callbacks run in ISR context.
static bool IRAM_ATTR i2s_on_sent_cb(i2s_chan_handle_t handle, i2s_event_data_t *event, void *user_data)
{
    (void)handle;
    (void)event;
    (void)user_data;
    portENTER_CRITICAL_ISR(&s_rt_stats_mux);
    s_rt_stats.isr_on_sent++;
    portEXIT_CRITICAL_ISR(&s_rt_stats_mux);
    return false;
}

static bool IRAM_ATTR i2s_on_send_q_ovf_cb(i2s_chan_handle_t handle, i2s_event_data_t *event, void *user_data)
{
    (void)handle;
    (void)event;
    (void)user_data;
    portENTER_CRITICAL_ISR(&s_rt_stats_mux);
    s_rt_stats.isr_on_send_q_ovf++;
    portEXIT_CRITICAL_ISR(&s_rt_stats_mux);
    return false;
}

// When esp32_audio_init() is called from core 0 (app_main), we still want the
// I2S driver (and thus its interrupts) to be allocated on the dedicated audio
// core. ESP-IDF allocates many peripheral interrupts on the calling core.
static int esp32_audio_init_impl(void);

static void i2s_init_task(void *param)
{
    (void)param;
    (void)esp32_audio_init_impl();
    vTaskDelete(NULL);
}

static int esp32_audio_init_impl(void)
{
    // Configure I2S channel
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(
        (i2s_port_t)FMRACK_I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = FMRACK_I2S_DMA_BUF_COUNT;
    chan_cfg.dma_frame_num = FMRACK_I2S_DMA_BUF_LEN;
    chan_cfg.auto_clear = true;   // Insert silent zeros on underrun (cleaner artifact than repeating old audio during USB attach)
    // I2S ISR at priority 7 (highest allowable without being NMI).
    // USB SOF fires at LEVEL2. Priority 7 > 2 so the DMA completion ISR
    // is never delayed by a USB SOF burst, preventing FIFO underruns.
    chan_cfg.intr_priority = 7;

    esp_err_t ret = i2s_new_channel(&chan_cfg, &s_tx_handle, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S channel: %s", esp_err_to_name(ret));
        return -1;
    }

    // Register ISR callbacks to detect TX queue overflow (often audible as crackles).
    const i2s_event_callbacks_t cbs = {
        .on_recv = NULL,
        .on_recv_q_ovf = NULL,
        .on_sent = i2s_on_sent_cb,
        .on_send_q_ovf = i2s_on_send_q_ovf_cb,
    };
    (void)i2s_channel_register_event_callback(s_tx_handle, &cbs, NULL);

    // Configure I2S standard mode (Philips format) with MCLK
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(FMRACK_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = (gpio_num_t)FMRACK_I2S_MCLK_PIN,
            .bclk = (gpio_num_t)FMRACK_I2S_BCK_PIN,
            .ws   = (gpio_num_t)FMRACK_I2S_WS_PIN,
            .dout = (gpio_num_t)FMRACK_I2S_DOUT_PIN,
            .din  = (gpio_num_t)I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    // ESP32-S3 can generate MCLK = 256 * sample_rate for most DACs
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

    ret = i2s_channel_init_std_mode(s_tx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init I2S standard mode: %s", esp_err_to_name(ret));
        i2s_del_channel(s_tx_handle);
        s_tx_handle = NULL;
        return -1;
    }

    ret = i2s_channel_enable(s_tx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable I2S channel: %s", esp_err_to_name(ret));
        i2s_del_channel(s_tx_handle);
        s_tx_handle = NULL;
        return -1;
    }

    ESP_LOGI(TAG, "I2S audio output initialized (MCLK=%d x fs)", 256);
    return 0;
}

int esp32_audio_init(void)
{
    ESP_LOGI(TAG, "Initializing I2S audio output...");
    ESP_LOGI(TAG, "  Sample rate: %d Hz", FMRACK_SAMPLE_RATE);
    ESP_LOGI(TAG, "  Buffer size: %d samples", FMRACK_BUFFER_SIZE);
    ESP_LOGI(TAG, "  BCK pin: %d, WS pin: %d, DOUT pin: %d, MCLK pin: %d",
             FMRACK_I2S_BCK_PIN, FMRACK_I2S_WS_PIN, FMRACK_I2S_DOUT_PIN,
             FMRACK_I2S_MCLK_PIN);

    // Allocate mono int16 render buffer in internal memory.
    // IMPORTANT: MALLOC_CAP_INTERNAL so it is never placed in PSRAM.
    s_mono_buffer = (int16_t *)heap_caps_calloc(
        1,
        FMRACK_BUFFER_SIZE * sizeof(int16_t),
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_mono_buffer) {
        ESP_LOGE(TAG, "Failed to allocate mono buffer");
        return -1;
    }

    // Initialize SPX90 Symphonic effect.
    // Allocated in PSRAM (delay lines ~17KB); the hot-path float working
    // buffers (s_mono_float, s_left_float, s_right_float) are already in
    // internal SRAM via DRAM_ATTR.
    if (!s_symphonic) {
        void *sym_mem = heap_caps_malloc(sizeof(FMRack::AudioEffectSymphonic), MALLOC_CAP_SPIRAM);
        if (!sym_mem) {
            ESP_LOGW(TAG, "PSRAM alloc failed for Symphonic, trying internal");
            sym_mem = malloc(sizeof(FMRack::AudioEffectSymphonic));
        }
        if (sym_mem) {
            s_symphonic = new (sym_mem) FMRack::AudioEffectSymphonic(
                static_cast<float>(FMRACK_SAMPLE_RATE));
            // Enable with SPX90 defaults: 100% wet, 50% depth, 0.7 Hz
            s_symphonic->setMix(1.0f);
            s_symphonic->setDepth(0.5f);
            s_symphonic->setSpeed(0.7f);
            s_symphonic->setEnabled(true);
            ESP_LOGI(TAG, "SPX90 Symphonic effect initialized (enabled)");
        } else {
            ESP_LOGW(TAG, "Failed to allocate Symphonic effect (disabled)");
        }
    }

    // Initialize I2S from the dedicated audio core so that the driver allocates
    // its interrupts/ISRs on that core (reduces interference from USB/protocol
    // activity on core 0 that can cause intermittent crackles).
    if (xPortGetCoreID() == AUDIO_TASK_CORE) {
        return esp32_audio_init_impl();
    }

    // Fire-and-forget: start initialization task on audio core, don't wait.
    // 8192-byte stack: i2s_new_channel + i2s_channel_init_std_mode + internal
    // ESP-IDF allocator calls consume more than 4096 bytes and will overflow.
    BaseType_t ok = xTaskCreatePinnedToCore(
        i2s_init_task,
        "i2s_init",
        8192,
        NULL,
        AUDIO_TASK_PRIORITY,
        NULL,
        AUDIO_TASK_CORE);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create i2s_init task");
        return -1;
    }

    // Return success regardless; if init_impl ultimately fails the i2s_init_task
    // will log an error but won't crash the main loop.
    return 0;
}

/**
 * audio_render_task  —  Core 1, highest priority
 *
 * Continuously renders audio from the Dexed FM engine into the pre-render
 * ring buffer.  Blocks only when the ring is full (waiting for the write
 * task to drain one slot).  Never touches I2S or DMA directly.
 *
 * IRAM_ATTR: hot path kept in IRAM so a USB-driven I-cache eviction on Core 0
 * (shared instruction cache) cannot stall the prologue or loop head.
 */
static IRAM_ATTR void audio_render_task(void *param)
{
    ESP_LOGI(TAG, "Audio render task on core %d", xPortGetCoreID());

    const int      num_samples = FMRACK_BUFFER_SIZE;
    const uint64_t period_us   = (uint64_t)num_samples * 1000000ULL / FMRACK_SAMPLE_RATE;

    while (s_audio_running) {
        // Wait for a free slot in the pre-render ring.
        // Normally instant; only blocks when all PRERENDER_RING_BLOCKS slots
        // are filled (write task is briefly behind).
        if (xSemaphoreTake(s_pre_ring_free_sem, pdMS_TO_TICKS(50)) != pdTRUE) {
            continue; // timeout: re-check s_audio_running
        }
        if (!s_audio_running) break;

        uint64_t t0 = esp_timer_get_time();

        // Render mono audio.
        dexed_raw_process_audio_i16(s_mono_buffer, num_samples);

        // Apply SPX90 Symphonic effect (mono-in → stereo-out).
        // Convert mono int16 → float, process, convert stereo float → int16.
        int16_t *dst = s_pre_ring[s_pre_ring_write];
        uint32_t clip = 0;
        int16_t  peak = 0;

        if (s_symphonic && s_symphonic->isEnabled()) {
            // Convert mono int16 → float (-1.0 to 1.0)
            const float scale_in = 1.0f / 32768.0f;
            for (int i = 0; i < num_samples; i++) {
                s_mono_float[i] = static_cast<float>(s_mono_buffer[i]) * scale_in;
            }

            // Process: mono → stereo with tri-chorus effect
            s_symphonic->process(s_mono_float, s_left_float, s_right_float, num_samples);

            // Convert stereo float → interleaved int16 into the pre-ring slot
            for (int i = 0; i < num_samples; i++) {
                // Apply 4x gain boost to symphonic output (2x * 2x)
                float fL = s_left_float[i] * 32768.0f * 4.0f;
                float fR = s_right_float[i] * 32768.0f * 4.0f;

                // Clamp to int16 range
                if (fL >  32767.0f) fL =  32767.0f;
                if (fL < -32768.0f) fL = -32768.0f;
                if (fR >  32767.0f) fR =  32767.0f;
                if (fR < -32768.0f) fR = -32768.0f;

                int16_t sL = static_cast<int16_t>(fL);
                int16_t sR = static_cast<int16_t>(fR);

                // Peak/clip diagnostics on left channel (representative)
                int16_t abs_s = (sL < 0) ? (int16_t)-sL : sL;
                if (abs_s > peak) peak = abs_s;
                if (sL == INT16_MAX || sL == INT16_MIN) clip++;

                dst[i * 2]     = sL;
                dst[i * 2 + 1] = sR;
            }
        } else {
            // Bypass: expand mono → interleaved stereo with 4x gain (consistent with symphonic on).
            for (int i = 0; i < num_samples; i++) {
                // Apply same 4x gain as symphonic path to maintain consistent volume
                float fL = static_cast<float>(s_mono_buffer[i]) * 4.0f;
                float fR = static_cast<float>(s_mono_buffer[i]) * 4.0f;

                // Clamp to int16 range
                if (fL >  32767.0f) fL =  32767.0f;
                if (fL < -32768.0f) fL = -32768.0f;
                if (fR >  32767.0f) fR =  32767.0f;
                if (fR < -32768.0f) fR = -32768.0f;

                int16_t sL = static_cast<int16_t>(fL);
                int16_t sR = static_cast<int16_t>(fR);

                // Peak/clip diagnostics on left channel (representative)
                int16_t abs_s = (sL < 0) ? (int16_t)-sL : sL;
                if (abs_s > peak) peak = abs_s;
                if (sL == INT16_MAX || sL == INT16_MIN) clip++;

                dst[i * 2]     = sL;
                dst[i * 2 + 1] = sR;
            }
        }

        s_pre_ring_write = (s_pre_ring_write + 1) % PRERENDER_RING_BLOCKS;

        uint64_t render_us = esp_timer_get_time() - t0;
        portENTER_CRITICAL(&s_rt_stats_mux);
        s_rt_stats.blocks++;
        s_rt_stats.render_us_total += render_us;
        if (render_us > s_rt_stats.render_us_max)
            s_rt_stats.render_us_max = (uint32_t)render_us;
        if (render_us > period_us)
            s_rt_stats.render_overruns++;
        s_rt_stats.clip_count += clip;
        if (peak > s_rt_stats.peak_sample)
            s_rt_stats.peak_sample = peak;
        portEXIT_CRITICAL(&s_rt_stats_mux);

        // Signal the write task that one filled slot is available.
        xSemaphoreGive(s_pre_ring_data_sem);
    }

    ESP_LOGI(TAG, "Audio render task exiting");
    vTaskDelete(NULL);
}

/**
 * audio_write_task  —  Core 1, second-highest priority
 *
 * Drains one pre-rendered block at a time from the ring buffer into the I2S
 * DMA.  The i2s_channel_write() call blocks for ~5.8 ms waiting for a DMA
 * descriptor to become free; during that time the higher-priority render task
 * can run and pre-fill the ring.
 *
 * Key: the ring slot is released back to the render task AFTER the block is
 * copied to a local write buffer, so the render task is never prevented from
 * filling the next slot while i2s_channel_write waits on the DMA ISR.
 */
static void audio_write_task(void *param)
{
    ESP_LOGI(TAG, "Audio write task on core %d", xPortGetCoreID());

    /* Wait for i2s_init_task to finish and populate s_tx_handle.
     * esp32_audio_start() launches both this task and the fire-and-forget
     * i2s_init_task concurrently.  Calling i2s_channel_write() on a NULL
     * handle causes a LoadProhibited fault.  Spin here (in 10 ms steps) until
     * the handle is valid, then enter the main loop. */
    while (s_audio_running && s_tx_handle == NULL) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (!s_audio_running) {
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "I2S handle ready, starting write loop");

    const size_t buf_bytes = FMRACK_BUFFER_SIZE * 2 * sizeof(int16_t);

    // Local write buffer (1 KB on stack — well within the 4 KB stack).
    // Copied from the ring slot before releasing the slot, so the render
    // task can reuse the slot while i2s_channel_write blocks.
    int16_t write_buf[FMRACK_BUFFER_SIZE * 2];

    while (s_audio_running) {
        // Wait for a filled slot.
        if (xSemaphoreTake(s_pre_ring_data_sem, pdMS_TO_TICKS(50)) != pdTRUE) {
            continue;
        }
        if (!s_audio_running) {
            xSemaphoreGive(s_pre_ring_free_sem);
            break;
        }

        // Copy the slot to our local buffer.
        memcpy(write_buf, s_pre_ring[s_pre_ring_read], buf_bytes);
        s_pre_ring_read = (s_pre_ring_read + 1) % PRERENDER_RING_BLOCKS;

        // Release the ring slot immediately so the render task can render
        // the next block while we wait on the DMA descriptor below.
        xSemaphoreGive(s_pre_ring_free_sem);

        // Route audio: USB audio device takes over when connected; I2S gets
        // silence so the DMA keeps running (maintains timing) but is inaudible.
        // When no USB device is present the full path goes to I2S as before.
        static DRAM_ATTR int16_t s_i2s_silence[FMRACK_BUFFER_SIZE * 2]; /* zero-init */
        bool usb_active = esp32_usb_audio_is_ready();
        if (usb_active) {
            esp32_usb_audio_write_stereo(write_buf, FMRACK_BUFFER_SIZE);
        }

        // Write to I2S DMA — real audio when no USB device, silence otherwise.
        // Must always run to keep the DMA clock ticking (~5.8 ms per block).
        uint64_t t0 = esp_timer_get_time();
        size_t total_written = 0;
        const int16_t *i2s_src = usb_active ? s_i2s_silence : write_buf;
        while (total_written < buf_bytes) {
            size_t bytes_written = 0;
            const uint8_t *p = (const uint8_t *)i2s_src + total_written;
            esp_err_t ret = i2s_channel_write(
                s_tx_handle, p, buf_bytes - total_written,
                &bytes_written, pdMS_TO_TICKS(20));
            if (ret != ESP_OK) {
                portENTER_CRITICAL(&s_rt_stats_mux);
                s_rt_stats.write_errors++;
                portEXIT_CRITICAL(&s_rt_stats_mux);
                break;
            }
            if (bytes_written == 0) {
                portENTER_CRITICAL(&s_rt_stats_mux);
                s_rt_stats.write_zero_bytes++;
                portEXIT_CRITICAL(&s_rt_stats_mux);
                break;
            }
            if (bytes_written < (buf_bytes - total_written)) {
                portENTER_CRITICAL(&s_rt_stats_mux);
                s_rt_stats.write_partial_calls++;
                s_rt_stats.write_partial_bytes += (uint32_t)((buf_bytes - total_written) - bytes_written);
                portEXIT_CRITICAL(&s_rt_stats_mux);
            }
            total_written += bytes_written;
        }
        uint64_t write_us = esp_timer_get_time() - t0;
        portENTER_CRITICAL(&s_rt_stats_mux);
        s_rt_stats.write_calls++;
        s_rt_stats.write_us_total += write_us;
        if (write_us > s_rt_stats.write_us_max)
            s_rt_stats.write_us_max = (uint32_t)write_us;
        portEXIT_CRITICAL(&s_rt_stats_mux);
    }

    ESP_LOGI(TAG, "Audio write task exiting");
    vTaskDelete(NULL);
}

static void audio_stats_task(void *param)
{
    (void)param;
    const int num_samples = FMRACK_BUFFER_SIZE;
    const uint64_t period = (uint64_t)num_samples * 1000000ULL / FMRACK_SAMPLE_RATE;

    audio_rt_stats_t prev = {};
    while (s_audio_running) {
        vTaskDelay(pdMS_TO_TICKS(2000));

        // PM CPU lock is held unconditionally for the entire audio session
        // (acquired at esp32_audio_start, released at esp32_audio_stop).
        // No duty-cycle toggling needed here.

        audio_rt_stats_t cur;
        portENTER_CRITICAL(&s_rt_stats_mux);
        cur = s_rt_stats;
        s_rt_stats.peak_sample = 0;  // reset peak for next period
        portEXIT_CRITICAL(&s_rt_stats_mux);

        const uint64_t d_blocks = cur.blocks - prev.blocks;
        const uint64_t d_render_us = cur.render_us_total - prev.render_us_total;
        const uint64_t d_write_us = cur.write_us_total - prev.write_us_total;
        const uint32_t d_overruns = cur.render_overruns - prev.render_overruns;
        const uint32_t d_werr = cur.write_errors - prev.write_errors;
        const uint32_t d_wzero = cur.write_zero_bytes - prev.write_zero_bytes;
        const uint32_t d_wpart = cur.write_partial_calls - prev.write_partial_calls;
        const uint64_t d_isr_sent = cur.isr_on_sent - prev.isr_on_sent;
        const uint32_t d_isr_qovf = cur.isr_on_send_q_ovf - prev.isr_on_send_q_ovf;
        const uint32_t d_clips = cur.clip_count - prev.clip_count;

        uint64_t avg_render = d_blocks ? (d_render_us / d_blocks) : 0;
        uint64_t avg_write  = d_blocks ? (d_write_us  / d_blocks) : 0;
        float render_cpu = (period != 0) ? ((float)avg_render * 100.0f / (float)period) : 0.0f;

        int voices = dexed_raw_get_active_voices();
        // ring_fill = free_sem tokens consumed = data_sem count (approximate, lock-free read)
        int ring_fill = (int)uxSemaphoreGetCount(s_pre_ring_data_sem);
        ESP_LOGI(TAG,
                 "RT: voices=%d render=%.1f%% avg=%lluus max=%uus overruns=%u ring=%d/%d"
                 " | i2s avg=%lluus max=%uus err=%u zero=%u partial=%u"
                 " | isr sent=%llu qovf=%u | peak=%d clips=%u",
                 voices,
                 render_cpu,
                 (unsigned long long)avg_render,
                 (unsigned)cur.render_us_max,
                 (unsigned)d_overruns,
                 ring_fill,
                 PRERENDER_RING_BLOCKS,
                 (unsigned long long)avg_write,
                 (unsigned)cur.write_us_max,
                 (unsigned)d_werr,
                 (unsigned)d_wzero,
                 (unsigned)d_wpart,
                 (unsigned long long)d_isr_sent,
                 (unsigned)d_isr_qovf,
                 (int)cur.peak_sample,
                 (unsigned)d_clips);

        prev = cur;
    }

    vTaskDelete(NULL);
}

int esp32_audio_start(void)
{
    if (s_audio_running) {
        ESP_LOGW(TAG, "Audio task already running");
        return 0;
    }

    // if s_tx_handle is NULL the init task hasn't completed yet; we'll still
    // start render/write tasks and they will see the NULL handle and do nothing.

    s_audio_running = true;

    // Initialise pre-render ring semaphores.
    // free_sem starts full (all PRERENDER_RING_BLOCKS slots available to render).
    // data_sem starts empty (no rendered data yet).
    s_pre_ring_write = 0;
    s_pre_ring_read  = 0;
    s_pre_ring_free_sem = xSemaphoreCreateCounting(PRERENDER_RING_BLOCKS, PRERENDER_RING_BLOCKS);
    s_pre_ring_data_sem = xSemaphoreCreateCounting(PRERENDER_RING_BLOCKS, 0);
    if (!s_pre_ring_free_sem || !s_pre_ring_data_sem) {
        ESP_LOGE(TAG, "Failed to create pre-render ring semaphores");
        s_audio_running = false;
        return -1;
    }

    // Create PM locks and acquire them immediately so the CPU runs at maximum
    // frequency for the entire duration of audio playback.  This prevents
    // USB enumeration or idle-sleep from downclocking the CPU at any point
    // while audio is active, which was a previously observed crackle source.
    // (no-op if PM is disabled in sdkconfig)
    if (!s_pm_lock_cpu && !s_pm_lock_no_ls) {
        esp_err_t e1 = esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "audio_cpu", &s_pm_lock_cpu);
        esp_err_t e2 = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "audio_nols", &s_pm_lock_no_ls);
        s_pm_supported = (e1 == ESP_OK && e2 == ESP_OK);
        if (s_pm_supported) {
            (void)esp_pm_lock_acquire(s_pm_lock_cpu);
            (void)esp_pm_lock_acquire(s_pm_lock_no_ls);
            s_pm_locked = true;
            ESP_LOGI(TAG, "PM CPU freq lock acquired (unconditional)");
        } else {
            // PM might be disabled; don't spam logs.
            s_pm_lock_cpu = NULL;
            s_pm_lock_no_ls = NULL;
        }
    }

    s_audio_task_handle = xTaskCreateStaticPinnedToCore(
        audio_render_task,
        "audio_render",
        AUDIO_TASK_STACK_SIZE / sizeof(StackType_t),
        NULL,
        AUDIO_TASK_PRIORITY,
        s_render_stack,
        &s_render_tcb,
        AUDIO_TASK_CORE);

    if (!s_audio_task_handle) {
        ESP_LOGE(TAG, "Failed to create audio render task");
        s_audio_running = false;
        return -1;
    }

    // Write task: same core, one priority step below render task.
    // When write task blocks on i2s_channel_write (~5.8 ms DMA wait),
    // the higher-priority render task runs and pre-fills the ring.
    s_audio_write_handle = xTaskCreateStaticPinnedToCore(
        audio_write_task,
        "audio_write",
        AUDIO_WRITE_TASK_STACK_SIZE / sizeof(StackType_t),
        NULL,
        AUDIO_WRITE_TASK_PRIORITY,
        s_write_stack,
        &s_write_tcb,
        AUDIO_TASK_CORE);

    if (!s_audio_write_handle) {
        ESP_LOGE(TAG, "Failed to create audio write task");
        s_audio_running = false;
        return -1;
    }

    // Start low-priority stats task on core 0 (protocol core) so it can't
    // interfere with real-time audio scheduling on core 1.
    (void)xTaskCreatePinnedToCore(
        audio_stats_task,
        "audio_stats",
        4096,
        NULL,
        1,
        &s_audio_stats_task_handle,
        0);

    ESP_LOGI(TAG, "Audio tasks started (core %d, render prio %d, write prio %d, prerender_blocks %d)",
             AUDIO_TASK_CORE, AUDIO_TASK_PRIORITY, AUDIO_WRITE_TASK_PRIORITY,
             FMRACK_AUDIO_PRERENDER_BLOCKS);
    return 0;
}

void esp32_audio_stop(void)
{
    s_audio_running = false;

    // Unblock both tasks so they can observe s_audio_running = false and exit.
    if (s_pre_ring_free_sem) xSemaphoreGive(s_pre_ring_free_sem);
    if (s_pre_ring_data_sem) xSemaphoreGive(s_pre_ring_data_sem);

    // Release PM locks if held
    if (s_pm_supported && s_pm_locked) {
        if (s_pm_lock_no_ls) (void)esp_pm_lock_release(s_pm_lock_no_ls);
        if (s_pm_lock_cpu) (void)esp_pm_lock_release(s_pm_lock_cpu);
        s_pm_locked = false;
    }
    if (s_pm_lock_cpu) {
        esp_pm_lock_delete(s_pm_lock_cpu);
        s_pm_lock_cpu = NULL;
    }
    if (s_pm_lock_no_ls) {
        esp_pm_lock_delete(s_pm_lock_no_ls);
        s_pm_lock_no_ls = NULL;
    }

    // Wait for tasks to exit
    if (s_audio_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(100));
        s_audio_task_handle = NULL;
    }
    if (s_audio_write_handle) {
        vTaskDelay(pdMS_TO_TICKS(100));
        s_audio_write_handle = NULL;
    }
    if (s_audio_stats_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(50));
        s_audio_stats_task_handle = NULL;
    }

    // Clean up ring semaphores
    if (s_pre_ring_free_sem) {
        vSemaphoreDelete(s_pre_ring_free_sem);
        s_pre_ring_free_sem = NULL;
    }
    if (s_pre_ring_data_sem) {
        vSemaphoreDelete(s_pre_ring_data_sem);
        s_pre_ring_data_sem = NULL;
    }

    if (s_tx_handle) {
        i2s_channel_disable(s_tx_handle);
        i2s_del_channel(s_tx_handle);
        s_tx_handle = NULL;
    }

    if (s_mono_buffer) {
        heap_caps_free(s_mono_buffer);
        s_mono_buffer = NULL;
    }

    if (s_symphonic) {
        s_symphonic->~AudioEffectSymphonic();
        heap_caps_free(s_symphonic);
        s_symphonic = nullptr;
    }

    ESP_LOGI(TAG, "Audio stopped and I2S deinitialized");
}

bool esp32_audio_is_running(void)
{
    return s_audio_running;
}

void esp32_audio_toggle_symphonic(void)
{
    if (s_symphonic) {
        bool current = s_symphonic->isEnabled();
        s_symphonic->setEnabled(!current);
        // Note: No logging here — this is called from ISR context where ESP_LOGI is unsafe.
        // The rendering task will reflect the state change on the next block.
    }
}
