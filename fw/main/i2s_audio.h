#pragma once

#include <stdint.h>
#include "esp_err.h"

/*
 * I2S audio input. The host is the I2S master (provides BCLK/WS and drives DIN);
 * the ESP32 is a slave receiver. Playback state is irrelevant here: audio is
 * simply whatever the host sends.
 *
 * Pins (see protocol.h / README):
 *   BCLK = IO25, WS = IO26, DIN = IO34 (input-only pin, fine for DIN)
 */
esp_err_t i2s_audio_init(void);

/*
 * Pull `len` bytes of PCM (16-bit stereo, 44.1 kHz) for the A2DP encoder.
 * Non-blocking. Underruns are padded with silence so the stream never stalls.
 * Returns `len`.
 */
int32_t i2s_audio_pull(uint8_t *dst, int32_t len);
