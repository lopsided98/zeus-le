#pragma once

#include <zephyr/net/buf.h>

int net_audio_init(void);

int net_audio_send(struct net_buf *buf);