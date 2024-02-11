// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio.h"

#include <hal/nrf_clock.h>
#include <hal/nrf_i2s.h>
#include <nrfx_dppi.h>
#include <nrfx_egu.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/logging/log.h>

#include "fixed.h"
#include "freq_ctlr.h"
#include "freq_est.h"
#include "input_codec.h"
#include "sync_timer.h"

LOG_MODULE_REGISTER(audio);

#define AUDIO_EGU_IDX 0
#define AUDIO_EGU_IRQ NRFX_IRQ_NUMBER_GET(NRF_EGU_INST_GET(AUDIO_EGU_IDX))

// TODO: sample rate must currently be divisible by the number of samples
#define AUDIO_BLOCK_SIZE 17640
#define AUDIO_BLOCK_COUNT 4

#define AUDIO_HFCLKAUDIO_FREQ_NOMINAL \
    DT_PROP(DT_NODELABEL(clock), hfclkaudio_frequency)

#define AUDIO_HFCLKAUDIO_FREQ_REG_MIN 12519
#define AUDIO_HFCLKAUDIO_FREQ_REG_MAX 18068

K_MEM_SLAB_DEFINE_STATIC(audio_slab, AUDIO_BLOCK_SIZE, AUDIO_BLOCK_COUNT, 4);

static K_SEM_DEFINE(audio_started, 0, 1);
static K_THREAD_STACK_DEFINE(audio_thread_stack, 512);

static void audio_sync_work_handler(struct k_work *);
static K_WORK_DEFINE(audio_sync_work, audio_sync_work_handler);

static struct audio {
    // Resources
    const struct device *const codec;
    const struct device *const i2s;
    struct k_mem_slab *const slab;
    struct k_sem *const started;
    struct k_thread thread;

    struct freq_est freq_est;
    const struct freq_ctlr freq_ctlr;
    struct k_work *const sync_work;

    // State
    /// Time increment per buffer
    qu32_32 time_increment;
    ///
    qu32_32 time;
    qu32_32 ref_time;
    bool target_locked;
    qu32_32 target_theta;
    /// Last controller input
    int16_t hfclkaudio_increment;
} audio = {
    .codec = DEVICE_DT_GET(DT_NODELABEL(sgtl5000)),
    .i2s = DEVICE_DT_GET(DT_NODELABEL(i2s_codec)),
    .slab = &audio_slab,
    .started = &audio_started,

    .freq_ctlr =
        {
            .k_theta = 4.03747559e-11,
            .k_f = 6.45996094e-05,
            .max_step = 1000,
        },
    .sync_work = &audio_sync_work,
};

static const struct freq_est_config FREQ_EST_CONFIG = {
    .nominal_freq = SYNC_TIMER_FREQ,
    .k_u = 32e6 / (12.0 * (1 << 16) * AUDIO_HFCLKAUDIO_FREQ_NOMINAL),
    .q_theta = 0.0,
    .q_f = 256.0,
    .r = 390625.0,
    .p0 = 1e6,
};

#define TRIGGER_PIN 4
static const struct device *gpio = DEVICE_DT_GET(DT_NODELABEL(gpio0));

static void audio_thread_run(void *p1, void *p2, void *p3) {
    int err;
    struct audio *a = &audio;
    gpio_pin_configure(gpio, TRIGGER_PIN, GPIO_OUTPUT);

    err = i2s_trigger(a->i2s, I2S_DIR_RX, I2S_TRIGGER_START);
    if (err) {
        LOG_ERR("failed to trigger I2S (err %d)", err);
        return;
    }

    while (true) {
        void *block = NULL;
        uint32_t block_size;

        err = i2s_read(a->i2s, &block, &block_size);
        if (err) {
            LOG_ERR("failed to read I2S (err %d)", err);
            break;
        }
        k_sem_give(a->started);

        k_mem_slab_free(a->slab, block);
    }
}

static void audio_sync_work_handler(struct k_work *item) {
    struct audio *a = &audio;
    irq_disable(AUDIO_EGU_IRQ);
    qu32_32 time = a->time;
    qu32_32 ref_time = a->ref_time;
    irq_enable(AUDIO_EGU_IRQ);

    if (!sync_timer_correct_time(&ref_time)) {
        return;
    }

    freq_est_update(&a->freq_est, time, ref_time, a->hfclkaudio_increment);

    struct freq_est_state state = freq_est_get_state(&a->freq_est);
    if (!a->target_locked) {
        a->target_theta = state.theta;
        a->target_locked = true;
    }
    a->hfclkaudio_increment =
        freq_ctlr_update(&a->freq_ctlr, a->target_theta, state);

    uint16_t freq = nrf_clock_hfclkaudio_config_get(NRF_CLOCK);

    // Clamp frequency in bounds
    int16_t max_inc = AUDIO_HFCLKAUDIO_FREQ_REG_MAX - freq;
    int16_t min_inc = AUDIO_HFCLKAUDIO_FREQ_REG_MIN - freq;
    if (a->hfclkaudio_increment > max_inc) {
        a->hfclkaudio_increment = max_inc;
    } else if (a->hfclkaudio_increment < min_inc) {
        a->hfclkaudio_increment = min_inc;
    }
    freq += a->hfclkaudio_increment;

    nrf_clock_hfclkaudio_config_set(NRF_CLOCK, freq);
    printk("audio,%" PRIu64 ",%" PRIu64 ",%" PRIu16 ",%" PRIi16 ",%e\n", time,
           ref_time + a->target_theta, freq, a->hfclkaudio_increment,
           state.f / QU32_32_ONE);
}

