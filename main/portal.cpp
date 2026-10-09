#include "portal.h"
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "app_common.h"
#include "esp_http_server.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "io.h"
#include "lwip/sockets.h"
#include "portal_page.h"
#include "settings.h"

static const char *TAG = "portal";

// Credenciales de emparejamiento de PRUEBA de esp-matter (CONFIG_ENABLE_TEST_SETUP_PARAMS).
#define PAIR_MANUAL "3497-011-2332"
#define PAIR_QR     "MT:Y.K9042C00KA0648G00"

static volatile int64_t s_last_activity_us;
static volatile bool s_uploading;
#define TOUCH() (s_last_activity_us = esp_timer_get_time())

// ------------------------------------------------------------------ utilidades
static void out(httpd_req_t *r, const char *s) { httpd_resp_sendstr_chunk(r, s); }

static void outf(httpd_req_t *r, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void outf(httpd_req_t *r, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    httpd_resp_sendstr_chunk(r, buf);
}

static void esc(httpd_req_t *r, const char *s)   // escapa HTML para texto y atributos
{
    char buf[128];
    size_t j = 0;
    for (; *s; s++) {
        const char *rep = nullptr;
        switch (*s) {
        case '&': rep = "&amp;"; break;
        case '<': rep = "&lt;"; break;
        case '>': rep = "&gt;"; break;
        case '"': rep = "&quot;"; break;
        case '\'': rep = "&#39;"; break;
        }
        size_t need = rep ? strlen(rep) : 1;
        if (j + need >= sizeof(buf)) { buf[j] = 0; out(r, buf); j = 0; }
        if (rep) { memcpy(buf + j, rep, need); j += need; } else buf[j++] = *s;
    }
    buf[j] = 0;
    if (j) out(r, buf);
}

static void urldecode(char *s)
{
    char *w = s;
    for (; *s; s++) {
        if (*s == '+') *w++ = ' ';
        else if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
            char h[3] = {s[1], s[2], 0};
            *w++ = (char)strtol(h, nullptr, 16);
            s += 2;
        } else *w++ = *s;
    }
    *w = 0;
}

static void trim(char *s)
{
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
}

static bool form_get(const char *body, const char *key, char *dst, size_t len)
{
    if (httpd_query_key_value(body, key, dst, len) != ESP_OK) return false;
    urldecode(dst);
    return true;
}

static bool form_has(const char *body, const char *key)
{
    char tmp[8];
    esp_err_t e = httpd_query_key_value(body, key, tmp, sizeof(tmp));
    return e == ESP_OK || e == ESP_ERR_HTTPD_RESULT_TRUNC;
}

static esp_err_t read_body(httpd_req_t *r, char *buf, size_t max)
{
    if (r->content_len == 0 || r->content_len >= max) return ESP_FAIL;
    size_t got = 0;
    while (got < r->content_len) {
        int n = httpd_req_recv(r, buf + got, r->content_len - got);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) return ESP_FAIL;
        got += n;
    }
    buf[got] = 0;
    return ESP_OK;
}

enum { ACT_MATTER, ACT_FACTORY, ACT_REBOOT };
static void action_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(1500));   // deja que el navegador reciba la respuesta
    switch ((int)(intptr_t)arg) {
    case ACT_MATTER:  app_restart_to_matter(); break;
    case ACT_FACTORY: app_factory_reset();     break;
    default:          esp_restart();           break;
    }
    vTaskDelete(nullptr);
}
static void schedule(int act) { xTaskCreate(action_task, "act", 4096, (void *)(intptr_t)act, 5, nullptr); }

