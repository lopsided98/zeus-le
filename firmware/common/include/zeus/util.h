#pragma once

#include <stdlib.h>
#include <zephyr/kernel.h>
#include <string.h>

static inline void k_mutex_cleanup___(struct k_mutex **mutex) {
    k_mutex_unlock(*mutex);
}

#define K_MUTEX_AUTO_LOCK(mutex)                                         \
    k_mutex_lock(mutex, K_FOREVER);                                      \
    __attribute__((cleanup(k_mutex_cleanup___))) struct k_mutex *CONCAT( \
        mutex_auto_ptr_, __COUNTER__, ___) = mutex
