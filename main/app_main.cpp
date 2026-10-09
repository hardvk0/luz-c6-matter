#include <string.h>
#include "app_common.h"
#include "esp_log.h"
#include "esp_system.h"
#include "io.h"
#include "matter_node.h"
#include "nvs_flash.h"
#include "portal.h"
#include "settings.h"

#include <esp_matter.h>

static const char *TAG = "app";

app_mode_t g_mode = MODE_PORTAL;

void app_restart_to_portal()
{
    ESP_LOGI(TAG, "Reiniciando en modo portal");
    g_cfg.force_portal = true;
    settings_save();
    esp_restart();
}

void app_restart_to_matter()
{
    ESP_LOGI(TAG, "Reiniciando en modo Matter");
    g_cfg.setup_done = true;
    g_cfg.force_portal = false;
    settings_save();
    esp_restart();
}

void app_factory_reset()
{
    ESP_LOGW(TAG, "Restauración de fábrica");
    settings_erase();
    if (g_mode == MODE_MATTER) {
        esp_matter::factory_reset();      // borra Matter + Thread y reinicia
    } else {
        nvs_flash_erase();
        esp_restart();
    }
}

static void portal_button_cb(btn_event_t evt)
{
    if (evt == BTN_FACTORY) app_factory_reset();
    // BTN_COMMISSION / BTN_PORTAL no tienen sentido dentro del portal.
}

extern "C" void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    settings_load();
    io_init();

    // Primera vez (setup_done = false) o portal solicitado -> modo portal.
    // En cualquier otro caso -> modo Matter. Nunca conviven (radio y RAM).
    if (g_cfg.force_portal || !g_cfg.setup_done) {
        g_mode = MODE_PORTAL;
        ESP_LOGI(TAG, "Modo PORTAL (id %s)", device_id());
        io_set_button_handler(portal_button_cb);
        portal_run();
    } else {
        g_mode = MODE_MATTER;
        ESP_LOGI(TAG, "Modo MATTER (id %s)", device_id());
        matter_run();
    }
}
