#include "io.h"
#include "settings.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "io";

#define RELAY_GPIO  ((gpio_num_t)CONFIG_LUZ_RELAY_GPIO)
#define BUTTON_GPIO ((gpio_num_t)CONFIG_LUZ_BUTTON_GPIO)
#define LED_GPIO    ((gpio_num_t)CONFIG_LUZ_LED_GPIO)

#ifdef CONFIG_LUZ_RELAY_ACTIVE_HIGH
#define RELAY_LEVEL(on) ((on) ? 1 : 0)
#else
#define RELAY_LEVEL(on) ((on) ? 0 : 1)
#endif
#ifdef CONFIG_LUZ_LED_ACTIVE_HIGH
#define LED_LEVEL(on) ((on) ? 1 : 0)
#else
#define LED_LEVEL(on) ((on) ? 0 : 1)
#endif

// ------------------------------------------------------------------ relé
static bool s_relay;
static SemaphoreHandle_t s_mux;
static relay_listener_t s_listeners[3];
static int s_nlisteners;

bool relay_get() { return s_relay; }

void io_add_relay_listener(relay_listener_t cb)
{
    if (s_nlisteners < 3) s_listeners[s_nlisteners++] = cb;
}

bool relay_set(bool on, const char *source)
{
    xSemaphoreTake(s_mux, portMAX_DELAY);
    if (on == s_relay) {          // idempotente: evita bucles Matter <-> MQTT
        xSemaphoreGive(s_mux);
        return false;
    }
    s_relay = on;
    gpio_set_level(RELAY_GPIO, RELAY_LEVEL(on));
    xSemaphoreGive(s_mux);

    ESP_LOGI(TAG, "Relé %s (origen: %s)", on ? "ON" : "OFF", source);
    settings_relay_store(on);
    for (int i = 0; i < s_nlisteners; i++) s_listeners[i](on, source);
    return true;
}

void relay_toggle(const char *source) { relay_set(!s_relay, source); }

// ------------------------------------------------------------------ LED
struct pattern_t { const uint16_t *d; uint8_t n; };   // duraciones ms: encendido, apagado, ...
static const uint16_t D_WAIT[]     = {200, 800};
static const uint16_t D_PORTAL[]   = {100, 100};
static const uint16_t D_ONLINE[]   = {100, 1900};
static const uint16_t D_NOBROKER[] = {100, 100, 100, 1700};
static const uint16_t D_OTA[]      = {500, 500};
static const uint16_t D_IDENT[]    = {50, 50};
static const uint16_t D_SOLID[]    = {1000, 1};
static const pattern_t P_WAIT     = {D_WAIT, 2};
static const pattern_t P_PORTAL   = {D_PORTAL, 2};
static const pattern_t P_ONLINE   = {D_ONLINE, 2};
static const pattern_t P_NOBROKER = {D_NOBROKER, 4};
static const pattern_t P_OTA      = {D_OTA, 2};
static const pattern_t P_IDENT    = {D_IDENT, 2};
static const pattern_t P_SOLID    = {D_SOLID, 2};

static volatile led_mode_t s_mode = LED_WAIT;
static volatile bool s_ota;
static volatile int64_t s_ident_until_us;   // 0 = no, INT64_MAX = hasta STOP
static volatile int s_hold_zone;            // 0 = botón libre, 1..3 = zonas de pulsación larga

void led_set_mode(led_mode_t m) { s_mode = m; }
void led_set_ota(bool a) { s_ota = a; }
void led_identify(bool on) { s_ident_until_us = on ? INT64_MAX : 0; }
void led_identify_for(uint32_t secs) { s_ident_until_us = esp_timer_get_time() + (int64_t)secs * 1000000; }

