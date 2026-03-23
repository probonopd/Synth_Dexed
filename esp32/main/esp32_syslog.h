/*
 * UDP syslog redirector for ESP32.
 *
 * After calling esp32_syslog_start(), all ESP_LOGx() output is sent as
 * UDP datagrams to the configured broadcast address in addition to UART.
 *
 * Receive on the host:
 *   nc -lup 5140          (Linux/macOS)
 *   or any syslog viewer  (RFC 3164 headers)
 */

#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start the UDP syslog forwarder.
 *
 * @param dest_ip  Destination IP string, e.g. "192.168.1.255" (broadcast)
 *                 or a specific host IP. Use "255.255.255.255" for global
 *                 broadcast (works on most home networks).
 * @param port     Destination UDP port (default 5140 to avoid root privilege).
 *
 * Must be called after the network interface is up (after WiFi connect).
 * Safe to call multiple times; subsequent calls are ignored.
 */
void esp32_syslog_start(const char *dest_ip, uint16_t port);

#ifdef __cplusplus
}
#endif
