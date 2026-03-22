/*
 * FMRack ESP32-S3 Port – Captive WLAN configuration portal
 *
 * Runs while the device is in Access Point mode.  All DNS queries on the AP
 * subnet are answered with 192.168.4.1 so that phones and laptops open the
 * configuration page automatically.  The web form lets the user enter WLAN
 * credentials which are stored in NVS; the device then reboots into STA mode.
 */

#include "esp32_captive_portal.h"
#include "esp32_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_system.h"
#include "lwip/sockets.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "captive";

#define NVS_NAMESPACE   "fmrack"
#define NVS_KEY_SSID    "wlan_ssid"
#define NVS_KEY_PASS    "wlan_pass"

/* -------------------------------------------------------------------------
 * NVS credential management
 * ---------------------------------------------------------------------- */
bool esp32_wlan_load_credentials(char *ssid, char *pass)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return false;

    size_t ssid_len = 33, pass_len = 65;
    bool ok = false;

    if (nvs_get_str(h, NVS_KEY_SSID, ssid, &ssid_len) == ESP_OK &&
        ssid_len > 1) {   /* at least one char + null terminator */
        pass[0] = '\0';
        pass_len = 65;
        if (nvs_get_str(h, NVS_KEY_PASS, pass, &pass_len) != ESP_OK)
            pass[0] = '\0';
        ok = true;
    }

    nvs_close(h);
    return ok;
}

int esp32_wlan_save_credentials(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return -1;
    }

    nvs_set_str(h, NVS_KEY_SSID, ssid);
    nvs_set_str(h, NVS_KEY_PASS, pass ? pass : "");
    nvs_commit(h);
    nvs_close(h);

    ESP_LOGI(TAG, "WLAN credentials saved (ssid='%s')", ssid);
    return 0;
}

void esp32_wlan_erase_credentials(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, NVS_KEY_SSID);
        nvs_erase_key(h, NVS_KEY_PASS);
        nvs_commit(h);
        nvs_close(h);
    }
}

/* -------------------------------------------------------------------------
 * URL-decode helper: decode %xx escapes and '+' → space in-place.
 * ---------------------------------------------------------------------- */
static void url_decode(char *dst, const char *src, size_t dst_max)
{
    size_t di = 0;
    for (size_t si = 0; src[si] && di + 1 < dst_max; si++) {
        if (src[si] == '%' && src[si+1] && src[si+2]) {
            char hex[3] = { src[si+1], src[si+2], 0 };
            dst[di++] = (char)strtol(hex, NULL, 16);
            si += 2;
        } else if (src[si] == '+') {
            dst[di++] = ' ';
        } else {
            dst[di++] = src[si];
        }
    }
    dst[di] = '\0';
}

/* Extract a field value from an application/x-www-form-urlencoded body.
 * e.g. "ssid=MyNet&pass=abc" → field "ssid" → "MyNet"             */
static bool form_get_field(const char *body, const char *key,
                            char *val_out, size_t val_max)
{
    char search[64];
    snprintf(search, sizeof(search), "%s=", key);
    const char *p = strstr(body, search);
    if (!p) return false;
    p += strlen(search);

    /* find the end of this field value (& or end-of-string) */
    const char *end = strchr(p, '&');
    size_t vlen = end ? (size_t)(end - p) : strlen(p);
    if (vlen >= val_max) vlen = val_max - 1;

    char raw[256];
    if (vlen >= sizeof(raw)) vlen = sizeof(raw) - 1;
    memcpy(raw, p, vlen);
    raw[vlen] = '\0';

    url_decode(val_out, raw, val_max);
    return true;
}

/* -------------------------------------------------------------------------
 * HTML pages
 * ---------------------------------------------------------------------- */

/* Main configuration page */
static const char PORTAL_HTML[] =
    "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>FMSynthESP – WLAN Setup</title>"
    "<style>"
    "body{font-family:system-ui,sans-serif;background:#1a1a2e;color:#eee;"
         "display:flex;justify-content:center;align-items:center;"
         "min-height:100vh;margin:0;}"
    ".card{background:#16213e;border-radius:12px;padding:2rem;max-width:360px;"
          "width:90%;box-shadow:0 8px 32px rgba(0,0,0,.5);}"
    "h1{margin:0 0 .25rem;font-size:1.4rem;color:#e94560;}"
    "p{margin:0 0 1.5rem;font-size:.85rem;color:#aaa;}"
    "label{display:block;margin:.75rem 0 .25rem;font-size:.85rem;color:#ccc;}"
    "input[type=text],input[type=password]{"
    "  width:100%;box-sizing:border-box;padding:.6rem .8rem;"
    "  border:1px solid #334;border-radius:6px;"
    "  background:#0f3460;color:#eee;font-size:1rem;}"
    "input[type=text]:focus,input[type=password]:focus{"
    "  outline:2px solid #e94560;border-color:#e94560;}"
    "button{margin-top:1.5rem;width:100%;padding:.75rem;"
    "  background:#e94560;color:#fff;border:none;border-radius:6px;"
    "  font-size:1rem;cursor:pointer;}"
    "button:hover{background:#c73652;}"
    "</style></head><body>"
    "<div class='card'>"
    "<h1>&#127926; FMSynthESP</h1>"
    "<p>Enter your WLAN credentials so the synthesizer can join your network "
    "and appear in Apple&nbsp;MIDI&nbsp;Setup.</p>"
    "<form method='POST' action='/configure'>"
    "<label>Network name (SSID)</label>"
    "<input type='text' name='ssid' maxlength='32' required autocomplete='off' "
    "  autocapitalize='none' spellcheck='false'>"
    "<label>Password</label>"
    "<input type='password' name='pass' maxlength='64' autocomplete='off'>"
    "<button type='submit'>Connect &amp; Reboot</button>"
    "</form>"
    "</div></body></html>";

