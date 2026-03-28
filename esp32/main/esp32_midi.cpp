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
/* Private ESP-IDF USB headers — we use the lower-level USBH endpoint API
 * (usbh_ep_alloc / usbh_ep_enqueue_urb / urb_alloc) to allocate ONLY the
 * Bulk IN pipe for each MIDI device.  The standard usb_host_interface_claim()
 * allocates a pipe for EVERY endpoint in the interface, including the Bulk OUT
 * we never use.  Each pipe consumes one of the ESP32-S3's 8 hardware HCD
 * channels in the DWC OTG controller, so wasting them on unused OUT endpoints
 * prevents the 4th USB device from enumerating. */
extern "C" {
#include "usbh.h"         /* usbh_ep_alloc, usbh_ep_free, usbh_ep_enqueue_urb, etc. */
#include "usb_private.h"  /* urb_t, urb_alloc, urb_free */
}
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
            uint8_t s  = p->status & 0xF0;
            uint8_t d0 = p->data[0];
            uint8_t d1 = p->data[1];
            if (s == 0x90 && d1 > 0) {
                /* Note On: quantize → record mapping → scale velocity */
                d0 = step_seq_field_quantize_note(d0);
                step_seq_field_record_noteon(p->data[0], d0);
                d1 = step_seq_melodic_scale_velocity(d0, d1);
            } else if (s == 0x80 || (s == 0x90 && d1 == 0)) {
                /* Note Off: resolve stored mapping — never re-quantize */
                d0 = step_seq_field_resolve_noteoff(d0);
            }
            dexed_raw_handle_midi(p->status, d0, d1);
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
/* OUT buffer needs to be larger for Launchpad batch SysEx LED updates.
 * Max SysEx ~248 bytes → ~332 bytes in USB-MIDI packets → 384 (6×64). */
#define MIDI_USB_OUT_BUF_SIZE      384

/* State for one connected MIDI device */
typedef struct {
    usb_device_handle_t dev_hdl;
    uint8_t dev_addr;               /* USB device address */
    uint8_t midi_intf_num;
    uint8_t ep_in;                  /* Bulk IN endpoint address */
    uint8_t ep_out;                 /* Bulk OUT endpoint address (optional) */
    uint16_t ep_in_mps;            /* Max packet size */
    /* --- Direct endpoint / URB handles (private USBH API) --- */
    usbh_ep_handle_t ep_in_hdl;    /* Direct IN endpoint handle (bypasses interface_claim) */
    urb_t *urb_in;                 /* URB for IN transfers */
    usbh_ep_handle_t ep_out_hdl;   /* Direct OUT endpoint handle (Launchpad LED control) */
    urb_t *urb_out;                /* URB for OUT transfers */
    bool connected;
    volatile bool xfer_out_busy;    /* true while OUT transfer in-flight */
    volatile bool out_urb_done;     /* set by OUT ISR callback; main loop dequeues */
    volatile bool xfer_needs_resubmit; /* set by ISR callback; cleared by host task */
    volatile bool xfer_needs_first_submit; /* set at open, cleared after first submit to defer HCD allocation */
    bool is_launchpad;              /* Novation VID detected */
} midi_device_t;

#define MIDI_MAX_DEVICES 4
static midi_device_t s_midi_devs[MIDI_MAX_DEVICES] = {};
static usb_host_client_handle_t s_client_hdl = NULL;
static volatile bool s_usb_host_running = false;
static TaskHandle_t s_usb_host_lib_task = NULL;
static TaskHandle_t s_midi_host_task_hdl = NULL;

/* Pending open/close events from client_event_cb */
#define MIDI_MAX_PENDING_OPENS 8
static volatile uint8_t s_pending_open_addrs[MIDI_MAX_PENDING_OPENS];
static volatile int s_pending_open_count = 0;
static volatile usb_device_handle_t s_close_dev_hdl = NULL;
static SemaphoreHandle_t s_pending_open_mutex = NULL;

/* Non-MIDI device cache: remember addresses already probed and found to have no
 * MIDI Streaming interface, so the 500ms scan loop doesn't re-open them every
 * cycle (which spams the log and wastes HCD channels). Cleared on DEV_GONE so
 * a physically re-plugged device always gets a fresh probe. */
