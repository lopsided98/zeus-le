#pragma once

#include <stdint.h>

#define Q_TYPE(prefix, whole, frac) prefix##whole##_##frac
#define Q_NAME(prefix, whole, frac, name) prefix##whole##_##frac##_##name

#define Q_DEFINE(lp, up, tp, b, w, f)                                          \
    typedef tp##b##_t Q_TYPE(lp, w, f);                                        \
                                                                               \
    static const Q_TYPE(lp, w, f) Q_NAME(up, w, f, ONE) = (Q_TYPE(lp, w, f))1  \
                                                          << f;                \
                                                                               \
    inline static Q_TYPE(lp, w, f) Q_NAME(lp, w, f, from_int)(tp##w##_t val) { \
        return (Q_TYPE(lp, w, f))val << f;                                     \
    }                                                                          \
                                                                               \
    inline static tp##w##_t Q_NAME(lp, w, f, whole)(Q_TYPE(lp, w, f) val) {    \
        return val >> f;                                                       \
    }                                                                          \
                                                                               \
    inline static float Q_NAME(lp, w, f, to_float)(Q_TYPE(lp, w, f) val) {     \
        return (float)val / Q_NAME(up, w, f, ONE);                             \
    }                                                                          \
                                                                               \
    inline static double Q_NAME(lp, w, f, to_double)(Q_TYPE(lp, w, f) val) {   \
        return (double)val / Q_NAME(up, w, f, ONE);                            \
    }

Q_DEFINE(q, Q, int, 64, 32, 32)
Q_DEFINE(qu, QU, uint, 64, 32, 32)
Q_DEFINE(q, Q, int, 32, 16, 16)
Q_DEFINE(qu, QU, uint, 32, 16, 16)