// ------------------------------------------------------------------ página
static void render_page(httpd_req_t *r, const char *error)
{
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    out(r, PAGE_HEAD);

    out(r, "<header><div><h1>");
    esc(r, g_cfg.device_name);
    outf(r, "</h1><p>Luz C6 · %s · v%s</p></div>", device_id(), FW_VERSION_STR);
    out(r, "<button class=lamp id=lamp data-on=0 type=button onclick=\"rel('POST')\"><i></i>Probar relé</button></header>");

    if (error) {
        out(r, "<div class=err role=alert>");
        esc(r, error);
        out(r, "</div>");
    }

    out(r, "<form method=post action=/save>");

    out(r, "<fieldset><legend>Dispositivo</legend><label>Nombre<input name=device_name maxlength=32 value=\"");
    esc(r, g_cfg.device_name);
    out(r, "\"></label><label>Estado del relé al arrancar<select name=boot_state>");
    static const char *opts[] = {"Apagado", "Encendido", "El último que tenía"};
    for (int i = 0; i < 3; i++) outf(r, "<option value=%d%s>%s</option>", i, g_cfg.boot_state == i ? " selected" : "", opts[i]);
    out(r, "</select></label><label>Contraseña de este punto de acceso<input name=ap_pass minlength=8 maxlength=63 value=\"");
    esc(r, g_cfg.ap_pass);
    out(r, "\"></label><p class=hint>Se usará la próxima vez que se abra el portal (8 a 63 caracteres).</p></fieldset>");

    out(r, "<fieldset><legend>Matter sobre Thread</legend>"
           "<p class=hint>Para emparejar, abre tu app de Matter (Home Assistant, Apple Home, Google Home…) "
           "y empareja por Bluetooth con este código:</p>"
           "<p>Código manual: <code>" PAIR_MANUAL "</code><br>Código QR: <code>" PAIR_QR "</code></p>"
           "<label>Dataset operativo de Thread (opcional)"
           "<textarea name=dataset spellcheck=false autocapitalize=off placeholder=\"0e080000000000010000…\"></textarea></label>"
           "<p class=hint>Déjalo vacío para emparejar por Bluetooth. Si lo rellenas, el módulo se une él solo a esa red Thread.");
    if (g_cfg.thread_dataset[0]) out(r, " Hay un dataset guardado pendiente de aplicar.");
    out(r, "</p>");
    if (g_cfg.thread_dataset[0]) out(r, "<label class=chk><input type=checkbox name=clear_dataset>Borrar el dataset guardado</label>");
    out(r, "</fieldset>");

    out(r, "<fieldset><legend>MQTT (opcional)</legend>"
           "<p class=hint>Sin servidor, MQTT queda desactivado. Con Thread, el broker debe ser alcanzable por IPv6 o a través del NAT64 de tu Border Router.</p>"
           "<label>Servidor<input name=mqtt_host maxlength=63 autocapitalize=off placeholder=\"broker.local\" value=\"");
    esc(r, g_cfg.mqtt_host);
    outf(r, "\"></label><label>Puerto<input name=mqtt_port type=number min=1 max=65535 value=%u></label>"
            "<label>Usuario<input name=mqtt_user maxlength=47 autocapitalize=off value=\"", (unsigned)g_cfg.mqtt_port);
    esc(r, g_cfg.mqtt_user);
    outf(r, "\"></label><label>Contraseña<input name=mqtt_pass type=password maxlength=63 placeholder=\"%s\"></label>",
         g_cfg.mqtt_pass[0] ? "(sin cambios)" : "");
    outf(r, "<label class=chk><input type=checkbox name=mqtt_tls%s>Conexión cifrada (TLS)</label>", g_cfg.mqtt_tls ? " checked" : "");
    out(r, "<label>Tema base<input name=mqtt_base maxlength=63 autocapitalize=off placeholder=\"luz-c6/");
    esc(r, device_id());
    out(r, "\" value=\"");
    esc(r, g_cfg.mqtt_base);
    outf(r, "\"></label><label class=chk><input type=checkbox name=mqtt_ha%s>Publicar descubrimiento para Home Assistant</label>",
         g_cfg.mqtt_ha ? " checked" : "");
    out(r, "<p class=hint>Temas: <code>&lt;base&gt;/state</code>, <code>&lt;base&gt;/set</code> (ON, OFF, TOGGLE), "
           "<code>&lt;base&gt;/availability</code> y <code>&lt;base&gt;/ota/set</code> (URL de un .bin).</p></fieldset>");

    out(r, "<button class=go type=submit>Guardar y reiniciar</button></form>"
           "<form method=post action=/continue><button class=sec type=submit>Continuar sin guardar</button></form>");

    out(r, "<fieldset style=\"margin-top:20px\"><legend>Actualizar firmware</legend>"
           "<form id=ota><label>Archivo <code>luz_c6.bin</code> (no el .ota de Matter)"
           "<input id=fw type=file accept=\".bin\"></label>"
           "<button class=sec type=submit>Subir y reiniciar</button><p class=hint id=pg></p></form></fieldset>");

    out(r, "<form method=post action=/factory onsubmit=\"return confirm('¿Borrar todos los ajustes y el emparejamiento de Matter?')\">"
           "<button class=\"sec danger\" type=submit>Restaurar de fábrica</button></form>");

    out(r, PAGE_SCRIPT);
    httpd_resp_sendstr_chunk(r, nullptr);
}