#define NON_MIDI_CACHE_SIZE 16
typedef struct { uint8_t addr; usb_device_handle_t hdl; } non_midi_entry_t;
static non_midi_entry_t s_non_midi_cache[NON_MIDI_CACHE_SIZE];
static int s_non_midi_cache_count = 0;

static void non_midi_cache_add(uint8_t addr, usb_device_handle_t hdl)
{
    /* Avoid duplicates */
    for (int i = 0; i < s_non_midi_cache_count; i++) {
        if (s_non_midi_cache[i].addr == addr) return;
    }
    if (s_non_midi_cache_count < NON_MIDI_CACHE_SIZE) {
        s_non_midi_cache[s_non_midi_cache_count].addr = addr;
        s_non_midi_cache[s_non_midi_cache_count].hdl  = hdl;
        s_non_midi_cache_count++;
        ESP_LOGD(TAG, "non-MIDI cache: added addr=%u (cached=%d)", addr, s_non_midi_cache_count);
    }
}

static bool non_midi_cache_contains(uint8_t addr)
{
    for (int i = 0; i < s_non_midi_cache_count; i++) {
        if (s_non_midi_cache[i].addr == addr) return true;
    }
    return false;
}

static void non_midi_cache_remove_by_hdl(usb_device_handle_t hdl)
{
    for (int i = 0; i < s_non_midi_cache_count; i++) {
        if (s_non_midi_cache[i].hdl == hdl) {
            ESP_LOGD(TAG, "non-MIDI cache: removed addr=%u on disconnect",
                     s_non_midi_cache[i].addr);
            s_non_midi_cache[i] = s_non_midi_cache[--s_non_midi_cache_count];
            return;
        }
    }
}

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

