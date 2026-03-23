/*
 * FMRack ESP32-S3 Port - MIDI Input Implementation
 *
 * Three MIDI input paths:
 *
 * 1. Hardware UART MIDI (31250 baud, standard DIN-5 via optocoupler)
 *    - Classic 5-pin DIN MIDI input, parsed byte-by-byte
 *    - Running status supported
 *    - SysEx buffered up to 4 KB (enough for DX7 32-voice bulk dumps)
 *
 * 2. USB Host MIDI (ESP32-S3 USB OTG in Host mode, GPIO19/20)
 *    - Plug a class-compliant USB-MIDI keyboard directly into the USB port
 *    - ESP32 acts as the USB host -- enumerates and reads from the device
 *    - Supports hot-plug: connect/disconnect at any time
 *
 * 3. UDP MIDI over Wi-Fi (handled in esp32_wifi.cpp)
 *
 * MIDI routing: all three sources feed into the same FMRack engine
 * via fmrack_handle_midi() / fmrack_handle_sysex().
 */

#include "esp32_midi.h"
#include "esp32_config.h"
#include "dexed_raw.h"
#include "launchpad.h"
#include "step_sequencer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"

#include <string.h>

/* ---------- USB Host MIDI ---------- */
#if FMRACK_MIDI_USB_ENABLE
#include "usb/usb_host.h"
#endif

static const char *TAG = "dexed_midi";

#if FMRACK_MIDI_USB_ENABLE
static void usb_log_event_flags(uint32_t event_flags)
{
    if (event_flags == 0) {
        return;
    }

    ESP_LOGI(TAG, "USB Host lib event flags: 0x%08lx%s%s",
             (unsigned long)event_flags,
             (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) ? " NO_CLIENTS" : "",
             (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) ? " ALL_FREE" : "");
}
#endif

// Task handles
static TaskHandle_t s_midi_uart_task = NULL;
static volatile bool s_midi_running = false;

// SysEx buffer (max 4 KB for bulk dumps)
#define SYSEX_BUF_SIZE 4200
static uint8_t s_sysex_buf[SYSEX_BUF_SIZE];
static int s_sysex_len = 0;
static bool s_in_sysex = false;

// Separate SysEx state for USB-MIDI
#if FMRACK_MIDI_USB_ENABLE
static uint8_t s_usb_sysex_buf[SYSEX_BUF_SIZE];
static int s_usb_sysex_len = 0;
#endif

/* ================================================
 * MIDI byte-stream parser (for UART input)
 * ================================================ */

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
        default:   return 0;
    }
}

typedef struct {
    uint8_t status;
    uint8_t data[2];
    int     data_count;
    int     data_needed;
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
            if (s_sysex_len >= 2) {
                uint8_t sysex_channel = 0;
                if (s_sysex_len > 2 && s_sysex_buf[1] == 0x43) {
                    sysex_channel = (s_sysex_buf[2] & 0x0F) + 1;
                }
                dexed_raw_handle_sysex(s_sysex_buf, s_sysex_len, sysex_channel);
            }
            s_in_sysex = false;
            s_sysex_len = 0;
        }
        return;
    }

    if (byte == 0xF0) {
        s_in_sysex = true;
        s_sysex_len = 0;
        s_sysex_buf[s_sysex_len++] = byte;
        return;
    }

    if (byte >= 0xF8) return;  // Real-time: ignore

    if (byte >= 0xF1 && byte <= 0xF7) {
        p->status = 0;
        p->data_count = 0;
        return;
    }

    if (byte & 0x80) {
        p->status = byte;
        p->data_count = 0;
        p->data_needed = midi_msg_len(byte);
        return;
    }

    if (p->status == 0) return;

    p->data[p->data_count++] = byte;

    if (p->data_count >= p->data_needed) {
        if (p->data_needed == 2) {
            dexed_raw_handle_midi(p->status, p->data[0], p->data[1]);
        } else if (p->data_needed == 1) {
            dexed_raw_handle_midi(p->status, p->data[0], 0);
        }
        p->data_count = 0;
    }
}

/* ================================================
 * Hardware UART MIDI task
 * ================================================ */

static void midi_uart_task(void *param)
{
    ESP_LOGI(TAG, "MIDI UART task started on core %d (UART%d, RX pin %d)",
             xPortGetCoreID(), FMRACK_MIDI_UART_NUM, FMRACK_MIDI_RX_PIN);

    midi_parser_t parser;
    midi_parser_init(&parser);

    uint8_t buf[64];

    while (s_midi_running) {
        int len = uart_read_bytes((uart_port_t)FMRACK_MIDI_UART_NUM,
                                   buf, sizeof(buf), pdMS_TO_TICKS(1));
        if (len > 0) {
            for (int i = 0; i < len; i++) {
                midi_parser_process_byte(&parser, buf[i]);
            }
        }
    }

    ESP_LOGI(TAG, "MIDI UART task exiting");
    vTaskDelete(NULL);
}

/* ================================================
 * USB Host MIDI (ESP32-S3 as USB host, reads from
 * an attached USB-MIDI keyboard/controller)
 * ================================================ */

#if FMRACK_MIDI_USB_ENABLE

/* USB Audio / MIDI class constants */
#define USB_CLASS_AUDIO             0x01
#define USB_SUBCLASS_MIDI_STREAMING 0x03

