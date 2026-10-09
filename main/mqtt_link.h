#pragma once
// Cliente MQTT opcional: se activa solo si hay un host configurado.
//   <base>/state         ON|OFF   (retenido)
//   <base>/set           ON|OFF|TOGGLE
//   <base>/availability  online|offline (LWT, retenido)
//   <base>/ota/set       URL de un .bin de aplicación
void mqtt_link_start();
bool mqtt_link_connected();
