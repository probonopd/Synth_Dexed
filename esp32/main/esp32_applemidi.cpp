/*
 * FMRack ESP32-S3 Port – Apple MIDI (RTP-MIDI) implementation
 *
 * Protocol references:
 *   RFC 6295 – RTP Payload Format for MIDI
 *   Apple MIDI session protocol (de-facto standard, reverse-engineered)
 *
 * Two adjacent UDP ports are used:
 *   Control port (FMRACK_APPLEMIDI_PORT + 0):  session management
 *   Data    port (FMRACK_APPLEMIDI_PORT + 1):  RTP-MIDI payload
 *
 * The device acts as a passive responder only.  When a Mac opens
 * Audio MIDI Setup → Network and clicks Connect, it sends an IN
 * packet; this code answers OK, then on the data port responds to
 * a second IN, performs CK0→CK1 clock sync, and from that point
 * decodes arriving RTP-MIDI frames into MIDI events that are fed to
 * dexed_raw_handle_midi().
 *
 * mDNS advertisement registers a _apple-midi._udp service so the
 * synth appears automatically in Audio MIDI Setup without the user
 * manually entering an IP address.  The ESP-IDF mDNS component
 * probes for the hostname and resolves any name collision (RFC 6762
 * §9) before announcing.
 */

#include "esp32_applemidi.h"
#include "esp32_config.h"
#include "dexed_raw.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mdns.h"

#include <string.h>
#include <stdint.h>
#include <sys/select.h>
#include <errno.h>

static const char *TAG = "applemidi";

/* -----------------------------------------------------------------------
 * Protocol magic / commands
 * ---------------------------------------------------------------------- */
#define AM_MAGIC        0xFFFFU
#define AM_VERSION      0x00000002UL

/* 16-bit command words (big-endian wire format) */
#define AM_CMD_IN       0x494EU  /* session invitation     */
#define AM_CMD_OK       0x4F4BU  /* invitation accepted    */
#define AM_CMD_NO       0x4E4FU  /* invitation rejected    */
#define AM_CMD_BY       0x4259U  /* end session (goodbye)  */
#define AM_CMD_CK       0x434BU  /* clock sync             */
#define AM_CMD_RS       0x5253U  /* reset feedback         */

#define RTP_MIDI_PT     97       /* RTP payload type for MIDI (0x61) */

/* -----------------------------------------------------------------------
 * Configuration defaults
 * ---------------------------------------------------------------------- */
#ifndef FMRACK_APPLEMIDI_PORT
#define FMRACK_APPLEMIDI_PORT   5004
#endif

#ifndef FMRACK_APPLEMIDI_NAME
#define FMRACK_APPLEMIDI_NAME   "FMSynthESP"
#endif

/* -----------------------------------------------------------------------
 * Helpers: big-endian field read/write (avoid alignment issues)
 * ---------------------------------------------------------------------- */
static inline uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}
static inline uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) | (uint32_t)p[3];
}
static inline uint64_t rd64(const uint8_t *p)
{
    return ((uint64_t)rd32(p) << 32) | rd32(p + 4);
}
static inline void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v;
}
static inline void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >>  8); p[3] = (uint8_t)v;
}
static inline void wr64(uint8_t *p, uint64_t v)
{
    wr32(p, (uint32_t)(v >> 32)); wr32(p + 4, (uint32_t)v);
}

/* 100-microsecond ticks from boot (matches Apple MIDI clock units) */
static inline uint64_t now_ticks(void)
{
    return (uint64_t)esp_timer_get_time() / 100ULL;
}

/* -----------------------------------------------------------------------
 * Session state
 * ---------------------------------------------------------------------- */
typedef enum {
    SESSION_IDLE = 0,
    SESSION_CTRL_INVITED,   /* control port OK sent, wait data port IN */
    SESSION_CONNECTED,
} session_state_t;

