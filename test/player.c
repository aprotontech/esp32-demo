#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "clist.h"
#include "driver/i2s.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "esp_bt.h"
#include "esp_gap_bt_api.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "rc_http_manager.h"
#include "rc_http_request.h"
#include "test.h"

#define WAV_SWAP_SIZE 4096

typedef struct _bt_box_player_t {
    char media_ready;
    char media_stoped;
    char to_send_stop_cmd;

    char finish_download;
    char download_result;

    rc_buf_queue buf_queue;

    int local_buffer_offset;
    rc_thread swap_thread;
    char local_buffer[WAV_SWAP_SIZE];

    char* in_use;
    char* in_swap;
} bt_box_player_t;

bt_box_player_t* _player;

static int _music_offset = 0;

void bt_app_rc_ct_cb(esp_avrc_ct_cb_event_t event,
                     esp_avrc_ct_cb_param_t* param) {
    switch (event) {
    case ESP_AVRC_CT_METADATA_RSP_EVT:
    case ESP_AVRC_CT_CONNECTION_STATE_EVT:
    case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT:
    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT:
    case ESP_AVRC_CT_REMOTE_FEATURES_EVT:
    case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT:
    case ESP_AVRC_CT_SET_ABSOLUTE_VOLUME_RSP_EVT: {
        // bt_app_work_dispatch(bt_av_hdl_avrc_ct_evt, event, param,
        // sizeof(esp_avrc_ct_cb_param_t), NULL);
        break;
    }
    default: LOGW(BT_TAG, "Invalid AVRC event: %d", event); break;
    }
}

void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t* param) {
    LOGI(BT_TAG, "%s evt 0x%x", __func__, event);
    switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT: {
        esp_a2d_cb_param_t* a2d = (esp_a2d_cb_param_t*)(param);
        if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
            LOGI(BT_TAG, "a2dp connected");
            esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE,
                                     ESP_BT_NON_DISCOVERABLE);
            esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY);
        } else if (a2d->conn_stat.state ==
                   ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
            LOGI(BT_TAG, "a2dp disconnected");
        }
        break;
    }
    case ESP_A2D_AUDIO_STATE_EVT:
    case ESP_A2D_AUDIO_CFG_EVT: break;
    case ESP_A2D_MEDIA_CTRL_ACK_EVT: {
        esp_a2d_cb_param_t* a2d = (esp_a2d_cb_param_t*)(param);
        if (a2d->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY) {
            if (a2d->media_ctrl_stat.status == ESP_A2D_MEDIA_CTRL_ACK_SUCCESS) {
                LOGI(BT_TAG,
                     "esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY) "
                     "success");
                // esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
                if (_player != NULL) _player->media_ready = 1;
            }
        } else if (a2d->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_START) {
            if (a2d->media_ctrl_stat.status == ESP_A2D_MEDIA_CTRL_ACK_SUCCESS) {
                LOGI(BT_TAG,
                     "esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START) success");
                if (_player != NULL) _player->media_stoped = 0;
            }

        } else if (a2d->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_STOP) {
            LOGI(BT_TAG, "recv stop command event, status(%d)",
                 a2d->media_ctrl_stat.status);
            // if (_player != NULL) _player->media_stoped = 1;
            if (a2d->media_ctrl_stat.status == ESP_A2D_MEDIA_CTRL_ACK_SUCCESS) {
                LOGI(BT_TAG,
                     "esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_STOP) success");
                if (_player != NULL) _player->media_stoped = 1;
            }
        }
        break;
    }
    default: LOGW(BT_TAG, "%s unhandled evt %d", __func__, event); break;
    }
}

int32_t bt_wav_data_cb_local(uint8_t* data, int32_t len) {
#if (USE_WAV_TYPE == WAV_TYPE_LOCAL)
    if (_music_offset + len > sizeof(sample)) {
        len = sizeof(sample) - _music_offset;
    }

    if (len < 0) len = 0;
    memcpy(data, ((char*)sample) + _music_offset, len);
    _music_offset += len;

#endif
    return len;
}

int32_t bt_wav_data_cb_online_pop_queue(uint8_t* data, int32_t len) {
    bt_box_player_t* player = _player;
    int offset = 0;
    while (offset < len) {
        int rc = rc_buf_queue_pop(player->buf_queue, (char*)data + offset,
                                  len - offset, 100);
        LOGI(BT_TAG, "got buffer %d from queue", rc);
        if (rc > 0) {
            offset += rc;
        } else if (player->finish_download) {  // download finished
            memset((char*)data + offset, 0, len - offset);
            offset = len;
        }
    }

    if (offset > len) {
        LOGW(BT_TAG, "offset > len, stop media");
        esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_STOP);
    }

    return offset;
}