static void usb_midi_process_packet(const uint8_t *pkt, midi_device_t *dev)
{
    uint8_t cin = pkt[0] & 0x0F;
    uint8_t b0  = pkt[1];
    uint8_t b1  = pkt[2];
    uint8_t b2  = pkt[3];

    switch (cin) {
        /* 3-byte channel messages */
        case 0x08: /* Note Off */
        case 0x09: /* Note On */
            if (dev->is_launchpad) {
                launchpad_handle_note(b1, (cin == 0x08) ? 0 : b2);
            } else {
                if (cin == 0x09 && b2 > 0) {
                    /* Note On: quantize → record mapping → scale velocity */
                    uint8_t qb1 = step_seq_field_quantize_note(b1);
                    step_seq_field_record_noteon(b1, qb1);
                    uint8_t qb2 = step_seq_melodic_scale_velocity(qb1, b2);
                    dexed_raw_handle_midi(b0, qb1, qb2);
                } else {
                    /* Note Off: resolve stored mapping — never re-quantize */
                    dexed_raw_handle_midi(b0, step_seq_field_resolve_noteoff(b1), b2);
                }
            }
            break;

        case 0x0B: /* Control Change */
            if (dev->is_launchpad) {
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
 * Device slot helpers
 * ---------------------------------------------------------------- */
static midi_device_t *find_free_slot(void)
{
    for (int i = 0; i < MIDI_MAX_DEVICES; i++) {
        if (!s_midi_devs[i].connected && s_midi_devs[i].dev_hdl == NULL)
            return &s_midi_devs[i];
    }
    return NULL;
}

static midi_device_t *find_device_by_addr(uint8_t addr)
{
    for (int i = 0; i < MIDI_MAX_DEVICES; i++) {
        if (s_midi_devs[i].dev_hdl != NULL && s_midi_devs[i].dev_addr == addr)
            return &s_midi_devs[i];
    }
    return NULL;
}

static midi_device_t *find_device_by_hdl(usb_device_handle_t hdl)
{
    for (int i = 0; i < MIDI_MAX_DEVICES; i++) {
        if (s_midi_devs[i].dev_hdl == hdl)
            return &s_midi_devs[i];
    }
    return NULL;
}

static midi_device_t *find_launchpad_device(void)
{
    for (int i = 0; i < MIDI_MAX_DEVICES; i++) {
        if (s_midi_devs[i].connected && s_midi_devs[i].is_launchpad)
            return &s_midi_devs[i];
    }
    return NULL;
}

static bool any_device_connected(void)
{
    for (int i = 0; i < MIDI_MAX_DEVICES; i++) {
        if (s_midi_devs[i].connected) return true;
    }
    return false;
}

/* ----------------------------------------------------------------
 * Direct USBH endpoint callback (called from ISR context)
 *
 * Unlike the usb_host wrapper callback (which runs during
 * usb_host_client_handle_events), this fires directly from the HCD
 * pipe ISR.  We must stay fast — just set a flag and return.
 * ---------------------------------------------------------------- */
static bool IRAM_ATTR midi_ep_in_isr_cb(usbh_ep_handle_t ep_hdl,
                                         usbh_ep_event_t ep_event,
                                         void *arg, bool in_isr)
{
    midi_device_t *dev = (midi_device_t *)arg;
    if (ep_event == USBH_EP_EVENT_URB_DONE) {
        dev->xfer_needs_resubmit = true;
    }
    /* Returning false = no context switch requested. */
    return false;
}

/* Dummy transfer callback — required by usbh_ep_enqueue_urb()'s
 * urb_check_args() validation but never actually called since we use the
 * ISR-level endpoint callback (midi_ep_in_isr_cb) instead. */
static void midi_urb_dummy_cb(usb_transfer_t *transfer) { (void)transfer; }

/* OUT endpoint ISR callback — marks the OUT URB as done so the main loop
 * can dequeue it and clear xfer_out_busy. */
static bool IRAM_ATTR midi_ep_out_isr_cb(usbh_ep_handle_t ep_hdl,
                                          usbh_ep_event_t ep_event,
                                          void *arg, bool in_isr)
{
    midi_device_t *dev = (midi_device_t *)arg;
    if (ep_event == USBH_EP_EVENT_URB_DONE) {
        dev->out_urb_done = true;
    }
    return false;
}

/* Process a completed Bulk IN URB: dequeue, parse MIDI packets, resubmit.
 * Called from the main MIDI task loop (NOT from ISR). */
static void midi_process_urb(midi_device_t *dev)
{
    if (!dev || !dev->ep_in_hdl || !dev->urb_in) return;

    urb_t *done_urb = NULL;
    esp_err_t ret = usbh_ep_dequeue_urb(dev->ep_in_hdl, &done_urb);
    if (ret != ESP_OK || done_urb == NULL) return;

    usb_transfer_t *transfer = &done_urb->transfer;

    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
        int num_bytes = transfer->actual_num_bytes;
        uint8_t *data = transfer->data_buffer;

        if (num_bytes > 0) {
            ESP_LOGD(TAG, "USB MIDI IN: %d bytes", num_bytes);
            if (num_bytes <= 16) {
                ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, num_bytes, ESP_LOG_DEBUG);
            }
        }

        /* Process 4-byte USB-MIDI packets */
        for (int i = 0; i + 3 < num_bytes; i += 4) {
            if (data[i] == 0 && data[i+1] == 0 && data[i+2] == 0 && data[i+3] == 0)
                continue;
            ESP_LOGV(TAG, "MIDI pkt: [%02X %02X %02X %02X]",
                     data[i], data[i+1], data[i+2], data[i+3]);
            usb_midi_process_packet(&data[i], dev);
        }
    } else if (transfer->status == USB_TRANSFER_STATUS_NO_DEVICE) {
        ESP_LOGW(TAG, "USB MIDI device disconnected during transfer");
        return; /* Don't resubmit */
    } else {
        ESP_LOGW(TAG, "USB MIDI transfer status: %d", transfer->status);
    }

    /* Resubmit the URB for the next read */
    if (dev->connected) {
        transfer->num_bytes = transfer->data_buffer_size;
        esp_err_t sub = usbh_ep_enqueue_urb(dev->ep_in_hdl, dev->urb_in);
        if (sub != ESP_OK) {
            ESP_LOGW(TAG, "Failed to resubmit MIDI IN URB: %s", esp_err_to_name(sub));
        }
    }
}

/* ----------------------------------------------------------------
 * Open / close MIDI device
 * ---------------------------------------------------------------- */
/* Device open failure tracking: throttle rapid retries */
static TickType_t s_last_open_attempt_time = 0;
#define OPEN_ATTEMPT_MIN_INTERVAL_MS 200  /* Throttle opens by minimum 200ms to prevent HCD exhaustion */

/* Helper to throttle device open attempts and prevent HCD resource exhaustion */
static bool should_attempt_device_open(void)
{
    TickType_t now = xTaskGetTickCount();
    TickType_t elapsed = (now - s_last_open_attempt_time) * portTICK_PERIOD_MS;
    
    if (elapsed >= OPEN_ATTEMPT_MIN_INTERVAL_MS) {
        s_last_open_attempt_time = now;
        return true;
    }
    return false;
}

static void close_midi_device(midi_device_t *slot); /* forward declaration */
static void open_midi_device(uint8_t dev_addr)
{
    ESP_LOGI(TAG, "Opening USB device at address %d...", dev_addr);
    int step = 0; // debug step counter

    /* Skip if this device address is already in a slot */
    if (find_device_by_addr(dev_addr) != NULL) {
        ESP_LOGD(TAG, "Device %d already opened, skipping", dev_addr);
        return;
    }

    /* Find a free slot */
    midi_device_t *slot = find_free_slot();
    if (slot == NULL) {
        ESP_LOGW(TAG, "No free MIDI device slots for device %d (max=%d)", dev_addr, MIDI_MAX_DEVICES);
        return;
    }

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

    /* Scan using a local temporary struct so we never clobber the slot if
     * this device turns out to have no MIDI interface. */
    midi_device_t scan_dev = {};
    if (!find_midi_interface(dev_hdl, &scan_dev)) {
        ESP_LOGI(TAG, "Step %d: Device %d has no MIDI Streaming interface (class=1 subclass=3), caching addr to skip future scans", step, dev_addr);
        /* Cache this address so the periodic scan doesn't re-open it every 500ms.
         * The handle is stored so we can evict the entry when the device disconnects. */
        non_midi_cache_add(dev_addr, dev_hdl);
        usb_host_device_close(s_client_hdl, dev_hdl);
        return;
    }
    ESP_LOGI(TAG, "Step %d: Found MIDI interface", step);
    step++;

    /* Populate slot with scan results and take ownership of dev_hdl. */
    *slot = scan_dev;
    slot->dev_hdl = dev_hdl;
    slot->dev_addr = dev_addr;
    slot->is_launchpad = is_novation;

    /* Claim ONLY the Bulk IN endpoint via the private USBH API.
     *
     * usb_host_interface_claim() would allocate an HCD pipe for EVERY endpoint
     * in the interface (typically Bulk IN + Bulk OUT for MIDI Streaming).  Each
     * pipe consumes one of the ESP32-S3's 8 hardware host channels.  By
     * allocating only the IN endpoint we save 1 channel per MIDI device, which
     * is the difference between fitting 4 USB devices (hub + audio + 2 keyboards)
     * and running out of channels during enumeration. */
    usbh_ep_config_t ep_cfg = {
        .bInterfaceNumber  = slot->midi_intf_num,
        .bAlternateSetting = 0,
        .bEndpointAddress  = slot->ep_in,
        .ep_cb             = midi_ep_in_isr_cb,
        .ep_cb_arg         = (void *)slot,
        .context           = (void *)slot,
    };
    err = usbh_ep_alloc(dev_hdl, &ep_cfg, &slot->ep_in_hdl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Step %d: Failed to allocate IN endpoint 0x%02X: %s",
                 step, slot->ep_in, esp_err_to_name(err));
        usb_host_device_close(s_client_hdl, dev_hdl);
        memset(slot, 0, sizeof(*slot));
        return;
    }
    ESP_LOGI(TAG, "Step %d: IN endpoint allocated (direct USBH API, 1 HCD channel)", step);
    step++;

    /* Allocate a URB for Bulk IN data */
    size_t buf_size = (size_t)usb_round_up_to_mps(
        MIDI_USB_XFER_BUF_SIZE, (int)slot->ep_in_mps);
    slot->urb_in = urb_alloc(buf_size, 0);
    if (slot->urb_in == NULL) {
        ESP_LOGE(TAG, "Failed to alloc IN URB");
        usbh_ep_command(slot->ep_in_hdl, USBH_EP_CMD_HALT);
        usbh_ep_free(slot->ep_in_hdl);
        slot->ep_in_hdl = NULL;
        usb_host_device_close(s_client_hdl, dev_hdl);
        memset(slot, 0, sizeof(*slot));
        return;
    }
    /* Configure the URB's transfer fields for a Bulk IN read.
     * The callback field MUST be non-NULL to pass urb_check_args() validation
     * inside usbh_ep_enqueue_urb().  Actual completion notifications come via
     * our ISR endpoint callback (midi_ep_in_isr_cb), not this callback. */
    slot->urb_in->transfer.num_bytes = buf_size;
    slot->urb_in->transfer.callback = midi_urb_dummy_cb;

    slot->connected = true;
    slot->xfer_needs_first_submit = true;  /* Defer to main task to avoid HCD channel contention */

    ESP_LOGI(TAG, "Step %d: MIDI device ready (IN URB pending first submit)", step);
    step++;

    ESP_LOGI(TAG, "USB MIDI device connected -- streaming from EP 0x%02X (addr=%d, launchpad=%s)",
             slot->ep_in, dev_addr, is_novation ? "yes" : "no");

    /* Count connected devices for diagnostic output */
    int connected_count = 0;
    for (int i = 0; i < MIDI_MAX_DEVICES; i++) {
        if (s_midi_devs[i].connected) connected_count++;
    }
    ESP_LOGI(TAG, "Total MIDI devices connected: %d/%d", connected_count, MIDI_MAX_DEVICES);

    /* Allocate OUT endpoint via private USBH API (Launchpad LED / programmer mode).
     * This uses one additional HCD channel.  With the HAL +1 fix we have 8 channels,
     * which is enough for hub + audio + 2 MIDI IN + 1 MIDI OUT in most setups. */
    if (is_novation && slot->ep_out != 0) {
        usbh_ep_config_t out_ep_cfg = {
            .bInterfaceNumber  = slot->midi_intf_num,
            .bAlternateSetting = 0,
            .bEndpointAddress  = slot->ep_out,
            .ep_cb             = midi_ep_out_isr_cb,
            .ep_cb_arg         = (void *)slot,
            .context           = (void *)slot,
        };
        err = usbh_ep_alloc(dev_hdl, &out_ep_cfg, &slot->ep_out_hdl);
        if (err == ESP_OK) {
            size_t out_buf_size = (size_t)usb_round_up_to_mps(
                MIDI_USB_OUT_BUF_SIZE, (int)64);
            slot->urb_out = urb_alloc(out_buf_size, 0);
            if (slot->urb_out) {
                slot->urb_out->transfer.callback = midi_urb_dummy_cb;
                ESP_LOGI(TAG, "Step %d: OUT endpoint 0x%02X allocated (Launchpad LED control)",
                         step, slot->ep_out);
            } else {
                ESP_LOGW(TAG, "Failed to alloc OUT URB, freeing OUT endpoint");
                usbh_ep_free(slot->ep_out_hdl);
                slot->ep_out_hdl = NULL;
            }
        } else {
            slot->ep_out_hdl = NULL;
            ESP_LOGW(TAG, "OUT endpoint 0x%02X alloc failed: %s (Launchpad LED control unavailable)",
                     slot->ep_out, esp_err_to_name(err));
        }
        step++;
    }

    /* Notify Launchpad abstraction layer so it can enter Programmer Mode
     * and set up the initial grid display. */
    if (is_novation) {
        launchpad_on_connect();
    }
}