typedef struct {
    session_state_t state;

    /* Peer addresses */
    struct sockaddr_in ctrl_peer;   /* Mac's control-port address */
    struct sockaddr_in data_peer;   /* Mac's data-port address    */

    /* Session identifiers */
    uint32_t peer_ssrc;
    uint32_t peer_token;            /* token from first IN (must echo in OK) */
    char     peer_name[64];

    /* Our SSRC (random, chosen at init) */
    uint32_t our_ssrc;

    /* RTP receive state */
    uint16_t last_rtp_seq;
    bool     seq_initialised;

    /* SysEx accumulation */
    uint8_t  sysex_buf[4200];
    int      sysex_len;
    bool     in_sysex;

    /* MIDI running-status for the data stream */
    uint8_t  running_status;
} applemidi_session_t;

static applemidi_session_t s_session;
static int  s_fd_ctrl = -1;    /* control port socket */
static int  s_fd_data = -1;    /* data port socket    */
static TaskHandle_t s_task   = NULL;
static volatile bool s_running = false;

/* -----------------------------------------------------------------------
 * Build and send a session-management reply packet
 *
 * Wire layout:
 *   FF FF <cmd16> <version32> <token32> <ssrc32> [<name>\0]
 * ---------------------------------------------------------------------- */
static void send_session_reply(int fd, const struct sockaddr_in *dest,
                                uint16_t cmd,
                                uint32_t token, uint32_t ssrc,
                                const char *name)
{
    uint8_t pkt[128];
    int pos = 0;

    wr16(pkt + pos, AM_MAGIC);   pos += 2;
    wr16(pkt + pos, cmd);        pos += 2;
    wr32(pkt + pos, AM_VERSION); pos += 4;
    wr32(pkt + pos, token);      pos += 4;
    wr32(pkt + pos, ssrc);       pos += 4;

    if (name) {
        size_t nlen = strnlen(name, 64);
        memcpy(pkt + pos, name, nlen);
        pos += (int)nlen;
        pkt[pos++] = 0x00;  /* null-terminate */
    }

    sendto(fd, pkt, pos, 0,
           (const struct sockaddr *)dest, sizeof(*dest));
}

/* -----------------------------------------------------------------------
 * Build and send a clock-sync reply (CK1) in response to CK0
 * ---------------------------------------------------------------------- */
static void send_ck1(int fd, const struct sockaddr_in *dest,
                     uint32_t peer_ssrc, uint64_t ts1)
{
    uint8_t pkt[36];
    int pos = 0;

    wr16(pkt + pos, AM_MAGIC);   pos += 2;
    wr16(pkt + pos, AM_CMD_CK);  pos += 2;
    wr32(pkt + pos, s_session.our_ssrc); pos += 4;
    pkt[pos++] = 1;    /* count = 1 → CK1 */
    pkt[pos++] = 0; pkt[pos++] = 0; pkt[pos++] = 0;  /* 3-byte pad */
    wr64(pkt + pos, ts1);         pos += 8;  /* ts1: echo peer's ts1 */
    wr64(pkt + pos, now_ticks()); pos += 8;  /* ts2: our receive time */
    wr64(pkt + pos, 0ULL);        pos += 8;  /* ts3: reserved, must be 0 */

    sendto(fd, pkt, pos, 0,
           (const struct sockaddr *)dest, sizeof(*dest));
}

/* -----------------------------------------------------------------------
 * Parse a session-management command received on either port.
 * 'is_data_port' distinguishes which port the packet arrived on.
 * ---------------------------------------------------------------------- */
