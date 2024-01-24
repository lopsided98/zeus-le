// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

struct freq_est_params {
    // Nominal frequency of the timer (ticks/sec)
    uint32_t nominal_freq;
    // Phase variance per sec^2 (s^2/s^2, dimensionless)
    float q_theta;
    // Frequency ratio variance per sec^2 (1/s^2)
    float q_f;
    // TODO: need scaling?
    // Phase measurement variance (s^2)
    float r;
    float p0;
};

struct freq_est {
    // Parameters
    float q_theta;
    /// q_f converted from 1/s^2 to 1/ticks^2
    float q_f;
    float r;

    // State
    bool init;
    uint32_t last_time;
    /// Whole part of phase offset (ticks).
    ///
    /// Together with theta_frac this forms a kind of 64-bit pseudo fixed-point
    /// number. The phase offset can be anywhere from 0 to UINT32_MAX and the
    /// resolution should not vary significantly over that range. Fixed-point
    /// would be ideal for this, but it is easier to just do it with a
    /// floating-point value that is kept small.
    uint32_t theta_whole;
    /// Fractional part of phase offset (ticks).
    float theta_frac;
    /// Frequency error (ticks/tick, dimensionless). Zero means frequencies are
    /// exactly equal.
    float f;
    /// Uncertainty
    float p[2][2];
};

void freq_est_init(struct freq_est *e, const struct freq_est_params *params);

void freq_est_update(struct freq_est *e, uint32_t local_time,
                     uint32_t central_time);