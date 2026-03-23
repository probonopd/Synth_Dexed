/*
 * FMRack ESP32-S3 Port – WLAN management
 *
 * Boot flow:
 *
 *   1. Load SSID / password from NVS (saved by the captive portal).
 *   2a. If credentials exist → connect to the user's network (STA mode).
 *       On success → start Apple MIDI + mDNS.
 *       On failure → fall through to 2b.
 *   2b. No credentials or connection failed →
 *       Start our own AP "Synth-Dexed-Setup" and run the captive portal
 *       (DNS redirect + HTTP credentials form).  When the user submits
 *       the form the device reboots and retries from step 1.
 */

#include "esp32_wifi.h"
#include "esp32_config.h"
#include "esp32_captive_portal.h"
#include "esp32_applemidi.h"
#include "esp32_webserver.h"
#include "esp32_syslog.h"
#include "tsf_engine.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"

#include <string.h>

static const char *TAG = "wlan";

/* -----------------------------------------------------------------------
 * Event-group bits
 * ---------------------------------------------------------------------- */
static EventGroupHandle_t s_event_group = NULL;
#define CONNECTED_BIT   BIT0
#define FAIL_BIT        BIT1

#define STA_MAX_RETRY   5

static volatile bool s_sta_connected = false;
static volatile bool s_ap_active     = false;
static int  s_retry = 0;

/* Netif handles so we can tear them down cleanly */
static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif  = NULL;

/* -----------------------------------------------------------------------
 * Event handler
 * ---------------------------------------------------------------------- */
static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            s_sta_connected = false;
            if (s_retry < STA_MAX_RETRY) {
                s_retry++;
                ESP_LOGW(TAG, "WLAN disconnected, retry %d/%d", s_retry, STA_MAX_RETRY);
                esp_wifi_connect();
            } else {
                xEventGroupSetBits(s_event_group, FAIL_BIT);
            }
            break;
        case WIFI_EVENT_AP_STACONNECTED: {
            wifi_event_ap_staconnected_t *e = (wifi_event_ap_staconnected_t *)data;
            ESP_LOGI(TAG, "Client joined AP (AID=%d)", e->aid);
            break;
        }
        case WIFI_EVENT_AP_STADISCONNECTED: {
            wifi_event_ap_stadisconnected_t *e = (wifi_event_ap_stadisconnected_t *)data;
            ESP_LOGI(TAG, "Client left AP (AID=%d)", e->aid);
            break;
        }
        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "WLAN connected — IP: " IPSTR, IP2STR(&e->ip_info.ip));
        s_retry = 0;
        s_sta_connected = true;
        xEventGroupSetBits(s_event_group, CONNECTED_BIT);
    }
}

/* Registered once; used for both STA and AP modes */
static esp_event_handler_instance_t s_inst_wifi = NULL;
static esp_event_handler_instance_t s_inst_ip   = NULL;

/* -----------------------------------------------------------------------
 * Start in Station mode and wait for a connection (or timeout)
 * ---------------------------------------------------------------------- */
static bool start_sta(const char *ssid, const char *pass)
{
    s_sta_netif = esp_netif_create_default_wifi_sta();

    wifi_config_t cfg = {};
    strlcpy((char *)cfg.sta.ssid,     ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password));
    cfg.sta.threshold.authmode =
        (pass[0] != '\0') ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to WLAN '%s'...", ssid);

    EventBits_t bits = xEventGroupWaitBits(
        s_event_group,
        CONNECTED_BIT | FAIL_BIT,
        pdFALSE, pdFALSE,
        pdMS_TO_TICKS(15000));

    if (bits & CONNECTED_BIT) {
        /* Disable WiFi power-save.  The default WIFI_PS_MIN_MODEM causes the
         * radio to buffer packets and then wake with a DMA burst every DTIM
         * beacon (100–300 ms).  When a USB keyboard is also attached that
         * burst coincides with USB enumeration DMA traffic, saturating the
         * AHB bus long enough to cause I2S underruns (audible stutter).
         * WIFI_PS_NONE keeps DMA traffic low and steady — higher current draw
         * but no more wakeup spikes. */
        esp_wifi_set_ps(WIFI_PS_NONE);
        return true;
    }

    ESP_LOGW(TAG, "WLAN connection to '%s' failed", ssid);
    return false;
}

/* -----------------------------------------------------------------------
 * Start in Access Point mode (captive portal)
 * ---------------------------------------------------------------------- */
