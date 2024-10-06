// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdint.h>
#include <zephyr/bluetooth/uuid.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZEUS_TIME_NOMINAL_FREQ 16000000

#define ZEUS_BT_UUID_VAL \
    BT_UUID_128_ENCODE(0x0d45e195, 0x5ea6, 0x4131, 0xae16, 0xdd98081fba60)

#define ZEUS_BT_UUID BT_UUID_DECLARE_128(ZEUS_BT_UUID_VAL)

struct zeus_adv_sync {
    uint8_t seq;
    uint32_t time;
} __packed;

enum zeus_adv_cmd_id {
    ZEUS_ADV_CMD_NONE = 0,
    ZEUS_ADV_CMD_START,
    ZEUS_ADV_CMD_STOP,
} __packed;

struct zeus_adv_header {
    struct zeus_adv_sync sync;
    uint16_t seq;
}__packed;

struct zeus_adv_cmd_start {
    uint32_t time;
} __packed;

struct zeus_adv_cmd {
    enum zeus_adv_cmd_id id;
    union {
        struct zeus_adv_cmd_start start;
    };
} __packed;

struct zeus_adv_data {
    struct zeus_adv_header hdr;
    struct zeus_adv_cmd cmd;
} __packed;

#ifdef __cplusplus
}
#endif