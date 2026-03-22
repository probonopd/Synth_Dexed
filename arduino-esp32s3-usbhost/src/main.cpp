#include <Arduino.h>

// ESP-IDF (used for USB host + I2S)
extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "usb/usb_host.h"
#include "usb/usb_helpers.h"

#include "driver/i2s_std.h"
#include "esp_log.h"
}

// Dexed engine (from this repo's library sources)
#include "dexed.h"

static const char *TAG = "dexed_arduino";

// USB Audio/MIDI class IDs (avoid relying on indirect header ordering)
#ifndef USB_CLASS_AUDIO
#define USB_CLASS_AUDIO 0x01
#endif

static constexpr uint8_t kUsbSubClassMidiStreaming = 0x03;

// =========================
// User-configurable pins
// =========================
static constexpr gpio_num_t PIN_I2S_MCLK = GPIO_NUM_0;  // optional
static constexpr gpio_num_t PIN_I2S_BCLK = GPIO_NUM_5;
static constexpr gpio_num_t PIN_I2S_WS   = GPIO_NUM_6;
static constexpr gpio_num_t PIN_I2S_DOUT = GPIO_NUM_7;

// USB-OTG FS pins on ESP32-S3
// (wired by board design; listed here as documentation)
// GPIO19 = D-
// GPIO20 = D+

// =========================
// Audio / Synth config
// =========================
static constexpr uint32_t kSampleRate = 44100;
static constexpr uint16_t kBlockSamples = 128;   // keep small for latency
static constexpr uint8_t  kMaxNotes = 12;

static Dexed *g_dexed = nullptr;

// Built-in “E.PIANO 1” voice parameters (156 bytes) sourced from esp32/spiffs_data/performance.ini
// (Dexed::loadVoiceParameters expects 156 bytes and will fix OPE bitmask if it's 0)
static const uint8_t kVoice_EPiano1[NUM_VOICE_PARAMETERS] = {
  0x54, 0x24, 0x0A, 0x10, 0x63, 0x63, 0x5F, 0x00, 0x34, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00,
  0x38, 0x00, 0x03, 0x00, 0x00, 0x54, 0x24, 0x0A, 0x0C, 0x63, 0x63, 0x5F, 0x00, 0x34, 0x12, 0x00,
  0x03, 0x00, 0x00, 0x00, 0x01, 0x49, 0x00, 0x01, 0x00, 0x01, 0x54, 0x24, 0x0A, 0x00, 0x63, 0x63,
  0x63, 0x00, 0x26, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x50, 0x01, 0x00, 0x27, 0x04, 0x26,
  0x07, 0x07, 0x29, 0x5D, 0x5C, 0x5C, 0x00, 0x24, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x63,
  0x00, 0x01, 0x00, 0x0D, 0x2C, 0x24, 0x0A, 0x16, 0x63, 0x63, 0x63, 0x00, 0x22, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x01, 0x4C, 0x00, 0x01, 0x00, 0x07, 0x2D, 0x23, 0x0A, 0x30, 0x63, 0x63, 0x63,
  0x00, 0x24, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x03, 0x01, 0x61, 0x01, 0x00, 0x1A, 0x07, 0x54, 0x5F,
  0x5F, 0x3C, 0x32, 0x32, 0x32, 0x32, 0x0E, 0x07, 0x00, 0x1E, 0x0F, 0x12, 0x00, 0x00, 0x04, 0x01,
  0x18, 0x45, 0x2E, 0x50, 0x49, 0x41, 0x4E, 0x4F, 0x20, 0x31, 0x20, 0x00,
};

// =========================
// I2S output (ESP-IDF driver)
// =========================
static i2s_chan_handle_t g_i2s_tx = nullptr;

static void i2s_init_or_die()
{
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &g_i2s_tx, nullptr));

  i2s_std_config_t std_cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kSampleRate),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
      .mclk = PIN_I2S_MCLK,
      .bclk = PIN_I2S_BCLK,
      .ws   = PIN_I2S_WS,
      .dout = PIN_I2S_DOUT,
      .din  = I2S_GPIO_UNUSED,
      .invert_flags = {
        .mclk_inv = false,
        .bclk_inv = false,
        .ws_inv   = false,
      },
    },
  };

  // 256×fs MCLK is common for external DACs. If your DAC doesn't need MCLK,
  // leaving it connected is typically harmless.
  std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

  ESP_ERROR_CHECK(i2s_channel_init_std_mode(g_i2s_tx, &std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(g_i2s_tx));
}