/* Transfer buffer size (multiple of 64-byte MPS) */
#define MIDI_USB_XFER_BUF_SIZE     64

/* State for one connected MIDI device */
typedef struct {
    usb_device_handle_t dev_hdl;
    uint8_t midi_intf_num;
    uint8_t ep_in;                  /* Bulk IN endpoint address */
    uint8_t ep_out;                 /* Bulk OUT endpoint address (optional) */
    uint16_t ep_in_mps;            /* Max packet size */
    usb_transfer_t *xfer_in;       /* Persistent IN transfer */
    bool connected;
    usb_transfer_t *xfer_out;       /* Bulk OUT transfer (NULL if ep_out == 0) */
    volatile bool xfer_out_busy;    /* true while OUT transfer in-flight */
    bool is_launchpad;              /* Novation VID detected */
} midi_device_t;

static midi_device_t s_midi_dev = {};
static usb_host_client_handle_t s_client_hdl = NULL;
static volatile bool s_usb_host_running = false;
static TaskHandle_t s_usb_host_lib_task = NULL;
static TaskHandle_t s_midi_host_task_hdl = NULL;

/* Set by midi_transfer_cb; resubmit happens in midi_host_task to keep the
 * callback short and decouple USB DMA scheduling from the ISR context. */
static volatile bool s_xfer_needs_resubmit = false;

/* Action flags set from callbacks, processed in main loop */
#define MIDI_HOST_ACTION_OPEN  (1 << 0)
#define MIDI_HOST_ACTION_CLOSE (1 << 1)
static volatile uint32_t s_actions = 0;
static volatile uint8_t s_new_dev_addr = 0;

/* ----------------------------------------------------------------
 * USB-MIDI packet parser (4-byte USB-MIDI event packets -> engine)
 * ---------------------------------------------------------------- */
static void usb_midi_process_sysex_continue(const uint8_t *data, int count)
{
    for (int i = 0; i < count; i++) {
        if (s_usb_sysex_len < SYSEX_BUF_SIZE)
            s_usb_sysex_buf[s_usb_sysex_len++] = data[i];
    }
}

static void usb_midi_finish_sysex(const uint8_t *data, int count)
{
    for (int i = 0; i < count; i++) {
        if (s_usb_sysex_len < SYSEX_BUF_SIZE)
            s_usb_sysex_buf[s_usb_sysex_len++] = data[i];
    }
    if (s_usb_sysex_len >= 2) {
        uint8_t ch = 0;
        if (s_usb_sysex_len > 2 && s_usb_sysex_buf[1] == 0x43)
            ch = (s_usb_sysex_buf[2] & 0x0F) + 1;
        dexed_raw_handle_sysex(s_usb_sysex_buf, s_usb_sysex_len, ch);
    }
    s_usb_sysex_len = 0;
}

static void usb_midi_process_packet(const uint8_t *pkt)
{
    uint8_t cin = pkt[0] & 0x0F;
    uint8_t b0  = pkt[1];
    uint8_t b1  = pkt[2];
    uint8_t b2  = pkt[3];

    switch (cin) {
        /* 3-byte channel messages */
        case 0x08: /* Note Off */
        case 0x09: /* Note On */
            if (s_midi_dev.is_launchpad) {
                launchpad_handle_note(b1, (cin == 0x08) ? 0 : b2);
            } else {
                dexed_raw_handle_midi(b0, b1, b2);
            }
            break;

        case 0x0B: /* Control Change */
            if (s_midi_dev.is_launchpad) {
                launchpad_handle_cc(b1, b2);
            } else {
                dexed_raw_handle_midi(b0, b1, b2);
            }
            break;

        case 0x0A: /* Poly Aftertouch */
        case 0x0E: /* Pitch Bend */
            dexed_raw_handle_midi(b0, b1, b2);
            break;

        /* 2-byte channel messages */
        case 0x0C: /* Program Change */
        case 0x0D: /* Channel Aftertouch */
            dexed_raw_handle_midi(b0, b1, 0);
            break;

        /* SysEx start / continue -- 3 data bytes */
        case 0x04:
            usb_midi_process_sysex_continue(&pkt[1], 3);
            break;

        /* SysEx end with 1 byte */
        case 0x05:
            usb_midi_finish_sysex(&pkt[1], 1);
            break;

        /* SysEx end with 2 bytes */
        case 0x06:
            usb_midi_finish_sysex(&pkt[1], 2);
            break;

        /* SysEx end with 3 bytes */
        case 0x07:
            usb_midi_finish_sysex(&pkt[1], 3);
            break;

        default:
            break;
    }
}

/* ----------------------------------------------------------------
 * Descriptor walking: find MIDI Streaming interface + Bulk IN EP
 * ---------------------------------------------------------------- */
