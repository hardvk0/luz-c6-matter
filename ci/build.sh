#!/usr/bin/env bash
# Compila el firmware. Requiere haber ejecutado antes ci/setup.sh.
set -eo pipefail

ESP_ROOT="${ESP_ROOT:-$HOME/esp}"
export IDF_PATH="$ESP_ROOT/esp-idf"
export ESP_MATTER_PATH="$ESP_ROOT/esp-matter"
N="${RUN_NUMBER:-1}"

pushd "$IDF_PATH" >/dev/null && . ./export.sh && popd >/dev/null
pushd "$ESP_MATTER_PATH" >/dev/null && . ./export.sh && popd >/dev/null

cd "$(dirname "$0")/.."

idf.py -DCLI_PROJECT_VER="1.0.${N}" -DCLI_PROJECT_VER_NUMBER="${N}" set-target esp32c6
set +e
idf.py build
RC=$?
set -e
if [ "$RC" -ne 0 ]; then
  echo "::group::Desglose de tamaño por componente"
  idf.py size-components 2>/dev/null | head -45 || true
  echo "::endgroup::"
  exit "$RC"
fi
idf.py size

# Imagen completa para instalar desde el navegador (bootloader + tabla de particiones + app)
( cd build && python -m esptool --chip esp32c6 merge_bin --fill-flash-size 4MB -o luz-c6-factory.bin @flash_args )

# ¿Cabe en la ranura OTA (0x1F0000)?
SIZE=$(stat -c %s build/luz_c6.bin)
LIMIT=$((0x1F0000))
echo "luz_c6.bin: ${SIZE} bytes (límite ${LIMIT})"
if [ "$SIZE" -ge "$LIMIT" ]; then
  echo "::error::El firmware (${SIZE} B) no cabe en la ranura OTA (${LIMIT} B). Hay que recortar."
  exit 1
fi

rm -rf site && mkdir -p site
cp build/luz-c6-factory.bin site/         # instalación desde el navegador
cp build/luz_c6.bin         site/         # OTA por portal / MQTT
cp build/luz_c6-ota.bin     site/         # OTA de Matter
sed "s/__VERSION__/1.0.${N}/" web/manifest.json > site/manifest.json
cp web/index.html site/
echo "{\"version\":\"1.0.${N}\",\"number\":${N}}" > site/version.json
ls -la site
