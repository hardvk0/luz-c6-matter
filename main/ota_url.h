#pragma once
// Actualización OTA descargando un .bin de aplicación (no el .ota de Matter) desde una URL http(s).
// Devuelve false si ya hay una actualización en curso.
bool ota_url_start(const char *url);