static bool find_midi_interface(usb_device_handle_t dev_hdl, midi_device_t *midi)
{
    const usb_config_desc_t *config_desc = NULL;
    esp_err_t err = usb_host_get_active_config_descriptor(dev_hdl, &config_desc);
    if (err != ESP_OK || config_desc == NULL) {
        ESP_LOGE(TAG, "Failed to get config descriptor: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "Walking descriptors: wTotalLength=%d, looking for Audio/MIDI Streaming (class=1 subclass=3)...",
             config_desc->wTotalLength);

    int offset = 0;
    const usb_standard_desc_t *cur_desc = (const usb_standard_desc_t *)config_desc;
    uint16_t wTotalLength = config_desc->wTotalLength;
    int desc_count = 0;

    while (cur_desc != NULL) {
        desc_count++;
        ESP_LOGD(TAG, "  Descriptor #%d: type=0x%02X len=%d",
                 desc_count, cur_desc->bDescriptorType, cur_desc->bLength);

        if (cur_desc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
            const usb_intf_desc_t *intf = (const usb_intf_desc_t *)cur_desc;

            if (intf->bInterfaceClass == USB_CLASS_AUDIO &&
                intf->bInterfaceSubClass == USB_SUBCLASS_MIDI_STREAMING) {

                ESP_LOGI(TAG, "Found MIDI Streaming interface %d (%d endpoints)",
                         intf->bInterfaceNumber, intf->bNumEndpoints);

                midi->midi_intf_num = intf->bInterfaceNumber;

                /* Walk endpoints within this interface */
                for (int ep_idx = 0; ep_idx < intf->bNumEndpoints; ep_idx++) {
                    int ep_offset = offset;
                    const usb_ep_desc_t *ep =
                        usb_parse_endpoint_descriptor_by_index(
                            intf, ep_idx, wTotalLength, &ep_offset);

                    if (ep == NULL) continue;

                    if (USB_EP_DESC_GET_XFERTYPE(ep) == USB_TRANSFER_TYPE_BULK) {
                        if (USB_EP_DESC_GET_EP_DIR(ep)) {
                            /* IN endpoint (device -> host = MIDI input) */
                            midi->ep_in = ep->bEndpointAddress;
                            midi->ep_in_mps = USB_EP_DESC_GET_MPS(ep);
                            ESP_LOGI(TAG, "  Bulk IN  EP 0x%02X (MPS=%d)",
                                     midi->ep_in, midi->ep_in_mps);
                        } else {
                            /* OUT endpoint (host -> device) */
                            midi->ep_out = ep->bEndpointAddress;
                            ESP_LOGI(TAG, "  Bulk OUT EP 0x%02X", midi->ep_out);
                        }
                    }
                }
                return (midi->ep_in != 0);
            }
        }
        cur_desc = usb_parse_next_descriptor(cur_desc, wTotalLength, &offset);
    }

    ESP_LOGW(TAG, "No MIDI Streaming interface found after scanning %d descriptors", desc_count);
    return false;
}

/* ----------------------------------------------------------------
 * Bulk IN transfer callback -- called from usb_host_client_handle_events()
 * ---------------------------------------------------------------- */
static void midi_transfer_cb(usb_transfer_t *transfer)
{
    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
        int num_bytes = transfer->actual_num_bytes;
        uint8_t *data = transfer->data_buffer;

        if (num_bytes > 0) {
            ESP_LOGD(TAG, "USB MIDI IN: %d bytes", num_bytes);
            /* Log raw data at debug level for first few bytes */
            if (num_bytes <= 16) {
                ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, num_bytes, ESP_LOG_DEBUG);
            }
        }

        /* Process 4-byte USB-MIDI packets */
        for (int i = 0; i + 3 < num_bytes; i += 4) {
            /* Skip padding packets (all zeros) */
            if (data[i] == 0 && data[i+1] == 0 && data[i+2] == 0 && data[i+3] == 0)
                continue;
            ESP_LOGV(TAG, "MIDI pkt: [%02X %02X %02X %02X]",
                     data[i], data[i+1], data[i+2], data[i+3]);
            usb_midi_process_packet(&data[i]);
        }
    } else if (transfer->status == USB_TRANSFER_STATUS_NO_DEVICE) {
        ESP_LOGW(TAG, "USB MIDI device disconnected during transfer");
        return; /* Don't resubmit */
    } else {
        ESP_LOGW(TAG, "USB MIDI transfer status: %d", transfer->status);
    }

    /* Signal the host task to resubmit.  Do NOT call usb_host_transfer_submit
     * here: doing so inside the callback (which executes synchronously inside
     * usb_host_client_handle_events) causes the very next completion callback
     * to fire before the event-handling loop can yield, starving Core-0 tasks
     * and generating burst USB DMA traffic that contends with I2S DMA. */
    if (s_midi_dev.connected) {
        s_xfer_needs_resubmit = true;
    }
}

/* ----------------------------------------------------------------
 * Bulk OUT transfer callback
 * ---------------------------------------------------------------- */
static void midi_out_transfer_cb(usb_transfer_t *transfer)
{
    s_midi_dev.xfer_out_busy = false;
    if (transfer->status != USB_TRANSFER_STATUS_COMPLETED &&
        transfer->status != USB_TRANSFER_STATUS_NO_DEVICE) {
        ESP_LOGW(TAG, "MIDI OUT transfer status: %d", transfer->status);
    }
}

/* ----------------------------------------------------------------
 * Open / close MIDI device
 * ---------------------------------------------------------------- */
