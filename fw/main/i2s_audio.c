#include "i2s_audio.h"

#include <string.h>

#include "driver/i2s.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"

#define TAG          "i2s_audio"
#define I2S_PORT     I2S_NUM_0
#define PIN_BCLK     25
#define PIN_WS       26
#define PIN_DIN      34
#define SAMPLE_RATE  44100
#define RING_BYTES   (16 * 1024)   /* ~93 ms of 44.1 kHz 16-bit stereo */
#define READ_CHUNK   512           /* multiple of 4 bytes (one stereo frame) */

static RingbufHandle_t s_rb;

static void i2s_reader_task(void *arg)
{
    uint8_t buf[READ_CHUNK];
    size_t n = 0;
    uint32_t dropped = 0;

    for (;;) {
        if (i2s_read(I2S_PORT, buf, sizeof(buf), &n, portMAX_DELAY) != ESP_OK || n == 0) {
            continue;
        }
        if (xRingbufferSend(s_rb, buf, n, 0) != pdTRUE) {
            /* Ring full: the A2DP side is not draining fast enough. Drop newest. */
            if ((++dropped % 100) == 1) {
                ESP_LOGW(TAG, "ring full, dropped %u chunks", (unsigned)dropped);
            }
        }
    }
}

esp_err_t i2s_audio_init(void)
{
    s_rb = xRingbufferCreate(RING_BYTES, RINGBUF_TYPE_BYTEBUF);
    if (!s_rb) {
        return ESP_ERR_NO_MEM;
    }

    /* Slave RX: BCLK and WS come from the host. The sample rate field is informational here. */
    i2s_config_t cfg = {
        .mode = I2S_MODE_SLAVE | I2S_MODE_RX,
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 6,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0,
    };
    esp_err_t err = i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
    if (err != ESP_OK) {
        return err;
    }

    i2s_pin_config_t pins = {
        .bck_io_num = PIN_BCLK,
        .ws_io_num = PIN_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = PIN_DIN,
    };
    err = i2s_set_pin(I2S_PORT, &pins);
    if (err != ESP_OK) {
        return err;
    }

    BaseType_t ok = xTaskCreate(i2s_reader_task, "i2s_rx", 3072, NULL, 10, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

int32_t i2s_audio_pull(uint8_t *dst, int32_t len)
{
    size_t total = 0;

    /* Byte-buffer ring returns contiguous chunks; loop until we have len bytes or run dry. */
    while ((int32_t)total < len) {
        size_t chunk = 0;
        void *p = xRingbufferReceiveUpTo(s_rb, &chunk, 0, (size_t)len - total);
        if (!p) {
            break;
        }
        memcpy(dst + total, p, chunk);
        vRingbufferReturnItem(s_rb, p);
        total += chunk;
    }

    if ((int32_t)total < len) {
        memset(dst + total, 0, (size_t)len - total);   /* silence on underrun */
    }
    return len;
}
