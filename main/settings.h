#pragma once
#include <stdbool.h>
#include <stdint.h>

struct luz_settings_t {
    bool     setup_done;          // false hasta que el portal se use (o expire) la primera vez
    bool     force_portal;        // el próximo arranque será en modo portal
    uint8_t  boot_state;          // 0 apagado, 1 encendido, 2 último estado
    char     device_name[33];
    char     ap_pass[64];         // contraseña del AP del portal (8..63)
    char     thread_dataset[509]; // dataset operativo Thread en hex (opcional)
    char     mqtt_host[64];       // vacío = MQTT desactivado
    uint16_t mqtt_port;
    char     mqtt_user[48];
    char     mqtt_pass[64];
    bool     mqtt_tls;
    char     mqtt_base[64];       // vacío = luz-c6/<id>
    bool     mqtt_ha;             // publicar descubrimiento de Home Assistant
};

extern luz_settings_t g_cfg;

void        settings_load();
bool        settings_save();
void        settings_erase();                 // borra solo el namespace "luz"
bool        settings_relay_load();
void        settings_relay_store(bool on);
const char *device_id();                      // 6 hex, p. ej. "a1b2c3"
const char *mqtt_base_topic();                // base efectiva
inline bool mqtt_enabled() { return g_cfg.mqtt_host[0] != 0; }
