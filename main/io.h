#pragma once
#include <stdbool.h>
#include <stdint.h>

// ---- Relé (GPIO19, expuesto como luz) ----
typedef void (*relay_listener_t)(bool on, const char *source);
void io_init();
bool relay_get();
bool relay_set(bool on, const char *source);   // true si cambió el estado
void relay_toggle(const char *source);
void io_add_relay_listener(relay_listener_t cb);

// ---- Botón (GPIO9) ----
enum btn_event_t {
    BTN_COMMISSION,   // 3-8 s: abrir ventana de emparejamiento Matter
    BTN_PORTAL,       // 8-15 s: reiniciar en modo portal
    BTN_FACTORY,      // >=15 s: restauración de fábrica
};
typedef void (*button_handler_t)(btn_event_t evt);
void io_set_button_handler(button_handler_t cb);

// ---- LED (GPIO2) ----
enum led_mode_t {
    LED_WAIT,       // 200 ms cada 1 s: sin red Thread todavía / esperando emparejar
    LED_PORTAL,     // parpadeo rápido: portal Wi-Fi activo
    LED_ONLINE,     // 100 ms cada 2 s: en línea
    LED_NO_BROKER,  // doble destello cada 2 s: red OK pero MQTT configurado y desconectado
};
void led_set_mode(led_mode_t m);
void led_set_ota(bool active);          // latido lento mientras hay una OTA
void led_identify(bool on);             // Identify de Matter (START/STOP)
void led_identify_for(uint32_t secs);   // Identify con duración
