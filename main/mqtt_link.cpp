#include "mqtt_link.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "app_common.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "io.h"
#include "mqtt_client.h"
#include "ota_url.h"
#include "settings.h"

static const char *TAG = "mqtt";

static esp_mqtt_client_handle_t s_client;
static volatile bool s_conn;
static char s_t_state[96], s_t_set[96], s_t_avail[96], s_t_ota[96], s_t_ha[96], s_client_id[32];

bool mqtt_link_connected() { return s_conn; }

static void publish_state()
{
    if (s_conn) esp_mqtt_client_publish(s_client, s_t_state, relay_get() ? "ON" : "OFF", 0, 1, 1);
}

static void on_relay(bool, const char *) { publish_state(); }

static void json_clean(char *dst, size_t len, const char *src)   // quita comillas y barras
{
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 1 < len; i++) {
        dst[j++] = (src[i] == '"' || src[i] == '\\') ? ' ' : src[i];
    }
    dst[j] = 0;
}

static void publish_ha_discovery()
{
    char name[40], payload[800];
    json_clean(name, sizeof(name), g_cfg.device_name);
    snprintf(payload, sizeof(payload),
             "{\"name\":null,\"uniq_id\":\"luzc6_%s\","
             "\"cmd_t\":\"%s\",\"stat_t\":\"%s\",\"avty_t\":\"%s\","
             "\"pl_on\":\"ON\",\"pl_off\":\"OFF\",\"ret\":true,"
             "\"dev\":{\"ids\":[\"luzc6_%s\"],\"name\":\"%s\",\"mf\":\"DIY\","
             "\"mdl\":\"ESP32-C6 rele luz\",\"sw\":\"%s\"}}",
             device_id(), s_t_set, s_t_state, s_t_avail, device_id(), name, FW_VERSION_STR);
    esp_mqtt_client_publish(s_client, s_t_ha, payload, 0, 1, 1);
}

static void handle_data(esp_mqtt_event_handle_t e)
{
    // Ignora mensajes fragmentados: los comandos son cortos.
    if (e->current_data_offset != 0 || e->data_len != e->total_data_len || e->data_len > 255) return;

    char topic[96], data[256];
    size_t tl = e->topic_len < (int)sizeof(topic) - 1 ? e->topic_len : sizeof(topic) - 1;
    memcpy(topic, e->topic, tl); topic[tl] = 0;
    memcpy(data, e->data, e->data_len); data[e->data_len] = 0;

    if (!strcmp(topic, s_t_set)) {
        for (char *p = data; *p; p++) *p = tolower((unsigned char)*p);
        if (!strcmp(data, "on") || !strcmp(data, "1") || !strcmp(data, "true"))       relay_set(true, "mqtt");
        else if (!strcmp(data, "off") || !strcmp(data, "0") || !strcmp(data, "false")) relay_set(false, "mqtt");
        else if (!strcmp(data, "toggle"))                                              relay_toggle("mqtt");
        publish_state();   // confirma el estado también si no hubo cambio
    } else if (!strcmp(topic, s_t_ota)) {
        ESP_LOGI(TAG, "OTA solicitada por MQTT");
        ota_url_start(data);
    }
}

static void mqtt_event(void *, esp_event_base_t, int32_t id, void *data)
{
    auto *e = (esp_mqtt_event_handle_t)data;
    switch (id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Conectado al broker");
        s_conn = true;
        esp_mqtt_client_publish(s_client, s_t_avail, "online", 0, 1, 1);
        esp_mqtt_client_subscribe(s_client, s_t_set, 1);
        esp_mqtt_client_subscribe(s_client, s_t_ota, 1);
        publish_state();
        if (g_cfg.mqtt_ha) publish_ha_discovery();
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Desconectado del broker");
        s_conn = false;
        break;
    case MQTT_EVENT_DATA:
        handle_data(e);
        break;
    default:
        break;
    }
}

void mqtt_link_start()
{
    if (!mqtt_enabled() || s_client) return;

    const char *base = mqtt_base_topic();
    snprintf(s_t_state, sizeof(s_t_state), "%s/state", base);
    snprintf(s_t_set,   sizeof(s_t_set),   "%s/set", base);
    snprintf(s_t_avail, sizeof(s_t_avail), "%s/availability", base);
    snprintf(s_t_ota,   sizeof(s_t_ota),   "%s/ota/set", base);
    snprintf(s_t_ha,    sizeof(s_t_ha),    "homeassistant/light/luzc6_%s/config", device_id());
    snprintf(s_client_id, sizeof(s_client_id), "luzc6-%s", device_id());

    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.hostname = g_cfg.mqtt_host;
    cfg.broker.address.port = g_cfg.mqtt_port ? g_cfg.mqtt_port : (g_cfg.mqtt_tls ? 8883 : 1883);
    cfg.broker.address.transport = g_cfg.mqtt_tls ? MQTT_TRANSPORT_OVER_SSL : MQTT_TRANSPORT_OVER_TCP;
    if (g_cfg.mqtt_tls) cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.credentials.client_id = s_client_id;
    if (g_cfg.mqtt_user[0]) {
        cfg.credentials.username = g_cfg.mqtt_user;
        cfg.credentials.authentication.password = g_cfg.mqtt_pass;
    }
    cfg.session.last_will.topic = s_t_avail;
    cfg.session.last_will.msg = "offline";
    cfg.session.last_will.msg_len = 7;
    cfg.session.last_will.qos = 1;
    cfg.session.last_will.retain = 1;
    cfg.session.keepalive = 60;
    cfg.network.reconnect_timeout_ms = 10000;

    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "No se pudo crear el cliente MQTT");
        return;
    }
    esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, mqtt_event, nullptr);
    io_add_relay_listener(on_relay);
    esp_mqtt_client_start(s_client);
    ESP_LOGI(TAG, "MQTT activo: %s:%d, base=%s", g_cfg.mqtt_host, (int)cfg.broker.address.port, base);
}
