idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES "esp32-camera" "esp_http_server" nvs_flash "proton" "tests")