static void audio_task(void *param)
{
  static int16_t mono[kBlockSamples];
  static int16_t stereo[kBlockSamples * 2];

  while (true) {
    g_dexed->getSamples(mono, kBlockSamples);

    for (int i = 0; i < kBlockSamples; ++i) {
      stereo[i * 2 + 0] = mono[i];
      stereo[i * 2 + 1] = mono[i];
    }

    size_t bytes_written = 0;
    const size_t bytes_to_write = sizeof(stereo);
    esp_err_t err = i2s_channel_write(g_i2s_tx, stereo, bytes_to_write, &bytes_written, portMAX_DELAY);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "i2s_channel_write failed: %s", esp_err_to_name(err));
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}

// =========================
// USB Host MIDI (ESP-IDF USB Host Library)
// =========================
static usb_host_client_handle_t g_usb_client = nullptr;
static usb_device_handle_t g_dev = nullptr;
static usb_transfer_t *g_in_xfer = nullptr;
static uint8_t g_midi_intf = 0;
static uint8_t g_ep_in = 0;
static uint16_t g_ep_in_mps = 64;
static volatile bool g_connected = false;
static volatile bool g_resubmit = false;
static volatile bool g_need_close = false;

static uint8_t g_sysex[2048];
static size_t g_sysex_len = 0;

static void dexed_handle_midi_bytes(const uint8_t *data, int len)
{
  // Dexed expects midiChannel as 1..16
  (void)g_dexed->midiDataHandler(1, const_cast<uint8_t *>(data), static_cast<int16_t>(len));
}

static void usb_midi_push_sysex_bytes(const uint8_t *b, int n)
{
  for (int i = 0; i < n; ++i) {
    if (g_sysex_len < sizeof(g_sysex)) {
      g_sysex[g_sysex_len++] = b[i];
    }
  }
}

static void usb_midi_finish_sysex(const uint8_t *b, int n)
{
  usb_midi_push_sysex_bytes(b, n);
  if (g_sysex_len >= 2) {
    dexed_handle_midi_bytes(g_sysex, static_cast<int>(g_sysex_len));
  }
  g_sysex_len = 0;
}

static void usb_midi_process_packet(const uint8_t pkt[4])
{
  const uint8_t cin = pkt[0] & 0x0F;
  const uint8_t b0 = pkt[1];
  const uint8_t b1 = pkt[2];
  const uint8_t b2 = pkt[3];

  switch (cin) {
    case 0x8: // Note Off
    case 0x9: // Note On
    case 0xA: // Poly pressure
    case 0xB: // CC
    case 0xE: // Pitch bend
      {
        const uint8_t m[3] = {b0, b1, b2};
        dexed_handle_midi_bytes(m, 3);
      }
      break;

    case 0xC: // Program change (2 bytes)
    case 0xD: // Channel pressure (2 bytes)
      {
        const uint8_t m[2] = {b0, b1};
        dexed_handle_midi_bytes(m, 2);
      }
      break;

    // SysEx start/continue/end
    case 0x4: // SysEx starts or continues (3 bytes)
      usb_midi_push_sysex_bytes(&pkt[1], 3);
      break;
    case 0x5: // SysEx ends with 1 byte
      usb_midi_finish_sysex(&pkt[1], 1);
      break;
    case 0x6: // SysEx ends with 2 bytes
      usb_midi_finish_sysex(&pkt[1], 2);
      break;
    case 0x7: // SysEx ends with 3 bytes
      usb_midi_finish_sysex(&pkt[1], 3);
      break;

    case 0xF: // single-byte (e.g., realtime)
      if ((b0 & 0xF8) == 0xF8) {
        dexed_handle_midi_bytes(&b0, 1);
      }
      break;

    default:
      break;
  }
}

