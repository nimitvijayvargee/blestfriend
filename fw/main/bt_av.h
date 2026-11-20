#pragma once

#include <stdint.h>
#include "esp_err.h"

/*
 * Bluetooth Classic: A2DP source (streams PCM from i2s_audio to a speaker or
 * headphones), AVRCP target (receives play/pause/skip keys from the remote and
 * reports playback status), and GAP (discovery, pairing, bonding list).
 *
 * All functions here are called from the I2C command task. The BT callbacks
 * report back through i2c_link_post_event().
 */

esp_err_t bt_av_init(void);

/* Scan for nearby devices for about `seconds` seconds. Results via bt_av_get_scan_results. */
esp_err_t bt_av_start_scan(uint8_t seconds);

/* Writes [count]([addr6][name_len][name])... into out. Returns bytes written. */
uint8_t bt_av_get_scan_results(uint8_t *out, uint8_t max);

/* Writes [addr6]... for every bonded device. Returns bytes written. */
uint8_t bt_av_list_paired(uint8_t *out, uint8_t max);

/* Connect A2DP to addr6. If the device is not bonded, SSP pairing runs automatically. */
esp_err_t bt_av_connect(const uint8_t *addr6);
esp_err_t bt_av_disconnect(void);
esp_err_t bt_av_forget(const uint8_t *addr6);

/* Writes [conn_state][has_conn][addr6][play_state][scanning]. Returns bytes written (10). */
uint8_t bt_av_get_status(uint8_t *out);

/* Stores metadata for the current track. See bf_meta_field_t. */
esp_err_t bt_av_set_metadata(uint8_t field, const uint8_t *data, uint8_t len);

/* Updates play status and sends an AVRCP notification if the remote subscribed. */
esp_err_t bt_av_set_playback(uint8_t play_state);
