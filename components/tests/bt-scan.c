

#include "test.h"

#define MAX_BT_DEVICE 10

int bt_init() {
    esp_err_t err;

    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE) {
        LOGI(BT_TAG, "bt current status is ESP_BT_CONTROLLER_STATUS_IDLE");
        esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        if ((err = esp_bt_controller_init(&bt_cfg)) != ESP_OK) {
            LOGW(BT_TAG, "%s initialize controller failed: %s\n", __func__,
                 esp_err_to_name(err));
            return -1;
        }
    }

    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED) {
        LOGI(BT_TAG, "bt current status is ESP_BT_CONTROLLER_STATUS_INITED");
        if ((err = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT)) !=
            ESP_OK) {
            LOGW(BT_TAG, "%s enable controller failed: %s\n", __func__,
                 esp_err_to_name(err));
            return -1;
        }
    }

    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED &&
        esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_UNINITIALIZED) {
        LOGI(BT_TAG, "bt current status is ESP_BT_CONTROLLER_STATUS_ENABLED");
        if ((err = esp_bluedroid_init()) != ESP_OK) {
            LOGW(BT_TAG, "%s initialize bluedroid failed: %s\n", __func__,
                 esp_err_to_name(err));
            return -1;
        }
    }

    if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_INITIALIZED) {
        LOGI(BT_TAG, "bt current status is ESP_BLUEDROID_STATUS_INITIALIZED");
        if ((err = esp_bluedroid_enable()) != ESP_OK) {
            LOGW(BT_TAG, "%s enable bluedroid failed: %s\n", __func__,
                 esp_err_to_name(err));
            return -1;
        }
    }

    return 0;
}

int bt_uninit() {
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_bluedroid_disable());
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_bluedroid_deinit());
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_bt_controller_disable());
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_bt_controller_deinit());

    return 0;
}

typedef struct _bt_device_t {
    esp_bd_addr_t bda;
    char *bda_str;
    char *name;
    int32_t rssi;
    uint32_t cod;
} bt_device_t;

typedef struct _bt_scan_ctx_t {
    rc_event stop_event;
    int stoped;
    map_t kvdev;
    bt_device_t devices[MAX_BT_DEVICE];
    int count;
    rc_buf_t buff;
    char pad[1024];

} bt_scan_ctx_t;

bt_scan_ctx_t *_scan_ctx;

bt_device_t *on_found_device(bt_scan_ctx_t *ctx, esp_bt_gap_cb_param_t *param) {
    char *p = (char *)param->disc_res.bda;
    char *uuid = rc_buf_tail_ptr(&ctx->buff);
    int n = snprintf(uuid, RC_BUF_LEFT_SIZE(&ctx->buff),
                     "%02x:%02x:%02x:%02x:%02x:%02x", p[0], p[1], p[2], p[3],
                     p[4], p[5]);

    if (n < 0 || n >= RC_BUF_LEFT_SIZE(&ctx->buff)) {
        LOGI(BT_TAG, "buffer is too small");
        return NULL;
    }

    LOGI(BT_TAG, "Device found: %s", uuid);

    any_t dev = NULL;
    bt_device_t *device = NULL;
    if (hashmap_get(ctx->kvdev, uuid, &dev) == MAP_OK) {
        LOGI(BT_TAG, "device(%s) exists", uuid);
        device = (bt_device_t *)dev;
    } else if (ctx->count >= MAX_BT_DEVICE) {
        LOGI(BT_TAG, "device count is too many");
        return NULL;
    } else {
        ctx->buff.length += n + 1;
        device = &ctx->devices[ctx->count++];
        device->bda_str = uuid;
        device->name = "";
        device->rssi = -129; /* invalid value */
        memcpy(device->bda, param->disc_res.bda, ESP_BD_ADDR_LEN);
        hashmap_put(ctx->kvdev, uuid, device);
    }

    for (int i = 0; i < param->disc_res.num_prop; i++) {
        esp_bt_gap_dev_prop_t *p = param->disc_res.prop + i;
        switch (p->type) {
        case ESP_BT_GAP_DEV_PROP_COD:
            device->cod = *(uint32_t *)(p->val);
            LOGI(BT_TAG, "--Class of Device: 0x%x", device->cod);
            break;
        case ESP_BT_GAP_DEV_PROP_RSSI:
            device->rssi = *(int8_t *)(p->val);
            LOGI(BT_TAG, "--RSSI: %d", device->rssi);
            break;
        case ESP_BT_GAP_DEV_PROP_BDNAME: {
            if (device->name == NULL || strlen(device->name) == 0) {
                char x = '\0';
                device->name = rc_buf_tail_ptr(&ctx->buff);
                rc_buf_append(&ctx->buff, (char *)p->val, p->len);
                rc_buf_append(&ctx->buff, &x, 1);
                LOGI(BT_TAG, "--Name: %s", device->name);
            }
            break;
        }
        case ESP_BT_GAP_DEV_PROP_EIR: {
            if (p->val == NULL) break;
            uint8_t *rmt_bdname = NULL;
            uint8_t rmt_bdname_len = 0;

            rmt_bdname = esp_bt_gap_resolve_eir_data(
                p->val, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &rmt_bdname_len);
            if (!rmt_bdname) {
                rmt_bdname = esp_bt_gap_resolve_eir_data(
                    p->val, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &rmt_bdname_len);
            }

            if (rmt_bdname == NULL) break;

            char x = '\0';
            device->name = rc_buf_tail_ptr(&ctx->buff);
            rc_buf_append(&ctx->buff, (char *)rmt_bdname, rmt_bdname_len);
            rc_buf_append(&ctx->buff, &x, 1);
            LOGI(BT_TAG, "--Name: %s", device->name);
            break;
        }
        default: break;
        }
    }

    return device;
}

void gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
    switch (event) {
    case ESP_BT_GAP_DISC_RES_EVT: {
        LOGI(BT_TAG, "ESP_BT_GAP_DISC_RES_EVT");
        bt_device_t *device = on_found_device(_scan_ctx, param);
        if (device != NULL &&
            esp_bt_gap_get_cod_srvc(device->cod) & ESP_BT_COD_SRVC_RENDERING) {
            LOGI(BT_TAG, "found target device: (%s) %s", device->name,
                 device->bda_str);
            esp_bt_gap_cancel_discovery();
        }
        break;
    }
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT: {
        if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
            LOGI(BT_TAG, "Device discovery stopped.");
            _scan_ctx->stoped = 1;
            rc_event_signal(_scan_ctx->stop_event);
        } else if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) {
            LOGI(BT_TAG, "Discovery started.");
        }
        break;
    }
    case ESP_BT_GAP_RMT_SRVCS_EVT:
        LOGI(BT_TAG, "ESP_BT_GAP_RMT_SRVCS_EVT");
        break;

    case ESP_BT_GAP_RMT_SRVC_REC_EVT:
        LOGI(BT_TAG, "ESP_BT_GAP_RMT_SRVC_REC_EVT");
        break;
    case ESP_BT_GAP_AUTH_CMPL_EVT: {
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            LOGI(BT_TAG, "authentication success: %s",
                 param->auth_cmpl.device_name);
        } else {
            LOGE(BT_TAG, "authentication failed, %s status:%d",
                 param->auth_cmpl.device_name, param->auth_cmpl.stat);
        }
        break;
    }
    case ESP_BT_GAP_PIN_REQ_EVT: {
        LOGI(BT_TAG, "ESP_BT_GAP_PIN_REQ_EVT min_16_digit:%d",
             param->pin_req.min_16_digit);
        if (param->pin_req.min_16_digit) {
            LOGI(BT_TAG, "Input pin code: 0000 0000 0000 0000");
            esp_bt_pin_code_t pin_code = {0};
            esp_bt_gap_pin_reply(param->pin_req.bda, true, 16, pin_code);
        } else {
            LOGI(BT_TAG, "Input pin code: 1234");
            esp_bt_pin_code_t pin_code;
            pin_code[0] = '1';
            pin_code[1] = '2';
            pin_code[2] = '3';
            pin_code[3] = '4';
            esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
        }
        break;
    }
    case ESP_BT_GAP_MODE_CHG_EVT: break;
    default: LOGI(BT_TAG, "event: %d", event); break;
    }
}

void bt_scan_test(void *pvParameters) {
    LOGI(BT_TAG, "bt_scan_test");
    if (bt_init() != 0) {
        rc_sleep(10 * 1000);
        return;
    }

    LOGI(BT_TAG, "bt init success");
    _scan_ctx = (bt_scan_ctx_t *)rc_malloc(sizeof(bt_scan_ctx_t));
    memset(_scan_ctx, 0, sizeof(bt_scan_ctx_t));
    _scan_ctx->stop_event = rc_event_init();
    _scan_ctx->buff = rc_buf_stack();
    _scan_ctx->buff.total = sizeof(_scan_ctx->pad);
    _scan_ctx->kvdev = hashmap_new();

    esp_bt_gap_register_callback(gap_callback);

    ESP_ERROR_CHECK(esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE,
                                             ESP_BT_GENERAL_DISCOVERABLE));

    while (true) {
        ESP_ERROR_CHECK(esp_bt_gap_start_discovery(
            ESP_BT_INQ_MODE_GENERAL_INQUIRY, 0x20, 0));

        while (!_scan_ctx->stoped) {
            rc_event_wait(_scan_ctx->stop_event, 10 * 1000);
        }

        rc_sleep(3 * 1000);

        bt_device_t *player_device = NULL;
        LOGI(BT_TAG, "Found BT Devices....");
        for (int i = 0; i < _scan_ctx->count; ++i) {
            bt_device_t *device = &_scan_ctx->devices[i];
            LOGI(BT_TAG, "BT Device(%s), name(%s), rssi(%d), type(0x%x)",
                 device->bda_str, device->name, device->rssi, device->cod);
            if (esp_bt_gap_is_valid_cod(device->cod)/* ==
                ESP_BT_COD_MAJOR_DEV_AV*/) {
                LOGI(BT_TAG, "found ESP_BT_COD_MAJOR_DEV_AV device");
                esp_bt_gap_get_remote_services(device->bda);
            }

            if (esp_bt_gap_get_cod_srvc(device->cod) &
                ESP_BT_COD_SRVC_RENDERING) {
                LOGI(BT_TAG, "found ESP_BT_COD_SRVC_RENDERING device");
                player_device = device;
                break;
            }
        }

        extern int init_bt_palyer();
        extern int connect_to_bt_player(esp_bd_addr_t bda);
        if (player_device != NULL) {
            init_bt_palyer();
            LOGI(BT_TAG, "try to connect bt device %s", player_device->bda_str);
            connect_to_bt_player(player_device->bda);
            break;
        }

        rc_sleep(10 * 1000);
    }

    for (int i = 0; i < 100; ++i) {
        rc_sleep(10 * 1000);
    }

    hashmap_free(_scan_ctx->kvdev);
    rc_free(_scan_ctx);
    LOGI(BT_TAG, "before call bt_uninit");
    bt_uninit();
}