static const pattern_t *pick_pattern()
{
    if (s_hold_zone == 1) return &P_SOLID;
    if (s_hold_zone == 2) return &P_PORTAL;
    if (s_hold_zone == 3) return &P_IDENT;
    if (s_ident_until_us && esp_timer_get_time() < s_ident_until_us) return &P_IDENT;
    if (s_ota) return &P_OTA;
    switch (s_mode) {
    case LED_PORTAL:    return &P_PORTAL;
    case LED_ONLINE:    return &P_ONLINE;
    case LED_NO_BROKER: return &P_NOBROKER;
    default:            return &P_WAIT;
    }
}

static void led_task(void *)
{
    const pattern_t *cur = nullptr;
    uint8_t idx = 0;
    uint32_t left = 0;
    for (;;) {
        const pattern_t *want = pick_pattern();
        if (want != cur) {
            cur = want; idx = 0; left = cur->d[0];
            gpio_set_level(LED_GPIO, LED_LEVEL(true));
        }
        vTaskDelay(pdMS_TO_TICKS(10));
        if (left > 10) {
            left -= 10;
        } else {
            idx = (idx + 1) % cur->n;
            left = cur->d[idx];
            gpio_set_level(LED_GPIO, LED_LEVEL(idx % 2 == 0));
        }
    }
}

// ------------------------------------------------------------------ botón
static button_handler_t s_btn_handler;
void io_set_button_handler(button_handler_t cb) { s_btn_handler = cb; }

static void button_task(void *)
{
    bool pressed = false;
    int64_t t0 = 0;
    int stable = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10));
        bool raw = (gpio_get_level(BUTTON_GPIO) == 0);   // activo en bajo
        if (raw != pressed) {
            if (++stable >= 3) {                          // antirrebote 30 ms
                stable = 0;
                pressed = raw;
                if (pressed) {
                    t0 = esp_timer_get_time();
                } else {
                    s_hold_zone = 0;
                    int64_t ms = (esp_timer_get_time() - t0) / 1000;
                    if (ms < 1000) {
                        relay_toggle("button");
                    } else if (ms >= 15000) {
                        if (s_btn_handler) s_btn_handler(BTN_FACTORY);
                    } else if (ms >= 8000) {
                        if (s_btn_handler) s_btn_handler(BTN_PORTAL);
                    } else if (ms >= 3000) {
                        if (s_btn_handler) s_btn_handler(BTN_COMMISSION);
                    }
                }
            }
        } else {
            stable = 0;
            if (pressed) {                                // feedback en el LED mientras se mantiene
                int64_t ms = (esp_timer_get_time() - t0) / 1000;
                s_hold_zone = ms >= 15000 ? 3 : ms >= 8000 ? 2 : ms >= 3000 ? 1 : 0;
            }
        }
    }
}

// ------------------------------------------------------------------ init
void io_init()
{
    s_mux = xSemaphoreCreateMutex();

    gpio_config_t out = {};
    out.mode = GPIO_MODE_OUTPUT;
    out.pin_bit_mask = (1ULL << RELAY_GPIO) | (1ULL << LED_GPIO);
    gpio_config(&out);

    gpio_config_t in = {};
    in.mode = GPIO_MODE_INPUT;
    in.pull_up_en = GPIO_PULLUP_ENABLE;
    in.pin_bit_mask = (1ULL << BUTTON_GPIO);
    gpio_config(&in);

    switch (g_cfg.boot_state) {
    case 0:  s_relay = false; break;
    case 1:  s_relay = true;  break;
    default: s_relay = settings_relay_load(); break;
    }
    gpio_set_level(RELAY_GPIO, RELAY_LEVEL(s_relay));
    gpio_set_level(LED_GPIO, LED_LEVEL(false));

    xTaskCreate(led_task, "led", 2048, nullptr, 3, nullptr);
    xTaskCreate(button_task, "button", 3072, nullptr, 4, nullptr);
    ESP_LOGI(TAG, "IO listo (relé=%d, botón=%d, led=%d), relé=%s",
             (int)RELAY_GPIO, (int)BUTTON_GPIO, (int)LED_GPIO, s_relay ? "ON" : "OFF");
}
