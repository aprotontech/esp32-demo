
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"

#include "quark/quark.h"

extern int mock_virtual_device(char* env, char* app_id, char* app_secret, int test_time_sec);
extern void bt_scan_test(void * pvParameters);

void app_main(void)
{
    printf("Hello world!\n");

    /* Print chip information */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    printf("This is ESP32 chip with %d CPU cores, WiFi%s%s, ",
            chip_info.cores,
            (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
            (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

    printf("silicon revision %d, ", chip_info.revision);

    printf("%dMB %s flash\n", spi_flash_get_chip_size() / (1024 * 1024),
            (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");     
    rc_sleep(1000);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    TaskHandle_t xHandle = NULL;
    xTaskCreate(bt_scan_test, "bt-test", 5120, NULL, tskIDLE_PRIORITY, &xHandle);

    // restart every 1hour
    mock_virtual_device("test", "83PMU3EF65", "secret", 60 * 60);
/*    while (1) {
        rc_sleep(60 * 1000);
    }*/
    printf("Restarting now.\n");
    fflush(stdout);
    esp_restart();
}