static void handle_session_packet(int fd, const struct sockaddr_in *peer,
                                  const uint8_t *buf, int len,
                                  bool is_data_port)
{
    if (len < 12) return;
    if (rd16(buf) != AM_MAGIC) return;

    uint16_t cmd     = rd16(buf + 2);
    /* uint32_t version = rd32(buf + 4); */
    uint32_t token   = rd32(buf + 8);
    uint32_t ssrc    = rd32(buf + 12) ; /* might not be present for BY */

    switch (cmd) {

    /* ---- Invitation ---- */
    case AM_CMD_IN: {
        if (len < 16) return;
        ssrc = rd32(buf + 12);

        /* Extract device name (null-terminated, after SSRC) */
        char name[64] = "Unknown";
        if (len > 16) {
            int nlen = len - 16;
            if (nlen >= 64) nlen = 63;
            memcpy(name, buf + 16, nlen);
            name[nlen] = '\0';
            /* strip trailing null if present */
            if (nlen > 0 && name[nlen - 1] == '\0') {}
        }

        if (!is_data_port) {
            /* First invitation on control port */
            if (s_session.state != SESSION_IDLE) {
                /* Already have a session – reject */
                send_session_reply(fd, peer, AM_CMD_NO, token, ssrc, NULL);
                ESP_LOGW(TAG, "Session already active, rejecting new IN from %s",
                         inet_ntoa(peer->sin_addr));
                return;
            }

            /* Accept */
            s_session.state      = SESSION_CTRL_INVITED;
            s_session.peer_ssrc  = ssrc;
            s_session.peer_token = token;
            memcpy(&s_session.ctrl_peer, peer, sizeof(*peer));
            strncpy(s_session.peer_name, name, sizeof(s_session.peer_name) - 1);

            send_session_reply(fd, peer, AM_CMD_OK, token,
                               s_session.our_ssrc, FMRACK_APPLEMIDI_NAME);
            ESP_LOGI(TAG, "Control invitation from '%s' – OK sent", name);

        } else {
            /* Second invitation on data port */
            if (s_session.state != SESSION_CTRL_INVITED) return;
            if (ssrc != s_session.peer_ssrc) return;

            memcpy(&s_session.data_peer, peer, sizeof(*peer));
            s_session.state = SESSION_CONNECTED;

            send_session_reply(fd, peer, AM_CMD_OK, token,
                               s_session.our_ssrc, FMRACK_APPLEMIDI_NAME);
            ESP_LOGI(TAG, "WLAN MIDI session established with '%s' (%s)",
                     s_session.peer_name, inet_ntoa(peer->sin_addr));
        }
        break;
    }

    /* ---- Goodbye ---- */
    case AM_CMD_BY: {
        if (s_session.state == SESSION_IDLE) return;
        /* Echo BY back */
        send_session_reply(fd, peer, AM_CMD_BY,
                           token, s_session.our_ssrc, NULL);
        ESP_LOGI(TAG, "Session ended by peer '%s'", s_session.peer_name);
        memset(&s_session, 0, sizeof(s_session));
        s_session.our_ssrc = rd32(buf + 12); /* re-init with new SSRC below */
        /* Re-generate SSRC */
        s_session.our_ssrc = (uint32_t)(esp_timer_get_time() & 0xFFFFFFFF);
        break;
    }

    /* ---- Clock sync ---- */
    case AM_CMD_CK: {
        if (s_session.state != SESSION_CONNECTED) return;
        if (len < 11) return;
        uint8_t count = buf[8];  /* byte right after SSRC (4 bytes from offset 4) */
        /* Layout: FF FF CK <ssrc32> <count8> <pad24> <ts1_64> [<ts2_64> [<ts3_64>]] */
        /* Reparse with corrected offsets:
         *   0-1: magic
         *   2-3: CK
         *   4-7: sender SSRC
         *   8:   count
         *   9-11: padding
         *   12-19: ts1
         *   20-27: ts2 (if count >= 1)
         *   28-35: ts3 (if count >= 2)
         */
        if (len < 20) return;
        /* Adjust reading: ssrc at buf+4, count at buf+8, ts1 at buf+12 */
        uint32_t ck_ssrc = rd32(buf + 4);
        count = buf[8];
        uint64_t ts1 = rd64(buf + 12);

        if (count == 0) {
            /* CK0: sender wants to sync; reply with CK1 */
            ESP_LOGD(TAG, "CK0 received (ssrc=0x%08lX ts1=%llu), sending CK1",
                     (unsigned long)ck_ssrc, (unsigned long long)ts1);
            send_ck1(fd, peer, ck_ssrc, ts1);
        } else {
            ESP_LOGD(TAG, "CK%d received (ssrc=0x%08lX)", count, (unsigned long)ck_ssrc);
        }
        break;
    }

    default:
        break;
    }
}