static esp_err_t page_message(httpd_req_t *r, const char *title, const char *text)
{
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    out(r, PAGE_HEAD);
    outf(r, "<header><div><h1>%s</h1></div></header><fieldset><p>%s</p></fieldset></main></body></html>", title, text);
    httpd_resp_sendstr_chunk(r, nullptr);
    return ESP_OK;
}

// ------------------------------------------------------------------ manejadores
static esp_err_t h_root(httpd_req_t *r) { TOUCH(); render_page(r, nullptr); return ESP_OK; }

static esp_err_t h_relay(httpd_req_t *r)
{
    TOUCH();
    if (r->method == HTTP_POST) relay_toggle("portal");
    httpd_resp_set_type(r, "text/plain");
    httpd_resp_sendstr(r, relay_get() ? "ON" : "OFF");
    return ESP_OK;
}

static bool is_hex(const char *s)
{
    for (; *s; s++) if (!isxdigit((unsigned char)*s)) return false;
    return true;
}

static esp_err_t h_save(httpd_req_t *r)
{
    TOUCH();
    const size_t MAX = 3072;
    char *body = (char *)malloc(MAX);
    char *v = (char *)malloc(640);
    if (!body || !v) { free(body); free(v); httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "Sin memoria"); return ESP_FAIL; }
    if (read_body(r, body, MAX) != ESP_OK) {
        free(body); free(v);
        httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Formulario no válido");
        return ESP_FAIL;
    }

    luz_settings_t n = g_cfg;       // trabajamos sobre una copia y solo se confirma si todo es válido
    const char *err = nullptr;

    if (form_get(body, "device_name", v, 64)) { trim(v); if (v[0]) strlcpy(n.device_name, v, sizeof(n.device_name)); }
    if (form_get(body, "boot_state", v, 8)) { int b = atoi(v); n.boot_state = (b >= 0 && b <= 2) ? b : 2; }
    if (form_get(body, "ap_pass", v, 100)) {
        size_t l = strlen(v);
        if (l >= 8 && l <= 63) strlcpy(n.ap_pass, v, sizeof(n.ap_pass));
        else if (l != 0) err = "La contraseña del punto de acceso debe tener entre 8 y 63 caracteres.";
    }

    if (form_has(body, "clear_dataset")) n.thread_dataset[0] = 0;
    if (form_get(body, "dataset", v, 640)) {
        char *w = v;                                   // admite espacios y ':' al pegar
        for (char *p = v; *p; p++) if (!isspace((unsigned char)*p) && *p != ':') *w++ = *p;
        *w = 0;
        if (v[0]) {
            size_t l = strlen(v);
            if (l % 2 || l > 508 || !is_hex(v)) err = "El dataset de Thread debe ser hexadecimal (máx. 254 bytes).";
            else strlcpy(n.thread_dataset, v, sizeof(n.thread_dataset));
        }
    }

    if (form_get(body, "mqtt_host", v, 100)) { trim(v); strlcpy(n.mqtt_host, v, sizeof(n.mqtt_host)); }
    if (form_get(body, "mqtt_port", v, 16)) { int p = atoi(v); n.mqtt_port = (p > 0 && p < 65536) ? p : 1883; }
    if (form_get(body, "mqtt_user", v, 100)) { trim(v); strlcpy(n.mqtt_user, v, sizeof(n.mqtt_user)); }
    if (form_get(body, "mqtt_pass", v, 100) && v[0]) strlcpy(n.mqtt_pass, v, sizeof(n.mqtt_pass));
    n.mqtt_tls = form_has(body, "mqtt_tls");
    n.mqtt_ha = form_has(body, "mqtt_ha");
    if (form_get(body, "mqtt_base", v, 100)) {
        trim(v);
        bool ok = v[0] != '/';
        for (char *p = v; *p && ok; p++) ok = isalnum((unsigned char)*p) || strchr("/_-.", *p);
        if (!ok) err = "El tema base solo admite letras, números y los signos / _ - . (sin barra inicial).";
        else strlcpy(n.mqtt_base, v, sizeof(n.mqtt_base));
    }
    if (!n.mqtt_host[0]) { n.mqtt_pass[0] = 0; n.mqtt_user[0] = 0; }   // sin servidor no guardamos credenciales

    free(body); free(v);
    if (err) {
        httpd_resp_set_status(r, "400 Bad Request");
        render_page(r, err);
        return ESP_OK;
    }

    g_cfg = n;
    g_cfg.setup_done = true;
    g_cfg.force_portal = false;
    if (!settings_save()) {
        httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "No se pudo guardar");
        return ESP_FAIL;
    }
    page_message(r, "Guardado", "El módulo se reinicia en modo Matter. Ya puedes cerrar esta página.");
    schedule(ACT_MATTER);
    return ESP_OK;
}

