#include "bt_av.h"

#include <string.h>

#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "esp_bt.h"
#include "esp_bt_defs.h"
#include "esp_bt_device.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "i2c_link.h"
#include "i2s_audio.h"
#include "protocol.h"

#define TAG            "bt_av"
#define DEVICE_NAME    "blestfriend"
#define SCAN_MAX       8      /* devices kept from the last scan */
#define PAIRED_MAX     40     /* 40 * 6 = 240 bytes, fits in one frame */
#define NAME_MAX       32

typedef struct {
    esp_bd_addr_t bda;
    char name[NAME_MAX + 1];
    bool used;
} scan_entry_t;

static SemaphoreHandle_t s_lock;

static scan_entry_t s_scan[SCAN_MAX];
static uint8_t s_scan_count;
static bool s_scanning;

static bool s_has_conn;
static esp_bd_addr_t s_conn_addr;
static uint8_t s_conn_state = BF_CONN_DISCONNECTED;
static uint8_t s_play_state = BF_PLAY_STOPPED;

static char s_title[64];
static char s_artist[64];
static char s_album[64];
static uint32_t s_duration_ms;

/* ---------- helpers ---------- */

static void copy_str(char *dst, size_t cap, const uint8_t *src, uint8_t len)
{
    size_t n = len < cap - 1 ? len : cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void add_scan_entry(const scan_entry_t *in)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int slot = -1;
    for (int i = 0; i < SCAN_MAX; i++) {
        if (s_scan[i].used && memcmp(s_scan[i].bda, in->bda, ESP_BD_ADDR_LEN) == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0 && s_scan_count < SCAN_MAX) {
        slot = s_scan_count++;
        s_scan[slot].used = true;
        memcpy(s_scan[slot].bda, in->bda, ESP_BD_ADDR_LEN);
    }
    if (slot >= 0 && in->name[0] != '\0') {
        strncpy(s_scan[slot].name, in->name, NAME_MAX);
        s_scan[slot].name[NAME_MAX] = '\0';
    }
    xSemaphoreGive(s_lock);
}

/* ---------- GAP (discovery, pairing) ---------- */

static void gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_BT_GAP_DISC_RES_EVT: {
        scan_entry_t tmp = {0};
        memcpy(tmp.bda, param->disc_res.bda, ESP_BD_ADDR_LEN);
        for (int i = 0; i < param->disc_res.num_prop; i++) {
            esp_bt_gap_dev_prop_t *p = &param->disc_res.prop[i];
            if (p->type == ESP_BT_GAP_DEV_PROP_BDNAME) {
                copy_str(tmp.name, sizeof(tmp.name), p->val, p->len);
            }
        }
        tmp.used = true;
        add_scan_entry(&tmp);
        break;
    }
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
        if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
            s_scanning = false;
            i2c_link_post_event(BF_EVT_SCAN_DONE, NULL, 0);
        }
        break;

    /* Secure Simple Pairing with IO capability NONE: "Just Works", auto-accepted. */
    case ESP_BT_GAP_CFM_REQ_EVT:
        ESP_LOGI(TAG, "SSP confirm request, accepting");
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;

    case ESP_BT_GAP_PIN_REQ_EVT: {
        /* Legacy pairing fallback for speakers that do not support SSP. */
        esp_bt_pin_code_t pin = {'0', '0', '0', '0'};
        esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin);
        break;
    }

    case ESP_BT_GAP_AUTH_CMPL_EVT:
        ESP_LOGI(TAG, "auth complete, status=%d", param->auth_cmpl.stat);
        break;

    default:
        break;
    }
}

/* ---------- A2DP source ---------- */

static int32_t a2d_get_data(uint8_t *data, int32_t len)
{
    return i2s_audio_pull(data, len);
}

