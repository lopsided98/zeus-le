#pragma once

#include <zephyr/net/buf.h>

#include "audio.h"

int net_audio_init(void);

int net_audio_send(const uint8_t *buf, size_t len);