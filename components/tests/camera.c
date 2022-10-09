#include "esp_camera.h"
#include "esp_rom_gpio.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <nvs_flash.h>
#include <string.h>
#include <sys/param.h>

// ESP32-CAM
#define CAM_PIN_PWDN 32
#define CAM_PIN_RESET -1  // software reset will be performed
#define CAM_PIN_XCLK 0    // GPIO
#define CAM_PIN_SIOD 26   // GPIO
#define CAM_PIN_SIOC 27   // GPIO

#define CAM_PIN_D7 35     // GPIO
#define CAM_PIN_D6 34     // GPIO
#define CAM_PIN_D5 39     // GPIO (sensor VN)
#define CAM_PIN_D4 36     // GPIO (sensor VP)
#define CAM_PIN_D3 21     // GPIO
#define CAM_PIN_D2 19     // GPIO
#define CAM_PIN_D1 18     // GPIO
#define CAM_PIN_D0 5      // GPIO
#define CAM_PIN_VSYNC 25  // GPIO
#define CAM_PIN_HREF 23   // GPIO
#define CAM_PIN_PCLK 22   // GPIO

#define CAM_TAG "CAMERA"

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE =
    "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART =
    "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static camera_config_t camera_config = {
    .pin_pwdn = CAM_PIN_PWDN,
    .pin_reset = CAM_PIN_RESET,
    .pin_xclk = CAM_PIN_XCLK,
    .pin_sscb_sda = CAM_PIN_SIOD,
    .pin_sscb_scl = CAM_PIN_SIOC,

    .pin_d7 = CAM_PIN_D7,
    .pin_d6 = CAM_PIN_D6,
    .pin_d5 = CAM_PIN_D5,
    .pin_d4 = CAM_PIN_D4,
    .pin_d3 = CAM_PIN_D3,
    .pin_d2 = CAM_PIN_D2,
    .pin_d1 = CAM_PIN_D1,
    .pin_d0 = CAM_PIN_D0,
    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href = CAM_PIN_HREF,
    .pin_pclk = CAM_PIN_PCLK,

    // XCLK 20MHz or 10MHz for OV2640 double FPS (Experimental)
    .xclk_freq_hz = 10000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG,  // YUV422,GRAYSCALE,RGB565,JPEG
    .frame_size =
        FRAMESIZE_UXGA,  // QQVGA-QXGA Do not use sizes above QVGA when not JPEG

    .jpeg_quality = 12,  // 0-63 lower number means higher quality
    .fb_count =
        1  // if more than one, i2s runs in continuous mode. Use only with JPEG
};

esp_err_t camera_init() {
    // power up the camera if PWDN pin is defined
    if (CAM_PIN_PWDN != -1) {
        esp_rom_gpio_pad_select_gpio(CAM_PIN_PWDN);
        gpio_set_direction(CAM_PIN_PWDN, GPIO_MODE_OUTPUT);
        gpio_set_level(CAM_PIN_PWDN, 0);
    }

    // initialize the camera
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(CAM_TAG, "Camera Init Failed");
        return err;
    }

    return ESP_OK;
}

esp_err_t camera_capture() {
    // acquire a frame
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(CAM_TAG, "Camera Capture Failed");
        return ESP_FAIL;
    }
    // replace this with your own function
    // process_image(fb->width, fb->height, fb->format, fb->buf, fb->len);

    // return the frame buffer back to the driver for reuse
    esp_camera_fb_return(fb);
    return ESP_OK;
}

esp_err_t jpg_stream_httpd_handler(httpd_req_t *req) {
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len;
    uint8_t *_jpg_buf;
    char *part_buf[64];
    static int64_t last_frame = 0;
    if (!last_frame) {
        last_frame = esp_timer_get_time();
    }

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK) {
        return res;
    }

    while (true) {
        fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(CAM_TAG, "Camera capture failed");
            res = ESP_FAIL;
            break;
        }
        if (fb->format != PIXFORMAT_JPEG) {
            bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
            if (!jpeg_converted) {
                ESP_LOGE(CAM_TAG, "JPEG compression failed");
                esp_camera_fb_return(fb);
                res = ESP_FAIL;
            }
        } else {
            _jpg_buf_len = fb->len;
            _jpg_buf = fb->buf;
        }

        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY,
                                        strlen(_STREAM_BOUNDARY));
        }
        if (res == ESP_OK) {
            size_t hlen =
                snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);

            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf,
                                        _jpg_buf_len);
        }
        if (fb->format != PIXFORMAT_JPEG) {
            free(_jpg_buf);
        }
        esp_camera_fb_return(fb);
        if (res != ESP_OK) {
            break;
        }
        int64_t fr_end = esp_timer_get_time();
        int64_t frame_time = fr_end - last_frame;
        last_frame = fr_end;
        frame_time /= 1000;
        ESP_LOGI(CAM_TAG, "MJPG: %uKB %ums (%.1ffps)",
                 (unsigned int)(_jpg_buf_len / 1024), (unsigned int)frame_time,
                 1000.0 / (uint32_t)frame_time);
    }

    last_frame = 0;
    return res;
}

/* Our URI handler function to be called during GET /uri request */
esp_err_t get_handler(httpd_req_t *req) {
    /* Send a simple response */
    const char resp[] = "v1.0";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* URI handler structure for GET /uri */
httpd_uri_t uri_version = {.uri = "/version",
                           .method = HTTP_GET,
                           .handler = get_handler,
                           .user_ctx = NULL};

/* URI handler structure for POST /uri */
httpd_uri_t uri_camera = {.uri = "/camera",
                          .method = HTTP_GET,
                          .handler = jpg_stream_httpd_handler,
                          .user_ctx = NULL};

/* Function for starting the webserver */
httpd_handle_t start_webserver(void) {
    /* Generate default configuration */
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    /* Empty handle to esp_http_server */
    httpd_handle_t server = NULL;

    /* Start the httpd server */
    if (httpd_start(&server, &config) == ESP_OK) {
        /* Register URI handlers */
        httpd_register_uri_handler(server, &uri_version);
        httpd_register_uri_handler(server, &uri_camera);
    }
    /* If server failed to start, handle will be NULL */
    return server;
}

esp_err_t camera_test() {
    ESP_ERROR_CHECK(camera_init());

    httpd_handle_t server = start_webserver();
    while (1) {
        vTaskDelay(1000);
    }

    if (server) {
        httpd_stop(server);
    }
    return ESP_OK;
}

void *test_camera(void *params) {
    //camera_test();
    return NULL;
}