static void close_midi_device(midi_device_t *slot)
{
    if (!slot || !slot->dev_hdl) return;

    slot->connected = false;

    if (slot->is_launchpad) {
        launchpad_on_disconnect();
    }

    /* Free the IN endpoint pipe via private USBH API (mirrors the
     * usbh_ep_alloc in open_midi_device).  Halt → flush → dequeue → free. */
    if (slot->ep_in_hdl) {
        usbh_ep_command(slot->ep_in_hdl, USBH_EP_CMD_HALT);
        usbh_ep_command(slot->ep_in_hdl, USBH_EP_CMD_FLUSH);
        /* Drain any completed URBs so hcd_pipe_get_num_urbs returns 0 */
        urb_t *drain = NULL;
        while (usbh_ep_dequeue_urb(slot->ep_in_hdl, &drain) == ESP_OK && drain != NULL) {
            drain = NULL;
        }
        usbh_ep_free(slot->ep_in_hdl);
        slot->ep_in_hdl = NULL;
    }

    if (slot->urb_in) {
        urb_free(slot->urb_in);
        slot->urb_in = NULL;
    }

    /* Free the OUT endpoint pipe (Launchpad LED control) */
    if (slot->ep_out_hdl) {
        usbh_ep_command(slot->ep_out_hdl, USBH_EP_CMD_HALT);
        usbh_ep_command(slot->ep_out_hdl, USBH_EP_CMD_FLUSH);
        urb_t *drain = NULL;
        while (usbh_ep_dequeue_urb(slot->ep_out_hdl, &drain) == ESP_OK && drain != NULL) {
            drain = NULL;
        }
        usbh_ep_free(slot->ep_out_hdl);
        slot->ep_out_hdl = NULL;
    }

    if (slot->urb_out) {
        urb_free(slot->urb_out);
        slot->urb_out = NULL;
    }

    usb_host_device_close(s_client_hdl, slot->dev_hdl);
    memset(slot, 0, sizeof(*slot));

    ESP_LOGI(TAG, "USB MIDI device disconnected");
}

