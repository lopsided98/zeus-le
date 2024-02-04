// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdint.h>

#include "fixed.h"
#include "freq_est.h"

struct freq_ctlr {
    /// Phase gain
    float k_theta;
    /// Frequency gain
    float k_f;
    /// Maximum control step per iteration
    uint16_t max_step;
};

int16_t freq_ctlr_update(const struct freq_ctlr *c, qu32_32 target_theta,
                         struct freq_est_state state);