static void start_ap(void)
{
    s_ap_netif = esp_netif_create_default_wifi_ap();

    wifi_config_t cfg = {};
    strlcpy((char *)cfg.ap.ssid, FMRACK_CAPTIVE_AP_SSID,
            sizeof(cfg.ap.ssid));
    cfg.ap.ssid_len       = (uint8_t)strlen(FMRACK_CAPTIVE_AP_SSID);
    cfg.ap.max_connection = 4;
    cfg.ap.authmode       = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_ap_active = true;
    ESP_LOGI(TAG, "AP started — SSID: '%s' (no password)", FMRACK_CAPTIVE_AP_SSID);
}

/* -----------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */
int esp32_wlan_init(void)
{
    s_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    /* Register event handlers once — they work for both STA and AP modes */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID,
        wifi_event_handler, NULL, &s_inst_wifi));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP,
        wifi_event_handler, NULL, &s_inst_ip));

    /* Try to load credentials saved by captive portal */
    char ssid[33] = { 0 };
    char pass[65] = { 0 };

    bool have_creds = esp32_wlan_load_credentials(ssid, pass);

    if (have_creds) {
        ESP_LOGI(TAG, "Found saved credentials for '%s', trying STA...", ssid);

        if (start_sta(ssid, pass)) {
            /* Connected — spin up Apple MIDI */
            if (esp32_applemidi_init() == 0) {
                esp32_applemidi_start();
                ESP_LOGI(TAG, "Apple MIDI ready — synth visible in Audio MIDI Setup");
            }
            /* Start SFO upload web server (mDNS _http._tcp) */
            if (esp32_webserver_start() != 0) {
                ESP_LOGW(TAG, "Web server start failed (SFO upload unavailable)");
            }
            /* Start UDP syslog forwarder — receive with: nc -lup 5140 */
            esp32_syslog_start(FMRACK_SYSLOG_HOST, FMRACK_SYSLOG_PORT);
            /* Log status immediately so it appears in syslog on connect.
             * TSF init happened in Phase 3 (before WiFi), so its result is
             * only visible here via this deferred status log. */
            ESP_LOGI(TAG, "=== Post-WiFi status ===");
            ESP_LOGI(TAG, "TSF drum engine: %s (%d active voices)",
                     tsf_engine_is_loaded() ? "LOADED" : "NOT LOADED",
                     tsf_engine_active_voices());
            ESP_LOGI(TAG, "Web server:      http://synth-dexed.local/");
            ESP_LOGI(TAG, "Syslog:          broadcasting to %s:%d",
                     FMRACK_SYSLOG_HOST, FMRACK_SYSLOG_PORT);
            return 0;
        }

        /* Connection failed — fall through to AP mode.
         * Re-create the WiFi driver for AP mode. */
        ESP_LOGW(TAG, "STA connection failed, starting captive portal");
        esp_wifi_stop();
        esp_wifi_deinit();
        if (s_sta_netif) {
            esp_netif_destroy(s_sta_netif);
            s_sta_netif = NULL;
        }

        /* Re-initialise Wi-Fi for AP mode */
        ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
        ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    } else {
        ESP_LOGI(TAG, "No WLAN credentials stored — starting captive portal");
    }

    start_ap();
    esp32_captive_portal_start();

    return 0;
}

bool esp32_wlan_is_connected(void) { return s_sta_connected; }
bool esp32_wlan_is_ap_mode(void)   { return s_ap_active;     }

void esp32_wlan_stop(void)
{
    if (s_ap_active) {
        esp32_captive_portal_stop();
    }
    esp32_webserver_stop();
    esp32_applemidi_stop();
    esp_wifi_stop();
    esp_wifi_deinit();
    if (s_sta_netif) { esp_netif_destroy(s_sta_netif); s_sta_netif = NULL; }
    if (s_ap_netif)  { esp_netif_destroy(s_ap_netif);  s_ap_netif  = NULL; }
    s_sta_connected = false;
    s_ap_active     = false;
    ESP_LOGI(TAG, "WLAN stopped");
}

/* ---- Legacy shims (used from main.cpp) ---- */
int  esp32_wifi_init(void)         { return esp32_wlan_init(); }
int  esp32_wifi_udp_start(void)    { return 0; }   /* superseded by Apple MIDI */
void esp32_wifi_stop(void)         { esp32_wlan_stop(); }
bool esp32_wifi_is_connected(void) { return esp32_wlan_is_connected(); }