static void in_xfer_cb(usb_transfer_t *t)
{
  if (t->status == USB_TRANSFER_STATUS_COMPLETED && t->actual_num_bytes > 0) {
    const uint8_t *p = t->data_buffer;
    for (int i = 0; i + 3 < t->actual_num_bytes; i += 4) {
      usb_midi_process_packet(&p[i]);
    }
  }

  // keep callback short; resubmit/cleanup from the client task
  if (t->status == USB_TRANSFER_STATUS_NO_DEVICE) {
    g_need_close = true;
    g_resubmit = false;
  } else {
    g_resubmit = true;
  }
}

static bool find_midi_streaming_intf(const usb_config_desc_t *cfg, uint8_t *out_intf, uint8_t *out_ep_in, uint16_t *out_mps)
{
  int offset = 0;
  const usb_standard_desc_t *desc = (const usb_standard_desc_t *)cfg;

  while ((desc = usb_parse_next_descriptor(desc, cfg->wTotalLength, &offset)) != nullptr) {
    if (desc->bDescriptorType != USB_B_DESCRIPTOR_TYPE_INTERFACE) {
      continue;
    }

    const usb_intf_desc_t *intf = (const usb_intf_desc_t *)desc;
    if (intf->bInterfaceClass == USB_CLASS_AUDIO && intf->bInterfaceSubClass == kUsbSubClassMidiStreaming /* MIDI Streaming */) {
      // Walk endpoints inside this interface
      int ep_offset = offset;
      const usb_standard_desc_t *ep_desc = desc;
      for (int ep_idx = 0; ep_idx < intf->bNumEndpoints; ++ep_idx) {
        ep_desc = usb_parse_next_descriptor_of_type(ep_desc, cfg->wTotalLength, USB_B_DESCRIPTOR_TYPE_ENDPOINT, &ep_offset);
        if (!ep_desc) break;
        const usb_ep_desc_t *ep = (const usb_ep_desc_t *)ep_desc;
        const uint8_t ep_addr = ep->bEndpointAddress;
        const bool is_in = (ep_addr & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) != 0;
        const uint8_t xfer_type = USB_EP_DESC_GET_XFERTYPE(ep);
        if (is_in && xfer_type == USB_TRANSFER_TYPE_BULK) {
          *out_intf = intf->bInterfaceNumber;
          *out_ep_in = ep_addr;
          *out_mps = USB_EP_DESC_GET_MPS(ep);
          return true;
        }
      }
    }
  }
  return false;
}

static void close_device()
{
  if (!g_dev) return;

  g_connected = false;

  if (g_in_xfer) {
    usb_host_transfer_free(g_in_xfer);
    g_in_xfer = nullptr;
  }

  usb_host_interface_release(g_usb_client, g_dev, g_midi_intf);
  usb_host_device_close(g_usb_client, g_dev);
  g_dev = nullptr;
}

static void try_open_device(uint8_t addr)
{
  if (g_connected) return;

  usb_device_handle_t dev = nullptr;
  esp_err_t err = usb_host_device_open(g_usb_client, addr, &dev);
  if (err != ESP_OK) {
    return;
  }

  const usb_config_desc_t *cfg = nullptr;
  err = usb_host_get_active_config_descriptor(dev, &cfg);
  if (err != ESP_OK || !cfg) {
    usb_host_device_close(g_usb_client, dev);
    return;
  }

  uint8_t intf_num = 0;
  uint8_t ep_in = 0;
  uint16_t mps = 64;
  if (!find_midi_streaming_intf(cfg, &intf_num, &ep_in, &mps)) {
    usb_host_device_close(g_usb_client, dev);
    return;
  }

  err = usb_host_interface_claim(g_usb_client, dev, intf_num, 0);
  if (err != ESP_OK) {
    usb_host_device_close(g_usb_client, dev);
    return;
  }

  usb_transfer_t *t = nullptr;
  const int xfer_bytes = usb_round_up_to_mps(64, mps);
  err = usb_host_transfer_alloc(xfer_bytes, 0, &t);
  if (err != ESP_OK || !t) {
    usb_host_interface_release(g_usb_client, dev, intf_num);
    usb_host_device_close(g_usb_client, dev);
    return;
  }

  g_dev = dev;
  g_midi_intf = intf_num;
  g_ep_in = ep_in;
  g_ep_in_mps = mps;
  g_in_xfer = t;

  g_in_xfer->device_handle = g_dev;
  g_in_xfer->bEndpointAddress = g_ep_in;
  g_in_xfer->callback = in_xfer_cb;
  g_in_xfer->context = nullptr;
  g_in_xfer->num_bytes = xfer_bytes;

  err = usb_host_transfer_submit(g_in_xfer);
  if (err != ESP_OK) {
    close_device();
    return;
  }

  g_connected = true;
  ESP_LOGI(TAG, "USB-MIDI connected (addr=%u intf=%u ep=0x%02X mps=%u)", addr, intf_num, ep_in, (unsigned)mps);
}

