#!/usr/bin/env bash
# Instala ESP-IDF 5.5.5 y esp-matter release/v1.6 en $ESP_ROOT (por defecto ~/esp).
# Reproduce los pasos con los que Espressif construye su imagen Docker, pero sin depender de Docker Hub.
set -eo pipefail

ESP_ROOT="${ESP_ROOT:-$HOME/esp}"
IDF_REF="${IDF_REF:-v5.5.5}"
MATTER_REF="${MATTER_REF:-release/v1.6}"

mkdir -p "$ESP_ROOT"
cd "$ESP_ROOT"

echo "::group::ESP-IDF $IDF_REF"
git clone --depth=1 --branch="$IDF_REF" --recurse-submodules --shallow-submodules --jobs 8 \
    https://github.com/espressif/esp-idf.git esp-idf
( cd esp-idf && ./install.sh --disable-gdbgui --disable-pytest --disable-ci --disable-docs esp32c6 )
echo "::endgroup::"

echo "::group::esp-matter $MATTER_REF"
mkdir -p esp-matter
cd esp-matter
git init -q
git remote add origin https://github.com/espressif/esp-matter.git
git fetch --depth=1 origin "$MATTER_REF"
git checkout -q FETCH_HEAD
git submodule update --init --depth=1
( cd connectedhomeip/connectedhomeip && ./scripts/checkout_submodules.py --platform esp32 linux --shallow )
echo "::endgroup::"

echo "::group::Instalar entorno de esp-matter"
export IDF_PATH="$ESP_ROOT/esp-idf"
export IDF_PATH_FORCE=1
. "$IDF_PATH/export.sh"
./install.sh --no-host-tool
echo "::endgroup::"
