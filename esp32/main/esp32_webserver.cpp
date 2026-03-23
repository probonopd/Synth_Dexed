/*
 * FMRack ESP32-S3 Port – SFO Upload Web Server
 *
 * HTTP server that serves a status/upload page and accepts SFO file uploads.
 * The uploaded file is written to SPIFFS as a temp file, then renamed and
 * the TSF engine is reloaded.
 *
 * mDNS: registers _http._tcp so the page is discoverable as
 *   http://synth-dexed.local/
 */

#include "esp32_webserver.h"
#include "esp32_config.h"
#include "tsf_engine.h"

#include "esp_http_server.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "webserver";

#define WEBSERVER_PORT      80
#define UPLOAD_BUF_SIZE     4096
#define SFO_PATH            TSF_DEFAULT_SFO_PATH
#define SFO_TMP_PATH        TSF_DEFAULT_SFO_PATH ".tmp"
#define MAX_SFO_SIZE        (4 * 1024 * 1024)  /* 4 MB limit */

static httpd_handle_t s_server = NULL;

/* -------------------------------------------------------------------------
 * HTML pages
 * ---------------------------------------------------------------------- */

static const char STATUS_HTML_HEAD[] =
    "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>FMSynthESP – SoundFont Manager</title>"
    "<style>"
    "body{font-family:system-ui,sans-serif;background:#1a1a2e;color:#eee;"
         "display:flex;justify-content:center;align-items:center;"
         "min-height:100vh;margin:0;}"
    ".card{background:#16213e;border-radius:12px;padding:2rem;max-width:420px;"
          "width:90%;box-shadow:0 8px 32px rgba(0,0,0,.5);}"
    "h1{margin:0 0 .25rem;font-size:1.4rem;color:#e94560;}"
    "p{margin:.5rem 0;font-size:.85rem;color:#aaa;}"
    ".info{background:#0f3460;border-radius:6px;padding:.75rem 1rem;margin:1rem 0;}"
    ".info span{color:#4caf50;font-weight:600;}"
    "label{display:block;margin:1rem 0 .25rem;font-size:.85rem;color:#ccc;}"
    "input[type=file]{width:100%;box-sizing:border-box;padding:.5rem;"
    "  border:1px solid #334;border-radius:6px;background:#0f3460;color:#eee;}"
    "button{margin-top:1rem;width:100%;padding:.75rem;"
    "  background:#e94560;color:#fff;border:none;border-radius:6px;"
    "  font-size:1rem;cursor:pointer;}"
    "button:hover{background:#c73652;}"
    "#prog{display:none;margin-top:.5rem;font-size:.85rem;color:#4caf50;}"
    "</style></head><body>"
    "<div class='card'>"
    "<h1>FMSynthESP</h1>"
    "<p>SoundFont (SFO) file manager for the drum engine.</p>"
    "<div class='info'>";

static const char STATUS_HTML_TAIL[] =
    "</div>"
    "<form method='POST' action='/upload' enctype='multipart/form-data'"
    "  onsubmit=\"document.getElementById('prog').style.display='block';"
    "  this.querySelector('button').disabled=true;"
    "  this.querySelector('button').textContent='Uploading...'\">"
    "<label>Upload new SFO file</label>"
    "<input type='file' name='file' accept='.sfo,.sf2'>"
    "<button type='submit'>Upload &amp; Reload</button>"
    "<div id='prog'>Uploading... this may take 30-60 seconds for large files.</div>"
    "</form>"
    "</div></body></html>";

static const char UPLOAD_OK_HTML[] =
    "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<meta http-equiv='refresh' content='3;url=/'>"
    "<title>FMSynthESP – Upload Complete</title>"
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
    "<h2>Upload complete</h2>"
    "<p>The drum engine has been reloaded with the new SoundFont.<br>"
    "Redirecting...</p>"
    "</div></body></html>";

