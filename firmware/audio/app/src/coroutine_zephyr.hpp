#pragma once

#include <zephyr/kernel.h>

#include "coroutine.hpp"

template <typename C>
class co_sync {
    struct k_mutex mutex;
    C co;

   public:
    co_sync() { k_mutex_init(&mutex); }

    template <typename... Args>
    co_result<int> operator()(k_timeout_t timeout, Args... args) {
        int ret = k_mutex_lock(&mutex, timeout);
        if (ret) return co_result<int>::ready(ret);
        auto res = co(args...);
        k_mutex_unlock(&mutex);
        return res;
    }
};
