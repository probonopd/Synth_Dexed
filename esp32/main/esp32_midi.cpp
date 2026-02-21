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
#include "fmrack_wrapper.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"

#include <string.h>

/* ---------- USB Host MIDI ---------- */
#if FMRACK_MIDI_USB_ENABLE
#include "usb/usb_host.h"
#endif

static const char *TAG = "fmrack_midi";

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
                fmrack_handle_sysex(s_sysex_buf, s_sysex_len, sysex_channel);
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
            fmrack_handle_midi(p->status, p->data[0], p->data[1]);
        } else if (p->data_needed == 1) {
            fmrack_handle_midi(p->status, p->data[0], 0);
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
} midi_device_t;

static midi_device_t s_midi_dev = {};
static usb_host_client_handle_t s_client_hdl = NULL;
static volatile bool s_usb_host_running = false;
static TaskHandle_t s_usb_host_lib_task = NULL;
static TaskHandle_t s_midi_host_task_hdl = NULL;

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
        fmrack_handle_sysex(s_usb_sysex_buf, s_usb_sysex_len, ch);
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
        case 0x0A: /* Poly Aftertouch */
        case 0x0B: /* Control Change */
        case 0x0E: /* Pitch Bend */
            fmrack_handle_midi(b0, b1, b2);
            break;

        /* 2-byte channel messages */
        case 0x0C: /* Program Change */
        case 0x0D: /* Channel Aftertouch */
            fmrack_handle_midi(b0, b1, 0);
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

    int offset = 0;
    const usb_standard_desc_t *cur_desc = (const usb_standard_desc_t *)config_desc;
    uint16_t wTotalLength = config_desc->wTotalLength;

    while (cur_desc != NULL) {
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

        /* Process 4-byte USB-MIDI packets */
        for (int i = 0; i + 3 < num_bytes; i += 4) {
            /* Skip padding packets (all zeros) */
            if (data[i] == 0 && data[i+1] == 0 && data[i+2] == 0 && data[i+3] == 0)
                continue;
            usb_midi_process_packet(&data[i]);
        }
    } else if (transfer->status == USB_TRANSFER_STATUS_NO_DEVICE) {
        ESP_LOGW(TAG, "USB MIDI device disconnected during transfer");
        return; /* Don't resubmit */
    } else {
        ESP_LOGW(TAG, "USB MIDI transfer status: %d", transfer->status);
    }

    /* Resubmit for continuous reading */
    if (s_midi_dev.connected) {
        esp_err_t err = usb_host_transfer_submit(transfer);
        if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to resubmit MIDI IN transfer: %s",
                     esp_err_to_name(err));
        }
    }
}

/* ----------------------------------------------------------------
 * Open / close MIDI device
 * ---------------------------------------------------------------- */
static void open_midi_device(uint8_t dev_addr)
{
    usb_device_handle_t dev_hdl = NULL;
    esp_err_t err = usb_host_device_open(s_client_hdl, dev_addr, &dev_hdl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open device %d: %s", dev_addr, esp_err_to_name(err));
        return;
    }

    /* Log device info */
    const usb_device_desc_t *dev_desc = NULL;
    usb_host_get_device_descriptor(dev_hdl, &dev_desc);
    if (dev_desc) {
        ESP_LOGI(TAG, "USB device: VID=0x%04X PID=0x%04X class=%d",
                 dev_desc->idVendor, dev_desc->idProduct, dev_desc->bDeviceClass);
    }

    /* Look for MIDI Streaming interface */
    memset(&s_midi_dev, 0, sizeof(s_midi_dev));
    if (!find_midi_interface(dev_hdl, &s_midi_dev)) {
        ESP_LOGI(TAG, "Device %d is not a MIDI device, closing", dev_addr);
        usb_host_device_close(s_client_hdl, dev_hdl);
        return;
    }

    s_midi_dev.dev_hdl = dev_hdl;

    /* Claim the MIDI Streaming interface */
    err = usb_host_interface_claim(s_client_hdl, dev_hdl,
                                    s_midi_dev.midi_intf_num, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to claim interface %d: %s",
                 s_midi_dev.midi_intf_num, esp_err_to_name(err));
        usb_host_device_close(s_client_hdl, dev_hdl);
        memset(&s_midi_dev, 0, sizeof(s_midi_dev));
        return;
    }

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

    s_midi_dev.xfer_in->device_handle = dev_hdl;
    s_midi_dev.xfer_in->bEndpointAddress = s_midi_dev.ep_in;
    s_midi_dev.xfer_in->callback = midi_transfer_cb;
    s_midi_dev.xfer_in->context = &s_midi_dev;
    s_midi_dev.xfer_in->num_bytes = buf_size;

    s_midi_dev.connected = true;

    /* Submit first IN transfer -- starts continuous MIDI reading */
    err = usb_host_transfer_submit(s_midi_dev.xfer_in);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to submit initial IN transfer: %s",
                 esp_err_to_name(err));
        s_midi_dev.connected = false;
        usb_host_transfer_free(s_midi_dev.xfer_in);
        usb_host_interface_release(s_client_hdl, dev_hdl, s_midi_dev.midi_intf_num);
        usb_host_device_close(s_client_hdl, dev_hdl);
        memset(&s_midi_dev, 0, sizeof(s_midi_dev));
        return;
    }

    ESP_LOGI(TAG, "USB MIDI keyboard connected -- streaming from EP 0x%02X",
             s_midi_dev.ep_in);
}