static esp_err_t h_continue(httpd_req_t *r)
{
    TOUCH();
    page_message(r, "Reiniciando", "El módulo arranca en modo Matter con los ajustes actuales.");
    schedule(ACT_MATTER);
    return ESP_OK;
}

static esp_err_t h_factory(httpd_req_t *r)
{
    TOUCH();
    page_message(r, "Restaurando", "Se borran todos los ajustes y el módulo se reinicia.");
    schedule(ACT_FACTORY);
    return ESP_OK;
}

static esp_err_t h_ota(httpd_req_t *r)
{
    TOUCH();
    const esp_partition_t *part = esp_ota_get_next_update_partition(nullptr);
    if (!part || r->content_len == 0 || r->content_len > part->size) {
        httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Archivo vacío o demasiado grande");
        return ESP_FAIL;
    }
    s_uploading = true;
    led_set_ota(true);
    esp_ota_handle_t h = 0;
    char *buf = (char *)malloc(2048);
    esp_err_t err = buf ? esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &h) : ESP_ERR_NO_MEM;
    size_t left = r->content_len;
    while (err == ESP_OK && left > 0) {
        int n = httpd_req_recv(r, buf, left < 2048 ? left : 2048);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) { err = ESP_FAIL; break; }
        err = esp_ota_write(h, buf, n);
        left -= n;
        TOUCH();
    }
    free(buf);
    if (err == ESP_OK) err = esp_ota_end(h);
    else if (h) esp_ota_abort(h);
    if (err == ESP_OK) err = esp_ota_set_boot_partition(part);

    s_uploading = false;
    if (err != ESP_OK) {
        led_set_ota(false);
        ESP_LOGE(TAG, "OTA por web fallida: %s", esp_err_to_name(err));
        httpd_resp_set_status(r, "400 Bad Request");
        httpd_resp_sendstr(r, "Imagen no válida: sube luz_c6.bin (aplicación), no el .ota de Matter.");
        return ESP_OK;
    }
    httpd_resp_sendstr(r, "OK");
    schedule(ACT_REBOOT);
    return ESP_OK;
}

static esp_err_t h_redirect(httpd_req_t *r)   // portal cautivo: cualquier otra ruta va a la página principal
{
    httpd_resp_set_status(r, "302 Found");
    httpd_resp_set_hdr(r, "Location", "http://192.168.4.1/");
    httpd_resp_send(r, nullptr, 0);
    return ESP_OK;
}

static void http_start()
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;
    cfg.max_uri_handlers = 12;
    cfg.lru_purge_enable = true;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.recv_wait_timeout = 20;
    cfg.send_wait_timeout = 20;

    httpd_handle_t srv = nullptr;
    if (httpd_start(&srv, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo iniciar el servidor HTTP");
        return;
    }
    // El orden importa: las rutas concretas antes que el comodín.
    static const httpd_uri_t uris[] = {
        {"/",         HTTP_GET,  h_root,     nullptr},
        {"/relay",    HTTP_GET,  h_relay,    nullptr},
        {"/relay",    HTTP_POST, h_relay,    nullptr},
        {"/save",     HTTP_POST, h_save,     nullptr},
        {"/continue", HTTP_POST, h_continue, nullptr},
        {"/factory",  HTTP_POST, h_factory,  nullptr},
        {"/ota",      HTTP_POST, h_ota,      nullptr},
        {"/*",        HTTP_GET,  h_redirect, nullptr},
    };
    for (const auto &u : uris) httpd_register_uri_handler(srv, &u);
}