/* -----------------------------------------------------------------------
 * Decode an RTP-MIDI payload into individual MIDI messages and dispatch
 * them to the Dexed engine.
 *
 * RTP-MIDI payload layout (RFC 6295 §3):
 *   Command section header (1 or 2 bytes):
 *     B[7] J[6] Z[5] P[4] LEN[3:0]  (short header, B=0)
 *     B[7] J[6] Z[5] P[4] LEN_H[2:0] | LEN_L[7:0]  (long, B=1)
 *   MIDI list: sequence of (delta-time, MIDI bytes)
 *     Delta-time uses MIDI VLQ; first event may omit it if P flag set.
 *
 * We ignore journaling (J flag) as it's optional for live performance.
 * ---------------------------------------------------------------------- */
static void dispatch_rtp_midi(const uint8_t *rtp, int rtp_len)
{
    /* Skip RTP header (12 bytes fixed) */
    if (rtp_len < 13) return;  /* 12 header + at least 1 payload byte */

    const uint8_t *payload = rtp + 12;
    int plen = rtp_len - 12;
    int pos  = 0;

    /* Parse MIDI command section header */
    uint8_t h0 = payload[pos++];
    bool B = (h0 >> 7) & 1;
    bool P = (h0 >> 4) & 1;  /* P=1: first event has no delta time */
    bool Z = (h0 >> 5) & 1;  /* Z=1: empty MIDI section */

    int midi_len;
    if (!B) {
        midi_len = h0 & 0x0F;
    } else {
        if (pos >= plen) return;
        midi_len = ((h0 & 0x0F) << 8) | payload[pos++];
    }

    if (Z || midi_len == 0) return;
    if (pos + midi_len > plen) midi_len = plen - pos;

    ESP_LOGD(TAG, "RTP-MIDI: B=%d P=%d Z=%d midi_len=%d plen=%d", B, P, Z, midi_len, plen);

    const uint8_t *midi_list = payload + pos;
    int ml_pos = 0;
    bool first_event = true;

    while (ml_pos < midi_len) {
        /* Parse variable-length delta-time.
         * RFC 6295: P=1 → first event has no delta-time.
         * macOS quirk: sends P=0 for live keyboard events but still omits
         * the delta byte.  Detect this by peeking at the first byte: if it
         * looks like a MIDI status (bit 7 set, not a real-time 0xF8-0xFF),
         * skip delta parsing regardless of the P flag. */
        bool have_delta = !(first_event && P);   /* RFC 6295 default */
        if (have_delta && first_event && ml_pos < midi_len) {
            uint8_t peek = midi_list[ml_pos];
            if ((peek & 0x80) && peek < 0xF8) have_delta = false;
        }
        if (have_delta) {
            while (ml_pos < midi_len && (midi_list[ml_pos] & 0x80))
                ml_pos++;  /* consume VLQ continuation bytes */
            if (ml_pos < midi_len) ml_pos++;  /* consume final VLQ byte */
        }
        first_event = false;

        if (ml_pos >= midi_len) break;

        /* Parse MIDI message */
        uint8_t b = midi_list[ml_pos];

        /* SysEx */
        if (b == 0xF0) {
            int start = ml_pos;
            ml_pos++;
            while (ml_pos < midi_len && midi_list[ml_pos] != 0xF7)
                ml_pos++;
            if (ml_pos < midi_len) ml_pos++;  /* consume 0xF7 */
            int slen = ml_pos - start;
            if (slen >= 2) {
                uint8_t ch = 0;
                if (slen > 2 && midi_list[start + 1] == 0x43)
                    ch = (midi_list[start + 2] & 0x0F) + 1;
                dexed_raw_handle_sysex(&midi_list[start], slen, ch);
            }
            s_session.running_status = 0;
            continue;
        }

        /* Real-time single-byte message (0xF8-0xFF, except F0) */
        if (b >= 0xF8) {
            ml_pos++;
            continue;  /* ignore clock / active sensing etc. */
        }

        /* Handle running status: if no status byte, reuse last */
        uint8_t status;
        if (b & 0x80) {
            status = b;
            s_session.running_status = (b < 0xF0) ? b : 0;
            ml_pos++;
        } else {
            status = s_session.running_status;
            if (!status) { ml_pos++; continue; }  /* no valid status, skip */
        }

        /* Determine data byte count */
        uint8_t high_nibble = status & 0xF0;
        int bytes_needed;
        if (high_nibble == 0xC0 || high_nibble == 0xD0) {
            bytes_needed = 1;
        } else if (status >= 0xF0) {
            bytes_needed = 0;  /* single-byte system messages */
        } else {
            bytes_needed = 2;
        }

        uint8_t d1 = 0, d2 = 0;
        if (bytes_needed >= 1 && ml_pos < midi_len) d1 = midi_list[ml_pos++];
        if (bytes_needed >= 2 && ml_pos < midi_len) d2 = midi_list[ml_pos++];

        ESP_LOGD(TAG, "RTP-MIDI dispatch: status=0x%02X d1=0x%02X d2=0x%02X", status, d1, d2);
        dexed_raw_handle_midi(status, d1, d2);
    }
}