/* Shown after the user submits credentials */
static const char SUCCESS_HTML[] =
    "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>FMSynthESP – Connecting</title>"
    "<style>"
    "body{font-family:system-ui,sans-serif;background:#1a1a2e;color:#eee;"
         "display:flex;justify-content:center;align-items:center;"
         "min-height:100vh;margin:0;text-align:center;}"
    ".card{background:#16213e;border-radius:12px;padding:2rem;max-width:360px;"
          "width:90%;}"
    "h1{color:#4caf50;font-size:2rem;margin:0 0 .5rem;}"
    "p{color:#aaa;}"
    "</style></head><body>"
    "<div class='card'>"
    "<h1>&#10003;</h1>"
    "<h2>Credentials saved</h2>"
    "<p>The synthesizer will now reboot and connect to your network.<br>"
    "Open <strong>Audio MIDI Setup</strong> on your Mac, click "
    "<em>Network</em>, and look for <strong>Synth&nbsp;Dexed</strong>.</p>"
    "</div></body></html>";

/* -------------------------------------------------------------------------
 * HTTP handlers
 * ---------------------------------------------------------------------- */
static esp_err_t handler_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, PORTAL_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* Apple captive-portal probe: deliberately NOT returning "Success" causes
 * iOS/macOS to show the captive portal notification.               */
