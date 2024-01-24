// SPDX-License-Identifier: GPL-3.0-or-later
#include "freq_est.h"

#include <math.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(freq_est);

#define THETA_WRAP ((uint64_t)UINT32_MAX + 1)

void freq_est_init(struct freq_est *e, const struct freq_est_params *params) {
    memset(e, 0, sizeof(*e));
    float nominal_freq_2 = (float)params->nominal_freq * params->nominal_freq;
    // Both numerator and denominator of q_theta need scaling, so they cancel
    // out. Therefore, we don't need any conversion here.
    e->q_theta = params->q_theta;
    e->q_f = params->q_f / nominal_freq_2;
    e->r = params->r * nominal_freq_2;

    e->p[0][0] = params->p0;
    e->p[1][1] = params->p0;
}

void freq_est_update(struct freq_est *e, uint32_t local_time,
                     uint32_t central_time) {
    if (!e->init) {
        e->theta_whole = central_time - local_time;
        e->theta_frac = 0.0;
        e->last_time = local_time;
        e->init = true;
        return;
    }

    uint32_t z_unsigned = (central_time - (local_time + e->theta_whole));
    float z;
    if (z_unsigned >= (THETA_WRAP / 2)) {
        z = -(float)(THETA_WRAP - z_unsigned);
    } else {
        z = z_unsigned;
    }

    // TODO: correct for frequency error in timestep calculation?
    float dt = local_time - e->last_time;
    e->last_time = local_time;

    e->theta_frac += dt * e->f;

    float dt_p11 = dt * e->p[1][1];
    e->p[0][0] += dt * (dt * e->q_theta + e->p[0][1] + e->p[1][0] + dt_p11);
    e->p[0][1] += dt_p11;
    e->p[1][0] += dt_p11;
    e->p[1][1] += dt * dt * e->q_f;

    float p00_r = e->p[0][0] + e->r;
    float k0 = e->p[0][0] / p00_r;
    float k1 = e->p[1][0] / p00_r;

    float theta_error = z - e->theta_frac;
    e->theta_frac += k0 * theta_error;
    e->f += k1 * theta_error;

    int32_t delta_theta_whole = e->theta_frac;
    e->theta_whole += delta_theta_whole;
    e->theta_frac -= delta_theta_whole;

    // Order is important; must only use p values from the prediction step, not
    // ones that were written in this block.
    e->p[1][1] -= e->p[0][1] * e->p[1][0] / p00_r;
    e->p[0][1] = e->r * e->p[0][1] / p00_r;
    e->p[0][0] = e->r * k0;
    e->p[1][0] = e->r * k1;

    LOG_INF("th: %.3f, f: %e, z: %f, e: %e, k0: %f",
            (double)e->theta_whole + e->theta_frac, e->f, z, theta_error, k0);
}