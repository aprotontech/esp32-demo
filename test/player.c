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

typedef struct _bt_box_player_t {
    int media_ready;

    int finish_download;
    int download_result;

    rc_buf_queue buf_queue;
} bt_box_player_t;

bt_box_player_t* _player;

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
    case ESP_A2D_AUDIO_CFG_EVT:
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
            }

        } else if (a2d->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_STOP) {
            if (a2d->media_ctrl_stat.status == ESP_A2D_MEDIA_CTRL_ACK_SUCCESS) {
                LOGI(BT_TAG,
                     "esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_STOP) success");
            }
        }
        break;
    }
    default: LOGW(BT_TAG, "%s unhandled evt %d", __func__, event); break;
    }
}

int32_t bt_app_a2d_data_cb(uint8_t* data, int32_t len) {
    LOGI(BT_TAG, "bt_app_a2d_data_cb(%d)", len);
    if (len < 0 || data == NULL) {
        return 0;
    }

    if (_player != NULL) {
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
    } else {
        // generate random sequence
        int val = rand() % (1 << 16);
        for (int i = 0; i < (len >> 1); i++) {
            data[(i << 1)] = val & 0xff;
            data[(i << 1) + 1] = (val >> 8) & 0xff;
        }
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
    esp_a2d_source_register_data_callback(bt_app_a2d_data_cb);
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

int on_wav_content(http_request request, int status_code, const char* body,
                   int len) {
    LOGI(BT_TAG, "on_wav_content: %d", len);
    bt_box_player_t* player = (bt_box_player_t*)http_request_get_data(request);

    int offset = 0;
    while (offset < len) {
        int rc = rc_buf_queue_push(player->buf_queue, body + offset,
                                   len - offset, 1000);
        LOGI(BT_TAG, "push buffer %d to queue", rc);
        if (rc > 0) {
            offset += rc;
        }
    }

    return 0;
}

int download_wav_file(bt_box_player_t* player) {
    const char* url = "http://82.157.138.167/test-esp32.wav";

    http_manager mgr = http_manager_init();

    LOGI(BT_TAG, "try to query url %s", url);

    http_request request =
        http_request_init(mgr, url, "82.157.138.167", HTTP_REQUEST_GET);
    if (request == NULL) {
        http_manager_uninit(mgr);
        return -1;
    }

    int timeout = 3 * 1000;
    http_request_set_opt(request, HTTP_REQUEST_OPT_TIMEOUT, &timeout);
    http_request_set_opt(request, HTTP_REQUEST_OPT_RES_CHUNK_CALLBACK,
                         on_wav_content);
    http_request_set_opt(request, HTTP_REQUEST_OPT_USER_DATA, player);

    int rc = http_request_execute(request);

    LOGI(BT_TAG, "all body had recved");

    player->finish_download = 1;
    player->download_result = rc;

    http_request_uninit(request);
    http_manager_uninit(mgr);

    return rc;
}

void* download_thread(void* param) {
    download_wav_file((bt_box_player_t*)param);
    return NULL;
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

    player->buf_queue = rc_buf_queue_init(2048, 3, WAV_HEADER_BYTES);

    while (true) {
        player->finish_download = 0;
        player->download_result = -1;

        rc_buf_queue_clean(player->buf_queue);

        // query wav from url
        rc_thread dt = rc_thread_create(download_thread, player);

        while (rc_buf_queue_is_empty(player->buf_queue)) {
            rc_sleep(300);
        }
        rc_sleep(1000);  // waitfor all buffer had fill

        // paly music
        LOGI(BT_TAG, "start to play music");
        esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);

        while (!_player->finish_download ||
               !rc_buf_queue_is_empty(player->buf_queue)) {
            // wait download and play finished
            rc_sleep(1000);
        }

        LOGI(BT_TAG, "play finished");
        rc_sleep(2000);

        LOGI(BT_TAG, "stop play music");
        esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_STOP);

        // rc_thread_join(dt);

        rc_sleep(120 * 1000);
    }

    esp_a2d_source_disconnect(bda);

    rc_buf_queue_uninit(player->buf_queue);

    rc_free(player);
    _player = NULL;

    return 0;
}