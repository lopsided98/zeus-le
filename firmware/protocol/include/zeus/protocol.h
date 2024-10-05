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

struct zeus_adv_header {
    uint8_t seq;
    uint32_t time;
} __attribute__((packed));

enum zeus_adv_cmd_id {
    ZEUS_ADV_CMD_NONE = 0,
    ZEUS_ADV_CMD_START,
    ZEUS_ADV_CMD_STOP,
} __attribute__((packed));

struct zeus_adv_cmd_header {
    uint16_t seq;
    enum zeus_adv_cmd_id id;
} __attribute__((packed));

struct zeus_adv_cmd_start {
    uint32_t time;
} __attribute__((packed));

struct zeus_adv_cmd {
    struct zeus_adv_cmd_header hdr;
    union {
        struct zeus_adv_cmd_start start;
    };
} __attribute__((packed));

struct zeus_adv_data {
    struct zeus_adv_header hdr;
    struct zeus_adv_cmd cmd;
} __attribute__((packed));

#ifdef __cplusplus
}
#endif