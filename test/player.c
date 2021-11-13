#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_system.h"

#include "esp_bt.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "driver/i2s.h"

#include "test.h"
#include "rc_http_manager.h"
#include "rc_http_request.h"
#include "clist.h"

typedef struct _bt_box_player_t {
    rc_event conn_event;
    int media_ready;

    int offset;
    rc_buf_t* wav_buff;
} bt_box_player_t;

bt_box_player_t* _player;

void bt_app_rc_ct_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param)
{
    switch (event) {
    case ESP_AVRC_CT_METADATA_RSP_EVT:
    case ESP_AVRC_CT_CONNECTION_STATE_EVT:
    case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT:
    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT:
    case ESP_AVRC_CT_REMOTE_FEATURES_EVT:
    case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT:
    case ESP_AVRC_CT_SET_ABSOLUTE_VOLUME_RSP_EVT: {
        //bt_app_work_dispatch(bt_av_hdl_avrc_ct_evt, event, param, sizeof(esp_avrc_ct_cb_param_t), NULL);
        break;
    }
    default:
        LOGW(BT_TAG, "Invalid AVRC event: %d", event);
        break;
    }
}

void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    LOGI(BT_TAG, "%s evt 0x%x", __func__, event);
    switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT: {
        esp_a2d_cb_param_t *a2d = (esp_a2d_cb_param_t *)(param);
        if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
            LOGI(BT_TAG, "a2dp connected");
            esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
            esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY);
        } else if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
            LOGI(BT_TAG, "a2dp disconnected");
        }
        break;
    }
    case ESP_A2D_AUDIO_STATE_EVT:
    case ESP_A2D_AUDIO_CFG_EVT:
    case ESP_A2D_MEDIA_CTRL_ACK_EVT: {
        esp_a2d_cb_param_t *a2d = (esp_a2d_cb_param_t *)(param);
        if (a2d->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY) {
            if (a2d->media_ctrl_stat.status == ESP_A2D_MEDIA_CTRL_ACK_SUCCESS) {
                LOGI(BT_TAG, "esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY) success");
                //esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
                if (_player != NULL) _player->media_ready = 1;
            }
        } else if (a2d->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_START) {
            if (a2d->media_ctrl_stat.status == ESP_A2D_MEDIA_CTRL_ACK_SUCCESS) {
                LOGI(BT_TAG, "esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START) success");
            }

        } else if (a2d->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_STOP) {
            if (a2d->media_ctrl_stat.status == ESP_A2D_MEDIA_CTRL_ACK_SUCCESS) {
                LOGI(BT_TAG, "esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_STOP) success");
            }
        }
        break;
    }
    default:
        LOGW(BT_TAG, "%s unhandled evt %d", __func__, event);
        break;
    }
}

int32_t bt_app_a2d_data_cb(uint8_t *data, int32_t len)
{
    LOGI(BT_TAG, "bt_app_a2d_data_cb(%d)", len);
    if (len < 0 || data == NULL) {
        return 0;
    }

    if (_player != NULL) {
        if (_player->wav_buff == NULL) {
            LOGI(BT_TAG, "all music data had send, so stop player");
            esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_STOP);
            return 0;
        }

        int flen = len;
        while (len > 0 && _player->wav_buff != NULL) {
            if (_player->offset < _player->wav_buff->total) {
                int copy_size = _player->wav_buff->total - _player->offset;
                if (copy_size > len) copy_size = len;
                memcpy(data, rc_buf_head_ptr(_player->wav_buff) + _player->offset, copy_size);

                _player->offset += copy_size;
                len -= copy_size;
            }

            if (_player->offset == _player->wav_buff->total) {
                rc_buf_t* p = _player->wav_buff;
                if (LL_isspin(&_player->wav_buff->link)) {
                    _player->wav_buff = NULL;
                } else {
                    _player->wav_buff = (rc_buf_t*)LL_remove(&_player->wav_buff->link);
                }
                _player->offset = 0;
                rc_buf_free(p);
            }
        }

        len = flen - len;
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

int init_bt_palyer()
{
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
    esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &evt_set, ESP_AVRC_RN_VOLUME_CHANGE);
    assert(esp_avrc_tg_set_rn_evt_cap(&evt_set) == ESP_OK);

    /* initialize A2DP source */
    esp_a2d_register_callback(&bt_app_a2d_cb);
    esp_a2d_source_register_data_callback(bt_app_a2d_data_cb);
    esp_a2d_source_init();

    return 0;
}

rc_buf_t* move_link_list(list_link_t* head)
{
    list_link_t* p = head->next;

    if (LL_isspin(head)) return NULL;
    else {
        LL_remove(head);
    }
    return (rc_buf_t*)p;
}

int download_wav_file(bt_box_player_t* player)
{
    const char* url = "http://82.157.138.167/test-esp32.wav";

    http_manager mgr = http_manager_init();

    LOGI(BT_TAG, "try to query url %s", url);

    http_request request = http_request_init(mgr, url, "82.157.138.167", HTTP_REQUEST_GET);
    if (request == NULL) {
        http_manager_uninit(mgr);
        return -1;
    }

    int timeout = 3 * 1000;
    http_request_set_opt(request, HTTP_REQUEST_OPT_TIMEOUT, &timeout);

    int rc = http_request_execute(request);
    if (rc != 0) {
        http_request_uninit(request);
        http_manager_uninit(mgr);
        return -1;
    }

    int scode = 0;
    list_link_t* response = NULL;
    http_request_get_raw_response(request, &scode, &response);

    rc = -1;
    LOGI(BT_TAG, "query %s status code %d", url, scode);
    if (scode >= 200 && scode < 300) {
        LOGI(BT_TAG, "get all wav data success");
        _player->wav_buff = move_link_list(response);
        if (_player->wav_buff != NULL) {
            LOGI(BT_TAG, "buffer size=%d", _player->wav_buff->total);
            list_link_t* p = _player->wav_buff->link.next;
            while (p != &_player->wav_buff->link) {
                LOGI(BT_TAG, "buffer size=%d", ((rc_buf_t*)p)->total);
                p = p->next;
            }

            rc = 0;
        }
    }

    http_request_uninit(request);
    http_manager_uninit(mgr);

    return rc;
}

int connect_to_bt_player(esp_bd_addr_t bda)
{
    bt_box_player_t* player = (bt_box_player_t*)rc_malloc(sizeof(bt_box_player_t));
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

    // query wav from url
    while (download_wav_file(player) != 0) {
        rc_sleep(60 * 1000);
    }

    while (!_player->media_ready) {
        rc_sleep(1000);
    }

    // paly music
    LOGI(BT_TAG, "start to play music");
    esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);

    while (_player->wav_buff != NULL) {
        rc_sleep(1000);
    }

    LOGI(BT_TAG, "play finished");

    rc_sleep(20 * 1000);

    esp_a2d_source_disconnect(bda);

    rc_free(player);
    _player = NULL;

    return 0;
}