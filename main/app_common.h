#pragma once
// Declaraciones compartidas entre módulos.

enum app_mode_t { MODE_PORTAL, MODE_MATTER };
extern app_mode_t g_mode;

void app_restart_to_portal();   // guarda "forzar portal" y reinicia
void app_restart_to_matter();   // marca configuración hecha y reinicia en modo Matter
void app_factory_reset();       // borra ajustes propios + datos de Matter y reinicia

#include "esp_app_desc.h"
#define FW_VERSION_STR  (esp_app_get_description()->version)
