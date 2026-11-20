#pragma once

/*
 * Host <-> ESP32 link protocol (shared by firmware and host).
 *
 * Transport: I2C, ESP32 is a slave at BF_I2C_SLAVE_ADDR.
 *
 * Host -> ESP32 (write transaction):
 *     [cmd][len][payload x len]           len <= BF_MAX_PAYLOAD
 *
 * ESP32 -> Host (read transaction, issued after the write):
 *     [status][len][payload x len]        len <= BF_MAX_PAYLOAD
 *
 * Host waits ~5 ms after a write before reading the response.
 *
 * Events (playback keys, connection changes) are queued on the ESP32.
 * The IRQ line (BF_PIN_IRQ) is pulled low while events are pending.
 * The host reads them with BF_CMD_GET_EVENT; the response payload is
 * [event_id][event data...], or [BF_EVT_NONE] if the queue is empty.
 */

#include <stdint.h>

#define BF_PROTO_VERSION     1
#define BF_I2C_SLAVE_ADDR    0x42
#define BF_MAX_PAYLOAD       250
#define BF_ADDR_LEN          6    /* Bluetooth device address length */

#define BF_PIN_SDA           21   /* I2C SDA (J3 header) */
#define BF_PIN_SCL           22   /* I2C SCL (J3 header) */
#define BF_PIN_IRQ           33   /* event IRQ to host, active low (J2 header) */

/* Commands (host -> ESP32) */
typedef enum {
    BF_CMD_PING            = 0x01, /* resp: [proto_version] */
    BF_CMD_GET_STATUS      = 0x02, /* resp: [conn_state][has_conn][addr6][play_state][scanning] */
    BF_CMD_LIST_PAIRED     = 0x03, /* resp: [addr6]... */
    BF_CMD_START_SCAN      = 0x04, /* req:  [seconds] */
    BF_CMD_GET_SCAN_RESULTS = 0x05, /* resp: [count]([addr6][name_len][name])... */
    BF_CMD_PAIR_CONNECT    = 0x06, /* req:  [addr6]  connect; SSP pairing runs if not bonded */
    BF_CMD_CONNECT         = 0x07, /* req:  [addr6]  connect to an already bonded device */
    BF_CMD_DISCONNECT      = 0x08, /* req:  none */
    BF_CMD_FORGET          = 0x09, /* req:  [addr6]  remove bond */
    BF_CMD_SET_METADATA    = 0x0A, /* req:  [field][utf8 bytes...] (see bf_meta_field_t) */
    BF_CMD_SET_PLAYBACK    = 0x0B, /* req:  [bf_play_t] */
    BF_CMD_GET_EVENT       = 0x0C, /* resp: [event_id][data...] */
} bf_cmd_t;

/* Events (ESP32 -> host, read via BF_CMD_GET_EVENT) */
typedef enum {
    BF_EVT_NONE        = 0x00, /* nothing pending */
    BF_EVT_CONN_STATE  = 0x80, /* data: [conn_state][addr6] */
    BF_EVT_PASSTHROUGH = 0x81, /* data: [avrcp_key_code][key_state] (0 pressed, 1 released) */
    BF_EVT_SCAN_DONE   = 0x82, /* data: none */
} bf_evt_t;

/* Status byte of a response frame */
typedef enum {
    BF_STATUS_OK      = 0x00,
    BF_STATUS_BAD_LEN = 0x01,
    BF_STATUS_UNKNOWN = 0x02,
    BF_STATUS_FAILED  = 0x03,
} bf_status_t;

/* Connection state; values match esp_a2d_connection_state_t */
typedef enum {
    BF_CONN_DISCONNECTED  = 0,
    BF_CONN_CONNECTING    = 1,
    BF_CONN_CONNECTED     = 2,
    BF_CONN_DISCONNECTING = 3,
} bf_conn_t;

/* Playback state; values match esp_avrc_playback_stat_t */
typedef enum {
    BF_PLAY_STOPPED = 0,
    BF_PLAY_PLAYING = 1,
    BF_PLAY_PAUSED  = 2,
} bf_play_t;

/* Metadata fields for BF_CMD_SET_METADATA */
typedef enum {
    BF_META_TITLE       = 0x01, /* utf8 */
    BF_META_ARTIST      = 0x02, /* utf8 */
    BF_META_ALBUM       = 0x03, /* utf8 */
    BF_META_DURATION_MS = 0x04, /* uint32, little endian */
} bf_meta_field_t;

/* AVRCP passthrough keys forwarded as BF_EVT_PASSTHROUGH (match ESP_AVRC_PT_CMD_*) */
#define BF_KEY_PLAY     0x44
#define BF_KEY_PAUSE    0x46
#define BF_KEY_STOP     0x45
#define BF_KEY_NEXT     0x4B
#define BF_KEY_PREV     0x4C
#define BF_KEY_VOL_UP   0x41
#define BF_KEY_VOL_DOWN 0x42