static void a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT: {
        uint8_t state = (uint8_t)param->conn_stat.state;   /* values match bf_conn_t */
        ESP_LOGI(TAG, "A2DP conn state=%u", state);

        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_conn_state = state;
        if (state == BF_CONN_CONNECTED) {
            memcpy(s_conn_addr, param->conn_stat.remote_bda, ESP_BD_ADDR_LEN);
            s_has_conn = true;
        } else if (state == BF_CONN_DISCONNECTED) {
            s_has_conn = false;
        }
        xSemaphoreGive(s_lock);

        uint8_t evt[1 + ESP_BD_ADDR_LEN];
        evt[0] = state;
        memcpy(evt + 1, param->conn_stat.remote_bda, ESP_BD_ADDR_LEN);
        i2c_link_post_event(BF_EVT_CONN_STATE, evt, sizeof(evt));

        if (state == BF_CONN_CONNECTED) {
            esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
        }
        break;
    }
    case ESP_A2D_AUDIO_STATE_EVT:
        ESP_LOGI(TAG, "A2DP audio state=%d", param->audio_stat.state);
        break;
    default:
        break;
    }
}

/* ---------- AVRCP ---------- */

static void avrc_ct_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param)
{
    /* We do not control the remote (it is the source of keys), so nothing to do yet. */
    (void)event;
    (void)param;
}

static void avrc_tg_cb(esp_avrc_tg_cb_event_t event, esp_avrc_tg_cb_param_t *param)
{
    switch (event) {
    case ESP_AVRC_TG_PASSTHROUGH_CMD_EVT: {
        uint8_t d[2] = {param->psth_cmd.key_code, param->psth_cmd.key_state};
        ESP_LOGI(TAG, "AVRCP key 0x%02x state %u", d[0], d[1]);
        i2c_link_post_event(BF_EVT_PASSTHROUGH, d, sizeof(d));
        break;
    }
    case ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT: {
        /* Remote subscribed to play status: answer with the current state. */
        if (param->reg_ntf.event_id == ESP_AVRC_RN_PLAY_STATUS_CHANGE) {
            esp_avrc_rn_param_t p = {0};
            p.playback = s_play_state;
            esp_avrc_tg_send_rn_rsp(ESP_AVRC_RN_PLAY_STATUS_CHANGE, ESP_AVRC_RN_RSP_INTERIM, &p);
        }
        break;
    }
    case ESP_AVRC_TG_CONNECTION_STATE_EVT:
        ESP_LOGI(TAG, "AVRCP target connected=%d", param->conn_stat.connected);
        break;
    default:
        break;
    }
}

/* ---------- public API ---------- */

esp_err_t bt_av_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    ESP_ERROR_CHECK(esp_bt_gap_register_callback(gap_cb));
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_NONE;
    ESP_ERROR_CHECK(esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &iocap, sizeof(uint8_t)));
    ESP_ERROR_CHECK(esp_bt_dev_set_device_name(DEVICE_NAME));
    ESP_ERROR_CHECK(esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE));

    ESP_ERROR_CHECK(esp_avrc_ct_init());
    ESP_ERROR_CHECK(esp_avrc_ct_register_callback(avrc_ct_cb));
    ESP_ERROR_CHECK(esp_avrc_tg_init());
    ESP_ERROR_CHECK(esp_avrc_tg_register_callback(avrc_tg_cb));

    ESP_ERROR_CHECK(esp_a2d_register_callback(a2d_cb));
    ESP_ERROR_CHECK(esp_a2d_source_register_data_callback(a2d_get_data));
    ESP_ERROR_CHECK(esp_a2d_source_init());

    ESP_LOGI(TAG, "Bluetooth A2DP source up as \"%s\"", DEVICE_NAME);
    return ESP_OK;
}

esp_err_t bt_av_start_scan(uint8_t seconds)
{
    if (seconds == 0) {
        seconds = 10;
    }
    /* Inquiry length is in units of 1.28 s. */
    uint32_t inq = ((uint32_t)seconds * 100) / 128;
    if (inq < 1) inq = 1;
    if (inq > 48) inq = 48;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    memset(s_scan, 0, sizeof(s_scan));
    s_scan_count = 0;
    s_scanning = true;
    xSemaphoreGive(s_lock);

    esp_err_t err = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, (uint8_t)inq, 0);
    if (err != ESP_OK) {
        s_scanning = false;
    }
    return err;
}