static void close_midi_device(void); /* forward declaration */
static void open_midi_device(uint8_t dev_addr)
{
    ESP_LOGI(TAG, "Opening USB device at address %d...", dev_addr);
    int step = 0; // debug step counter

    usb_device_handle_t dev_hdl = NULL;
    esp_err_t err = usb_host_device_open(s_client_hdl, dev_addr, &dev_hdl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Step %d: Failed to open device %d: %s", step, dev_addr, esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "Step %d: Device %d opened successfully", step, dev_addr);
    step++;

    /* Log device info */
    const usb_device_desc_t *dev_desc = NULL;
    usb_host_get_device_descriptor(dev_hdl, &dev_desc);
    if (dev_desc) {
        ESP_LOGI(TAG, "USB device descriptor:");
        ESP_LOGI(TAG, "  VID=0x%04X PID=0x%04X",
                 dev_desc->idVendor, dev_desc->idProduct);
        ESP_LOGI(TAG, "  bDeviceClass=%d bDeviceSubClass=%d bDeviceProtocol=%d",
                 dev_desc->bDeviceClass, dev_desc->bDeviceSubClass,
                 dev_desc->bDeviceProtocol);
        ESP_LOGI(TAG, "  bNumConfigurations=%d", dev_desc->bNumConfigurations);
    } else {
        ESP_LOGW(TAG, "Could not get device descriptor!");
    }

    /* Check for Novation Launchpad */
    bool is_novation = (dev_desc && dev_desc->idVendor == NOVATION_VID);
    if (is_novation) {
        ESP_LOGI(TAG, "Novation device detected (VID=0x%04X PID=0x%04X)",
                 dev_desc->idVendor, dev_desc->idProduct);
    }

    /* Log config descriptor */
    const usb_config_desc_t *config_desc = NULL;
    err = usb_host_get_active_config_descriptor(dev_hdl, &config_desc);
    if (err == ESP_OK && config_desc) {
        ESP_LOGI(TAG, "Step %d: Config descriptor: wTotalLength=%d bNumInterfaces=%d",
                 step, config_desc->wTotalLength, config_desc->bNumInterfaces);
        step++;

        /* Dump all interface descriptors for diagnostics */
        int offset = 0;
        const usb_standard_desc_t *desc = (const usb_standard_desc_t *)config_desc;
        uint16_t wTotal = config_desc->wTotalLength;
        while (desc != NULL) {
            if (desc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
                const usb_intf_desc_t *intf = (const usb_intf_desc_t *)desc;
                ESP_LOGI(TAG, "  Interface %d: class=%d subclass=%d protocol=%d eps=%d",
                         intf->bInterfaceNumber, intf->bInterfaceClass,
                         intf->bInterfaceSubClass, intf->bInterfaceProtocol,
                         intf->bNumEndpoints);
            }
            desc = usb_parse_next_descriptor(desc, wTotal, &offset);
        }
    } else {
        ESP_LOGW(TAG, "Could not get config descriptor: %s", esp_err_to_name(err));
    }

    /* Scan using a local temporary struct so we never clobber s_midi_dev if
     * this device turns out to have no MIDI interface.  The previous approach
     * (memset s_midi_dev here) caused a handle leak: if the startup scan
     * already connected the keyboard and a concurrent NEW_DEV for the USB
     * audio device triggered another open_midi_device() call, the memset
     * would zero s_midi_dev.dev_hdl without calling usb_host_device_close().
     * The USB host library kept the handle open, so every later attempt to
     * re-open the keyboard returned ESP_ERR_INVALID_STATE. */
    midi_device_t scan_dev = {};
    if (!find_midi_interface(dev_hdl, &scan_dev)) {
        ESP_LOGW(TAG, "Step %d: Device %d has no MIDI Streaming interface (class=1 subclass=3), closing", step, dev_addr);
        usb_host_device_close(s_client_hdl, dev_hdl);
        return;
    }
    ESP_LOGI(TAG, "Step %d: Found MIDI interface", step);
    step++;

    /* If there is an existing (leaked) device handle from a previous failed
     * open, close it now before taking ownership of the new device. */
    if (s_midi_dev.dev_hdl != NULL) {
        ESP_LOGW(TAG, "Closing leaked previous MIDI device handle before opening new one");
        close_midi_device();
    }

    /* Copy scan results and take ownership of dev_hdl. */
    s_midi_dev = scan_dev;
    s_midi_dev.dev_hdl = dev_hdl;
    s_midi_dev.is_launchpad = is_novation;

    /* Claim the MIDI Streaming interface */
    err = usb_host_interface_claim(s_client_hdl, dev_hdl,
                                    s_midi_dev.midi_intf_num, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Step %d: Failed to claim interface %d: %s",
                 step, s_midi_dev.midi_intf_num, esp_err_to_name(err));
        usb_host_device_close(s_client_hdl, dev_hdl);
        memset(&s_midi_dev, 0, sizeof(s_midi_dev));
        return;
    }
    ESP_LOGI(TAG, "Step %d: Interface claimed", step);
    step++;

    /* Allocate Bulk IN transfer */
    size_t buf_size = (size_t)usb_round_up_to_mps(
        MIDI_USB_XFER_BUF_SIZE, (int)s_midi_dev.ep_in_mps);
    err = usb_host_transfer_alloc(buf_size, 0, &s_midi_dev.xfer_in);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to alloc IN transfer: %s", esp_err_to_name(err));
        usb_host_interface_release(s_client_hdl, dev_hdl, s_midi_dev.midi_intf_num);
        usb_host_device_close(s_client_hdl, dev_hdl);
        memset(&s_midi_dev, 0, sizeof(s_midi_dev));
        return;
    }
    /* Optionally allocate an internal RAM buffer and keep the original PSRAM
       buffer as a harmless leak.  Having the buffer in internal RAM reduces
       PSRAM contention during USB DMA; the leak is only one buffer per device
       and is small (~64 bytes). */
    void *int_buf = heap_caps_malloc(buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_CACHE_ALIGNED);
    if (int_buf) {
        void **bufptr = (void **)&s_midi_dev.xfer_in->data_buffer;
        *bufptr = int_buf;
        size_t *szptr = (size_t *)&s_midi_dev.xfer_in->data_buffer_size;
        *szptr = heap_caps_get_allocated_size(int_buf);
    } else {
        ESP_LOGW(TAG, "Internal RAM allocation failed, using PSRAM buffer");
    }

    s_midi_dev.xfer_in->device_handle = dev_hdl;
    s_midi_dev.xfer_in->bEndpointAddress = s_midi_dev.ep_in;
    s_midi_dev.xfer_in->callback = midi_transfer_cb;
    s_midi_dev.xfer_in->context = &s_midi_dev;
    s_midi_dev.xfer_in->num_bytes = buf_size;

    s_midi_dev.connected = true;

    /* Submit first IN transfer -- starts continuous MIDI reading */
    err = usb_host_transfer_submit(s_midi_dev.xfer_in);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Step %d: Failed to submit initial IN transfer: %s",
                 step, esp_err_to_name(err));
        s_midi_dev.connected = false;
        usb_host_transfer_free(s_midi_dev.xfer_in);
        usb_host_interface_release(s_client_hdl, dev_hdl, s_midi_dev.midi_intf_num);
        usb_host_device_close(s_client_hdl, dev_hdl);
        memset(&s_midi_dev, 0, sizeof(s_midi_dev));
        return;
    }
    ESP_LOGI(TAG, "Step %d: Initial transfer submitted", step);
    step++;

    ESP_LOGI(TAG, "USB MIDI keyboard connected -- streaming from EP 0x%02X",
             s_midi_dev.ep_in);

    /* Allocate Bulk OUT transfer if endpoint exists (needed for LED control) */
    if (s_midi_dev.ep_out != 0) {
        size_t out_buf_size = 512; /* ample for batched SysEx LED updates */
        err = usb_host_transfer_alloc(out_buf_size, 0, &s_midi_dev.xfer_out);
        if (err == ESP_OK) {
            s_midi_dev.xfer_out->device_handle = dev_hdl;
            s_midi_dev.xfer_out->bEndpointAddress = s_midi_dev.ep_out;
            s_midi_dev.xfer_out->callback = midi_out_transfer_cb;
            s_midi_dev.xfer_out->context = &s_midi_dev;
            s_midi_dev.xfer_out_busy = false;
            ESP_LOGI(TAG, "Bulk OUT transfer allocated for EP 0x%02X", s_midi_dev.ep_out);
        } else {
            ESP_LOGW(TAG, "Failed to allocate OUT transfer: %s (LED output disabled)",
                     esp_err_to_name(err));
            s_midi_dev.xfer_out = NULL;
        }
    }

    /* If Launchpad detected, enter Programmer Mode and notify sequencer */
    if (s_midi_dev.is_launchpad && s_midi_dev.xfer_out) {
        ESP_LOGI(TAG, "Launchpad X connected -- entering Programmer Mode");
        launchpad_on_connect();
    }
}

