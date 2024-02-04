// SPDX-License-Identifier: GPL-3.0-or-later
#include "freq_ctlr.h"

#include <math.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/logging/log.h>

#include "fixed.h"

LOG_MODULE_REGISTER(freq_ctrl);

#define FREQ_CTRL_MAX_STEP 1000

static q32_32 phase_diff_signed(qu32_32 a, qu32_32 b) {
    qu32_32 diff = a - b;
    q32_32 diff_signed;
    memcpy(&diff_signed, &diff, sizeof(diff));
    return diff_signed;
}

static int16_t round_f_to_i16(float f) {
    return f + (f > 0 ? 0.5 : -0.5);
}

int16_t freq_ctlr_update(const struct freq_ctlr *c, qu32_32 target_theta,
                         struct freq_est_state state) {
    float theta_err = (float)phase_diff_signed(target_theta, state.theta);
    float f_err = -state.f;
    float u = c->k_theta * theta_err + c->k_f * f_err;
    // LOG_INF("te: %f, fe: %f, u: %f", theta_err, f_err, u);
    if (u > c->max_step) {
        return c->max_step;
    } else if (u < -(float)c->max_step) {
        return -(int16_t)c->max_step;
    } else {
        return round_f_to_i16(u);
    }
}