static const char UPLOAD_FAIL_HTML[] =
    "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<meta http-equiv='refresh' content='5;url=/'>"
    "<title>FMSynthESP – Upload Failed</title>"
    "<style>"
    "body{font-family:system-ui,sans-serif;background:#1a1a2e;color:#eee;"
         "display:flex;justify-content:center;align-items:center;"
         "min-height:100vh;margin:0;text-align:center;}"
    ".card{background:#16213e;border-radius:12px;padding:2rem;max-width:360px;"
          "width:90%;}"
    "h1{color:#e94560;font-size:2rem;margin:0 0 .5rem;}"
    "p{color:#aaa;}"
    "</style></head><body>"
    "<div class='card'>"
    "<h1>&#10007;</h1>"
    "<h2>Upload failed</h2>"
    "<p>Could not write the file to flash storage. "
    "Check that the file is a valid SFO and under 4 MB.<br>"
    "Redirecting...</p>"
    "</div></body></html>";

/* -------------------------------------------------------------------------
 * HTTP handlers
 * ---------------------------------------------------------------------- */

static esp_err_t handler_status(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");

    /* Build dynamic status info */
    char info[256];
    bool loaded = tsf_engine_is_loaded();

    struct stat st;
    long sfo_size = 0;
    if (stat(SFO_PATH, &st) == 0) {
        sfo_size = st.st_size;
    }

    snprintf(info, sizeof(info),
             "<span>Drum engine:</span> %s<br>"
             "<span>SFO file:</span> %s (%ld bytes)<br>"
             "<span>Active voices:</span> %d",
             loaded ? "Loaded" : "Not loaded",
             sfo_size > 0 ? "Present" : "Not found",
             sfo_size,
             tsf_engine_active_voices());

    /* Send response in chunks */
    httpd_resp_send_chunk(req, STATUS_HTML_HEAD, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, info, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, STATUS_HTML_TAIL, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, NULL, 0);  /* finish chunked response */

    return ESP_OK;
}

/* Skip past multipart headers to find the start of file data.
 * Returns pointer to first byte after "\r\n\r\n", or NULL if not found. */
static const char *find_file_data_start(const char *buf, int len)
{
    for (int i = 0; i < len - 3; i++) {
        if (buf[i] == '\r' && buf[i+1] == '\n' &&
            buf[i+2] == '\r' && buf[i+3] == '\n') {
            return &buf[i + 4];
        }
    }
    return NULL;
}

/* Find the multipart boundary at the end of the upload.
 * The boundary starts with "\r\n--".  We strip trailing boundary bytes. */
static int strip_trailing_boundary(const char *buf, int len, const char *boundary, int bnd_len)
{
    /* Search backward for "\r\n--" + boundary.  The final boundary also has "--" appended.
     * Total trailer: "\r\n--" + boundary + "--\r\n" = 4 + bnd_len + 4 bytes.
     * Sometimes there's no final "\r\n". Be conservative. */
    int trailer_max = bnd_len + 10;
    if (trailer_max > len) trailer_max = len;
    for (int i = len - trailer_max; i < len - 3; i++) {
        if (i >= 0 && buf[i] == '\r' && buf[i+1] == '\n' &&
            buf[i+2] == '-' && buf[i+3] == '-') {
            return i;  /* file data ends here */
        }
    }
    return len;  /* no boundary found (shouldn't happen) */
}

