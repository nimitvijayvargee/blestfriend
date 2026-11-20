#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "bt_av.h"
#include "i2c_link.h"
#include "i2s_audio.h"

#define TAG "main"

void app_main(void)
{
    /* NVS stores Bluetooth bonding keys, so it must be ready first. */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(i2c_link_init());   /* host command channel */
    ESP_ERROR_CHECK(i2s_audio_init());  /* audio in from host */
    ESP_ERROR_CHECK(bt_av_init());      /* A2DP source + AVRCP target */

    ESP_LOGI(TAG, "blestfriend firmware ready");
}