static void close_midi_device(void)
{
    if (!s_midi_dev.dev_hdl) return;

    s_midi_dev.connected = false;

    if (s_midi_dev.is_launchpad) {
        launchpad_on_disconnect();
    }

    usb_host_interface_release(s_client_hdl, s_midi_dev.dev_hdl,
                                s_midi_dev.midi_intf_num);

    if (s_midi_dev.xfer_in) {
        usb_host_transfer_free(s_midi_dev.xfer_in);
        s_midi_dev.xfer_in = NULL;
    }

    if (s_midi_dev.xfer_out) {
        usb_host_transfer_free(s_midi_dev.xfer_out);
        s_midi_dev.xfer_out = NULL;
    }

    usb_host_device_close(s_client_hdl, s_midi_dev.dev_hdl);
    memset(&s_midi_dev, 0, sizeof(s_midi_dev));

    ESP_LOGI(TAG, "USB MIDI device disconnected");
}

/* ----------------------------------------------------------------
 * Client event callback -- called asynchronously
 * ---------------------------------------------------------------- */
static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    ESP_LOGD(TAG, "USB client event: %d", event_msg->event);
    switch (event_msg->event) {
        case USB_HOST_CLIENT_EVENT_NEW_DEV:
            ESP_LOGI(TAG, ">>> New USB device at address %d",
                     event_msg->new_dev.address);
            s_new_dev_addr = event_msg->new_dev.address;
            s_actions |= MIDI_HOST_ACTION_OPEN;
            break;
        case USB_HOST_CLIENT_EVENT_DEV_GONE:
            /* Only close if the device that left is the one we opened.
             * DEV_GONE fires for ALL device removals (any client), not just
             * for devices this client has open.  Ignoring foreign DEV_GONE
             * events prevents the MIDI connection from being torn down when
             * an unrelated device (e.g. the USB audio output device) changes
             * state. */
            if (s_midi_dev.dev_hdl != NULL &&
                    event_msg->dev_gone.dev_hdl == s_midi_dev.dev_hdl) {
                ESP_LOGI(TAG, ">>> USB MIDI device gone");
                s_actions |= MIDI_HOST_ACTION_CLOSE;
            } else {
                ESP_LOGD(TAG, ">>> USB device gone (not our MIDI device, ignoring)");
            }
            break;
        default:
            ESP_LOGD(TAG, ">>> Unknown USB client event: %d", event_msg->event);
            break;
    }
}