/* -----------------------------------------------------------------------
 * Main Apple MIDI task: select() on both sockets and dispatch
 * ---------------------------------------------------------------------- */
static void applemidi_task(void *param)
{
    uint8_t buf[1500];
    struct sockaddr_in peer;
    socklen_t peer_len;

    ESP_LOGI(TAG, "Apple MIDI task started on ports %d/%d",
             FMRACK_APPLEMIDI_PORT, FMRACK_APPLEMIDI_PORT + 1);

    while (s_running) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(s_fd_ctrl, &fds);
        FD_SET(s_fd_data, &fds);
        int maxfd = (s_fd_ctrl > s_fd_data) ? s_fd_ctrl : s_fd_data;

        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        int r = select(maxfd + 1, &fds, NULL, NULL, &tv);
        if (r <= 0) continue;

        /* Control port */
        if (FD_ISSET(s_fd_ctrl, &fds)) {
            peer_len = sizeof(peer);
            int len = recvfrom(s_fd_ctrl, buf, sizeof(buf), 0,
                               (struct sockaddr *)&peer, &peer_len);
            if (len > 0)
                handle_session_packet(s_fd_ctrl, &peer, buf, len, false);
        }

        /* Data port */
        if (FD_ISSET(s_fd_data, &fds)) {
            peer_len = sizeof(peer);
            int len = recvfrom(s_fd_data, buf, sizeof(buf), 0,
                               (struct sockaddr *)&peer, &peer_len);
            if (len <= 0) continue;

            /* Is this a session management packet (magic FF FF)? */
            if (len >= 2 && rd16(buf) == AM_MAGIC) {
                handle_session_packet(s_fd_data, &peer, buf, len, true);
            } else if (s_session.state == SESSION_CONNECTED) {
                /* RTP-MIDI data: basic validation */
                if (len >= 12) {
                    uint8_t pt = buf[1] & 0x7F;  /* strip marker bit */
                    if (pt == RTP_MIDI_PT) {
                        dispatch_rtp_midi(buf, len);
                    } else {
                        ESP_LOGW(TAG, "Unexpected RTP payload type %d (want %d)",
                                 pt, RTP_MIDI_PT);
                    }
                }
            } else {
                ESP_LOGD(TAG, "Data packet ignored: state=%d len=%d", s_session.state, len);
            }
        }
    }

    ESP_LOGI(TAG, "Apple MIDI task exiting");
    vTaskDelete(NULL);
}

