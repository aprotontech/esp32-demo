
#include <stdio.h>

#include "esp_event.h"
#include "spi_flash_mmap.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_chip_info.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "quark/quark.h"
#include "test.h"

#define DM_TAG "demo"

#define DEMO_EXCEPT_SUCCESS(expr)                                              \
    {                                                                          \
        int __err = (expr);                                                    \
        if (__err != 0) {                                                      \
            LOGW(DM_TAG, "check expr=%s, failed with error=%d", #expr, __err); \
            return;                                                            \
        }                                                                      \
    }

int demo_on_wifi_status_change(int connected) {
    static int access = 0;
    if (connected && !access) {  // wifi status connected
        access = 1;

        char ip[16] = {0};
        rc_get_wifi_local_ip(ip);
        LOGI(DM_TAG, "Local Ip: %s", ip);

        rc_thread_create(test_camera, NULL, NULL);
    }

    return 0;
}

void app_main(void) {
    int i = 0;

    /* Print chip information */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    printf("This is ESP32 chip with %d CPU cores, WiFi%s%s, ", chip_info.cores,
           (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

    printf("silicon revision %d, ", chip_info.revision);

    uint32_t size_flash_chip;
    esp_flash_get_size(NULL, &size_flash_chip);
    printf("%dMB %s flash\n", (int)(size_flash_chip / (1024 * 1024)),
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded"
                                                         : "external");
    rc_sleep(1000);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    rc_settings_t settings;

    LOGI(DM_TAG, "quark sdk version: %s", rc_sdk_version());

    rc_settings_init(&settings);
    settings.wifi_status_callback = demo_on_wifi_status_change;
    settings.app_id = "test";
    settings.app_secret = "7045298f456cea6d7a4737c62dd3b89e";
    settings.uuid = NULL;
    settings.enable_keepalive = 1;
    settings.auto_report_location = 1;
    settings.iot_platform = RC_IOT_QUARK;
    settings.service_url = "http://home.aproton.tech/api";

    // init sdk
    DEMO_EXCEPT_SUCCESS(rc_sdk_init("test", 1, &settings));

    // get wifi status, if is not connected, do connect
    for (i = 5; i >= 0; i--) {
        LOGI(DM_TAG, "Start to connect to wifi in %d seconds...", i);

        rc_sleep(1000);
    }

    DEMO_EXCEPT_SUCCESS(rc_set_wifi("aproton", "aproton@2021"));

    // entry working thread
    for (i = 10; i < 60 * 60; ++i) {
        if (i % 60 == 0) {
            LOGI(DM_TAG, "tick in %d seconds...", i);
        }
        rc_sleep(1000);
    }

    DEMO_EXCEPT_SUCCESS(rc_sdk_uninit());

    printf("Restarting now.\n");
    fflush(stdout);
    esp_restart();
}