/* ----------------------------------------------------------------
 * USB Host Library daemon task (handles enumeration, hub events)
 * ---------------------------------------------------------------- */
static void usb_host_lib_task(void *arg)
{
    ESP_LOGI(TAG, "USB Host lib task starting on core %d...", xPortGetCoreID());

    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        // Keep root port powered immediately.  Power-cycling while a keyboard
        // is attached has been observed to cause enumeration failures
        // (CHECK_SHORT_DEV_DESC).  Let the USB host driver handle power itself.
        .root_port_unpowered = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL2,
        .enum_filter_cb = NULL,
    };
    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "USB Host install failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    /* Signal the caller that host lib is ready BEFORE entering the event loop.
     * The previous code had a 2-second vTaskDelay() here, which prevented
     * usb_host_lib_handle_events() from running during that window.  This
     * blocked hub and device enumeration, so NEW_DEV events were never fired.
     * Signal immediately and let the event loop below drive enumeration. */
    xTaskNotifyGive((TaskHandle_t)arg);

    ESP_LOGI(TAG, "USB Host Library installed -- daemon running on core %d", xPortGetCoreID());

    while (s_usb_host_running) {
        uint32_t event_flags = 0;
        err = usb_host_lib_handle_events(pdMS_TO_TICKS(200), &event_flags);
        if (err == ESP_OK) {
            usb_log_event_flags(event_flags);
            if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
                ESP_LOGW(TAG, "USB Host: no clients, freeing all devices");
                usb_host_device_free_all();
            }
        } else if (err != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "USB Host lib_handle_events error: %s", esp_err_to_name(err));
        }
    }

    /* Clean up */
    usb_host_uninstall();
    ESP_LOGI(TAG, "USB Host Library uninstalled");
    vTaskDelete(NULL);
}

/* ----------------------------------------------------------------
 * MIDI Host client task (registers client, opens devices, reads MIDI)
 * ---------------------------------------------------------------- */
