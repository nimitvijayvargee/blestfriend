# blestfriend firmware

ESP-IDF firmware for the blestfriend devboard (ESP32-WROOM-32E + CH340C).

The board acts as a **Bluetooth Classic A2DP source** (streams audio to a speaker or headphones) with an **AVRCP target** (receives playback keys from the remote). A host MCU drives it over:

- **I2C** (ESP32 is the slave): discovery, pairing and connection commands, track metadata and playback status in; device/connection status, and playback key events out.
- **I2S** (ESP32 is the slave receiver): PCM audio in, streamed over A2DP. I2S is independent of playback state.

## Pin map

| Function | Signal | ESP32 pin | Header |
|---|---|---|---|
| I2C slave | SDA | IO21 | J3 |
| I2C slave | SCL | IO22 | J3 |
| I2S slave | BCLK (in) | IO25 | J2 |
| I2S slave | WS / LRCK (in) | IO26 | J2 |
| I2S slave | DIN (in) | IO34 (input only) | J2 |
| Event IRQ to host | active low | IO33 | J2 |

Pins come from the 15x2 headers in the schematic. Do not use IO4, IO5, IO12–IO16, IO19 or IO23 for these functions. The schematic uses those for other things, or they are strapping pins.

## Layout

```
fw/
  CMakeLists.txt
  sdkconfig.defaults
  main/
    main.c          boot order: NVS -> I2C -> I2S -> Bluetooth
    protocol.h      shared command, event and status IDs (keep in sync with the host)
    i2c_link.[ch]   I2C slave, frame parser, command dispatch, event queue + IRQ
    bt_av.[ch]      GAP (scan, pair, bonds), A2DP source, AVRCP target, metadata
    i2s_audio.[ch]  I2S RX task -> ring buffer -> A2DP data callback
```

## I2C protocol

Host writes `[cmd][len][payload]`. Then, after about 5 ms, it reads `[status][len][payload]`.

| Cmd | Name | Request payload | Response payload |
|---|---|---|---|
| 0x01 | PING | - | `[proto_version]` |
| 0x02 | GET_STATUS | - | `[conn_state][has_conn][addr6][play_state][scanning]` |
| 0x03 | LIST_PAIRED | - | `[addr6]...` |
| 0x04 | START_SCAN | `[seconds]` | - |
| 0x05 | GET_SCAN_RESULTS | - | `[count]([addr6][name_len][name])...` |
| 0x06 | PAIR_CONNECT | `[addr6]` | - (pairs via SSP if the device is not bonded) |
| 0x07 | CONNECT | `[addr6]` | - (bonded device) |
| 0x08 | DISCONNECT | - | - |
| 0x09 | FORGET | `[addr6]` | - |
| 0x0A | SET_METADATA | `[field][bytes]` | - (field 1 title, 2 artist, 3 album, 4 duration ms as u32 LE) |
| 0x0B | SET_PLAYBACK | `[0 stopped / 1 playing / 2 paused]` | - |
| 0x0C | GET_EVENT | - | `[event_id][data]` or `[0x00]` if empty |

Status byte: `0x00` OK, `0x01` bad length, `0x02` unknown command, `0x03` failed.

Events (read with GET_EVENT, and signalled by IRQ going low):

| Event | Data |
|---|---|
| 0x80 CONN_STATE | `[state][addr6]` (0 disconnected, 1 connecting, 2 connected, 3 disconnecting) |
| 0x81 PASSTHROUGH | `[avrcp_key][key_state]` (0 pressed, 1 released) |
| 0x82 SCAN_DONE | - |

The AVRCP keys are in `protocol.h` as `BF_KEY_*`: play 0x44, pause 0x46, stop 0x45, next 0x4B, prev 0x4C, volume up/down 0x41/0x42.

## Build

Requires ESP-IDF 5.x.

```sh
cd fw
idf.py set-target esp32
idf.py menuconfig      # optional: confirm the BT/A2DP options from sdkconfig.defaults
idf.py build flash monitor
```

## Known gaps and things to verify

Nothing here has been compiled or run yet. Check these first:

- **Build check.** I did not have an ESP-IDF toolchain, so the code is unbuilt. Expect small fixes, mostly in Kconfig symbol names in `sdkconfig.defaults` and in the legacy `driver/i2c.h` and `driver/i2s.h` calls. Those legacy drivers still work on ESP-IDF 5.x, but they emit deprecation warnings.
- **Metadata on the speaker.** The ESP-IDF AVRCP target API does not document a way to answer GetElementAttributes, which is how a speaker pulls title and artist. The firmware stores the metadata the host sends, but the speaker may not display it until that response is implemented. Playback status notifications and passthrough keys do work through the target API.
- **Pairing security.** Pairing uses Secure Simple Pairing with NoInputNoOutput, so it is "Just Works" and every confirm request is accepted automatically. This is convenient for a headless device, but it provides no MITM protection.
- **I2C response timing.** The response is written into the slave TX FIFO after the host's write is handled. The host must wait about 5 ms before reading. Polling `GET_STATUS` or `GET_EVENT` with a short retry is more robust.
- **Bond list API.** `bt_av_list_paired` uses `esp_bt_gap_get_bond_device_num` and `esp_bt_gap_get_bond_device_list`. Confirm their signatures against your IDF version.
- **Scan results.** Only the last 8 discovered devices are kept, and each response frame is capped at 250 bytes.
