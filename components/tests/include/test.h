

#ifndef _DEMO_TEST_H_
#define _DEMO_TEST_H_

#include <string.h>

//#include "esp_bt.h"
//#include "esp_bt_device.h"
//#include "esp_bt_main.h"
//#include "esp_gap_bt_api.h"
#include "hashmap.h"
#include "quark/quark.h"
#include "quark/framework/system/include/rc_buf_queue.h"

#define BT_TAG "[BT]"

#define WAV_URL "http://82.157.138.167/test-esp32.wav"

#define WAV_HEADER_BYTES 44

#define WAV_TYPE_RAND 0
#define WAV_TYPE_LOCAL 1
#define WAV_TYPE_ONLINE_POP_QUEUE 2
#define WAV_TYPE_ONLINE_ONE_SWAP 3
#define WAV_TYPE_ONLINE_TWO_SWAP 4

#define USE_WAV_TYPE 4

#if (USE_WAV_TYPE == WAV_TYPE_LOCAL)
#include "SoundData.h"
#endif

void *test_camera(void *params);

#endif