uint8_t bt_av_get_scan_results(uint8_t *out, uint8_t max)
{
    uint8_t len = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);

    uint8_t count = 0;
    uint8_t *count_pos = out;
    len = 1;
    for (int i = 0; i < SCAN_MAX; i++) {
        if (!s_scan[i].used) continue;
        uint8_t name_len = (uint8_t)strnlen(s_scan[i].name, NAME_MAX);
        uint8_t need = ESP_BD_ADDR_LEN + 1 + name_len;
        if (len + need > max) break;
        memcpy(out + len, s_scan[i].bda, ESP_BD_ADDR_LEN);
        out[len + ESP_BD_ADDR_LEN] = name_len;
        memcpy(out + len + ESP_BD_ADDR_LEN + 1, s_scan[i].name, name_len);
        len += need;
        count++;
    }
    *count_pos = count;

    xSemaphoreGive(s_lock);
    return len;
}

uint8_t bt_av_list_paired(uint8_t *out, uint8_t max)
{
    int num = esp_bt_gap_get_bond_device_num();
    if (num > PAIRED_MAX) num = PAIRED_MAX;
    if (num * ESP_BD_ADDR_LEN > max) num = max / ESP_BD_ADDR_LEN;

    esp_bd_addr_t list[PAIRED_MAX];
    int count = num;
    if (num > 0 && esp_bt_gap_get_bond_device_list(&count, list) != ESP_OK) {
        return 0;
    }
    if (count > num) count = num;

    for (int i = 0; i < count; i++) {
        memcpy(out + i * ESP_BD_ADDR_LEN, list[i], ESP_BD_ADDR_LEN);
    }
    return (uint8_t)(count * ESP_BD_ADDR_LEN);
}

esp_err_t bt_av_connect(const uint8_t *addr6)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool busy = s_has_conn;
    xSemaphoreGive(s_lock);
    if (busy) {
        return ESP_ERR_INVALID_STATE;   /* disconnect first */
    }

    esp_bt_gap_cancel_discovery();      /* discovery and connect do not mix well */
    return esp_a2d_source_connect((uint8_t *)addr6);
}

esp_err_t bt_av_disconnect(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool connected = s_has_conn;
    esp_bd_addr_t addr;
    memcpy(addr, s_conn_addr, ESP_BD_ADDR_LEN);
    xSemaphoreGive(s_lock);

    if (!connected) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_a2d_source_disconnect(addr);
}

esp_err_t bt_av_forget(const uint8_t *addr6)
{
    return esp_bt_gap_remove_bond_device((uint8_t *)addr6);
}

uint8_t bt_av_get_status(uint8_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    out[0] = s_conn_state;
    out[1] = s_has_conn ? 1 : 0;
    memset(out + 2, 0, ESP_BD_ADDR_LEN);
    if (s_has_conn) {
        memcpy(out + 2, s_conn_addr, ESP_BD_ADDR_LEN);
    }
    out[2 + ESP_BD_ADDR_LEN] = s_play_state;
    out[3 + ESP_BD_ADDR_LEN] = s_scanning ? 1 : 0;
    xSemaphoreGive(s_lock);
    return 4 + ESP_BD_ADDR_LEN;
}

esp_err_t bt_av_set_metadata(uint8_t field, const uint8_t *data, uint8_t len)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    switch (field) {
    case BF_META_TITLE:
        copy_str(s_title, sizeof(s_title), data, len);
        break;
    case BF_META_ARTIST:
        copy_str(s_artist, sizeof(s_artist), data, len);
        break;
    case BF_META_ALBUM:
        copy_str(s_album, sizeof(s_album), data, len);
        break;
    case BF_META_DURATION_MS:
        if (len != 4) {
            xSemaphoreGive(s_lock);
            return ESP_ERR_INVALID_ARG;
        }
        s_duration_ms = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
                        ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
        break;
    default:
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "metadata field %u updated (%u bytes)", field, len);
    return ESP_OK;
}

esp_err_t bt_av_set_playback(uint8_t play_state)
{
    if (play_state > BF_PLAY_PAUSED) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_play_state = play_state;
    xSemaphoreGive(s_lock);

    esp_avrc_rn_param_t p = {0};
    p.playback = play_state;
    esp_avrc_tg_send_rn_rsp(ESP_AVRC_RN_PLAY_STATUS_CHANGE, ESP_AVRC_RN_RSP_CHANGED, &p);
    return ESP_OK;
}
