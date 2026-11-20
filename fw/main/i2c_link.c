#include "i2c_link.h"

#include <string.h>

#include "bt_av.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "protocol.h"

#define TAG            "i2c_link"
#define I2C_PORT       I2C_NUM_0
#define I2C_RX_BUF     512
#define I2C_TX_BUF     512
#define EVT_QUEUE_LEN  16
#define EVT_DATA_MAX   8

typedef struct {
    uint8_t id;
    uint8_t len;
    uint8_t data[EVT_DATA_MAX];
} evt_t;

static QueueHandle_t s_evt_q;

/* IRQ is active low: low while at least one event is waiting. */
static void irq_update(void)
{
    gpio_set_level(BF_PIN_IRQ, uxQueueMessagesWaiting(s_evt_q) ? 0 : 1);
}

void i2c_link_post_event(uint8_t id, const uint8_t *data, uint8_t len)
{
    evt_t e = {.id = id, .len = len > EVT_DATA_MAX ? EVT_DATA_MAX : len};
    if (data && e.len) {
        memcpy(e.data, data, e.len);
    }
    if (xQueueSend(s_evt_q, &e, 0) != pdTRUE) {
        ESP_LOGW(TAG, "event queue full, dropping event 0x%02x", id);
    }
    gpio_set_level(BF_PIN_IRQ, 0);
}

/* Write one response frame. Flush first so the host never reads a stale response. */
static void send_resp(uint8_t status, const uint8_t *payload, uint8_t len)
{
    uint8_t frame[2 + BF_MAX_PAYLOAD];
    frame[0] = status;
    frame[1] = len;
    if (len) {
        memcpy(frame + 2, payload, len);
    }
    i2c_reset_tx_fifo(I2C_PORT);
    i2c_slave_write_buffer(I2C_PORT, frame, 2 + len, 0);
}

static void dispatch(uint8_t cmd, const uint8_t *p, uint8_t len)
{
    uint8_t resp[BF_MAX_PAYLOAD];
    uint8_t rlen = 0;
    uint8_t status = BF_STATUS_OK;

    switch (cmd) {
    case BF_CMD_PING:
        resp[0] = BF_PROTO_VERSION;
        rlen = 1;
        break;

    case BF_CMD_GET_STATUS:
        rlen = bt_av_get_status(resp);
        break;

    case BF_CMD_LIST_PAIRED:
        rlen = bt_av_list_paired(resp, sizeof(resp));
        break;

    case BF_CMD_START_SCAN:
        if (len != 1) { status = BF_STATUS_BAD_LEN; break; }
        if (bt_av_start_scan(p[0]) != ESP_OK) status = BF_STATUS_FAILED;
        break;

    case BF_CMD_GET_SCAN_RESULTS:
        rlen = bt_av_get_scan_results(resp, sizeof(resp));
        break;

    case BF_CMD_PAIR_CONNECT:
    case BF_CMD_CONNECT:
        if (len != BF_ADDR_LEN) { status = BF_STATUS_BAD_LEN; break; }
        if (bt_av_connect(p) != ESP_OK) status = BF_STATUS_FAILED;
        break;

    case BF_CMD_DISCONNECT:
        if (bt_av_disconnect() != ESP_OK) status = BF_STATUS_FAILED;
        break;

    case BF_CMD_FORGET:
        if (len != BF_ADDR_LEN) { status = BF_STATUS_BAD_LEN; break; }
        if (bt_av_forget(p) != ESP_OK) status = BF_STATUS_FAILED;
        break;

    case BF_CMD_SET_METADATA:
        if (len < 1) { status = BF_STATUS_BAD_LEN; break; }
        if (bt_av_set_metadata(p[0], p + 1, len - 1) != ESP_OK) status = BF_STATUS_FAILED;
        break;

    case BF_CMD_SET_PLAYBACK:
        if (len != 1) { status = BF_STATUS_BAD_LEN; break; }
        if (bt_av_set_playback(p[0]) != ESP_OK) status = BF_STATUS_FAILED;
        break;

    case BF_CMD_GET_EVENT: {
        evt_t e;
        if (xQueueReceive(s_evt_q, &e, 0) == pdTRUE) {
            resp[0] = e.id;
            memcpy(resp + 1, e.data, e.len);
            rlen = 1 + e.len;
        } else {
            resp[0] = BF_EVT_NONE;
            rlen = 1;
        }
        irq_update();
        break;
    }

    default:
        status = BF_STATUS_UNKNOWN;
        break;
    }

    send_resp(status, resp, rlen);
}

/*
 * Reads frames from the host: [cmd][len][payload]. The host writes the whole
 * frame in one I2C transaction, so the header and payload normally arrive together.
 */
static void i2c_task(void *arg)
{
    uint8_t hdr[2];
    uint8_t payload[BF_MAX_PAYLOAD];

    for (;;) {
        if (i2c_slave_read_buffer(I2C_PORT, hdr, 2, portMAX_DELAY) != 2) {
            continue;
        }
        uint8_t cmd = hdr[0];
        uint8_t len = hdr[1];

        int got = 0;
        while (got < len) {
            int r = i2c_slave_read_buffer(I2C_PORT, payload + got, len - got, pdMS_TO_TICKS(50));
            if (r <= 0) break;
            got += r;
        }
        if (got != len) {
            ESP_LOGW(TAG, "short frame: cmd 0x%02x, got %d of %u", cmd, got, len);
            send_resp(BF_STATUS_BAD_LEN, NULL, 0);
            continue;
        }

        dispatch(cmd, payload, len);
    }
}

esp_err_t i2c_link_init(void)
{
    s_evt_q = xQueueCreate(EVT_QUEUE_LEN, sizeof(evt_t));
    if (!s_evt_q) {
        return ESP_ERR_NO_MEM;
    }

    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BF_PIN_IRQ,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(BF_PIN_IRQ, 1);

    i2c_config_t conf = {
        .mode = I2C_MODE_SLAVE,
        .sda_io_num = BF_PIN_SDA,
        .scl_io_num = BF_PIN_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .slave.addr_10bit_en = 0,
        .slave.slave_addr = BF_I2C_SLAVE_ADDR,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_PORT, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_PORT, I2C_MODE_SLAVE, I2C_RX_BUF, I2C_TX_BUF, 0));

    BaseType_t ok = xTaskCreate(i2c_task, "i2c_link", 4096, NULL, 8, NULL);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "I2C slave 0x%02x on SDA=%d SCL=%d, IRQ=%d",
             BF_I2C_SLAVE_ADDR, BF_PIN_SDA, BF_PIN_SCL, BF_PIN_IRQ);
    return ESP_OK;
}
