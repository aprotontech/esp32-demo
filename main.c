
#include <stdio.h>

#include "esp_event.h"
#include "esp_spi_flash.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "quark/quark.h"

extern int mock_virtual_device(char* env, char* app_id, char* app_secret,
                               int test_time_sec);
extern void bt_scan_test(void* pvParameters);
extern void test_spearker(void* pvParameters);
extern void test_camera(void* pvParameters);

void app_main(void) {
    printf("Hello world!\n");

    /* Print chip information */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    printf("This is ESP32 chip with %d CPU cores, WiFi%s%s, ", chip_info.cores,
           (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

    printf("silicon revision %d, ", chip_info.revision);

    printf("%dMB %s flash\n", spi_flash_get_chip_size() / (1024 * 1024),
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded"
                                                         : "external");
    rc_sleep(1000);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // TaskFunction_t func = bt_scan_test;
    TaskFunction_t func = test_camera;

    TaskHandle_t xHandle = NULL;
    // xTaskCreate(func, "my-test", 4096, NULL, tskIDLE_PRIORITY, &xHandle);

    // restart every 1hour
    mock_virtual_device("test", "test", "7045298f456cea6d7a4737c62dd3b89e",
                        60 * 60);
    while (1) {
        rc_sleep(60 * 1000);
    }
    printf("Restarting now.\n");
    fflush(stdout);
    esp_restart();
}