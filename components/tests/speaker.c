#include "driver/i2s.h"
#include "test.h"

void play_random_sound(int times) {
    extern int32_t get_data(uint8_t * data, int32_t len);
    char buff[512];

    while (times-- > 0) {
        get_data((uint8_t*)buff, sizeof(buff));
        size_t bytes_written = 0;
        i2s_write(0, buff, sizeof(buff), &bytes_written, portMAX_DELAY);
        // LOGI(BT_TAG, "write to speaker %d bytes", bytes_written);
    }
}

void test_spearker(void* pvParameters) {
    rc_sleep(1000);

    // wait for wifi connected
    while (true) {
        int connected = 0;
        if (rc_get_wifi_status(&connected) == 0 && connected) {
            break;
        }
        rc_sleep(1000);
    }

    // play_random_sound();
    // return;

    rc_buf_queue queue = rc_buf_queue_init(4096, 3, WAV_HEADER_BYTES);
    assert(queue != NULL);

    rc_player player = rc_player_init(queue, NULL);

    // const char* url = "http://82.157.138.167/test-16k-2ch-16bit.wav";
    const char* url = WAV_URL;
    for (int i = 0; i < 20; ++i) {
        LOGI(BT_TAG, "start to play noise");
        play_random_sound(200);

        rc_player_restart(player, 0, 16, 16000, 2);

        LOGI(BT_TAG, "start player music");
        rc_downloader downloader = rc_downloader_init(
            url, NULL, 0, 10000, DOWNLOAD_TO_BUF_QUEUE, queue);
        assert(downloader != NULL);
        rc_downloader_start(downloader, 0);

        int total, current;
        rc_downloader_get_status(downloader, &total, &current);
        LOGI(BT_TAG, "wav size total(%d), current(%d)", total, current);

        rc_downloader_uninit(downloader);

        rc_sleep(30 * 1000);
    }

    LOGI(BT_TAG, "stop speaker test");

    rc_player_uninit(player);

    rc_sleep(5000);
}