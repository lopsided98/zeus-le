// SPDX-License-Identifier: GPL-3.0-or-later
#include "freq_est.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/logging/log.h>

#include "fixed.h"

LOG_MODULE_REGISTER(freq_est);

static qu32_32 phase_add_float(qu32_32 theta, float inc) {
    if (inc >= 0) {
        theta += (uint64_t)inc;
    } else {
        theta -= (uint64_t)-inc;
    }
    return theta;
}

static q32_32 phase_diff_signed(qu32_32 a, qu32_32 b) {
    qu32_32 diff = a - b;
    q32_32 diff_signed;
    memcpy(&diff_signed, &diff, sizeof(diff));
    return diff_signed;
}

void freq_est_init(struct freq_est *e, const struct freq_est_config *cfg) {
    float nominal_freq_2 = (float)cfg->nominal_freq * cfg->nominal_freq;

    *e = (struct freq_est){
        .config = cfg,

        // Scale input gain to match frequency units
        .k_u = cfg->k_u * (float)(UINT64_C(1) << 32),
        // Both numerator and denominator of q_theta need scaling, so they
        // cancel out. Therefore, we don't need any conversion here.
        .q_theta = cfg->q_theta,
        .q_f = cfg->q_f / nominal_freq_2,
        .r = cfg->r * nominal_freq_2,

        .status = FREQ_EST_STATUS_RESET,
        .f = 0,
        .theta = 0,
    };
}

qu32_32 freq_est_predict(const struct freq_est *e, qu32_32 time) {
    float dt = q32_32_to_float(phase_diff_signed(time, e->last_time));
    return phase_add_float(e->theta, dt * e->f);
}

enum freq_est_result freq_est_update(struct freq_est *e, qu32_32 local_time,
                                     qu32_32 ref_time, int16_t input) {
    const struct freq_est_config *cfg = e->config;
    qu32_32 z = local_time - ref_time;

    if (e->status == FREQ_EST_STATUS_RESET) {
        e->status = FREQ_EST_STATUS_CONVERGING;
        e->last_time = local_time;
        e->theta = z;
        // Don't reset frequency, since it likely stays the same even when the
        // phase jumps
        e->p[0][0] = cfg->p0;
        e->p[0][1] = 0.0f;
        e->p[1][0] = 0.0f;
        e->p[1][1] = cfg->p0;
        e->outlier_count = 0;
        return FREQ_EST_RESULT_INIT;
    }

    // TODO: correct for frequency error in timestep calculation?
    float dt = qu32_32_to_float(local_time - e->last_time);
    e->last_time = local_time;

    float scaled_input = (float)input * e->k_u;
    e->theta = phase_add_float(e->theta, dt * (e->f + scaled_input));
    e->f += scaled_input;

    float dt_p11 = dt * e->p[1][1];
    e->p[0][0] += dt * (dt * e->q_theta + e->p[0][1] + e->p[1][0] + dt_p11);
    e->p[0][1] += dt_p11;
    e->p[1][0] += dt_p11;
    e->p[1][1] += dt * dt * e->q_f;

    float p00_r = e->p[0][0] + e->r;
    float k0 = e->p[0][0] / p00_r;
    float k1 = e->p[1][0] / p00_r;

    int64_t theta_error = phase_diff_signed(z, e->theta);

    // Optional outlier detection
    if (cfg->outlier_threshold > 0) {
        // Mahalanobis distance, aka (in 1-d) the number of standard deviations
        // of error between the measurement and prediction.
        float d_m = llabs(theta_error) / sqrtf(p00_r);

        // LOG_INF("d_m: %f, outliers: %u", (double)d_m, e->outlier_count);

        if (d_m >= cfg->outlier_threshold) {
            e->outlier_count++;

            if (cfg->outlier_resync_count > 0 &&
                e->outlier_count >= cfg->outlier_resync_count) {
                // Reset state assuming theta has jumped
                e->status = FREQ_EST_STATUS_RESET;
                return FREQ_EST_RESULT_OUTLIER_RESET;
            } else {
                // Ignore outlier
                return FREQ_EST_RESULT_OUTLIER;
            }
        } else {
            e->outlier_count = 0;
        }
    }

    e->theta = phase_add_float(e->theta, k0 * theta_error);
    e->f += k1 * theta_error;

    // Order is important; must only use p values from the prediction step, not
    // ones that were written in this block.
    e->p[1][1] -= e->p[0][1] * e->p[1][0] / p00_r;
    e->p[0][1] = e->r * e->p[0][1] / p00_r;
    e->p[0][0] = e->r * k0;
    e->p[1][0] = e->r * k1;

    // LOG_INF("th: %" PRIu64 ", f: %e, z: %" PRIu64 ", e: %" PRIi64 ", k0: %f",
    //         e->theta, e->f / Q32_32_ONE, z, theta_error, k0);
    return FREQ_EST_RESULT_OK;
}

struct freq_est_state freq_est_get_state(const struct freq_est *e) {
    return (struct freq_est_state){
        .status = e->status,
        .theta = e->theta,
        .f = e->f,
    };
}