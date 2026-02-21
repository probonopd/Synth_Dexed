/*
 * FMRack ESP32-S3 Port - Wi-Fi and UDP MIDI Implementation
 *
 * Provides Wi-Fi connectivity and a UDP server for receiving
 * MIDI messages over the network (compatible with the desktop
 * FMRack UDP protocol on port 50007).
 */

#include "esp32_wifi.h"
#include "esp32_config.h"
#include "fmrack_wrapper.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/sockets.h"

#include <string.h>

static const char *TAG = "fmrack_wifi";

#if FMRACK_MIDI_UDP_ENABLE

// Event group for Wi-Fi status
static EventGroupHandle_t s_wifi_event_group = NULL;
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static TaskHandle_t s_udp_task = NULL;
static volatile bool s_udp_running = false;
static volatile bool s_wifi_connected = false;

static int s_retry_count = 0;
#define MAX_RETRY 10

// Wi-Fi event handler
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "Wi-Fi STA started, connecting...");
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                s_wifi_connected = false;
                if (s_retry_count < MAX_RETRY) {
                    esp_wifi_connect();
                    s_retry_count++;
                    ESP_LOGI(TAG, "Retrying Wi-Fi connection (%d/%d)", s_retry_count, MAX_RETRY);
                } else {
                    xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                    ESP_LOGW(TAG, "Wi-Fi connection failed after %d retries", MAX_RETRY);
                }
                break;
            case WIFI_EVENT_AP_STACONNECTED: {
                wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
                ESP_LOGI(TAG, "Station connected (AID=%d)", event->aid);
                break;
            }
            case WIFI_EVENT_AP_STADISCONNECTED: {
                wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
                ESP_LOGI(TAG, "Station disconnected (AID=%d)", event->aid);
                break;
            }
            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_count = 0;
        s_wifi_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/**
 * UDP MIDI server task.
 * Listens for incoming MIDI messages on the configured port.
 * Protocol is compatible with the desktop FMRack UDP server.
 */
static void udp_midi_task(void *param)
{
    ESP_LOGI(TAG, "UDP MIDI server starting on port %d", FMRACK_MIDI_UDP_PORT);

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(FMRACK_MIDI_UDP_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create UDP socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    // Set receive timeout so we can check s_udp_running
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind UDP socket: errno %d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "UDP MIDI server listening on port %d", FMRACK_MIDI_UDP_PORT);

    uint8_t buf[512];
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    while (s_udp_running) {
        int len = recvfrom(sock, buf, sizeof(buf), 0,
                           (struct sockaddr *)&client_addr, &addr_len);
        if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;  // Timeout, check running flag
            }
            ESP_LOGW(TAG, "UDP receive error: errno %d", errno);
            continue;
        }

        if (len == 0) continue;

        // Parse MIDI messages from the UDP packet
        // Same protocol as desktop FMRack UDP server
        int i = 0;
        while (i < len) {
            uint8_t status = buf[i];

            if (status == 0xF0) {
                // SysEx message
                int sysex_end = i + 1;
                while (sysex_end < len && buf[sysex_end] != 0xF7) sysex_end++;
                if (sysex_end < len && buf[sysex_end] == 0xF7) sysex_end++;
                int sysex_len = sysex_end - i;
                if (sysex_len >= 2) {
                    uint8_t sysex_channel = 0;
                    if (sysex_len > 2 && buf[i + 1] == 0x43) {
                        sysex_channel = (buf[i + 2] & 0x0F) + 1;
                    }
                    fmrack_handle_sysex(&buf[i], sysex_len, sysex_channel);
                }
                i = sysex_end;
            } else if ((status & 0xF0) >= 0x80 && (status & 0xF0) <= 0xE0 && (i + 2) < len) {
                // Channel message (3 bytes)
                fmrack_handle_midi(buf[i], buf[i + 1], buf[i + 2]);
                i += 3;
            } else if (status >= 0xF8) {
                // Real-time (1 byte)
                i += 1;
            } else {
                i += 1;
            }
        }
    }

    close(sock);
    ESP_LOGI(TAG, "UDP MIDI server stopped");
    vTaskDelete(NULL);
}

int esp32_wifi_init(void)
{
    ESP_LOGI(TAG, "Initializing Wi-Fi...");

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

#if FMRACK_WIFI_AP_MODE
    // Access Point mode
    esp_netif_create_default_wifi_ap();
#else
    // Station mode
    esp_netif_create_default_wifi_sta();
#endif

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

#if FMRACK_WIFI_AP_MODE
    // Configure AP
    wifi_config_t wifi_config = {};
    strncpy((char *)wifi_config.ap.ssid, FMRACK_WIFI_SSID, sizeof(wifi_config.ap.ssid));
    wifi_config.ap.ssid_len = strlen(FMRACK_WIFI_SSID);
    wifi_config.ap.max_connection = 4;

    if (strlen(FMRACK_WIFI_PASSWORD) > 0) {
        strncpy((char *)wifi_config.ap.password, FMRACK_WIFI_PASSWORD,
                sizeof(wifi_config.ap.password));
        wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi AP started: SSID=%s", FMRACK_WIFI_SSID);
    s_wifi_connected = true;
#else
    // Configure STA
    wifi_config_t wifi_config = {};
    strncpy((char *)wifi_config.sta.ssid, FMRACK_WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, FMRACK_WIFI_PASSWORD,
            sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi STA connecting to SSID=%s", FMRACK_WIFI_SSID);

    // Wait for connection (with timeout)
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE,
        pdMS_TO_TICKS(15000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Wi-Fi connected successfully");
    } else {
        ESP_LOGW(TAG, "Wi-Fi connection timed out (will retry in background)");
    }
#endif

    return 0;
}

int esp32_wifi_udp_start(void)
{
    if (s_udp_running) {
        return 0;
    }

    s_udp_running = true;

    BaseType_t ret = xTaskCreatePinnedToCore(
        udp_midi_task,
        "udp_midi",
        UDP_TASK_STACK_SIZE,
        NULL,
        UDP_TASK_PRIORITY,
        &s_udp_task,
        tskNO_AFFINITY
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create UDP MIDI task");
        s_udp_running = false;
        return -1;
    }

    return 0;
}

void esp32_wifi_stop(void)
{
    s_udp_running = false;

    if (s_udp_task) {
        vTaskDelay(pdMS_TO_TICKS(1500));  // Wait for socket timeout
        s_udp_task = NULL;
    }

    esp_wifi_stop();
    esp_wifi_deinit();

    ESP_LOGI(TAG, "Wi-Fi stopped");
}

bool esp32_wifi_is_connected(void)
{
    return s_wifi_connected;
}

#else /* !FMRACK_MIDI_UDP_ENABLE */

// Stubs when UDP MIDI is disabled
int esp32_wifi_init(void) { return 0; }
int esp32_wifi_udp_start(void) { return 0; }
void esp32_wifi_stop(void) {}
bool esp32_wifi_is_connected(void) { return false; }

#endif /* FMRACK_MIDI_UDP_ENABLE */