int32_t bt_wav_data_cb_online_one_swap(uint8_t* data, int32_t len) {
    bt_box_player_t* player = _player;

    int buf_offset = player->local_buffer_offset;
    int queue_size = rc_buf_queue_get_size(player->buf_queue);
    LOGI(BT_TAG, "current buffer size=%d", queue_size);
    queue_size = (queue_size / 2) * 2;
    if (buf_offset < WAV_SWAP_SIZE) {
        if (WAV_SWAP_SIZE - buf_offset < len) {
            len = WAV_SWAP_SIZE - buf_offset;
        }
        memcpy(data, &player->local_buffer[buf_offset], len);
        buf_offset += len;
    } else if ((!player->finish_download && queue_size >= WAV_SWAP_SIZE) ||
               (player->finish_download && queue_size > 0)) {
        int pop_size = WAV_SWAP_SIZE;
        if (queue_size < pop_size) {
            pop_size = queue_size;
        }
        int rlen = rc_buf_queue_pop(player->buf_queue, player->local_buffer,
                                    pop_size, 500);
        LOGI(BT_TAG, "get %d buffer size from queue", rlen);
        memset(&player->local_buffer[rlen], 0, WAV_SWAP_SIZE - rlen);
        memcpy(data, player->local_buffer, len);
        buf_offset = len;
    } else {
        if (!rc_buf_queue_is_empty(player->buf_queue) &&
            player->finish_download) {
            rc_buf_queue_pop(player->buf_queue, player->local_buffer,
                             WAV_SWAP_SIZE, 0);
        }
        rc_sleep(500);
        memset(data, 0, len);
    }

    player->local_buffer_offset = buf_offset;

    return len;
}

void* swap_buffer_thread(void* param) {
    bt_box_player_t* player = param;

    static char swap_buffer[WAV_SWAP_SIZE];
    while (!player->media_stoped) {
        if (player->in_swap != NULL) {
            rc_sleep(30);
        } else {
            int offset = 0;

            char* ptr = swap_buffer;
            if (player->in_use == swap_buffer) {
                ptr = player->local_buffer;
            }
            memset(ptr, 0, WAV_SWAP_SIZE);
            while (offset < WAV_SWAP_SIZE) {
                int wait_time = 500;
                if (player->finish_download) {
                    wait_time = 0;
                }

                offset += rc_buf_queue_pop(player->buf_queue, ptr + offset,
                                           WAV_SWAP_SIZE - offset, wait_time);
                LOGI(BT_TAG, "backend buffer(%p) current is %d bytes", ptr,
                     offset);
                if (player->finish_download) {
                    break;
                }
            }

            player->in_swap = ptr;  // fill swap buffer
        }
    }

    LOGI(BT_TAG, "swap_buffer_thread stoped");
    return NULL;
}

int32_t bt_wav_data_cb_online_two_swap(uint8_t* data, int32_t len) {
    bt_box_player_t* player = _player;

    int buf_offset = player->local_buffer_offset;
    if (buf_offset < WAV_SWAP_SIZE) {
        if (WAV_SWAP_SIZE - buf_offset < len) {
            len = WAV_SWAP_SIZE - buf_offset;
        }
        memcpy(data, &player->in_use[buf_offset], len);
        buf_offset += len;
        LOGI(BT_TAG, "from buffer (%p) data(%d), left(%d)", player->in_use, len,
             WAV_SWAP_SIZE - buf_offset);
    } else if (player->in_swap != NULL) {  // has new buffer
        player->in_use = player->in_swap;
        player->in_swap = NULL;
        memcpy(data, player->in_use, len);
        buf_offset = len;
        LOGI(BT_TAG, "swap buffer (%p) data(%d), left(%d)", player->in_use, len,
             WAV_SWAP_SIZE - buf_offset);
    } else {
        LOGI(BT_TAG, "no buffer found");
        rc_sleep(100);
        memset(data, 0, len);
    }

    player->local_buffer_offset = buf_offset;

    if (player->to_send_stop_cmd) {
        LOGI(BT_TAG, "stop play music");
        esp_err_t err = esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_STOP);
        LOGI(BT_TAG, "send stop command finish with err:[%d] %s", err,
             esp_err_to_name(err));
    }

    return len;
}

int init_bt_palyer() {
    /*
     * Set default parameters for Legacy Pairing
     * Use variable pin, input pin code when pairing
     */
    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_VARIABLE;
    esp_bt_pin_code_t pin_code;
    esp_bt_gap_set_pin(pin_type, 0, pin_code);

    esp_avrc_ct_init();
    esp_avrc_ct_register_callback(bt_app_rc_ct_cb);

    esp_avrc_rn_evt_cap_mask_t evt_set = {0};
    esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &evt_set,
                                       ESP_AVRC_RN_VOLUME_CHANGE);
    assert(esp_avrc_tg_set_rn_evt_cap(&evt_set) == ESP_OK);

    /* initialize A2DP source */
    esp_a2d_register_callback(&bt_app_a2d_cb);
#if (USE_WAV_TYPE == WAV_TYPE_LOCAL)
    esp_a2d_source_register_data_callback(bt_wav_data_cb_local);
#elif (USE_WAV_TYPE == WAV_TYPE_ONLINE_POP_QUEUE)
    esp_a2d_source_register_data_callback(bt_wav_data_cb_online_pop_queue);