static esp_err_t handler_upload(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Upload request: content_len=%d", req->content_len);

    if (req->content_len <= 0 || req->content_len > MAX_SFO_SIZE) {
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, UPLOAD_FAIL_HTML, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    /* Extract multipart boundary from Content-Type header.
     * Format: "multipart/form-data; boundary=----WebKitFormBoundaryXXX" */
    char content_type[128] = {0};
    if (httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type)) != ESP_OK) {
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, UPLOAD_FAIL_HTML, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    char *boundary_start = strstr(content_type, "boundary=");
    char boundary[80] = {0};
    int bnd_len = 0;
    if (boundary_start) {
        boundary_start += 9;  /* skip "boundary=" */
        strncpy(boundary, boundary_start, sizeof(boundary) - 1);
        bnd_len = (int)strlen(boundary);
    }

    char *buf = (char *)malloc(UPLOAD_BUF_SIZE);
    if (!buf) {
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, UPLOAD_FAIL_HTML, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    FILE *f = fopen(SFO_TMP_PATH, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s for writing", SFO_TMP_PATH);
        free(buf);
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, UPLOAD_FAIL_HTML, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    int remaining = req->content_len;
    int total_written = 0;
    bool header_skipped = false;
    bool success = true;

    while (remaining > 0) {
        int to_read = remaining;
        if (to_read > UPLOAD_BUF_SIZE) to_read = UPLOAD_BUF_SIZE;

        int received = httpd_req_recv(req, buf, (size_t)to_read);
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;  /* retry on timeout */
            }
            ESP_LOGE(TAG, "Upload recv error: %d", received);
            success = false;
            break;
        }
        remaining -= received;

        const char *data = buf;
        int data_len = received;

        /* Skip multipart headers in the first chunk */
        if (!header_skipped) {
            const char *file_start = find_file_data_start(buf, received);
            if (!file_start) {
                /* Headers span multiple chunks (unlikely).  Skip this chunk. */
                continue;
            }
            data = file_start;
            data_len = received - (int)(file_start - buf);
            header_skipped = true;
        }

        /* If this is the last chunk, strip the trailing multipart boundary */
        if (remaining == 0 && bnd_len > 0) {
            data_len = strip_trailing_boundary(data, data_len, boundary, bnd_len);
        }

        if (data_len > 0) {
            size_t written = fwrite(data, 1, (size_t)data_len, f);
            if ((int)written != data_len) {
                ESP_LOGE(TAG, "SPIFFS write error (wrote %d of %d)", (int)written, data_len);
                success = false;
                break;
            }
            total_written += data_len;
        }
    }

    fclose(f);
    free(buf);

    if (success && total_written > 0) {
        /* Rename temp file to final path */
        remove(SFO_PATH);
        if (rename(SFO_TMP_PATH, SFO_PATH) != 0) {
            ESP_LOGE(TAG, "Failed to rename %s → %s", SFO_TMP_PATH, SFO_PATH);
            remove(SFO_TMP_PATH);
            success = false;
        }
    } else {
        remove(SFO_TMP_PATH);
    }

    if (success) {
        ESP_LOGI(TAG, "SFO upload complete: %d bytes written", total_written);

        /* Reload TSF engine with new SFO */
        int rc = tsf_engine_reload();
        if (rc != 0) {
            ESP_LOGW(TAG, "TSF reload failed after upload");
        }

        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, UPLOAD_OK_HTML, HTTPD_RESP_USE_STRLEN);
    } else {
        ESP_LOGE(TAG, "SFO upload failed");
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, UPLOAD_FAIL_HTML, HTTPD_RESP_USE_STRLEN);
    }

    return ESP_OK;
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

int esp32_webserver_start(void)
{
    if (s_server) {
        ESP_LOGW(TAG, "Web server already running");
        return 0;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = WEBSERVER_PORT;
    config.stack_size = 8192;
    config.max_uri_handlers = 4;
    config.lru_purge_enable = true;

    /* Allow receiving large uploads (up to MAX_SFO_SIZE + multipart overhead) */
    config.max_resp_headers = 8;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return -1;
    }

    /* Register URI handlers */
    const httpd_uri_t uri_status = {
        .uri      = "/",
        .method   = HTTP_GET,
        .handler  = handler_status,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_server, &uri_status);

    const httpd_uri_t uri_upload = {
        .uri      = "/upload",
        .method   = HTTP_POST,
        .handler  = handler_upload,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_server, &uri_upload);

    ESP_LOGI(TAG, "Web server started on port %d", WEBSERVER_PORT);
    return 0;
}

void esp32_webserver_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
        ESP_LOGI(TAG, "Web server stopped");
    }
}
