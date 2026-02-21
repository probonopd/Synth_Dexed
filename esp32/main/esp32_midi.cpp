/*
 * FMRack ESP32-C5 Port - MIDI Input Implementation
 *
 * Implements hardware MIDI input via UART (31250 baud, standard DIN-5)
 * and USB MIDI via the CDC serial port.
 *
 * MIDI message parsing follows the standard:
 * - Status byte: 0x80-0xEF (channel messages), 0xF0-0xFF (system messages)
 * - Running status is supported
 * - SysEx messages (F0...F7) are buffered and forwarded to the engine
 */

#include "esp32_midi.h"
#include "esp32_config.h"
#include "fmrack_wrapper.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG = "fmrack_midi";

// Task handles
static TaskHandle_t s_midi_uart_task = NULL;
static TaskHandle_t s_midi_usb_task = NULL;
static volatile bool s_midi_running = false;

// SysEx buffer (max 4KB for bulk dumps)
#define SYSEX_BUF_SIZE 4200
static uint8_t s_sysex_buf[SYSEX_BUF_SIZE];
static int s_sysex_len = 0;
static bool s_in_sysex = false;

/**
 * Get the expected data byte count for a MIDI status byte.
 */
static int midi_msg_len(uint8_t status)
{
    switch (status & 0xF0) {
        case 0x80: return 2;  // Note Off
        case 0x90: return 2;  // Note On
        case 0xA0: return 2;  // Polyphonic Aftertouch
        case 0xB0: return 2;  // Control Change
        case 0xC0: return 1;  // Program Change
        case 0xD0: return 1;  // Channel Aftertouch
        case 0xE0: return 2;  // Pitch Bend
        default:   return 0;  // System messages handled separately
    }
}

/**
 * Process incoming MIDI bytes from any source.
 * Implements a state machine for MIDI message parsing with running status.
 */
typedef struct {
    uint8_t status;      // Current running status
    uint8_t data[2];     // Data bytes
    int     data_count;  // Number of data bytes received
    int     data_needed; // Number of data bytes expected
} midi_parser_t;

static void midi_parser_init(midi_parser_t *p)
{
    memset(p, 0, sizeof(*p));
}

static void midi_parser_process_byte(midi_parser_t *p, uint8_t byte)
{
    // Handle SysEx
    if (s_in_sysex) {
        if (s_sysex_len < SYSEX_BUF_SIZE) {
            s_sysex_buf[s_sysex_len++] = byte;
        }
        if (byte == 0xF7) {
            // SysEx complete - forward to engine
            if (s_sysex_len >= 2) {
                uint8_t sysex_channel = 0;
                if (s_sysex_len > 2 && s_sysex_buf[1] == 0x43) {
                    sysex_channel = (s_sysex_buf[2] & 0x0F) + 1;
                }
                fmrack_handle_sysex(s_sysex_buf, s_sysex_len, sysex_channel);
            }
            s_in_sysex = false;
            s_sysex_len = 0;
        }
        return;
    }

    if (byte == 0xF0) {
        // Start of SysEx
        s_in_sysex = true;
        s_sysex_len = 0;
        s_sysex_buf[s_sysex_len++] = byte;
        return;
    }

    // Real-time messages (0xF8-0xFF) can occur anywhere - ignore them
    if (byte >= 0xF8) {
        return;
    }

    // System common messages (0xF1-0xF7) - reset running status
    if (byte >= 0xF1 && byte <= 0xF7) {
        p->status = 0;
        p->data_count = 0;
        return;
    }

    // Status byte (0x80-0xEF)
    if (byte & 0x80) {
        p->status = byte;
        p->data_count = 0;
        p->data_needed = midi_msg_len(byte);
        return;
    }

    // Data byte - need valid running status
    if (p->status == 0) {
        return;  // No running status, discard
    }

    p->data[p->data_count++] = byte;

    if (p->data_count >= p->data_needed) {
        // Complete message - forward to engine
        if (p->data_needed == 2) {
            fmrack_handle_midi(p->status, p->data[0], p->data[1]);
        } else if (p->data_needed == 1) {
            fmrack_handle_midi(p->status, p->data[0], 0);
        }
        p->data_count = 0;  // Reset for running status
    }
}

/**
 * Hardware UART MIDI task.
 * Reads raw bytes from the UART at 31250 baud and parses MIDI messages.
 */
