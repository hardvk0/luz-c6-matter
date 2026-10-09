#include "ota_url.h"
#include <string.h>
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "io.h"

static const char *TAG = "ota_url";
static volatile bool s_busy;
static char s_url[256];

static void ota_task(void *)
{
    led_set_ota(true);
    ESP_LOGI(TAG, "Descargando %s", s_url);

    esp_http_client_config_t hc = {};
    hc.url = s_url;
    hc.crt_bundle_attach = esp_crt_bundle_attach;
    hc.timeout_ms = 20000;
    hc.keep_alive_enable = true;

    esp_https_ota_config_t oc = {};
    oc.http_config = &hc;

    esp_err_t err = esp_https_ota(&oc);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA correcta, reiniciando");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }
    ESP_LOGE(TAG, "OTA fallida: %s", esp_err_to_name(err));
    led_set_ota(false);
    s_busy = false;
    vTaskDelete(nullptr);
}

bool ota_url_start(const char *url)
{
    if (s_busy || !url || !*url || strlen(url) >= sizeof(s_url)) return false;
    s_busy = true;
    strlcpy(s_url, url, sizeof(s_url));
    if (xTaskCreate(ota_task, "ota_url", 8192, nullptr, 5, nullptr) != pdPASS) {
        s_busy = false;
        return false;
    }
    return true;
}
