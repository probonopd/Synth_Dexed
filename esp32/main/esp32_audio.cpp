/*
 * FMRack ESP32-C5 Port - Audio Output (I2S) Implementation
 *
 * Uses the ESP-IDF I2S driver (new API) to output stereo audio
 * from the FMRack synthesis engine to an external DAC.
 */

#include "esp32_audio.h"
#include "esp32_config.h"
#include "fmrack_wrapper.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include <string.h>
#include <math.h>

static const char *TAG = "fmrack_audio";

// I2S channel handle
static i2s_chan_handle_t s_tx_handle = NULL;

// Audio task handle
static TaskHandle_t s_audio_task_handle = NULL;
static volatile bool s_audio_running = false;

// DMA-capable audio output buffer (interleaved stereo 16-bit samples)
static int16_t *s_dma_buffer = NULL;

// Floating-point processing buffers (allocated in PSRAM)
static float *s_left_buffer = NULL;
static float *s_right_buffer = NULL;

int esp32_audio_init(void)
{
    ESP_LOGI(TAG, "Initializing I2S audio output...");
    ESP_LOGI(TAG, "  Sample rate: %d Hz", FMRACK_SAMPLE_RATE);
    ESP_LOGI(TAG, "  Buffer size: %d samples", FMRACK_BUFFER_SIZE);
    ESP_LOGI(TAG, "  BCK pin: %d, WS pin: %d, DOUT pin: %d",
             FMRACK_I2S_BCK_PIN, FMRACK_I2S_WS_PIN, FMRACK_I2S_DOUT_PIN);

    // Allocate DMA buffer in internal memory (required for DMA)
    size_t dma_buf_size = FMRACK_BUFFER_SIZE * 2 * sizeof(int16_t); // stereo
    s_dma_buffer = (int16_t *)heap_caps_calloc(1, dma_buf_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_dma_buffer) {
        ESP_LOGE(TAG, "Failed to allocate DMA buffer (%d bytes)", (int)dma_buf_size);
        return -1;
    }

    // Allocate float processing buffers in PSRAM
    size_t float_buf_size = FMRACK_BUFFER_SIZE * sizeof(float);
    s_left_buffer = (float *)heap_caps_calloc(1, float_buf_size, MALLOC_CAP_SPIRAM);
    s_right_buffer = (float *)heap_caps_calloc(1, float_buf_size, MALLOC_CAP_SPIRAM);
    if (!s_left_buffer || !s_right_buffer) {
        // Fall back to internal memory
        ESP_LOGW(TAG, "PSRAM allocation failed, using internal memory for float buffers");
        if (s_left_buffer) { heap_caps_free(s_left_buffer); s_left_buffer = NULL; }
        if (s_right_buffer) { heap_caps_free(s_right_buffer); s_right_buffer = NULL; }
        s_left_buffer = (float *)calloc(1, float_buf_size);
        s_right_buffer = (float *)calloc(1, float_buf_size);
        if (!s_left_buffer || !s_right_buffer) {
            ESP_LOGE(TAG, "Failed to allocate float buffers");
            return -1;
        }
    }

    // Configure I2S channel
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(
        (i2s_port_t)FMRACK_I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = FMRACK_I2S_DMA_BUF_COUNT;
    chan_cfg.dma_frame_num = FMRACK_I2S_DMA_BUF_LEN;
    chan_cfg.auto_clear = true;  // Clear DMA buffer on underflow

    esp_err_t ret = i2s_new_channel(&chan_cfg, &s_tx_handle, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S channel: %s", esp_err_to_name(ret));
        return -1;
    }

    // Configure I2S standard mode (Philips format)
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(FMRACK_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = (gpio_num_t)(FMRACK_I2S_MCLK_PIN >= 0 ? FMRACK_I2S_MCLK_PIN : I2S_GPIO_UNUSED),
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

    ESP_LOGI(TAG, "I2S audio output initialized successfully");
    return 0;
}

/**
 * Audio processing task - runs at highest priority.
 * Continuously renders audio from the FMRack engine and
 * writes it to the I2S DMA buffer.
 */
static void audio_task(void *param)
{
    ESP_LOGI(TAG, "Audio task started on core %d", xPortGetCoreID());

    const int num_samples = FMRACK_BUFFER_SIZE;
    size_t bytes_written = 0;
    size_t buf_bytes = num_samples * 2 * sizeof(int16_t);

    while (s_audio_running) {
        // Clear float buffers
        memset(s_left_buffer, 0, num_samples * sizeof(float));
        memset(s_right_buffer, 0, num_samples * sizeof(float));

        // Render audio from FMRack engine
        fmrack_process_audio(s_left_buffer, s_right_buffer, num_samples);

        // Convert float [-1.0, 1.0] to interleaved 16-bit PCM
        for (int i = 0; i < num_samples; i++) {
            float l = s_left_buffer[i];
            float r = s_right_buffer[i];

            // Soft clamp
            if (l > 1.0f) l = 1.0f;
            else if (l < -1.0f) l = -1.0f;
            if (r > 1.0f) r = 1.0f;
            else if (r < -1.0f) r = -1.0f;

            s_dma_buffer[i * 2]     = (int16_t)(l * 32767.0f);
            s_dma_buffer[i * 2 + 1] = (int16_t)(r * 32767.0f);
        }

        // Write to I2S (blocks until DMA buffer is available)
        esp_err_t ret = i2s_channel_write(s_tx_handle, s_dma_buffer, buf_bytes,
                                           &bytes_written, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "I2S write error: %s", esp_err_to_name(ret));
        }
    }

    ESP_LOGI(TAG, "Audio task exiting");
    vTaskDelete(NULL);
}

int esp32_audio_start(void)
{
    if (s_audio_running) {
        ESP_LOGW(TAG, "Audio task already running");
        return 0;
    }

    if (!s_tx_handle) {
        ESP_LOGE(TAG, "I2S not initialized, call esp32_audio_init() first");
        return -1;
    }

    s_audio_running = true;

    BaseType_t ret = xTaskCreatePinnedToCore(
        audio_task,
        "audio_task",
        AUDIO_TASK_STACK_SIZE,
        NULL,
        AUDIO_TASK_PRIORITY,
        &s_audio_task_handle,
        AUDIO_TASK_CORE
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create audio task");
        s_audio_running = false;
        return -1;
    }

    ESP_LOGI(TAG, "Audio task started (priority %d, stack %d bytes)",
             AUDIO_TASK_PRIORITY, AUDIO_TASK_STACK_SIZE);
    return 0;
}

void esp32_audio_stop(void)
{
    s_audio_running = false;

    // Wait for task to exit
    if (s_audio_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(100));
        s_audio_task_handle = NULL;
    }

    if (s_tx_handle) {
        i2s_channel_disable(s_tx_handle);
        i2s_del_channel(s_tx_handle);
        s_tx_handle = NULL;
    }

    if (s_dma_buffer) {
        heap_caps_free(s_dma_buffer);
        s_dma_buffer = NULL;
    }
    if (s_left_buffer) {
        heap_caps_free(s_left_buffer);
        s_left_buffer = NULL;
    }
    if (s_right_buffer) {
        heap_caps_free(s_right_buffer);
        s_right_buffer = NULL;
    }

    ESP_LOGI(TAG, "Audio stopped and I2S deinitialized");
}

bool esp32_audio_is_running(void)
{
    return s_audio_running;
}
