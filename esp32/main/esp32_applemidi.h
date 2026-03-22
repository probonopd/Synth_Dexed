/*
 * FMRack ESP32-S3 Port – Apple MIDI (RTP-MIDI) over WLAN
 *
 * Implements the Apple MIDI / Network MIDI session protocol (RFC 6295) so
 * that macOS / iOS can connect to the synthesizer directly from Audio MIDI
 * Setup or GarageBand without any additional software.
 *
 * The implementation acts as a passive *responder*:
 *   – Accepts session invitations (IN) from a Mac on the standard RTP-MIDI
 *     control port (default 5004) and its adjacent data port (5005).
 *   – Performs the three-way clock-synchronisation handshake (CK0/CK1/CK2).
 *   – Decodes incoming RTP-MIDI packets and forwards MIDI messages to the
 *     Dexed engine through dexed_raw_handle_midi() / dexed_raw_handle_sysex().
 *   – Handles graceful teardown (BY).
 *
 * mDNS / Zeroconf advertisement:
 *   – Announces a _apple-midi._udp service so that the synthesizer appears
 *     automatically in Audio MIDI Setup → Network on the Mac.
 *   – The ESP-IDF mDNS component performs Zeroconf probing and handles name
 *     collisions automatically (appends a number when a conflict is found).
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise the Apple MIDI subsystem.
 * Must be called after esp32_wlan_init() has established a STA connection.
 * Creates the two UDP sockets (control + data) and registers the mDNS
 * service advertisement.
 *
 * @return 0 on success, -1 on failure.
 */
int esp32_applemidi_init(void);

/**
 * Start the Apple MIDI background task.
 * The task runs on Core 0 (protocol core) alongside MIDI/USB.
 *
 * @return 0 on success, -1 on failure.
 */
int esp32_applemidi_start(void);

/**
 * Stop the Apple MIDI task and close all sockets.
 * Sends a BY (goodbye) to any active peer before tearing down.
 */
void esp32_applemidi_stop(void);

/**
 * @return true if a session with a Mac peer is currently active.
 */
bool esp32_applemidi_is_connected(void);

#ifdef __cplusplus
}
#endif