static void midi_host_task(void *arg)
{
    usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = {
            .client_event_callback = client_event_cb,
            .callback_arg = NULL,
        },
    };
    esp_err_t err = usb_host_client_register(&client_config, &s_client_hdl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "USB Host client register failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "USB Host MIDI client registered on core %d -- waiting for keyboard...",
             xPortGetCoreID());

    /* Check for devices that were already enumerated before we registered.
     * This handles the case where the device was connected and enumerated
     * before the MIDI client task started (NEW_DEV would have been missed). */
    {
        uint8_t addr_list[16];
        int num_devs = 0;
        if (usb_host_device_addr_list_fill(sizeof(addr_list), addr_list, &num_devs) == ESP_OK
                && num_devs > 0) {
            ESP_LOGI(TAG, "Found %d already-enumerated device(s), trying to open as MIDI...",
                     num_devs);
            for (int i = 0; i < num_devs && !s_midi_dev.connected; i++) {
                ESP_LOGI(TAG, "  Trying device at address %d", addr_list[i]);
                open_midi_device(addr_list[i]);
            }
        }
    }

    uint32_t loop_count = 0;   /* heartbeat counter */
    uint32_t scan_count  = 0;  /* device-list scan counter (separate from heartbeat) */

    while (s_usb_host_running) {
        /* Process client events -- THIS dispatches transfer callbacks.
         * 5 ms timeout keeps the loop responsive without busy-spinning. */
        usb_host_client_handle_events(s_client_hdl, pdMS_TO_TICKS(5));

        /* Resubmit USB MIDI bulk-IN transfer.
         * Runs in the task loop (not the ISR callback) to avoid reentrancy
         * inside usb_host_client_handle_events.
         *
         * A 4 ms yield before resubmit throttles USB bulk-IN polling to
         * ~100 cycles/s (was 2 ms → ~125/s).  When WLAN is also active the
         * WiFi DMA and USB DMA together can saturate the AHB bus; reducing
         * USB resubmit frequency lowers bus pressure enough that I2S DMA
         * descriptor fetches are no longer starved.  MIDI input latency is
         * ≤ 9 ms — imperceptible to a player. */
        if (s_xfer_needs_resubmit && s_midi_dev.connected && s_midi_dev.xfer_in) {
            s_xfer_needs_resubmit = false;
            vTaskDelay(pdMS_TO_TICKS(4));
            esp_err_t sub_err = usb_host_transfer_submit(s_midi_dev.xfer_in);
            if (sub_err != ESP_OK && sub_err != ESP_ERR_NOT_FOUND) {
                ESP_LOGE(TAG, "Failed to resubmit MIDI IN transfer: %s",
                         esp_err_to_name(sub_err));
            }
        }

        /* Process actions from callbacks */
        if (s_actions & MIDI_HOST_ACTION_OPEN) {
            s_actions &= ~MIDI_HOST_ACTION_OPEN;
            /* Skip if already connected: when multiple NEW_DEV events arrive
             * in one handle_events() call, s_new_dev_addr holds the last
             * address and the earlier addresses (including the keyboard) are
             * lost.  Calling open_midi_device for a non-MIDI device while
             * already connected would previously clobber s_midi_dev.  With
             * the local-scan-dev fix this is now safe, but there is still no
             * point opening a new device when one is already working. */
            if (!s_midi_dev.connected) {
                ESP_LOGI(TAG, "MIDI client: processing OPEN action for addr %d", s_new_dev_addr);
                open_midi_device(s_new_dev_addr);
            } else {
                ESP_LOGD(TAG, "MIDI client: ignoring OPEN for addr %d (already connected)", s_new_dev_addr);
            }
        }
        if (s_actions & MIDI_HOST_ACTION_CLOSE) {
            s_actions &= ~MIDI_HOST_ACTION_CLOSE;
            ESP_LOGI(TAG, "MIDI client: processing CLOSE action");
            close_midi_device();
        }

        /* When not connected, periodically query the host stack for any
         * newly-enumerated devices.  This is the correct approach for hub-
         * attached keyboards: usb_host_device_addr_list_fill() returns the
         * addresses that the host stack has *actually enumerated*, unlike the
         * old 1-127 blind scan which called open() on non-existent addresses.
         * Check every ~1 s (200 loops × 5 ms). */
        if (!s_midi_dev.connected) {
            if (++scan_count >= 200) {
                scan_count = 0;
                uint8_t addr_list[16];
                int num_devs = 0;
                esp_err_t fill_err = usb_host_device_addr_list_fill(
                        sizeof(addr_list), addr_list, &num_devs);
                if (fill_err == ESP_OK && num_devs > 0) {
                    ESP_LOGI(TAG, "USB scan: %d device(s) enumerated, trying to open as MIDI...",
                             num_devs);
                    for (int i = 0; i < num_devs && !s_midi_dev.connected; i++) {
                        ESP_LOGI(TAG, "  Trying address %d", addr_list[i]);
                        open_midi_device(addr_list[i]);
                    }
                } else {
                    ESP_LOGD(TAG, "USB scan: no devices enumerated yet (err=%s)",
                             esp_err_to_name(fill_err));
                }
            }
        } else {
            scan_count = 0;  /* reset when connected so we react quickly on reconnect */
        }

        /* Periodic heartbeat every ~10 seconds (2000 × 5 ms) */
        if (++loop_count >= 2000) {
            loop_count = 0;
            ESP_LOGI(TAG, "USB MIDI client heartbeat: connected=%s actions=0x%lx",
                     s_midi_dev.connected ? "yes" : "no",
                     (unsigned long)s_actions);
        }
    }

    close_midi_device();
    usb_host_client_deregister(s_client_hdl);
    s_client_hdl = NULL;
    ESP_LOGI(TAG, "USB Host MIDI client deregistered");
    vTaskDelete(NULL);
}

/* ----------------------------------------------------------------
 * USB Host MIDI init / stop
 * ---------------------------------------------------------------- */
static int usb_midi_host_init(void)
{
    ESP_LOGI(TAG, "Initializing USB Host for MIDI keyboard input...");

    s_usb_host_running = true;

    /* Task 1: USB Host Library daemon.
     * Priority one below the MIDI client: the daemon mostly sleeps in
     * usb_host_lib_handle_events and does not need real-time scheduling.
     * Keeping it lower ensures the MIDI client (and audio stats task) are
     * not starved on Core 0 during device connect / disconnect churn. */
    BaseType_t ret = xTaskCreatePinnedToCore(
        usb_host_lib_task,
        "usb_host_lib",
        4096,
        xTaskGetCurrentTaskHandle(), /* pass our handle for notification */
        USB_MIDI_TASK_PRIORITY - 1,
        &s_usb_host_lib_task,
        USB_MIDI_TASK_CORE
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create USB host lib task");
        s_usb_host_running = false;
        return -1;
    }

    /* Wait for host lib installation */
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000)) == 0) {
        ESP_LOGW(TAG, "Timeout waiting for USB Host Library installation (will continue)");
        // do not disable s_usb_host_running; the usb_host_lib_task will still install
        // and client task can be started regardless.
    }

    /* Task 2: MIDI Host client */
    ret = xTaskCreatePinnedToCore(
        midi_host_task,
        "midi_host",
        4096,
        NULL,
        USB_MIDI_TASK_PRIORITY,
        &s_midi_host_task_hdl,
        USB_MIDI_TASK_CORE
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create MIDI host task");
        s_usb_host_running = false;
        return -1;
    }

    ESP_LOGI(TAG, "USB Host MIDI ready -- plug in a USB-MIDI keyboard on the USB port");
    return 0;
}

static void usb_midi_host_stop(void)
{
    s_usb_host_running = false;
    /* Tasks will exit on their own */
    vTaskDelay(pdMS_TO_TICKS(500));
    s_usb_host_lib_task = NULL;
    s_midi_host_task_hdl = NULL;
}

#endif /* FMRACK_MIDI_USB_ENABLE */

/* ================================================
 * Public API
 * ================================================ */

