/*
 * FMRack ESP32-S3 Port - Configuration Header
 *
 * Centralizes all compile-time configuration for the ESP32-S3 port.
 * Leverages dual-core Xtensa LX7, 8 MB PSRAM, and USB OTG.
 * Values come from Kconfig (menuconfig) with sensible defaults.
 */

#pragma once

#include "sdkconfig.h"

// =====================
// Audio Configuration
// =====================
// Sample rate.  With the LUT and audio-path optimizations (reduced sine/exp2
// tables, int16-only rendering) the engine comfortably handles 48 kHz (and
// 96 kHz on lightly-loaded systems).  Higher rates improve audio fidelity but
// increase CPU cost.  This value is now configurable via menuconfig (see
// FMRACK_SAMPLE_RATE) so you can tweak it for "hifi" builds.
#ifdef CONFIG_FMRACK_SAMPLE_RATE
#define FMRACK_SAMPLE_RATE      CONFIG_FMRACK_SAMPLE_RATE
#else
#define FMRACK_SAMPLE_RATE      48000
#endif
// Polyphony (number of simultaneous notes)
#ifdef CONFIG_FMRACK_POLYPHONY
#define FMRACK_POLYPHONY        CONFIG_FMRACK_POLYPHONY
#else
#define FMRACK_POLYPHONY        12
#endif
#ifdef CONFIG_FMRACK_BUFFER_SIZE
#define FMRACK_BUFFER_SIZE      CONFIG_FMRACK_BUFFER_SIZE
#else
#define FMRACK_BUFFER_SIZE      256
#endif

// Select which Dexed engine to use. 0 = MSFA (modern), 1 = MKI (legacy),
// 2 = OPL.  MSFA has the best performance and is now the default.  You can
// override this from menuconfig by setting CONFIG_FMRACK_ENGINE or by editing
// sdkconfig directly.
#ifdef CONFIG_FMRACK_ENGINE
#define FMRACK_ENGINE           CONFIG_FMRACK_ENGINE
#else
#define FMRACK_ENGINE           0
#endif


#ifdef CONFIG_FMRACK_NUM_MODULES
#define FMRACK_NUM_MODULES      CONFIG_FMRACK_NUM_MODULES
#else
#define FMRACK_NUM_MODULES      8
#endif

// =====================
// I2S Pin Configuration (ESP32-S3 DevKitC defaults)
// =====================
#ifdef CONFIG_FMRACK_I2S_BCK_PIN
#define FMRACK_I2S_BCK_PIN     CONFIG_FMRACK_I2S_BCK_PIN
#else
#define FMRACK_I2S_BCK_PIN     5
#endif

#ifdef CONFIG_FMRACK_I2S_WS_PIN
#define FMRACK_I2S_WS_PIN      CONFIG_FMRACK_I2S_WS_PIN
#else
#define FMRACK_I2S_WS_PIN      6
#endif

#ifdef CONFIG_FMRACK_I2S_DOUT_PIN
#define FMRACK_I2S_DOUT_PIN    CONFIG_FMRACK_I2S_DOUT_PIN
#else
#define FMRACK_I2S_DOUT_PIN    7
#endif

#ifdef CONFIG_FMRACK_I2S_MCLK_PIN
#define FMRACK_I2S_MCLK_PIN    CONFIG_FMRACK_I2S_MCLK_PIN
#else
#define FMRACK_I2S_MCLK_PIN    0
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
#define FMRACK_MIDI_RX_PIN     18
#endif

#ifdef CONFIG_FMRACK_MIDI_TX_PIN
#define FMRACK_MIDI_TX_PIN     CONFIG_FMRACK_MIDI_TX_PIN
#else
#define FMRACK_MIDI_TX_PIN     17
#endif

// USB-MIDI via USB OTG (ESP32-S3 native USB, GPIO19=D-, GPIO20=D+)
#ifdef CONFIG_FMRACK_MIDI_USB_ENABLE
#define FMRACK_MIDI_USB_ENABLE CONFIG_FMRACK_MIDI_USB_ENABLE
#else
#define FMRACK_MIDI_USB_ENABLE 1
#endif

#ifdef CONFIG_FMRACK_MIDI_UDP_ENABLE
#define FMRACK_MIDI_UDP_ENABLE 1
#else
#define FMRACK_MIDI_UDP_ENABLE 0
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
// ESP32-S3-DevKitC-1 has an addressable RGB LED on GPIO48
#ifdef CONFIG_FMRACK_STATUS_LED_PIN
#define FMRACK_STATUS_LED_PIN  CONFIG_FMRACK_STATUS_LED_PIN
#else
#define FMRACK_STATUS_LED_PIN  48
#endif

// =====================
// Audio processing constants
// =====================
// I2S DMA ring: 4 descriptors x 256 frames = ~23 ms total DMA buffer.
// At steady state a newly-written descriptor sits behind (N-1)=3 already-queued
// descriptors, so audio output latency from write → DAC is ~3 x 5.83 ms ≈ 17 ms.
// This is a large reduction from the previous 32-descriptor (186 ms) configuration.
// The Dexed object is in internal SRAM, so render stalls from USB are eliminated
// and 4 descriptors (23 ms cushion) is ample.
#define FMRACK_I2S_DMA_BUF_COUNT        4
#define FMRACK_I2S_DMA_BUF_LEN         FMRACK_BUFFER_SIZE

// Pre-render ring between render task and write task.
// 2 blocks = double-buffering: render fills one slot while write drains the other.
// Latency contribution: ~1 block = 5.83 ms typical (one block is in-flight to DMA).
// Reduced from 16 (93 ms) to 2 (12 ms) for lower key-to-sound latency.
// The ring only needs more than 2 slots if the render task experiences sustained
// stalls longer than one block period (5.83 ms), which doesn't happen with Dexed
// in internal SRAM and audio pinned to Core 1.
#define FMRACK_AUDIO_PRERENDER_BLOCKS   2

// MIDI baud rate (standard)
#define MIDI_BAUD_RATE              31250
#define MIDI_UART_BUF_SIZE          512

// Task priorities (higher = more important)
#define AUDIO_TASK_PRIORITY         (configMAX_PRIORITIES - 1)  // render task
#define AUDIO_WRITE_TASK_PRIORITY   (configMAX_PRIORITIES - 2)  // write task (same core, lower)
#define MIDI_TASK_PRIORITY          (configMAX_PRIORITIES - 2)
#define USB_MIDI_TASK_PRIORITY      (configMAX_PRIORITIES - 2)
#define UDP_TASK_PRIORITY           (configMAX_PRIORITIES - 3)
#define WIFI_TASK_PRIORITY          5
#define STATUS_TASK_PRIORITY        2

// Task stack sizes
#define AUDIO_TASK_STACK_SIZE       (8 * 1024)
#define AUDIO_WRITE_TASK_STACK_SIZE (4 * 1024)
#define MIDI_TASK_STACK_SIZE        (4 * 1024)
#define USB_MIDI_TASK_STACK_SIZE    (4 * 1024)
#define UDP_TASK_STACK_SIZE         (4 * 1024)
#define STATUS_TASK_STACK_SIZE      (2 * 1024)

// =====================
// Dual-core task affinity (ESP32-S3 has 2 cores)
// Core 0: Protocol tasks (MIDI, Wi-Fi, USB)
// Core 1: Real-time audio rendering (dedicated)
// =====================
#define AUDIO_TASK_CORE             1
#define MIDI_TASK_CORE              0
#define USB_MIDI_TASK_CORE          0
#define WIFI_TASK_CORE              0
