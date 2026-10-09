#include "matter_node.h"
#include <string.h>
#include "app_common.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "io.h"
#include "mqtt_link.h"
#include "settings.h"

#include <esp_matter.h>
#include <esp_matter_ota.h>
#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <platform/CHIPDeviceLayer.h>
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/ESP32/OpenthreadLauncher.h>
#endif

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
// Como en esp-matter/examples/light/main/app_priv.h: ESP-IDF no los define.
#define ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG()                                           \
    {                                                                                   \
        .radio_mode = RADIO_MODE_NATIVE,                                                \
    }

#define ESP_OPENTHREAD_DEFAULT_HOST_CONFIG()                                            \
    {                                                                                   \
        .host_connection_mode = HOST_CONNECTION_MODE_NONE,                              \
    }

#define ESP_OPENTHREAD_DEFAULT_PORT_CONFIG()                                            \
    {                                                                                   \
        .storage_partition_name = "nvs", .netif_queue_size = 10, .task_queue_size = 10, \
    }
#endif

static const char *TAG = "matter";

using namespace esp_matter;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

static uint16_t s_ep_id;
static volatile bool s_ble_connected;   // un controlador está conectado por BLE emparejando

// ------------------------------------------------------------ callbacks Matter
static esp_err_t attribute_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                              uint32_t attribute_id, esp_matter_attr_val_t *val, void *)
{
    if (type == attribute::PRE_UPDATE && endpoint_id == s_ep_id && cluster_id == OnOff::Id &&
        attribute_id == OnOff::Attributes::OnOff::Id) {
        relay_set(val->val.b, "matter");
    }
    return ESP_OK;
}

static esp_err_t identify_cb(identification::callback_type_t type, uint16_t, uint8_t, uint8_t, void *)
{
    switch (type) {
    case identification::START:  led_identify(true);  break;
    case identification::STOP:   led_identify(false); break;
    case identification::EFFECT: led_identify_for(3); break;
    }
    return ESP_OK;
}

static void event_cb(const ChipDeviceEvent *event, intptr_t)
{
    using namespace chip::DeviceLayer;
    switch (event->Type) {
    case DeviceEventType::kCHIPoBLEConnectionEstablished:
        s_ble_connected = true;
        ESP_LOGI(TAG, "Controlador conectado por BLE");
        break;
    case DeviceEventType::kCHIPoBLEConnectionClosed:
    case DeviceEventType::kFailSafeTimerExpired:
        s_ble_connected = false;
        break;
    case DeviceEventType::kCommissioningComplete:
        s_ble_connected = false;
        ESP_LOGI(TAG, "Emparejamiento completado");
        break;
    case DeviceEventType::kOtaStateChanged:
        led_set_ota(event->OtaStateChanged.newState == kOtaDownloadInProgress);
        break;
    case DeviceEventType::kFabricRemoved:
        if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
            auto &mgr = chip::Server::GetInstance().GetCommissioningWindowManager();
            if (!mgr.IsCommissioningWindowOpen()) {
                // Sigue en la red Thread: se anuncia solo por DNS-SD, como en el ejemplo oficial.
                CHIP_ERROR e = mgr.OpenBasicCommissioningWindow(
                    chip::System::Clock::Seconds32(CHIP_DEVICE_CONFIG_DISCOVERY_TIMEOUT_SECS),
                    chip::CommissioningWindowAdvertisement::kDnssdOnly);
                if (e != CHIP_NO_ERROR) ESP_LOGW(TAG, "No se pudo reabrir la ventana de emparejamiento");
            }
        }
        break;
    default:
        break;
    }
}

// ------------------------------------------------------------ relé -> Matter
static void relay_to_matter(bool on, const char *source)
{
    if (!strcmp(source, "matter")) return;          // el cambio ya vino de Matter
    esp_matter_attr_val_t v = esp_matter_bool(on);
    attribute::update(s_ep_id, OnOff::Id, OnOff::Attributes::OnOff::Id, &v);
}

// ------------------------------------------------------------ botón
static void button_cb(btn_event_t evt)
{
    switch (evt) {
    case BTN_COMMISSION: {
        lock::ScopedChipStackLock guard(portMAX_DELAY);
        auto &srv = chip::Server::GetInstance();
        if (srv.GetFabricTable().FabricCount() == 0 && !srv.GetCommissioningWindowManager().IsCommissioningWindowOpen()) {
            ESP_LOGI(TAG, "Reabriendo ventana de emparejamiento");
            if (srv.GetCommissioningWindowManager().OpenBasicCommissioningWindow() != CHIP_NO_ERROR) {
                ESP_LOGW(TAG, "No se pudo abrir la ventana de emparejamiento");
            }
        } else {
            ESP_LOGI(TAG, "Ya emparejado o ventana abierta: usa el controlador para compartir el dispositivo");
        }
        break;
    }
    case BTN_PORTAL:  app_restart_to_portal(); break;
    case BTN_FACTORY: app_factory_reset();     break;
    }
}

