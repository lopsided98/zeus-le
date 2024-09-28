// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio.h"

#include <hal/nrf_clock.h>
#include <hal/nrf_i2s.h>
#include <nrfx_dppi.h>
#include <nrfx_egu.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "drivers/input_codec.h"
#include "fixed.h"
#include "freq_ctlr.h"
#include "freq_est.h"
#include "record.h"
#include "sync_timer.h"

LOG_MODULE_REGISTER(audio, LOG_LEVEL_DBG);

struct audio_block_time {
    /// Number of timer ticks (as Q32.32) that should have elapsed from the time
    /// I2S was started to the start of the I2S buffer.
    qu32_32 i2s_time;
    /// Local timer count captured at the start of the I2S buffer. This is a
    /// local timestamp, before correction by the state estimator.
    uint32_t ref_time;
};

// TODO: sample rate must currently be divisible by the number of samples
#define AUDIO_BLOCK_SIZE 8820
#if IS_ENABLED(CONFIG_I2S_NRFX)
// Queue size limits the number of buffered blocks, so no point having more than
// the queue size plus one for the block currently being used by the hardware.
#define AUDIO_BLOCK_COUNT (CONFIG_I2S_NRFX_RX_BLOCK_COUNT + 1)
#else
#define AUDIO_BLOCK_COUNT 5
#endif

K_MEM_SLAB_DEFINE_STATIC(audio_slab, AUDIO_BLOCK_SIZE, AUDIO_BLOCK_COUNT, 4);

static K_SEM_DEFINE(audio_started, 0, 1);
static K_THREAD_STACK_DEFINE(audio_thread_stack, 1536);

#define AUDIO_SYNC_ENABLED IS_ENABLED(CONFIG_I2S_NRFX)

#define AUDIO_EGU_IDX 0
#define AUDIO_EGU_IRQ NRFX_IRQ_NUMBER_GET(NRF_EGU_INST_GET(AUDIO_EGU_IDX))
#if AUDIO_SYNC_ENABLED
#define AUDIO_EGU_IRQ_PRIO IRQ_PRIO_LOWEST
#else
// Dummy for simulator
#define AUDIO_EGU_IRQ_PRIO 0
#endif

#if AUDIO_SYNC_ENABLED
#define AUDIO_HFCLKAUDIO_FREQ_NOMINAL \
    DT_PROP(DT_NODELABEL(clock), hfclkaudio_frequency)
#else
#define AUDIO_HFCLKAUDIO_FREQ_NOMINAL 11289600
#endif

#define AUDIO_HFCLKAUDIO_FREQ_REG_MIN 12519
#define AUDIO_HFCLKAUDIO_FREQ_REG_MAX 18068

K_MSGQ_DEFINE(audio_block_time_queue, sizeof(struct audio_block_time),
              AUDIO_BLOCK_COUNT, 1);

static const struct audio_config {
    const struct device *const codec;
    const struct device *const i2s;
    struct k_mem_slab *const slab;
    struct k_sem *const started;

    const struct freq_est_config freq_est_cfg;
    const struct freq_ctlr freq_ctlr;

    struct k_msgq *const block_time_queue;
} audio_config = {
    .codec = DEVICE_DT_GET(DT_ALIAS(codec)),
    .i2s = DEVICE_DT_GET(DT_ALIAS(i2s)),
    .slab = &audio_slab,
    .started = &audio_started,

    .freq_est_cfg =
        {
            .nominal_freq = ZEUS_TIME_NOMINAL_FREQ,
            .k_u = 32e6 / (12.0 * (1 << 16) * AUDIO_HFCLKAUDIO_FREQ_NOMINAL),
            .q_theta = 0.0,
            .q_f = 256.0,
            .r = 390625.0,
            .p0 = 1e6,
            .outlier_threshold = 20.0f,
            .outlier_resync_count = 5,
        },
    .freq_ctlr =
        {
            .k_theta = 4.03747559e-11,
            .k_f = 6.45996094e-05,
            .max_step = 1000,
        },
    .block_time_queue = &audio_block_time_queue,
};

static struct audio_data {
    bool init;
    struct k_thread thread;
    /// Audio sampling period (Q32.32)
    qu32_32 sample_period;
    /// Time increment per buffer (Q32.32)
    qu32_32 block_duration;
    struct freq_est freq_est;
    /// Number of timer ticks (as Q32.32) that should have elapsed from the time
    /// I2S was started to the start of the latest I2S buffer.
    qu32_32 i2s_time;
    /// Controller target phase difference between the elapsed ticks counter
    /// (`i2s_time` variable) and central time (recovered via state estimator).
    /// This is set once after both I2S has started and the state estimator is
    /// initialized.
    qu32_32 target_theta;
    /// Last controller input
    int16_t hfclkaudio_increment;
} audio_data;