static void midi_uart_task(void *param)
{
    ESP_LOGI(TAG, "MIDI UART task started (UART%d, RX pin %d)",
             FMRACK_MIDI_UART_NUM, FMRACK_MIDI_RX_PIN);

    midi_parser_t parser;
    midi_parser_init(&parser);

    uint8_t buf[64];

    while (s_midi_running) {
        int len = uart_read_bytes((uart_port_t)FMRACK_MIDI_UART_NUM,
                                   buf, sizeof(buf), pdMS_TO_TICKS(10));
        if (len > 0) {
            for (int i = 0; i < len; i++) {
                midi_parser_process_byte(&parser, buf[i]);
            }
        }
    }

    ESP_LOGI(TAG, "MIDI UART task exiting");
    vTaskDelete(NULL);
}

/**
 * USB CDC MIDI task.
 * Reads raw MIDI bytes from UART0 (USB CDC console).
 * This allows sending MIDI data over the USB connection.
 */
static void midi_usb_task(void *param)
{
    ESP_LOGI(TAG, "USB MIDI task started (UART0)");

    midi_parser_t parser;
    midi_parser_init(&parser);

    uint8_t buf[64];

    while (s_midi_running) {
        int len = uart_read_bytes(UART_NUM_0, buf, sizeof(buf), pdMS_TO_TICKS(10));
        if (len > 0) {
            for (int i = 0; i < len; i++) {
                // Only process bytes that look like MIDI (status byte 0x80+)
                // This prevents console text from being misinterpreted
                if (buf[i] & 0x80 || parser.status != 0 || s_in_sysex) {
                    midi_parser_process_byte(&parser, buf[i]);
                }
            }
        }
    }

    ESP_LOGI(TAG, "USB MIDI task exiting");
    vTaskDelete(NULL);
}

int esp32_midi_init(void)
{
    ESP_LOGI(TAG, "Initializing MIDI input...");

    // Configure hardware MIDI UART
    uart_config_t uart_config = {
        .baud_rate = MIDI_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_driver_install((uart_port_t)FMRACK_MIDI_UART_NUM,
                                         MIDI_UART_BUF_SIZE, 0, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART%d driver: %s",
                 FMRACK_MIDI_UART_NUM, esp_err_to_name(ret));
        return -1;
    }

    ret = uart_param_config((uart_port_t)FMRACK_MIDI_UART_NUM, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART%d: %s",
                 FMRACK_MIDI_UART_NUM, esp_err_to_name(ret));
        return -1;
    }

    ret = uart_set_pin((uart_port_t)FMRACK_MIDI_UART_NUM,
                        FMRACK_MIDI_TX_PIN, FMRACK_MIDI_RX_PIN,
                        UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART%d pins: %s",
                 FMRACK_MIDI_UART_NUM, esp_err_to_name(ret));
        return -1;
    }

    ESP_LOGI(TAG, "Hardware MIDI UART%d initialized (31250 baud, RX=%d, TX=%d)",
             FMRACK_MIDI_UART_NUM, FMRACK_MIDI_RX_PIN, FMRACK_MIDI_TX_PIN);

    return 0;
}

int esp32_midi_start(void)
{
    if (s_midi_running) {
        ESP_LOGW(TAG, "MIDI tasks already running");
        return 0;
    }

    s_midi_running = true;

    // Start hardware MIDI task
    BaseType_t ret = xTaskCreatePinnedToCore(
        midi_uart_task,
        "midi_uart",
        MIDI_TASK_STACK_SIZE,
        NULL,
        MIDI_TASK_PRIORITY,
        &s_midi_uart_task,
        MIDI_TASK_CORE
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create MIDI UART task");
        s_midi_running = false;
        return -1;
    }

#if FMRACK_MIDI_USB_ENABLE
    // Start USB MIDI task
    ret = xTaskCreatePinnedToCore(
        midi_usb_task,
        "midi_usb",
        MIDI_TASK_STACK_SIZE,
        NULL,
        MIDI_TASK_PRIORITY - 1,
        &s_midi_usb_task,
        MIDI_TASK_CORE
    );
    if (ret != pdPASS) {
        ESP_LOGW(TAG, "Failed to create USB MIDI task (non-fatal)");
        // Not fatal - hardware MIDI still works
    }
#endif

    ESP_LOGI(TAG, "MIDI input started");
    return 0;
}

void esp32_midi_stop(void)
{
    s_midi_running = false;

    if (s_midi_uart_task) {
        vTaskDelay(pdMS_TO_TICKS(50));
        s_midi_uart_task = NULL;
    }
    if (s_midi_usb_task) {
        vTaskDelay(pdMS_TO_TICKS(50));
        s_midi_usb_task = NULL;
    }

    uart_driver_delete((uart_port_t)FMRACK_MIDI_UART_NUM);

    ESP_LOGI(TAG, "MIDI input stopped");
}
