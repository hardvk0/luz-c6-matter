cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{ESP_MATTER_PATH})
    message(FATAL_ERROR "Define ESP_MATTER_PATH con la ruta del repositorio esp-matter")
endif()

# Versión: la inyecta el CI (-DCLI_PROJECT_VER=1.0.N -DCLI_PROJECT_VER_NUMBER=N).
# VER_NUMBER debe crecer en cada release para que Matter OTA acepte la imagen.
if(NOT DEFINED CLI_PROJECT_VER)
    set(PROJECT_VER "1.0.0")
else()
    set(PROJECT_VER "${CLI_PROJECT_VER}")
endif()
if(NOT DEFINED CLI_PROJECT_VER_NUMBER)
    set(PROJECT_VER_NUMBER 1)
else()
    set(PROJECT_VER_NUMBER "${CLI_PROJECT_VER_NUMBER}")
endif()

set(ESP_MATTER_PATH $ENV{ESP_MATTER_PATH})
set(MATTER_SDK_PATH ${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(EXTRA_COMPONENT_DIRS
    "${MATTER_SDK_PATH}/config/esp32/components"
    "${ESP_MATTER_PATH}/components")

project(luz_c6)

idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H;-Wno-overloaded-virtual" APPEND)
idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security" APPEND)