/// Update the I2S frequency estimator and controller, and return the starting
/// central time for the block if available. If the central time is not
/// available, return false.
static bool audio_sync_update(const struct audio_block_time *block_time,
                              uint32_t *block_start_time) {
    const struct audio_config *config = &audio_config;
    struct audio_data *data = &audio_data;
    qu32_32 ref_time = qu32_32_from_int(block_time->ref_time);

    // Convert local timer to central timestamp, if available. If no central
    // reference is available yet, continue anyway. This will sync the audio
    // clock with the local timer. When the central time becomes available, it
    // will cause an outlier reset and the controller will resync with the
    // central clock.
    sync_timer_local_to_central(&ref_time);

    enum freq_est_result result =
        freq_est_update(&data->freq_est, block_time->i2s_time, ref_time,
                        data->hfclkaudio_increment);

    struct freq_est_state state = freq_est_get_state(&data->freq_est);
    if (result == FREQ_EST_RESULT_INIT) {
        LOG_INF("phase target reset");
        // Round target phase to multiple of sample period. This will
        // synchronize the sampling times of all devices.
        data->target_theta =
            DIV_ROUND_CLOSEST(state.theta, data->sample_period) *
            data->sample_period;
    }

    // Calculate central node block timestamp assuming the controller is
    // maintaining the setpoint perfectly. If the controller is still
    // converging, this means the start of the recording may not be perfectly in
    // sync, but it will gradually synchronize over time.
    *block_start_time =
        qu32_32_whole(block_time->i2s_time - data->target_theta);

    data->hfclkaudio_increment =
        freq_ctlr_update(&config->freq_ctlr, data->target_theta, state);

    uint16_t freq = nrf_clock_hfclkaudio_config_get(NRF_CLOCK);

    // Clamp frequency in bounds
    int16_t max_inc = AUDIO_HFCLKAUDIO_FREQ_REG_MAX - freq;
    int16_t min_inc = AUDIO_HFCLKAUDIO_FREQ_REG_MIN - freq;
    if (data->hfclkaudio_increment > max_inc) {
        data->hfclkaudio_increment = max_inc;
    } else if (data->hfclkaudio_increment < min_inc) {
        data->hfclkaudio_increment = min_inc;
    }
    freq += data->hfclkaudio_increment;

    nrf_clock_hfclkaudio_config_set(NRF_CLOCK, freq);
    // printk("audio,%" PRIu64 ",%" PRIu64 ",%" PRIu16 ",%" PRIi16 ",%e\n",
    //        block_time->i2s_time - data->target_theta, ref_time, freq,
    //        data->hfclkaudio_increment, (double)(state.f / QU32_32_ONE));
    return true;
}

static void audio_thread_run(void *p1, void *p2, void *p3) {
    const struct audio_config *config = &audio_config;
    struct audio_data *data = &audio_data;
    int err;

    err = i2s_trigger(config->i2s, I2S_DIR_RX, I2S_TRIGGER_START);
    if (err) {
        LOG_ERR("failed to trigger I2S (err %d)", err);
        return;
    }

    while (true) {
        void *block_buf = NULL;
        uint32_t block_size;

        err = i2s_read(config->i2s, &block_buf, &block_size);
        if (err) {
            LOG_ERR("failed to read I2S (err %d)", err);

            err = i2s_trigger(config->i2s, I2S_DIR_RX, I2S_TRIGGER_PREPARE);
            if (err) {
                LOG_ERR("failed to clear I2S error (err %d)", err);
                break;
            }
            err = i2s_trigger(config->i2s, I2S_DIR_RX, I2S_TRIGGER_START);
            if (err) {
                LOG_ERR("failed to re-start I2S (err %d)", err);
                break;
            }
            continue;
        }

        bool block_start_time_valid = false;
        uint32_t block_start_time;
        if (AUDIO_SYNC_ENABLED) {
            struct audio_block_time block_time;
            // RXPTRUPD should trigger at the beginning of each block, so we
            // should never have to wait for a timestamp to become available
            err = k_msgq_get(config->block_time_queue, &block_time, K_NO_WAIT);
            if (err) {
                // If this happens, EGU interrupt never ran
                LOG_ERR("did not receive block timestamp (err %d)", err);
                break;
            }
            block_start_time_valid =
                audio_sync_update(&block_time, &block_start_time);
        } else {
            block_start_time = qu32_32_whole(sync_timer_get_central_time());
            block_start_time_valid = true;
        }

        k_sem_give(config->started);

        int64_t record_start_time = k_uptime_ticks();

        // Don't pass buffer to recording module if we don't have a valid
        // timestamp for it
        if (block_start_time_valid) {
            const struct audio_block block = {
                .buf = block_buf,
                .len = block_size,
                .start_time = block_start_time,
                .duration = qu32_32_whole(data->block_duration),
                // TODO: don't hardcode
                .bytes_per_frame = 4,
            };

            record_buffer(&block);
        }
        int64_t record_us =
            k_ticks_to_us_near64(k_uptime_ticks() - record_start_time);

        // LOG_INF("bt: %" PRIi64 ", rt: %" PRIu64 ", s: %u, b: %p",
        //         block_time_wait_us, record_us,
        //         k_mem_slab_num_used_get(config->slab), block_buf);

        k_mem_slab_free(config->slab, block_buf);
    }
}