static void audio_egu_handler(uint8_t event_idx, void *p_context) {
    struct audio *a = p_context;
    a->time += a->time_increment;
    a->ref_time = qu32_32_from_int(sync_timer_get_i2s_time());
    k_work_submit(a->sync_work);
}

int audio_init() {
    int err;
    struct audio *a = &audio;

    if (!device_is_ready(a->codec)) {
        LOG_ERR("%s is not ready\n", a->codec->name);
        return -ENODEV;
    }

    if (!device_is_ready(a->i2s)) {
        LOG_ERR("%s is not ready\n", a->i2s->name);
        return -ENODEV;
    }

    struct audio_codec_cfg cfg = {
        .mclk_freq = AUDIO_HFCLKAUDIO_FREQ_NOMINAL,
        .dai_type = AUDIO_DAI_TYPE_I2S,
        .dai_cfg.i2s =
            {
                .word_size = 16,
                .channels = 2,
                .format = I2S_FMT_DATA_FORMAT_I2S,
                .options = I2S_OPT_BIT_CLK_MASTER | I2S_OPT_FRAME_CLK_MASTER,
                .frame_clk_freq = 44100,
                .mem_slab = a->slab,
                .block_size = a->slab->info.block_size,
                .timeout = 1000,
            },
    };

    // TODO: allow sample-rates that aren't a multiple of sample/block.
    // Naive implementation overflows 64-bit integer with intermediate
    // result.
    a->time_increment =
        qu32_32_from_int((uint64_t)SYNC_TIMER_FREQ *
                         (AUDIO_BLOCK_SIZE / cfg.dai_cfg.i2s.channels /
                          2 /* bytes per sample */) /
                         cfg.dai_cfg.i2s.frame_clk_freq);

    err = i2s_configure(a->i2s, I2S_DIR_RX, &cfg.dai_cfg.i2s);
    if (err < 0) {
        LOG_ERR("failed to configure I2S (err %d)", err);
        return err;
    }

    freq_est_init(&a->freq_est, &FREQ_EST_CONFIG);

    nrfx_egu_t egu = NRFX_EGU_INSTANCE(AUDIO_EGU_IDX);

    IRQ_CONNECT(AUDIO_EGU_IRQ, IRQ_PRIO_LOWEST,
                NRFX_EGU_INST_HANDLER_GET(AUDIO_EGU_IDX), 0, 0);

    nrfx_err_t nerr =
        nrfx_egu_init(&egu, 0 /* ignored */, audio_egu_handler, a);
    if (NRFX_SUCCESS != nerr) {
        LOG_ERR("failed to configure EGU for I2S (err %d)", nerr);
        return -EALREADY;
    }

    uint8_t i2s_dppi = sync_timer_get_i2s_dppi();
    nrf_i2s_publish_set(NRF_I2S0, NRF_I2S_EVENT_RXPTRUPD, i2s_dppi);
    nrf_egu_subscribe_set(egu.p_reg, NRF_EGU_TASK_TRIGGER0, i2s_dppi);
    nrfx_egu_int_enable(&egu, NRF_EGU_INT_TRIGGERED0);
    nrfx_dppi_channel_enable(i2s_dppi);

    k_thread_create(&a->thread, audio_thread_stack,
                    K_THREAD_STACK_SIZEOF(audio_thread_stack), audio_thread_run,
                    NULL, NULL, NULL, K_PRIO_COOP(7), 0, K_NO_WAIT);
    k_thread_name_set(&a->thread, "audio");

    err = k_sem_take(a->started, K_MSEC(500));
    if (err < 0) {
        LOG_ERR("audio did not start");
        return err;
    }

    err = input_codec_configure(a->codec, &cfg);
    if (err < 0) {
        LOG_ERR("failed to configure codec (err %d)", err);
        return err;
    }

    return 0;
}

int audio_start(struct audio *a) {
    return i2s_trigger(a->i2s, I2S_DIR_RX, I2S_TRIGGER_START);
}