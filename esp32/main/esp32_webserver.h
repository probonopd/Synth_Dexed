/*
 * FMRack ESP32-S3 Port – SFO Upload Web Server
 *
 * Lightweight HTTP server for uploading SoundFont (SFO) files to SPIFFS.
 * Runs in STA mode alongside Apple MIDI.  Registers an _http._tcp mDNS
 * service so the upload page is discoverable via zeroconf.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start the HTTP server for SFO file uploads.
 * Registers an _http._tcp mDNS service for zeroconf discovery.
 * Call after Wi-Fi STA is connected and mDNS is initialized.
 * @return 0 on success, -1 on failure.
 */
int esp32_webserver_start(void);

/**
 * Stop the HTTP server and unregister the mDNS service.
 */
void esp32_webserver_stop(void);

#ifdef __cplusplus
}
#endif