/* ----------------------------------------------------------------
 * Client event callback -- called asynchronously
 * ---------------------------------------------------------------- */
static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    switch (event_msg->event) {
        case USB_HOST_CLIENT_EVENT_NEW_DEV:
            {
                uint8_t addr = event_msg->new_dev.address;
                ESP_LOGI(TAG, ">>> USB_NEW_DEV: addr=%u (q_count before=%d)",
                         addr, s_pending_open_count);
                if (s_pending_open_mutex) xSemaphoreTake(s_pending_open_mutex, portMAX_DELAY);
                if (s_pending_open_count < MIDI_MAX_PENDING_OPENS) {
                    s_pending_open_addrs[s_pending_open_count] = addr;
                    int idx = s_pending_open_count;
                    s_pending_open_count++;
                    ESP_LOGD(TAG, "  Added to queue[%d], count now=%d", idx, s_pending_open_count);
                } else {
                    ESP_LOGW(TAG, "Pending open queue FULL (count=%d max=%d), dropping addr=%u",
                             s_pending_open_count, MIDI_MAX_PENDING_OPENS, addr);
                }
                if (s_pending_open_mutex) xSemaphoreGive(s_pending_open_mutex);
            }
            break;
        case USB_HOST_CLIENT_EVENT_DEV_GONE:
            ESP_LOGI(TAG, ">>> USB_DEV_GONE: handle=%p", event_msg->dev_gone.dev_hdl);
            /* Clear from non-MIDI cache so the address can be re-probed if
             * a different device is plugged into the same port later. */
            non_midi_cache_remove_by_hdl(event_msg->dev_gone.dev_hdl);
            s_close_dev_hdl = event_msg->dev_gone.dev_hdl;
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

static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg);

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

    ESP_LOGI(TAG, "USB Host MIDI client registered on core %d -- waiting for devices...",
             xPortGetCoreID());

    /* Check for devices that were already enumerated before we registered.
     * This handles the case where devices were connected and enumerated
     * before the MIDI client task started (NEW_DEV would have been missed). */
    {
        uint8_t addr_list[16];
        int num_devs = 0;
        if (usb_host_device_addr_list_fill(sizeof(addr_list), addr_list, &num_devs) == ESP_OK
                && num_devs > 0) {
            ESP_LOGI(TAG, "Found %d already-enumerated device(s), trying to open as MIDI...",
                     num_devs);
            for (int i = 0; i < num_devs; i++) {
                ESP_LOGI(TAG, "  Trying device at address %d", addr_list[i]);
                open_midi_device(addr_list[i]);
            }
        }
    }

    uint32_t loop_count = 0;   /* heartbeat counter */
    uint32_t scan_count  = 0;  /* device-list scan counter */

    while (s_usb_host_running) {
        /* Process client events -- THIS dispatches transfer callbacks.
         * 5 ms timeout keeps the loop responsive without busy-spinning. */
        usb_host_client_handle_events(s_client_hdl, pdMS_TO_TICKS(5));

        /* Submit first URB for newly-opened MIDI devices (private USBH API).
         * Defers submission to main loop to avoid HCD channel contention
         * when USB audio device is simultaneously allocating its channels. */
        for (int i = 0; i < MIDI_MAX_DEVICES; i++) {
            if (s_midi_devs[i].connected && s_midi_devs[i].xfer_needs_first_submit) {
                s_midi_devs[i].xfer_needs_first_submit = false;
                if (s_midi_devs[i].ep_in_hdl && s_midi_devs[i].urb_in) {
                    s_midi_devs[i].urb_in->transfer.num_bytes =
                        s_midi_devs[i].urb_in->transfer.data_buffer_size;
                    esp_err_t first_err = usbh_ep_enqueue_urb(
                        s_midi_devs[i].ep_in_hdl, s_midi_devs[i].urb_in);
                    if (first_err == ESP_OK) {
                        ESP_LOGI(TAG, "MIDI slot %d: first IN URB submitted (direct USBH)", i);
                    } else {
                        ESP_LOGW(TAG, "MIDI slot %d: failed to submit first IN URB: %s",
                                 i, esp_err_to_name(first_err));
                        /* Mark for resubmit next loop */
                        s_midi_devs[i].xfer_needs_first_submit = true;
                    }
                }
            }
        }

        /* Process completed MIDI IN URBs.  The ISR callback sets
         * xfer_needs_resubmit; we dequeue, parse MIDI data, and re-enqueue
         * in task context to keep latency low and avoid ISR-length issues. */
        for (int i = 0; i < MIDI_MAX_DEVICES; i++) {
            if (s_midi_devs[i].xfer_needs_resubmit) {
                s_midi_devs[i].xfer_needs_resubmit = false;
                midi_process_urb(&s_midi_devs[i]);
            }
        }

        /* Process completed OUT URBs — dequeue and clear busy flag so the
         * next send can proceed. */
        for (int i = 0; i < MIDI_MAX_DEVICES; i++) {
            if (s_midi_devs[i].out_urb_done) {
                s_midi_devs[i].out_urb_done = false;
                if (s_midi_devs[i].ep_out_hdl) {
                    urb_t *done = NULL;
                    usbh_ep_dequeue_urb(s_midi_devs[i].ep_out_hdl, &done);
                }
                s_midi_devs[i].xfer_out_busy = false;
            }
        }

        /* Process pending device opens from client_event_cb */
        if (s_pending_open_mutex) xSemaphoreTake(s_pending_open_mutex, portMAX_DELAY);
        if (s_pending_open_count > 0) {
            ESP_LOGI(TAG, "MIDI client: %d device(s) pending open", s_pending_open_count);
        }
        while (s_pending_open_count > 0) {
            uint8_t addr = s_pending_open_addrs[--s_pending_open_count];
            ESP_LOGI(TAG, "  Processing OPEN: addr=%u (remaining in queue: %d)",
                     addr, s_pending_open_count);
            if (s_pending_open_mutex) xSemaphoreGive(s_pending_open_mutex);

            /* Skip if already cached as non-MIDI (e.g. device re-queued after a DEV_GONE
             * that was spurious, or hub enumeration noise). */
            if (non_midi_cache_contains(addr)) {
                ESP_LOGI(TAG, "  addr=%u is in non-MIDI cache, skipping open", addr);
            } else {
                /* Event-triggered opens are not throttled; the throttle only applies to
                 * the periodic scan loop where many addresses could fire at once. */
                open_midi_device(addr);
            }
            if (s_pending_open_mutex) xSemaphoreTake(s_pending_open_mutex, portMAX_DELAY);
        }
        if (s_pending_open_mutex) xSemaphoreGive(s_pending_open_mutex);

        /* Process pending device close */
        if (s_close_dev_hdl != NULL) {
            usb_device_handle_t hdl = (usb_device_handle_t)s_close_dev_hdl;
            s_close_dev_hdl = NULL;
            midi_device_t *slot = find_device_by_hdl(hdl);
            if (slot) {
                ESP_LOGI(TAG, "MIDI client: closing slot for handle %p", hdl);
                close_midi_device(slot);
            } else {
                ESP_LOGD(TAG, "MIDI client: DEV_GONE for unknown handle %p (not our device, ignoring)", hdl);
            }
        }

        /* Periodically query the host stack for any newly-enumerated devices.
         * Scan more frequently (every 500 ms) to catch hotplugged devices quickly.
         * This handles hub-attached devices and late arrivals.
         * However, actual open attempts are throttled (200ms apart) to prevent
         * "No more HCD channels available" errors when many devices enumerate at once. */
        if (++scan_count >= 100) {  /* 100 * 5ms = 500ms */
            scan_count = 0;
            uint8_t addr_list[16];
            int num_devs = 0;
            esp_err_t fill_err = usb_host_device_addr_list_fill(
                    sizeof(addr_list), addr_list, &num_devs);
            if (fill_err == ESP_OK && num_devs > 0) {
                for (int i = 0; i < num_devs; i++) {
                    /* Try addresses not already in our MIDI slots, and not
                     * previously identified as non-MIDI (hub, audio, HID, etc.) */
                    if (find_device_by_addr(addr_list[i]) == NULL &&
                        !non_midi_cache_contains(addr_list[i])) {
                        /* Respect throttle to prevent HCD exhaustion */
                        if (should_attempt_device_open()) {
                            ESP_LOGI(TAG, "USB scan: new device at addr %d, trying MIDI open...",
                                     addr_list[i]);
                            open_midi_device(addr_list[i]);
                        } else {
                            ESP_LOGD(TAG, "USB scan: found device at addr %d, but throttled",
                                     addr_list[i]);
                        }
                    }
                }
            }
        }

        /* Periodic heartbeat every ~10 seconds (2000 × 5 ms) */
        if (++loop_count >= 2000) {
            loop_count = 0;
            int conn_count = 0;
            for (int i = 0; i < MIDI_MAX_DEVICES; i++) {
                if (s_midi_devs[i].connected) conn_count++;
            }
            ESP_LOGI(TAG, "USB MIDI client heartbeat: %d device(s) connected", conn_count);
        }
    }

    /* Close all open devices on exit */
    for (int i = 0; i < MIDI_MAX_DEVICES; i++) {
        if (s_midi_devs[i].dev_hdl) {
            close_midi_device(&s_midi_devs[i]);
        }
    }
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

    /* Create mutex for pending open queue */
    s_pending_open_mutex = xSemaphoreCreateMutex();
    if (s_pending_open_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create pending open mutex");
        s_usb_host_running = false;
        return -1;
    }

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

    /* Clean up mutex */
    if (s_pending_open_mutex) {
        vSemaphoreDelete(s_pending_open_mutex);
        s_pending_open_mutex = NULL;
    }
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
    return any_device_connected();
