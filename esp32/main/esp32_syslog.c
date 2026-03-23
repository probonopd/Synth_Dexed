/*
 * UDP syslog redirector for ESP32.
 *
 * Hooks into esp_log_set_vprintf() so that all ESP_LOGx() output is
 * forwarded as raw UDP datagrams to a configurable broadcast/unicast
 * address while still printing to UART.
 *
 * Usage: call esp32_syslog_start() once after WiFi is connected (STA mode).
 * Receive: nc -lup 5140
 */

#include "esp32_syslog.h"
#include "esp_log.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"

#include <string.h>
#include <stdarg.h>
#include <stdio.h>

static int               s_sock = -1;
static struct sockaddr_in s_dest;

static int syslog_vprintf(const char *fmt, va_list args)
{
    /* Forward to UART first (standard path) */
    va_list args2;
    va_copy(args2, args);
    int ret = vprintf(fmt, args);

    /* Send over UDP */
    if (s_sock >= 0) {
        char buf[512];
        int len = vsnprintf(buf, sizeof(buf), fmt, args2);
        if (len > 0) {
            sendto(s_sock, buf, (size_t)len, 0,
                   (struct sockaddr *)&s_dest, sizeof(s_dest));
        }
    }
    va_end(args2);
    return ret;
}

void esp32_syslog_start(const char *dest_ip, uint16_t port)
{
    if (s_sock >= 0) return;   /* idempotent */

    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_sock < 0) return;

    int bcast = 1;
    setsockopt(s_sock, SOL_SOCKET, SO_BROADCAST, &bcast, sizeof(bcast));

    memset(&s_dest, 0, sizeof(s_dest));
    s_dest.sin_family      = AF_INET;
    s_dest.sin_port        = htons(port);
    inet_aton(dest_ip, &s_dest.sin_addr);

    esp_log_set_vprintf(syslog_vprintf);
}
