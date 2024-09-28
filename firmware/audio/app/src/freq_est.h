// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "fixed.h"

enum freq_est_status {
    FREQ_EST_STATUS_RESET,
    FREQ_EST_STATUS_CONVERGING,
    FREQ_EST_STATUS_CONVERGED,
};

enum freq_est_result {
    /// Input was incorporated into the state estimate
    FREQ_EST_RESULT_OK = 0,
    /// Input was used to initialize the estimator
    FREQ_EST_RESULT_INIT,
    /// Input was an outlier and ignored
    FREQ_EST_RESULT_OUTLIER,
    /// Reset was triggered due to several consecutive outliers
    FREQ_EST_RESULT_OUTLIER_RESET,
};

struct freq_est_config {
    // Nominal frequency of the timer (ticks/sec)
    uint32_t nominal_freq;
    /// Input gain. Change in frequency ratio for unit input.
    float k_u;
    // Phase variance per sec^2 (s^2/s^2, dimensionless)
    float q_theta;
    // Frequency ratio variance per sec^2 (1/s^2)
    float q_f;
    // Phase measurement variance (s^2)
    float r;
    float p0;
    // Mahalanobis distance threshold to consider a measurement an outlier. If
    // zero, disable outlier detection.
    float outlier_threshold;
    // Number of consecutive outliers that trigger a resync. If zero, never
    // resync.
    uint32_t outlier_resync_count;
};

struct freq_est_state {
    enum freq_est_status status;
    qu32_32 theta;
    float f;
};

struct freq_est {
    // Parameters
    const struct freq_est_config *config;
    /// Input gain scaled by 2^32
    float k_u;
    float q_theta;
    /// q_f converted from 1/s^2 to 1/ticks^2
    float q_f;
    float r;

    // State
    enum freq_est_status status;
    qu32_32 last_time;
    /// Phase offset (ticks) as unsigned Q32.32 fixed point. We want sub-tick
    /// resolution over the entire range, therefore single-precision float is
    /// not sufficient. Double would be sufficient, but the FPU only does single
    /// precision and fixed-point also has the benefit of keeping the resolution
    /// constant over the whole range.
    qu32_32 theta;
    /// Frequency ratio (2^32 * ticks/tick, dimensionless) of local over
    /// reference frequency. Zero means frequencies are exactly equal. Positive
    /// indicates local clock is running faster than reference.
    float f;
    /// Uncertainty
    float p[2][2];
    /// Number of consecutive outliers
    uint32_t outlier_count;
};

/// Initialize the frequency estimator. The cfg pointer must be valid for the
/// lifetime of the estimator.
void freq_est_init(struct freq_est *e, const struct freq_est_config *cfg);

/// Predict the phase offset at the specified time
qu32_32 freq_est_predict(const struct freq_est *e, qu32_32 time);

enum freq_est_result freq_est_update(struct freq_est *e, qu32_32 local_time,
                                     qu32_32 ref_time, int16_t input);

struct freq_est_state freq_est_get_state(const struct freq_est *e);