#elif (USE_WAV_TYPE == WAV_TYPE_ONLINE_ONE_SWAP)
    esp_a2d_source_register_data_callback(bt_wav_data_cb_online_one_swap);
#elif (USE_WAV_TYPE == WAV_TYPE_ONLINE_TWO_SWAP)
    esp_a2d_source_register_data_callback(bt_wav_data_cb_online_two_swap);
#else
    extern int32_t get_data(uint8_t * data, int32_t len);
    esp_a2d_source_register_data_callback(get_data);
#endif
    esp_a2d_source_init();

    return 0;
}

rc_buf_t* move_link_list(list_link_t* head) {
    list_link_t* p = head->next;

    if (LL_isspin(head))
        return NULL;
    else {
        LL_remove(head);
    }
    return (rc_buf_t*)p;
}

void* download_thread(void* param) {
    bt_box_player_t* player = (bt_box_player_t*)param;

    const char* url = "http://82.157.138.167/test-esp32.wav";

    LOGI(BT_TAG, "try to query url %s", url);

    int timeout = 10 * 1000;

    rc_downloader downloader = rc_downloader_init(
        url, NULL, 0, timeout, DOWNLOAD_TO_BUF_QUEUE, player->buf_queue);

    if (downloader == NULL) {
        LOGI(BT_TAG, "create download failed");
        return NULL;
    }

    rc_downloader_start(downloader, 0);
    LOGI(BT_TAG, "all body had recved");

    rc_downloader_uninit(downloader);

    player->finish_download = 1;

    LOGI(BT_TAG, "download_thread stoped");
    return NULL;
}

int play_online_music(bt_box_player_t* player) {
    LOGI(BT_TAG, "prepare play online music");
    player->finish_download = 0;
    player->download_result = -1;
    player->to_send_stop_cmd = 0;
    _player->media_stoped = 0;
    player->in_use = NULL;
    player->in_swap = NULL;
    player->local_buffer_offset = WAV_SWAP_SIZE;

    rc_buf_queue_clean(player->buf_queue);

    // query wav from url
    rc_thread dt = rc_thread_create(download_thread, player, NULL);

    if (USE_WAV_TYPE == WAV_TYPE_ONLINE_TWO_SWAP) {
        player->swap_thread =
            rc_thread_create(swap_buffer_thread, _player, NULL);
    }

    while (!rc_buf_queue_is_full(player->buf_queue)) {
        rc_sleep(300);
    }

    // paly music
    LOGI(BT_TAG, "start to play music");
    esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);

    int max_times = 30;
    while (max_times-- > 0 && !(_player->finish_download &&
                                rc_buf_queue_is_empty(player->buf_queue))) {
        // wait download and play finished
        rc_sleep(1000);
    }

    LOGI(BT_TAG, "play finished");
    rc_sleep(2000);

    player->to_send_stop_cmd = 1;
    LOGI(BT_TAG, "stop play music");

    rc_sleep(1000);

    while (!rc_buf_queue_is_empty(player->buf_queue)) {
        rc_buf_queue_pop(player->buf_queue, player->local_buffer, WAV_SWAP_SIZE,
                         100);
    }

    if (_player->finish_download) {
        // rc_thread_join(dt);
    }

    LOGI(BT_TAG, "finish play online music");

    return 0;
}

int play_local_music() {
#if (USE_WAV_TYPE == WAV_TYPE_LOCAL)
    _music_offset = 0;

    esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);

    while (_music_offset < MUSIC_LEN) {
        rc_sleep(100);
    }

    rc_sleep(2000);
    esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_STOP);
#endif
    return 0;
}

int connect_to_bt_player(esp_bd_addr_t bda) {
    bt_box_player_t* player =
        (bt_box_player_t*)rc_malloc(sizeof(bt_box_player_t));
    memset(player, 0, sizeof(bt_box_player_t));
    _player = player;

    esp_a2d_source_connect(bda);

    // wait for wifi connected
    while (true) {
        int connected = 0;
        if (rc_get_wifi_status(&connected) == 0 && connected) {
            break;
        }
        rc_sleep(1000);
    }

    while (!_player->media_ready) {
        rc_sleep(1000);
    }

    player->buf_queue = rc_buf_queue_init(WAV_SWAP_SIZE, 3, WAV_HEADER_BYTES);

    while (true) {
#if (USE_WAV_TYPE == WAV_TYPE_LOCAL)
        play_local_music();
#elif (USE_WAV_TYPE == WAV_TYPE_ONLINE_ONE_SWAP) || \
    (USE_WAV_TYPE == WAV_TYPE_ONLINE_TWO_SWAP)
        play_online_music(player);
#else
        esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);

        rc_sleep(10 * 1000);
        esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_STOP);
#endif
        rc_sleep(120 * 1000);
    }

    esp_a2d_source_disconnect(bda);

    rc_buf_queue_uninit(player->buf_queue);

    rc_free(player);
    _player = NULL;

    return 0;
}