// ------------------------------------------------------------ dataset Thread opcional
static size_t hex_to_bin(const char *hex, uint8_t *out, size_t max)
{
    size_t n = strlen(hex);
    if (n == 0 || (n & 1) || n / 2 > max) return 0;
    for (size_t i = 0; i < n; i += 2) {
        int v = 0;
        for (int k = 0; k < 2; k++) {
            char c = hex[i + k];
            int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 :
                    (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
            if (d < 0) return 0;
            v = (v << 4) | d;
        }
        out[i / 2] = (uint8_t)v;
    }
    return n / 2;
}

static void apply_saved_dataset()
{
    if (!g_cfg.thread_dataset[0]) return;
    uint8_t buf[254];
    size_t n = hex_to_bin(g_cfg.thread_dataset, buf, sizeof(buf));
    if (!n) {
        ESP_LOGW(TAG, "Dataset Thread guardado no válido; se ignora");
        return;
    }
    lock::ScopedChipStackLock guard(portMAX_DELAY);
    if (chip::DeviceLayer::ConnectivityMgr().IsThreadProvisioned()) {
        ESP_LOGI(TAG, "Thread ya provisionado; se ignora el dataset guardado");
        return;
    }
    CHIP_ERROR err = chip::DeviceLayer::ThreadStackMgr().SetThreadProvision(chip::ByteSpan(buf, n));
    if (err == CHIP_NO_ERROR) err = chip::DeviceLayer::ThreadStackMgr().SetThreadEnabled(true);
    if (err == CHIP_NO_ERROR) {
        ESP_LOGI(TAG, "Dataset Thread aplicado desde el portal");
        g_cfg.thread_dataset[0] = 0;       // ya lo guarda OpenThread; no se reaplica
        settings_save();
    } else {
        ESP_LOGE(TAG, "No se pudo aplicar el dataset: %" CHIP_ERROR_FORMAT, err.Format());
    }
}

// ------------------------------------------------------------ supervisión de red / LED / portal
static void monitor_task(void *)
{
    const int64_t timeout_us = (int64_t)CONFIG_LUZ_NO_NET_TIMEOUT_MIN * 60 * 1000000;
    int64_t detached_since = esp_timer_get_time();
    bool was_attached = false;
    bool mqtt_started = false;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        bool attached, pairing;
        {
            lock::ScopedChipStackLock guard(portMAX_DELAY);
            attached = chip::DeviceLayer::ConnectivityMgr().IsThreadAttached();
            pairing = s_ble_connected || chip::Server::GetInstance().GetFailSafeContext().IsFailSafeArmed();
        }
        if (attached) {
            if (!was_attached) ESP_LOGI(TAG, "Unido a la red Thread");
            was_attached = true;
            if (mqtt_enabled() && !mqtt_started) {
                mqtt_link_start();
                mqtt_started = true;
            }
            led_set_mode((mqtt_enabled() && !mqtt_link_connected()) ? LED_NO_BROKER : LED_ONLINE);
        } else {
            if (was_attached) {
                was_attached = false;
                detached_since = esp_timer_get_time();
            }
            led_set_mode(LED_WAIT);
            if (!pairing && esp_timer_get_time() - detached_since > timeout_us) {
                ESP_LOGW(TAG, "%d min sin red Thread: abriendo portal de configuración", CONFIG_LUZ_NO_NET_TIMEOUT_MIN);
                app_restart_to_portal();
            }
        }
    }
}

// ------------------------------------------------------------ arranque
void matter_run()
{
    led_set_mode(LED_WAIT);

    node::config_t node_config;
    strlcpy(node_config.root_node.basic_information.node_label, g_cfg.device_name,
            sizeof(node_config.root_node.basic_information.node_label));
    node_t *node = node::create(&node_config, attribute_cb, identify_cb);
    if (!node) {
        ESP_LOGE(TAG, "No se pudo crear el nodo Matter");
        esp_restart();
    }

    on_off_light::config_t light_config;
    light_config.on_off.on_off = relay_get();
    light_config.on_off_lighting.start_up_on_off = nullptr;
    endpoint_t *ep = on_off_light::create(node, &light_config, ENDPOINT_FLAG_NONE, nullptr);
    if (!ep) {
        ESP_LOGE(TAG, "No se pudo crear el endpoint de la luz");
        esp_restart();
    }
    s_ep_id = endpoint::get_id(ep);
    ESP_LOGI(TAG, "Luz creada en el endpoint %d", s_ep_id);

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    esp_openthread_platform_config_t ot_config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&ot_config);
#endif

    io_add_relay_listener(relay_to_matter);
    io_set_button_handler(button_cb);

    esp_err_t err = esp_matter::start(event_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al arrancar Matter: %d", err);
        esp_restart();
    }

    apply_saved_dataset();
    xTaskCreate(monitor_task, "net_monitor", 4096, nullptr, 3, nullptr);
}
