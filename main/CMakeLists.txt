idf_component_register(
    SRCS "app_main.cpp" "settings.cpp" "io.cpp" "portal.cpp"
         "matter_node.cpp" "mqtt_link.cpp" "ota_url.cpp"
    INCLUDE_DIRS ".")

target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")