int esp32_midi_init(void)
{
    ESP_LOGI(TAG, "Initializing MIDI input...");

    // --- Hardware MIDI UART ---
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

    // --- USB Host MIDI ---
#if FMRACK_MIDI_USB_ENABLE
    if (usb_midi_host_init() != 0) {
        ESP_LOGW(TAG, "USB Host MIDI init failed (non-fatal, continuing without USB)");
    }
#endif

    return 0;
}

int esp32_midi_start(void)
{
    if (s_midi_running) {
        ESP_LOGW(TAG, "MIDI tasks already running");
        return 0;
    }

    s_midi_running = true;

    // Start hardware MIDI task on core 0
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

    /* USB Host MIDI tasks are already running from esp32_midi_init() */

    ESP_LOGI(TAG, "MIDI input started (UART + USB Host on core %d)", MIDI_TASK_CORE);
    return 0;
}

void esp32_midi_stop(void)
{
    s_midi_running = false;

    if (s_midi_uart_task) {
        vTaskDelay(pdMS_TO_TICKS(50));
        s_midi_uart_task = NULL;
    }

#if FMRACK_MIDI_USB_ENABLE
    usb_midi_host_stop();
#endif

    uart_driver_delete((uart_port_t)FMRACK_MIDI_UART_NUM);

    ESP_LOGI(TAG, "MIDI input stopped");
}

bool esp32_midi_usb_connected(void)
{
#if FMRACK_MIDI_USB_ENABLE
    return s_midi_dev.connected;
#else
    return false;
#endif
}

bool esp32_midi_is_launchpad(void)
{
#if FMRACK_MIDI_USB_ENABLE
    return s_midi_dev.connected && s_midi_dev.is_launchpad;
#else
    return false;
#endif
}

#if FMRACK_MIDI_USB_ENABLE
/* Wait for previous OUT transfer to complete, with timeout. */
static bool wait_out_ready(int timeout_ms)
{
    int waited = 0;
    while (s_midi_dev.xfer_out_busy && waited < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(1));
        waited++;
    }
    return !s_midi_dev.xfer_out_busy;
}
#endif

int esp32_midi_usb_send_packets(const uint8_t *packets, int len)
{
#if FMRACK_MIDI_USB_ENABLE
    if (!s_midi_dev.connected || !s_midi_dev.xfer_out || len <= 0) return -1;
    if (len % 4 != 0) return -1;
    if (!wait_out_ready(50)) return -1;

    if (len > (int)s_midi_dev.xfer_out->data_buffer_size)
        len = (int)s_midi_dev.xfer_out->data_buffer_size;

    memcpy(s_midi_dev.xfer_out->data_buffer, packets, len);
    s_midi_dev.xfer_out->num_bytes = len;
    s_midi_dev.xfer_out_busy = true;

    esp_err_t err = usb_host_transfer_submit(s_midi_dev.xfer_out);
    if (err != ESP_OK) {
        s_midi_dev.xfer_out_busy = false;
        ESP_LOGW(TAG, "MIDI OUT submit failed: %s", esp_err_to_name(err));
        return -1;
    }
    return 0;
#else
    return -1;
#endif
}

int esp32_midi_usb_send_sysex(const uint8_t *sysex, int len)
{
#if FMRACK_MIDI_USB_ENABLE
    if (!s_midi_dev.connected || !s_midi_dev.xfer_out || len < 2) return -1;
    if (!wait_out_ready(50)) return -1;

    uint8_t *buf = s_midi_dev.xfer_out->data_buffer;
    int buf_size = (int)s_midi_dev.xfer_out->data_buffer_size;
    int pos = 0;
    int i = 0;

    while (i < len && (pos + 4) <= buf_size) {
        int remaining = len - i;

        if (remaining > 3) {
            /* SysEx start or continue: 3 data bytes */
            buf[pos++] = 0x04;
            buf[pos++] = sysex[i++];
            buf[pos++] = sysex[i++];
            buf[pos++] = sysex[i++];
        } else if (remaining == 3) {
            /* SysEx end with 3 bytes */
            buf[pos++] = 0x07;
            buf[pos++] = sysex[i++];
            buf[pos++] = sysex[i++];
            buf[pos++] = sysex[i++];
        } else if (remaining == 2) {
            /* SysEx end with 2 bytes */
            buf[pos++] = 0x06;
            buf[pos++] = sysex[i++];
            buf[pos++] = sysex[i++];
            buf[pos++] = 0x00;
        } else {
            /* SysEx end with 1 byte */
            buf[pos++] = 0x05;
            buf[pos++] = sysex[i++];
            buf[pos++] = 0x00;
            buf[pos++] = 0x00;
        }
    }

    s_midi_dev.xfer_out->num_bytes = pos;
    s_midi_dev.xfer_out_busy = true;

    esp_err_t err = usb_host_transfer_submit(s_midi_dev.xfer_out);
    if (err != ESP_OK) {
        s_midi_dev.xfer_out_busy = false;
        return -1;
    }
    return 0;
#else
    return -1;
#endif
}

int esp32_midi_usb_send_msg(uint8_t status, uint8_t data1, uint8_t data2)
{
#if FMRACK_MIDI_USB_ENABLE
    uint8_t cin = (status >> 4) & 0x0F;
    uint8_t pkt[4] = { cin, status, data1, data2 };
    return esp32_midi_usb_send_packets(pkt, 4);
#else
    return -1;
#endif
}