static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg)
{
  if (event_msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
    // handled in task loop (keep callback short)
  } else if (event_msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
    g_need_close = true;
  }
}

static void usb_host_daemon_task(void *arg)
{
  usb_host_config_t cfg = {
    .skip_phy_setup = false,
    .root_port_unpowered = false,
    .intr_flags = ESP_INTR_FLAG_LEVEL2,
    .enum_filter_cb = nullptr,
  };

  ESP_ERROR_CHECK(usb_host_install(&cfg));
  ESP_LOGI(TAG, "USB Host installed");

  while (true) {
    uint32_t flags = 0;
    esp_err_t err = usb_host_lib_handle_events(pdMS_TO_TICKS(200), &flags);
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
      ESP_LOGW(TAG, "usb_host_lib_handle_events: %s", esp_err_to_name(err));
    }
  }
}

static void usb_midi_client_task(void *arg)
{
  usb_host_client_config_t client_cfg = {
    .is_synchronous = false,
    .max_num_event_msg = 5,
    .async = {
      .client_event_callback = client_event_cb,
      .callback_arg = nullptr,
    },
  };
  ESP_ERROR_CHECK(usb_host_client_register(&client_cfg, &g_usb_client));
  ESP_LOGI(TAG, "USB host client registered - waiting for MIDI device...");

  uint32_t scan_count = 0;

  while (true) {
    // Dispatch events + transfer callbacks
    usb_host_client_handle_events(g_usb_client, pdMS_TO_TICKS(5));

    if (g_need_close) {
      g_need_close = false;
      close_device();
    }

    if (g_resubmit && g_connected && g_in_xfer) {
      g_resubmit = false;
      esp_err_t err = usb_host_transfer_submit(g_in_xfer);
      if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "USB IN resubmit failed: %s", esp_err_to_name(err));
      }
    }

    // Periodic scan (works well with hubs: opens only enumerated addresses)
    if (!g_connected && ++scan_count >= 200) {
      scan_count = 0;
      uint8_t addrs[16];
      int num = 0;
      if (usb_host_device_addr_list_fill(sizeof(addrs), addrs, &num) == ESP_OK && num > 0) {
        for (int i = 0; i < num && !g_connected; ++i) {
          try_open_device(addrs[i]);
        }
      }
    }
  }
}

// =========================
// Arduino entry points
// =========================
void setup()
{
  Serial.begin(115200);
  delay(200);
  ESP_LOGI(TAG, "Booting Dexed (Arduino + ESP-IDF USB Host)...");

  g_dexed = new Dexed(kMaxNotes, kSampleRate);
  g_dexed->activate();
  g_dexed->loadVoiceParameters(const_cast<uint8_t *>(kVoice_EPiano1));
  g_dexed->setGain(0.6f);

  i2s_init_or_die();

  // Audio task on core 1 (keep USB on core 0)
  xTaskCreatePinnedToCore(audio_task, "audio", 8192, nullptr, configMAX_PRIORITIES - 1, nullptr, 1);

  // USB host tasks on core 0
  xTaskCreatePinnedToCore(usb_host_daemon_task, "usb_host", 4096, nullptr, 20, nullptr, 0);
  xTaskCreatePinnedToCore(usb_midi_client_task, "usb_midi", 6144, nullptr, 21, nullptr, 0);

  ESP_LOGI(TAG, "Ready: plug USB hub + USB-MIDI keyboard into the ESP32-S3 USB-OTG port");
}

void loop()
{
  // Real-time work is done in tasks; keep loop alive for Serial/debug.
  delay(1000);
}