#else
    return false;
#endif
}

bool esp32_midi_is_launchpad(void)
{
#if FMRACK_MIDI_USB_ENABLE
    return find_launchpad_device() != NULL;
#else
    return false;
#endif
}

#if FMRACK_MIDI_USB_ENABLE
/* Wait for previous OUT transfer to complete, with timeout. */
static bool wait_out_ready(midi_device_t *lp, int timeout_ms)
{
    int waited = 0;
    while (lp->xfer_out_busy && waited < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(1));
        waited++;
    }
    return !lp->xfer_out_busy;
}
#endif

int esp32_midi_usb_send_packets(const uint8_t *packets, int len)
{
#if FMRACK_MIDI_USB_ENABLE
    midi_device_t *lp = find_launchpad_device();
    if (!lp || !lp->ep_out_hdl || !lp->urb_out || len <= 0) return -1;
    if (len % 4 != 0) return -1;
    if (!wait_out_ready(lp, 50)) return -1;

    usb_transfer_t *transfer = &lp->urb_out->transfer;
    if (len > (int)transfer->data_buffer_size)
        len = (int)transfer->data_buffer_size;

    memcpy(transfer->data_buffer, packets, len);
    transfer->num_bytes = len;
    lp->xfer_out_busy = true;

    esp_err_t err = usbh_ep_enqueue_urb(lp->ep_out_hdl, lp->urb_out);
    if (err != ESP_OK) {
        lp->xfer_out_busy = false;
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
    midi_device_t *lp = find_launchpad_device();
    if (!lp || !lp->ep_out_hdl || !lp->urb_out || len < 2) return -1;
    if (!wait_out_ready(lp, 50)) return -1;

    usb_transfer_t *out_xfer = &lp->urb_out->transfer;
    uint8_t *buf = out_xfer->data_buffer;
    int buf_size = (int)out_xfer->data_buffer_size;
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

    out_xfer->num_bytes = pos;
    lp->xfer_out_busy = true;

    esp_err_t err = usbh_ep_enqueue_urb(lp->ep_out_hdl, lp->urb_out);
    if (err != ESP_OK) {
        lp->xfer_out_busy = false;
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
