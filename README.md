# Luz C6 — relé como luz Matter (Thread) para ESP32-C6

Firmware para **ESP32-C6-DevKitM**: un relé (GPIO19) que aparece como luz Matter sobre Thread, con botón (GPIO9),
LED de estado (GPIO2), MQTT opcional, OTA y un portal Wi-Fi de configuración. Se compila en línea con GitHub Actions
y se instala desde el navegador.

> **Estado:** la parte que no depende de Matter (portal, MQTT, IO, ajustes, OTA, arranque) se compiló con ESP-IDF 5.5.5
> para ESP32-C6 sin errores ni avisos. La parte Matter (`matter_node.cpp`) está escrita contra las APIs de
> esp-matter `release/v1.6`, pero **no se ha podido compilar ni probar en hardware**. Es probable que el primer
> build en GitHub Actions necesite algún retoque. Ver "Puntos a vigilar".

## Cómo funciona

El módulo arranca en uno de dos modos, **nunca los dos a la vez** (comparten radio y RAM; cambiar de modo reinicia):

| Modo | Cuándo | Qué hace |
|---|---|---|
| **Portal** | Primer arranque; 15 min sin red Thread; pulsación de 8 s | AP Wi-Fi `Luz-C6-xxxxxx` + web de configuración + DNS cautivo. El botón sigue manejando el relé. |
| **Matter** | Resto de casos; tras guardar en el portal; tras 10 min sin actividad en el portal | Nodo Matter sobre Thread (emparejamiento por BLE), MQTT si está configurado. |

El portal incluye: nombre, estado del relé al arrancar, contraseña del AP, dataset Thread opcional, MQTT
(servidor, puerto, usuario, contraseña, TLS, tema base, descubrimiento Home Assistant), subida de firmware y
restauración de fábrica. La contraseña inicial del AP es `luzconfig`.

### Botón (GPIO9)

| Pulsación | Acción |
|---|---|
| Corta (< 1 s) | Conmuta el relé |
| 3 – 8 s | Reabre la ventana de emparejamiento Matter (si no hay ningún controlador emparejado) |
| 8 – 15 s | Reinicia en modo portal |
| ≥ 15 s | Restauración de fábrica |

Mientras mantienes pulsado, el LED indica la zona: fijo (3 s) → parpadeo rápido (8 s) → parpadeo muy rápido (15 s).
Se actúa al soltar. GPIO9 es el pin BOOT: no lo mantengas pulsado al encender/reiniciar.

### LED (GPIO2)

| Patrón | Significado |
|---|---|
| 100 ms cada 2 s | En línea (Thread unido y, si hay MQTT, broker conectado) |
| Doble destello cada 2 s | Thread OK pero MQTT configurado y sin conexión |
| 200 ms cada 1 s | Sin red Thread todavía / esperando emparejar |
| Parpadeo rápido (5 Hz) | Portal activo |
| 500 ms / 500 ms | Actualización OTA en curso |
| Parpadeo muy rápido | Identify de Matter (Home, HA…) |

### MQTT (solo si hay servidor configurado)

`<base>` es el tema base del portal (por defecto `luz-c6/<id>`).

| Tema | Contenido |
|---|---|
| `<base>/state` | `ON` / `OFF` (retenido) |
| `<base>/set` | `ON`, `OFF`, `TOGGLE` |
| `<base>/availability` | `online` / `offline` (LWT, retenido) |
| `<base>/ota/set` | URL de un `.bin` de aplicación → actualiza y reinicia |

Con "descubrimiento Home Assistant" se publica también la configuración retenida en `homeassistant/light/…`.
Si además añades el dispositivo por Matter en Home Assistant, tendrás la luz duplicada: usa una de las dos vías.

## Compilar en línea e instalar (GitHub)

1. Crea un repositorio en GitHub y sube el contenido de esta carpeta (rama `main`).
2. **Settings → Pages → Build and deployment → Source: GitHub Actions**.
3. La pestaña **Actions** compila sola (o ejecuta "Compilar y publicar firmware" a mano). Usa la imagen oficial
   `espressif/esp-matter:release-v1.6` (se libera disco del runner antes de descargarla); tarda 10-20 minutos.
4. Al terminar, tu web de instalación está en `https://<usuario>.github.io/<repo>/`. Ábrela con Chrome o Edge,
   conecta la placa por USB y pulsa **Instalar** (Web Serial; la página necesita HTTPS, que Pages ya da).

Cada compilación publica en esa misma web:

| Archivo | Para qué |
|---|---|
| `luz-c6-factory.bin` | Instalación completa desde el navegador |
| `luz_c6.bin` | OTA por portal o MQTT |
| `luz_c6-ota.bin` | OTA de Matter (hay que servirlo desde un proveedor OTA de Matter) |

El número de versión sube con cada ejecución del workflow (necesario para que Matter acepte la actualización).

## Actualización OTA

1. **Matter:** el nodo trae el OTA Requestor; necesita un proveedor OTA en tu controlador, con `luz_c6-ota.bin`.
2. **MQTT:** publica la URL de `luz_c6.bin` en `<base>/ota/set`. Con Thread, el servidor de esa URL debe ser
   alcanzable por IPv6 o vía NAT64.
3. **Portal:** sube `luz_c6.bin` desde la sección "Actualizar firmware". Debe ser el `.bin` de aplicación, no el `.ota`.

## Emparejamiento Matter

Por BLE desde tu app (Home Assistant, Apple Home, Google Home…) con el código que muestra el portal:
manual `3497-011-2332`, QR `MT:Y.K9042C00KA0648G00`. Son las credenciales de **prueba** de esp-matter (IDs de prueba
`0xFFF1`): sirven para uso personal con Home Assistant y chip-tool; Apple y Google avisan de que el accesorio no
está certificado y Google exige registrar los dispositivos de prueba. Necesitas un Border Router Thread
(HomePod mini, Apple TV, Nest Hub, ZBT-1 con OTBR…).

Opcional: pegar el *dataset* operativo Thread en el portal hace que el módulo se una a esa red sin BLE.

## Compilar en local

```bash
docker run --rm -v "$PWD":/project -w /project espressif/esp-matter:release-v1.6 bash -lc \
  '. $IDF_PATH/export.sh && . $ESP_MATTER_PATH/export.sh && idf.py set-target esp32c6 && idf.py build'
```

Los pines, la polaridad del relé/LED y los tiempos están en `idf.py menuconfig` → **Luz C6**.

## Puntos a vigilar

- **Tamaño:** Matter + Thread + BLE + Wi-Fi AP + servidor web + MQTT/TLS es mucho para una ranura OTA de 1,9 MB
  (4 MB de flash). Mi estimación es que queda justo; el workflow falla con un mensaje claro si no cabe y
  el log del paso de compilación muestra el desglose (`idf.py size`). Si no cabe, lo primero que se recorta es TLS (certificados de MQTT/OTA).
- **MQTT sobre Thread:** el broker debe ser alcanzable por IPv6 o por NAT64 del Border Router
  (`CONFIG_OPENTHREAD_DNS64_CLIENT` está activo). Si no hay NAT64, usa un broker con dirección IPv6.
- **Primera vez sin BLE no hay emparejamiento:** el modo Matter abre BLE 15 min tras arrancar; si no empareja y
  no se une a Thread en 15 min, vuelve al portal (según lo pedido).
- **Pruebas pendientes:** coexistencia BLE/802.15.4 del C6 con el comportamiento real del Border Router,
  reapertura de la ventana de emparejamiento, y OTA de Matter extremo a extremo.
- **Mejoras futuras:** rollback automático de OTA, credenciales Matter únicas por dispositivo.