static esp_err_t handler_apple_probe(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* Android captive-portal probe (expects 204 → if we give 302, it pops up) */
static esp_err_t handler_android_probe(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* Catch-all: redirect anything else to the portal root */
static esp_err_t handler_catch_all(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* Handle form POST /configure */
static esp_err_t handler_configure(httpd_req_t *req)
{
    char body[256] = { 0 };
    int content_len = req->content_len;
    if (content_len <= 0 || content_len > (int)(sizeof(body) - 1)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad request");
        return ESP_FAIL;
    }

    int received = httpd_req_recv(req, body, (size_t)content_len);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }
    body[received] = '\0';

    char ssid[33] = { 0 };
    char pass[65] = { 0 };
    if (!form_get_field(body, "ssid", ssid, sizeof(ssid)) || ssid[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID required");
        return ESP_FAIL;
    }
    form_get_field(body, "pass", pass, sizeof(pass));

    /* Save and reboot */
    esp32_wlan_save_credentials(ssid, pass);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, SUCCESS_HTML, HTTPD_RESP_USE_STRLEN);

    /* Give the response time to reach the browser before rebooting */
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();

    return ESP_OK;
}

static httpd_handle_t s_httpd = NULL;

static int start_http_server(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.max_uri_handlers = 8;
    cfg.stack_size = 6144;
    cfg.max_resp_headers = 16;  // Increase number of allowed response headers

    if (httpd_start(&s_httpd, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return -1;
    }

    static const httpd_uri_t u_root = {
        .uri = "/", .method = HTTP_GET, .handler = handler_root
    };
    static const httpd_uri_t u_apple = {
        .uri = "/hotspot-detect.html", .method = HTTP_GET,
        .handler = handler_apple_probe
    };
    static const httpd_uri_t u_android = {
        .uri = "/generate_204", .method = HTTP_GET,
        .handler = handler_android_probe
    };
    static const httpd_uri_t u_configure = {
        .uri = "/configure", .method = HTTP_POST, .handler = handler_configure
    };
    static const httpd_uri_t u_catch = {
        .uri = "/*", .method = HTTP_GET, .handler = handler_catch_all
    };

    httpd_register_uri_handler(s_httpd, &u_root);
    httpd_register_uri_handler(s_httpd, &u_apple);
    httpd_register_uri_handler(s_httpd, &u_android);
    httpd_register_uri_handler(s_httpd, &u_configure);
    httpd_register_uri_handler(s_httpd, &u_catch);

    ESP_LOGI(TAG, "HTTP server started on port 80");
    return 0;
}

/* -------------------------------------------------------------------------
 * Minimal DNS server – redirects everything to 192.168.4.1
 *
 * DNS wire format (big-endian):
 *   Header:   ID(2) FLAGS(2) QDCOUNT(2) ANCOUNT(2) NSCOUNT(2) ARCOUNT(2)
 *   Question: QNAME(labels) QTYPE(2) QCLASS(2)
 *   Answer:   NAME(2-ptr) TYPE(2) CLASS(2) TTL(4) RDLEN(2) RDATA(4)
 * ---------------------------------------------------------------------- */
#define DNS_PORT  53

static const uint8_t CAPTIVE_IP[4] = { 192, 168, 4, 1 };

static TaskHandle_t s_dns_task = NULL;
static volatile bool s_dns_running = false;

static void dns_server_task(void *param)
{
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) {
        ESP_LOGE(TAG, "DNS socket failed");
        vTaskDelete(NULL);
        return;
    }

    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(DNS_PORT),
        .sin_addr   = { .s_addr = htonl(INADDR_ANY) },
    };
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "DNS bind failed (errno %d)", errno);
        close(fd);
        vTaskDelete(NULL);
        return;
    }

    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ESP_LOGI(TAG, "DNS redirect server listening on port %d", DNS_PORT);

    uint8_t req[512], resp[512];
    struct sockaddr_in client;
    socklen_t clen;

    while (s_dns_running) {
        clen = sizeof(client);
        int rlen = recvfrom(fd, req, sizeof(req), 0,
                            (struct sockaddr *)&client, &clen);
        if (rlen <= 0) continue;
        if (rlen < 12) continue;  /* need at least a DNS header */

        /* Reject non-standard queries and responses (QR bit set) */
        if (req[2] & 0x80) continue;

        /* Copy the entire received packet into response buffer */
        int resp_len = rlen;
        if (resp_len > (int)sizeof(resp) - 16) resp_len = (int)sizeof(resp) - 16;
        memcpy(resp, req, resp_len);

        /* Modify header to make it a response:
         *   QR=1, AA=1, RA=0, rcode=0, ANCOUNT=1 */
        resp[2] = 0x81;  /* QR=1, Opcode=0, AA=1, TC=0, RD=1 */
        resp[3] = 0x80;  /* RA=1, Z=0, rcode=0             */
        resp[6] = 0x00;  /* ANCOUNT high                    */
        resp[7] = 0x01;  /* ANCOUNT low = 1                 */
        resp[8] = 0x00;  /* NSCOUNT = 0                     */
        resp[9] = 0x00;
        resp[10] = 0x00; /* ARCOUNT = 0                     */
        resp[11] = 0x00;

        /* Append an A record answer pointing to CAPTIVE_IP.
         *
         *   NAME: pointer (0xC0 0x0C → offset 12, the question name)
         *   TYPE: A (0x0001)
         *   CLASS: IN (0x0001)
         *   TTL:  60 seconds
         *   RDLEN: 4
         *   RDATA: 192.168.4.1
         */
        int ap = resp_len;
        if (ap + 16 <= (int)sizeof(resp)) {
            resp[ap++] = 0xC0; resp[ap++] = 0x0C;  /* name ptr  */
            resp[ap++] = 0x00; resp[ap++] = 0x01;  /* TYPE A    */
            resp[ap++] = 0x00; resp[ap++] = 0x01;  /* CLASS IN  */
            resp[ap++] = 0x00; resp[ap++] = 0x00;  /* TTL (hi)  */
            resp[ap++] = 0x00; resp[ap++] = 0x3C;  /* TTL = 60s */
            resp[ap++] = 0x00; resp[ap++] = 0x04;  /* RDLEN = 4 */
            resp[ap++] = CAPTIVE_IP[0];
            resp[ap++] = CAPTIVE_IP[1];
            resp[ap++] = CAPTIVE_IP[2];
            resp[ap++] = CAPTIVE_IP[3];

            sendto(fd, resp, ap, 0,
                   (struct sockaddr *)&client, clen);
        }
    }

    close(fd);
    ESP_LOGI(TAG, "DNS server stopped");
    vTaskDelete(NULL);
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */
int esp32_captive_portal_start(void)
{
    /* Start DNS redirect task */
    s_dns_running = true;
    BaseType_t ret = xTaskCreatePinnedToCore(
        dns_server_task, "captive_dns",
        4096, NULL,
        WIFI_TASK_PRIORITY, &s_dns_task,
        WIFI_TASK_CORE);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create DNS task");
        s_dns_running = false;
        return -1;
    }

    /* Start HTTP server */
    if (start_http_server() != 0) {
        s_dns_running = false;
        return -1;
    }

    ESP_LOGI(TAG, "Captive portal active — connect to 'Synth-Dexed-Setup' WLAN");
    return 0;
}

void esp32_captive_portal_stop(void)
{
    s_dns_running = false;

    if (s_httpd) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
    }

    vTaskDelay(pdMS_TO_TICKS(1200));
    s_dns_task = NULL;
}
