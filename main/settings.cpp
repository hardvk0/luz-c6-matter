#include "settings.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs.h"

static const char *TAG = "settings";
static const char *NS  = "luz";

luz_settings_t g_cfg;

static void get_str(nvs_handle_t h, const char *key, char *dst, size_t len, const char *def)
{
    size_t n = len;
    if (nvs_get_str(h, key, dst, &n) != ESP_OK) {
        strlcpy(dst, def, len);
    }
}

const char *device_id()
{
    static char id[7];
    if (!id[0]) {
        uint8_t mac[6] = {0};
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(id, sizeof(id), "%02x%02x%02x", mac[3], mac[4], mac[5]);
    }
    return id;
}

const char *mqtt_base_topic()
{
    static char base[80];
    if (g_cfg.mqtt_base[0]) {
        strlcpy(base, g_cfg.mqtt_base, sizeof(base));
    } else {
        snprintf(base, sizeof(base), "luz-c6/%s", device_id());
    }
    // sin "/" final
    size_t n = strlen(base);
    while (n > 1 && base[n - 1] == '/') base[--n] = 0;
    return base;
}

void settings_load()
{
    memset(&g_cfg, 0, sizeof(g_cfg));
    nvs_handle_t h;
    uint8_t u8 = 0;
    uint16_t u16 = 0;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        // Primer arranque: valores por defecto.
        g_cfg.mqtt_port = 1883;
        strlcpy(g_cfg.device_name, "Luz", sizeof(g_cfg.device_name));
        strlcpy(g_cfg.ap_pass, "luzconfig", sizeof(g_cfg.ap_pass));
        g_cfg.boot_state = 2;
        return;
    }
    if (nvs_get_u8(h, "setup", &u8) == ESP_OK) g_cfg.setup_done = u8;
    if (nvs_get_u8(h, "portal", &u8) == ESP_OK) g_cfg.force_portal = u8;
    g_cfg.boot_state = (nvs_get_u8(h, "bstate", &u8) == ESP_OK) ? u8 : 2;
    g_cfg.mqtt_port  = (nvs_get_u16(h, "mport", &u16) == ESP_OK) ? u16 : 1883;
    if (nvs_get_u8(h, "mtls", &u8) == ESP_OK) g_cfg.mqtt_tls = u8;
    if (nvs_get_u8(h, "mha", &u8) == ESP_OK) g_cfg.mqtt_ha = u8;
    get_str(h, "name",  g_cfg.device_name, sizeof(g_cfg.device_name), "Luz");
    get_str(h, "appass", g_cfg.ap_pass, sizeof(g_cfg.ap_pass), "luzconfig");
    get_str(h, "dataset", g_cfg.thread_dataset, sizeof(g_cfg.thread_dataset), "");
    get_str(h, "mhost", g_cfg.mqtt_host, sizeof(g_cfg.mqtt_host), "");
    get_str(h, "muser", g_cfg.mqtt_user, sizeof(g_cfg.mqtt_user), "");
    get_str(h, "mpass", g_cfg.mqtt_pass, sizeof(g_cfg.mqtt_pass), "");
    get_str(h, "mbase", g_cfg.mqtt_base, sizeof(g_cfg.mqtt_base), "");
    nvs_close(h);
}

bool settings_save()
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo abrir NVS");
        return false;
    }
    nvs_set_u8(h, "setup", g_cfg.setup_done);
    nvs_set_u8(h, "portal", g_cfg.force_portal);
    nvs_set_u8(h, "bstate", g_cfg.boot_state);
    nvs_set_u16(h, "mport", g_cfg.mqtt_port);
    nvs_set_u8(h, "mtls", g_cfg.mqtt_tls);
    nvs_set_u8(h, "mha", g_cfg.mqtt_ha);
    nvs_set_str(h, "name", g_cfg.device_name);
    nvs_set_str(h, "appass", g_cfg.ap_pass);
    nvs_set_str(h, "dataset", g_cfg.thread_dataset);
    nvs_set_str(h, "mhost", g_cfg.mqtt_host);
    nvs_set_str(h, "muser", g_cfg.mqtt_user);
    nvs_set_str(h, "mpass", g_cfg.mqtt_pass);
    nvs_set_str(h, "mbase", g_cfg.mqtt_base);
    esp_err_t err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}

void settings_erase()
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
}

bool settings_relay_load()
{
    nvs_handle_t h;
    uint8_t v = 0;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    nvs_get_u8(h, "relay", &v);
    nvs_close(h);
    return v != 0;
}

void settings_relay_store(bool on)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "relay", on);
    nvs_commit(h);
    nvs_close(h);
}