static void close_midi_device(void)
{
    if (!s_midi_dev.dev_hdl) return;

    s_midi_dev.connected = false;

    usb_host_interface_release(s_client_hdl, s_midi_dev.dev_hdl,
                                s_midi_dev.midi_intf_num);

    if (s_midi_dev.xfer_in) {
        usb_host_transfer_free(s_midi_dev.xfer_in);
        s_midi_dev.xfer_in = NULL;
    }

    usb_host_device_close(s_client_hdl, s_midi_dev.dev_hdl);
    memset(&s_midi_dev, 0, sizeof(s_midi_dev));

    ESP_LOGI(TAG, "USB MIDI keyboard disconnected");
}

/* ----------------------------------------------------------------
 * Client event callback -- called asynchronously
 * ---------------------------------------------------------------- */
static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    switch (event_msg->event) {
        case USB_HOST_CLIENT_EVENT_NEW_DEV:
            ESP_LOGI(TAG, "New USB device at address %d",
                     event_msg->new_dev.address);
            s_new_dev_addr = event_msg->new_dev.address;
            s_actions |= MIDI_HOST_ACTION_OPEN;
            break;
        case USB_HOST_CLIENT_EVENT_DEV_GONE:
            ESP_LOGW(TAG, "USB device gone");
            s_actions |= MIDI_HOST_ACTION_CLOSE;
            break;
        default:
            break;
    }
}

/* ----------------------------------------------------------------
 * USB Host Library daemon task (handles enumeration, hub events)
 * ---------------------------------------------------------------- */
static void usb_host_lib_task(void *arg)
{
    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "USB Host install failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    /* Signal the caller that host lib is ready */
    xTaskNotifyGive((TaskHandle_t)arg);

    ESP_LOGI(TAG, "USB Host Library installed -- daemon running");

    while (s_usb_host_running) {
        uint32_t event_flags;
        err = usb_host_lib_handle_events(pdMS_TO_TICKS(200), &event_flags);
        if (err == ESP_OK) {
            if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
                usb_host_device_free_all();
            }
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

    ESP_LOGI(TAG, "USB Host MIDI client registered -- waiting for keyboard...");

    while (s_usb_host_running) {
        /* Process actions from callbacks */
        if (s_actions & MIDI_HOST_ACTION_OPEN) {
            s_actions &= ~MIDI_HOST_ACTION_OPEN;
            open_midi_device(s_new_dev_addr);
        }
        if (s_actions & MIDI_HOST_ACTION_CLOSE) {
            s_actions &= ~MIDI_HOST_ACTION_CLOSE;
            close_midi_device();
        }

        /* Process client events -- THIS dispatches transfer callbacks */
        usb_host_client_handle_events(s_client_hdl, pdMS_TO_TICKS(50));
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

    /* Task 1: USB Host Library daemon */
    BaseType_t ret = xTaskCreatePinnedToCore(
        usb_host_lib_task,
        "usb_host_lib",
        4096,
        xTaskGetCurrentTaskHandle(), /* pass our handle for notification */
        USB_MIDI_TASK_PRIORITY,
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
        ESP_LOGE(TAG, "Timeout waiting for USB Host Library installation");
        s_usb_host_running = false;
        return -1;
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