static void audio_egu_handler(uint8_t event_idx, void *p_context) {
    const struct audio_config *config = &audio_config;
    struct audio_data *data = &audio_data;
    const struct audio_block_time block_time = {
        .i2s_time = data->i2s_time,
        .ref_time = sync_timer_get_i2s_time(),
    };
    data->i2s_time += data->block_duration;
    int err = k_msgq_put(config->block_time_queue, &block_time, K_NO_WAIT);
    if (err < 0) {
        // I2S buffer should overrun before this happens
        LOG_ERR("failed to queue block timestamp (err %d)", err);
    }
}

int audio_init() {
    int err;
    const struct audio_config *config = &audio_config;
    struct audio_data *data = &audio_data;
    if (data->init) return -EALREADY;

    if (!device_is_ready(config->codec)) {
        LOG_ERR("%s is not ready\n", config->codec->name);
        return -ENODEV;
    }

    if (!device_is_ready(config->i2s)) {
        LOG_ERR("%s is not ready\n", config->i2s->name);
        return -ENODEV;
    }

    struct audio_codec_cfg cfg = {
#if AUDIO_SYNC_ENABLED
        .mclk_freq = AUDIO_HFCLKAUDIO_FREQ_NOMINAL,
#else
        .mclk_freq = 0,
#endif
        .dai_type = AUDIO_DAI_TYPE_I2S,
        .dai_cfg.i2s =
            {
                .word_size = 16,
                .channels = 2,
                .format = I2S_FMT_DATA_FORMAT_LEFT_JUSTIFIED,
                .options = I2S_OPT_BIT_CLK_MASTER | I2S_OPT_FRAME_CLK_MASTER,
                .frame_clk_freq = 44100,
                .mem_slab = config->slab,
                .block_size = config->slab->info.block_size,
                .timeout = 1000,
            },
    };

    data->sample_period = qu32_32_from_int(ZEUS_TIME_NOMINAL_FREQ) /
                          cfg.dai_cfg.i2s.frame_clk_freq;

    uint32_t frames_per_block = AUDIO_BLOCK_SIZE / cfg.dai_cfg.i2s.channels /
                                (cfg.dai_cfg.i2s.word_size / 8);
    // TODO: allow sample-rates that aren't a multiple of frame/block.
    // Naive implementation overflows 64-bit integer with intermediate
    // result.
    data->block_duration =
        qu32_32_from_int((uint64_t)ZEUS_TIME_NOMINAL_FREQ * frames_per_block /
                         cfg.dai_cfg.i2s.frame_clk_freq);

    freq_est_init(&data->freq_est, &config->freq_est_cfg);

    nrfx_egu_t egu = NRFX_EGU_INSTANCE(AUDIO_EGU_IDX);

    IRQ_CONNECT(AUDIO_EGU_IRQ, AUDIO_EGU_IRQ_PRIO,
                NRFX_EGU_INST_HANDLER_GET(AUDIO_EGU_IDX), 0, 0);

    nrfx_err_t nerr =
        nrfx_egu_init(&egu, 0 /* ignored */, audio_egu_handler, NULL);
    if (NRFX_SUCCESS != nerr) {
        LOG_ERR("failed to configure EGU for I2S (err %d)", nerr);
        return -EALREADY;
    }

    if (AUDIO_SYNC_ENABLED) {
        uint8_t i2s_dppi = sync_timer_get_i2s_dppi();
        nrf_i2s_publish_set(NRF_I2S0, NRF_I2S_EVENT_RXPTRUPD, i2s_dppi);
        nrf_egu_subscribe_set(egu.p_reg, NRF_EGU_TASK_TRIGGER0, i2s_dppi);
        nrfx_egu_int_enable(&egu, NRF_EGU_INT_TRIGGERED0);
        nrfx_dppi_channel_enable(i2s_dppi);
    }

    err = i2s_configure(config->i2s, I2S_DIR_RX, &cfg.dai_cfg.i2s);
    if (err < 0) {
        LOG_ERR("failed to configure I2S (err %d)", err);
        return err;
    }

    k_thread_create(&data->thread, audio_thread_stack,
                    K_THREAD_STACK_SIZEOF(audio_thread_stack), audio_thread_run,
                    NULL, NULL, NULL, K_PRIO_COOP(12), 0, K_NO_WAIT);
    k_thread_name_set(&data->thread, "audio");

    err = k_sem_take(config->started, K_MSEC(2000));
    if (err < 0) {
        LOG_ERR("audio did not start");
        return err;
    }
    LOG_INF("audio started");

    err = input_codec_configure(config->codec, &cfg);
    if (err < 0) {
        LOG_ERR("failed to configure codec (err %d)", err);
        return err;
    }

    err = input_codec_start_input(config->codec);
    if (err < 0) {
        LOG_ERR("failed to start codec (err %d)", err);
        return err;
    }

    data->init = true;
    return 0;
}