/* -----------------------------------------------------------------------
 * mDNS advertisement
 * ---------------------------------------------------------------------- */
static void register_mdns(void)
{
    esp_err_t err = mdns_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "mdns_init failed: %s", esp_err_to_name(err));
        return;
    }

    /* Hostname: "synth-dexed" (lowercase, no spaces).
     * The mDNS library probes for hostname conflicts per RFC 6762 §9 and
     * automatically appends "-2", "-3", … if a collision is detected on
     * the local network before announcing. */
    mdns_hostname_set("synth-dexed");
    mdns_instance_name_set(FMRACK_APPLEMIDI_NAME);

    /* Advertise the Apple MIDI (RTP-MIDI) service.
     * macOS Audio MIDI Setup discovers _apple-midi._udp on the local net. */
    err = mdns_service_add(FMRACK_APPLEMIDI_NAME,
                           "_apple-midi", "_udp",
                           FMRACK_APPLEMIDI_PORT, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mdns_service_add: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "mDNS: '%s' _apple-midi._udp port %d announced",
                 FMRACK_APPLEMIDI_NAME, FMRACK_APPLEMIDI_PORT);
    }
}

/* -----------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */
static int open_udp_port(int port)
{
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) return -1;

    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port   = htons((uint16_t)port),
        .sin_addr   = { .s_addr = htonl(INADDR_ANY) },
    };
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int esp32_applemidi_init(void)
{
    /* Generate a random-ish SSRC from the timer */
    memset(&s_session, 0, sizeof(s_session));
    s_session.our_ssrc = (uint32_t)(esp_timer_get_time() & 0xFFFFFFFF);

    s_fd_ctrl = open_udp_port(FMRACK_APPLEMIDI_PORT);
    if (s_fd_ctrl < 0) {
        ESP_LOGE(TAG, "Failed to open control port %d", FMRACK_APPLEMIDI_PORT);
        return -1;
    }

    s_fd_data = open_udp_port(FMRACK_APPLEMIDI_PORT + 1);
    if (s_fd_data < 0) {
        ESP_LOGE(TAG, "Failed to open data port %d", FMRACK_APPLEMIDI_PORT + 1);
        close(s_fd_ctrl);
        s_fd_ctrl = -1;
        return -1;
    }

    register_mdns();

    ESP_LOGI(TAG, "Apple MIDI initialised (SSRC=0x%08lX name='%s')",
             (unsigned long)s_session.our_ssrc, FMRACK_APPLEMIDI_NAME);
    return 0;
}

int esp32_applemidi_start(void)
{
    if (s_fd_ctrl < 0 || s_fd_data < 0) return -1;
    s_running = true;

    BaseType_t ret = xTaskCreatePinnedToCore(
        applemidi_task, "applemidi",
        APPLEMIDI_TASK_STACK_SIZE, NULL,
        APPLEMIDI_TASK_PRIORITY, &s_task,
        WIFI_TASK_CORE);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Apple MIDI task");
        s_running = false;
        return -1;
    }
    return 0;
}

void esp32_applemidi_stop(void)
{
    s_running = false;

    /* Graceful goodbye to peer */
    if (s_session.state == SESSION_CONNECTED) {
        send_session_reply(s_fd_data, &s_session.data_peer,
                           AM_CMD_BY, s_session.peer_token,
                           s_session.our_ssrc, NULL);
    }

    /* Allow task to exit */
    vTaskDelay(pdMS_TO_TICKS(1200));
    s_task = NULL;

    if (s_fd_ctrl >= 0) { close(s_fd_ctrl); s_fd_ctrl = -1; }
    if (s_fd_data >= 0) { close(s_fd_data); s_fd_data = -1; }

    mdns_free();

    memset(&s_session, 0, sizeof(s_session));
    ESP_LOGI(TAG, "Apple MIDI stopped");
}

bool esp32_applemidi_is_connected(void)
{
    return s_session.state == SESSION_CONNECTED;
}
