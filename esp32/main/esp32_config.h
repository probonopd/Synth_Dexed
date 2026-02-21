/*
 * FMRack ESP32-C5 Port - Configuration Header
 *
 * Centralizes all compile-time configuration for the ESP32 port.
 * Values come from Kconfig (menuconfig) with sensible defaults.
 */

#pragma once

#include "sdkconfig.h"

// =====================
// Audio Configuration
// =====================
#ifdef CONFIG_FMRACK_SAMPLE_RATE
#define FMRACK_SAMPLE_RATE      CONFIG_FMRACK_SAMPLE_RATE
#else
#define FMRACK_SAMPLE_RATE      48000
#endif

#ifdef CONFIG_FMRACK_BUFFER_SIZE
#define FMRACK_BUFFER_SIZE      CONFIG_FMRACK_BUFFER_SIZE
#else
#define FMRACK_BUFFER_SIZE      256
#endif

#ifdef CONFIG_FMRACK_NUM_MODULES
#define FMRACK_NUM_MODULES      CONFIG_FMRACK_NUM_MODULES
#else
#define FMRACK_NUM_MODULES      4
#endif

// =====================
// I2S Pin Configuration
// =====================
#ifdef CONFIG_FMRACK_I2S_BCK_PIN
#define FMRACK_I2S_BCK_PIN     CONFIG_FMRACK_I2S_BCK_PIN
#else
#define FMRACK_I2S_BCK_PIN     6
#endif

#ifdef CONFIG_FMRACK_I2S_WS_PIN
#define FMRACK_I2S_WS_PIN      CONFIG_FMRACK_I2S_WS_PIN
#else
#define FMRACK_I2S_WS_PIN      7
#endif

#ifdef CONFIG_FMRACK_I2S_DOUT_PIN
#define FMRACK_I2S_DOUT_PIN    CONFIG_FMRACK_I2S_DOUT_PIN
#else
#define FMRACK_I2S_DOUT_PIN    15
#endif

#ifdef CONFIG_FMRACK_I2S_MCLK_PIN
#define FMRACK_I2S_MCLK_PIN    CONFIG_FMRACK_I2S_MCLK_PIN
#else
#define FMRACK_I2S_MCLK_PIN    (-1)
#endif

#ifdef CONFIG_FMRACK_I2S_NUM
#define FMRACK_I2S_NUM         CONFIG_FMRACK_I2S_NUM
#else
#define FMRACK_I2S_NUM         0
#endif

// =====================
// MIDI Configuration
// =====================
#ifdef CONFIG_FMRACK_MIDI_UART_NUM
#define FMRACK_MIDI_UART_NUM   CONFIG_FMRACK_MIDI_UART_NUM
#else
#define FMRACK_MIDI_UART_NUM   1
#endif

#ifdef CONFIG_FMRACK_MIDI_RX_PIN
#define FMRACK_MIDI_RX_PIN     CONFIG_FMRACK_MIDI_RX_PIN
#else
#define FMRACK_MIDI_RX_PIN     4
#endif

#ifdef CONFIG_FMRACK_MIDI_TX_PIN
#define FMRACK_MIDI_TX_PIN     CONFIG_FMRACK_MIDI_TX_PIN
#else
#define FMRACK_MIDI_TX_PIN     5
#endif

#ifdef CONFIG_FMRACK_MIDI_USB_ENABLE
#define FMRACK_MIDI_USB_ENABLE 1
#else
#define FMRACK_MIDI_USB_ENABLE 1
#endif

#ifdef CONFIG_FMRACK_MIDI_UDP_ENABLE
#define FMRACK_MIDI_UDP_ENABLE 1
#else
#define FMRACK_MIDI_UDP_ENABLE 1
#endif

#ifdef CONFIG_FMRACK_MIDI_UDP_PORT
#define FMRACK_MIDI_UDP_PORT   CONFIG_FMRACK_MIDI_UDP_PORT
#else
#define FMRACK_MIDI_UDP_PORT   50007
#endif

// =====================
// Wi-Fi Configuration
// =====================
#ifdef CONFIG_FMRACK_WIFI_SSID
#define FMRACK_WIFI_SSID       CONFIG_FMRACK_WIFI_SSID
#else
#define FMRACK_WIFI_SSID       "FMRack"
#endif

#ifdef CONFIG_FMRACK_WIFI_PASSWORD
#define FMRACK_WIFI_PASSWORD   CONFIG_FMRACK_WIFI_PASSWORD
#else
#define FMRACK_WIFI_PASSWORD   ""
#endif

#ifdef CONFIG_FMRACK_WIFI_AP_MODE
#define FMRACK_WIFI_AP_MODE    1
#else
#define FMRACK_WIFI_AP_MODE    0
#endif

// =====================
// Storage Configuration
// =====================
#ifdef CONFIG_FMRACK_SPIFFS_MOUNT_POINT
#define FMRACK_SPIFFS_MOUNT    CONFIG_FMRACK_SPIFFS_MOUNT_POINT
#else
#define FMRACK_SPIFFS_MOUNT    "/spiffs"
#endif

#ifdef CONFIG_FMRACK_DEFAULT_PERFORMANCE
#define FMRACK_DEFAULT_PERF    CONFIG_FMRACK_DEFAULT_PERFORMANCE
#else
#define FMRACK_DEFAULT_PERF    "/spiffs/performance.ini"
#endif

// =====================
// Hardware
// =====================
#ifdef CONFIG_FMRACK_STATUS_LED_PIN
#define FMRACK_STATUS_LED_PIN  CONFIG_FMRACK_STATUS_LED_PIN
#else
#define FMRACK_STATUS_LED_PIN  8
#endif

// =====================
// Audio processing constants
// =====================
#define FMRACK_I2S_DMA_BUF_COUNT   4
#define FMRACK_I2S_DMA_BUF_LEN     FMRACK_BUFFER_SIZE

// MIDI baud rate (standard)
#define MIDI_BAUD_RATE              31250
#define MIDI_UART_BUF_SIZE          512

// Task priorities (higher = more important)
#define AUDIO_TASK_PRIORITY         (configMAX_PRIORITIES - 1)
#define MIDI_TASK_PRIORITY          (configMAX_PRIORITIES - 2)
#define UDP_TASK_PRIORITY           (configMAX_PRIORITIES - 3)
#define WIFI_TASK_PRIORITY          5
#define STATUS_TASK_PRIORITY        2

// Task stack sizes
#define AUDIO_TASK_STACK_SIZE       (8 * 1024)
#define MIDI_TASK_STACK_SIZE        (4 * 1024)
#define UDP_TASK_STACK_SIZE         (4 * 1024)
#define STATUS_TASK_STACK_SIZE      (2 * 1024)

// Audio task core affinity (ESP32-C5 is single-core, so always 0)
#define AUDIO_TASK_CORE             0
#define MIDI_TASK_CORE              0