// ------------------------------------------------------------------ DNS cautivo
static void dns_task(void *)
{
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (s < 0 || bind(s, (sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "DNS cautivo no disponible");
        vTaskDelete(nullptr);
    }
    static const uint8_t answer[16] = {0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, 192, 168, 4, 1};
    uint8_t buf[300];
    for (;;) {
        sockaddr_in from;
        socklen_t fl = sizeof(from);
        int n = recvfrom(s, buf, sizeof(buf) - 16, 0, (sockaddr *)&from, &fl);
        if (n < 12) continue;
        int i = 12;
        while (i < n && buf[i]) i += buf[i] + 1;
        i += 5;                                   // cero final + QTYPE + QCLASS
        if (i > n) continue;
        uint16_t qtype = (buf[i - 4] << 8) | buf[i - 3];
        buf[2] = 0x81; buf[3] = 0x80;             // respuesta estándar, sin error
        buf[6] = 0; buf[7] = (qtype == 1) ? 1 : 0;  // solo respondemos a consultas A
        buf[8] = buf[9] = buf[10] = buf[11] = 0;
        int len = i;
        if (qtype == 1) { memcpy(buf + i, answer, 16); len += 16; }
        sendto(s, buf, len, 0, (sockaddr *)&from, fl);
    }
}

// ------------------------------------------------------------------ Wi-Fi AP
static void wifi_ap_start()
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *ap = esp_netif_create_default_wifi_ap();

    wifi_init_config_t icfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&icfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    wifi_config_t wc = {};
    snprintf((char *)wc.ap.ssid, sizeof(wc.ap.ssid), "%s%s", CONFIG_LUZ_AP_SSID_PREFIX, device_id());
    wc.ap.ssid_len = strlen((char *)wc.ap.ssid);
    wc.ap.channel = 1;
    wc.ap.max_connection = 4;
    if (strlen(g_cfg.ap_pass) >= 8) {
        strlcpy((char *)wc.ap.password, g_cfg.ap_pass, sizeof(wc.ap.password));
        wc.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        wc.ap.authmode = WIFI_AUTH_OPEN;
    }
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wc));

    // El servidor DHCP debe anunciar al propio módulo como DNS para que salte el portal cautivo.
    esp_netif_ip_info_t ip;
    esp_netif_get_ip_info(ap, &ip);
    esp_netif_dns_info_t dns = {};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4.addr = ip.ip.addr;
    uint8_t offer = 1;   // OFFER_DNS
    esp_netif_dhcps_stop(ap);
    esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer, sizeof(offer));
    esp_netif_set_dns_info(ap, ESP_NETIF_DNS_MAIN, &dns);
    esp_netif_dhcps_start(ap);

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "AP activo: SSID \"%s\", contraseña %s", wc.ap.ssid,
             wc.ap.authmode == WIFI_AUTH_OPEN ? "(abierto)" : "configurada");
}

static void idle_task(void *)
{
    const int64_t limit = (int64_t)CONFIG_LUZ_PORTAL_IDLE_MIN * 60 * 1000000;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        if (!s_uploading && esp_timer_get_time() - s_last_activity_us > limit) {
            ESP_LOGI(TAG, "Portal inactivo %d min: vuelvo al modo Matter", CONFIG_LUZ_PORTAL_IDLE_MIN);
            app_restart_to_matter();
        }
    }
}

void portal_run()
{
    led_set_mode(LED_PORTAL);
    TOUCH();
    wifi_ap_start();
    http_start();
    xTaskCreate(dns_task, "dns", 4096, nullptr, 4, nullptr);
    xTaskCreate(idle_task, "portal_idle", 3072, nullptr, 3, nullptr);
}
