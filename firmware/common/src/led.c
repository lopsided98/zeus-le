#include "zeus/led.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zeus/drivers/led/lp58xx.h>

#include "zeus/protocol.h"
#include "zeus/util.h"

LOG_MODULE_REGISTER(led);

/// Interval between LED sync pulses while recording, in sync timer units. Power
/// of two to avoid dealing with uneven interval at timer wraparound.
#define LED_RECORD_SYNC_INTERVAL NHPOT(ZEUS_TIME_NOMINAL_FREQ * 8)

#define LED_CHANNELS 3
#define LED_BLUE 0
#define LED_GREEN 1
#define LED_RED 2

#define LED_FADE_MS 360

#define LED_AEU_FADE(start_pwm, end_pwm)                  \
    (struct lp58xx_aeu_config) {                          \
        .pwm = {start_pwm, end_pwm, 0, 0, 0},             \
        .time_msec = {LED_FADE_MS, 0, 0, 0}, .repeat = 0, \
    }

#define LED_AEU_CONSTANT(pwm_val)                             \
    (struct lp58xx_aeu_config) {                              \
        .pwm = {pwm_val, pwm_val, pwm_val, pwm_val, pwm_val}, \
        .time_msec = {8050, 8050, 8050, 8050},                \
        .repeat = LP58XX_AEU_REPEAT_INFINITE,                 \
    }

static inline struct lp58xx_aeu_config led_aeu_scale(
    struct lp58xx_aeu_config aeu, uint8_t scale_pwm) {
    return (struct lp58xx_aeu_config){
        .pwm =
            {
                aeu.pwm[0] * scale_pwm / 255,
                aeu.pwm[1] * scale_pwm / 255,
                aeu.pwm[2] * scale_pwm / 255,
                aeu.pwm[3] * scale_pwm / 255,
                aeu.pwm[4] * scale_pwm / 255,
            },
        .time_msec =
            {
                aeu.time_msec[0],
                aeu.time_msec[1],
                aeu.time_msec[2],
                aeu.time_msec[3],
            },
        .repeat = aeu.repeat,
    };
}

static inline struct lp58xx_ae_config led_ae_scale(struct lp58xx_ae_config ae,
                                                   uint8_t scale_pwm) {
    return (struct lp58xx_ae_config){
        .pause_start_msec = ae.pause_start_msec,
        .pause_end_msec = ae.pause_end_msec,
        .num_aeu = ae.num_aeu,
        .repeat = ae.repeat,
        .aeu =
            {
                led_aeu_scale(ae.aeu[0], scale_pwm),
                led_aeu_scale(ae.aeu[1], scale_pwm),
                led_aeu_scale(ae.aeu[1], scale_pwm),
            },
    };
}

enum led_power_state {
    /// Not known yet whether the system will boot or power off
    LED_POWER_UNKNOWN,
    LED_POWER_ON,
    LED_POWER_OFF,
};

enum led_record_state {
    LED_RECORD_WAITING,
    LED_RECORD_RUNNING,
    LED_RECORD_IDLE,
};

enum led_battery_state {
    LED_BATTERY_DISCHARGING,
    LED_BATTERY_CHARGING,
    LED_BATTERY_FULL,
    LED_BATTERY_ERROR,
};

enum led_pattern {
    LED_PATTERN_OFF,
    LED_PATTERN_STARTUP,
    LED_PATTERN_BATTERY_CHARGING,
    LED_PATTERN_BATTERY_FULL,
#if IS_ENABLED(CONFIG_ZEUS_NODE_AUDIO)
    LED_PATTERN_IDLE_SYNCED,
    LED_PATTERN_IDLE_NOT_SYNCED,
#elif IS_ENABLED(CONFIG_ZEUS_NODE_CENTRAL)
    LED_PATTERN_IDLE,
#endif
    LED_PATTERN_RECORD_WAITING,
    LED_PATTERN_RECORD_RUNNING,
    LED_PATTERN_COUNT,
};

typedef int(led_pattern_func)(void);

static int led_pattern_off(void);
static int led_pattern_startup(void);
static int led_pattern_battery_charging(void);
static int led_pattern_battery_full(void);
#if IS_ENABLED(CONFIG_ZEUS_NODE_AUDIO)
static int led_pattern_idle_synced(void);
static int led_pattern_idle_not_synced(void);
#elif IS_ENABLED(CONFIG_ZEUS_NODE_CENTRAL)
static int led_pattern_idle(void);
#endif
static int led_pattern_record_waiting(void);
static int led_pattern_record_running(void);

static void led_record_sync_work_handler(struct k_work* work);
static K_WORK_DELAYABLE_DEFINE(led_record_sync_work,
                               led_record_sync_work_handler);

static K_MUTEX_DEFINE(led_mutex);

static const struct led_config {
    const struct device* led;
    struct k_mutex* mutex;
    struct k_work_delayable* record_sync_work;
    led_pattern_func* patterns[LED_PATTERN_COUNT];
} led_config = {
    .led = DEVICE_DT_GET(DT_NODELABEL(rgb_led)),
    .mutex = &led_mutex,
    .record_sync_work = &led_record_sync_work,
    .patterns =
        {
            [LED_PATTERN_OFF] = led_pattern_off,
            [LED_PATTERN_STARTUP] = led_pattern_startup,
            [LED_PATTERN_BATTERY_CHARGING] = led_pattern_battery_charging,
            [LED_PATTERN_BATTERY_FULL] = led_pattern_battery_full,
#if IS_ENABLED(CONFIG_ZEUS_NODE_AUDIO)
            [LED_PATTERN_IDLE_SYNCED] = led_pattern_idle_synced,
            [LED_PATTERN_IDLE_NOT_SYNCED] = led_pattern_idle_not_synced,
#elif IS_ENABLED(CONFIG_ZEUS_NODE_CENTRAL)
            [LED_PATTERN_IDLE] = led_pattern_idle,
#endif
            [LED_PATTERN_RECORD_WAITING] = led_pattern_record_waiting,
            [LED_PATTERN_RECORD_RUNNING] = led_pattern_record_running,
        },
};

static struct led_data {
    enum led_power_state power_state;
    enum led_battery_state battery_state;
#if IS_ENABLED(CONFIG_ZEUS_NODE_AUDIO)
    bool synced;
#endif
    enum led_record_state record_state;
    enum led_pattern pattern;
} led_data = {
    .power_state = LED_POWER_UNKNOWN,
    .battery_state = LED_BATTERY_DISCHARGING,
#if IS_ENABLED(CONFIG_ZEUS_NODE_AUDIO)
    .synced = false,
#endif
    .record_state = LED_RECORD_IDLE,
    .pattern = LED_PATTERN_OFF,
};

static int led_configure_fade_out(uint8_t channel, uint8_t start_pwm) {
    const struct led_config* cfg = &led_config;

    const struct lp58xx_ae_config ae_cfg = {
        .pause_start_msec = 0,
        .pause_end_msec = 0,
        .num_aeu = 1,
        .repeat = 0,
        .aeu = {LED_AEU_FADE(start_pwm, 0)},
    };

    return lp58xx_ae_configure(cfg->led, channel, &ae_cfg);
}

static int led_pattern_off(void) {
    const struct led_config* cfg = &led_config;
    int ret;

    ret = lp58xx_pause(cfg->led);
    if (ret < 0) return ret;

    uint8_t start_pwm[LED_CHANNELS];
    ret = lp58xx_get_auto_pwm(cfg->led, 0, ARRAY_SIZE(start_pwm), start_pwm);
    if (ret < 0) return ret;

    ret = led_configure_fade_out(LED_BLUE, start_pwm[LED_BLUE]);
    if (ret < 0) return ret;

    ret = led_configure_fade_out(LED_GREEN, start_pwm[LED_GREEN]);
    if (ret < 0) return ret;

    ret = led_configure_fade_out(LED_RED, start_pwm[LED_RED]);
    if (ret < 0) return ret;

    return lp58xx_start(cfg->led);
}

#if IS_ENABLED(CONFIG_ZEUS_NODE_AUDIO)
static int led_pattern_startup(void) {
    const struct led_config* cfg = &led_config;
    int ret;

    static const struct lp58xx_ae_config ae_cfg = {
        .pause_start_msec = 0,
        .pause_end_msec = 0,
        .num_aeu = 2,
        .repeat = 0,
        .aeu = {{
                    .pwm = {0, 255, 100, 100, 100},
                    .time_msec = {540, 360, 0, 0},
                    .repeat = 0,
                },
                {
                    .pwm = {100, 50, 2, 50, 100},
                    .time_msec = {360, 360, 360, 360},
                    .repeat = LP58XX_AEU_REPEAT_INFINITE,
                }},
    };

    ret = lp58xx_ae_configure(cfg->led, LED_BLUE, &ae_cfg);
    if (ret < 0) return ret;

    return lp58xx_start(cfg->led);
}
#elif IS_ENABLED(CONFIG_ZEUS_NODE_CENTRAL)
static int led_pattern_startup(void) {
    const struct led_config* cfg = &led_config;
    int ret;

    static const struct lp58xx_ae_config ae_cfg = {
        .pause_start_msec = 0,
        .pause_end_msec = 0,
        .num_aeu = 2,
        .repeat = 0,
        .aeu =
            {
                {
                    .pwm = {0, 255, 50, 50, 50},
                    .time_msec = {540, 540, 0, 0},
                    .repeat = 0,
                },
                LED_AEU_CONSTANT(50),
            },
    };

    const struct lp58xx_ae_config red_ae_cfg = led_ae_scale(ae_cfg, 70);
    const struct lp58xx_ae_config blue_ae_cfg = led_ae_scale(ae_cfg, 100);

    ret = lp58xx_ae_configure(cfg->led, LED_RED, &red_ae_cfg);
    if (ret < 0) return ret;

    ret = lp58xx_ae_configure(cfg->led, LED_BLUE, &blue_ae_cfg);
    if (ret < 0) return ret;

    return lp58xx_start(cfg->led);
}
#endif

static int led_pattern_battery_charging(void) {
    const struct led_config* cfg = &led_config;
    int ret;

    ret = lp58xx_pause(cfg->led);
    if (ret < 0) return ret;

    uint8_t start_pwm[LED_CHANNELS];
    ret = lp58xx_get_auto_pwm(cfg->led, 0, ARRAY_SIZE(start_pwm), start_pwm);
    if (ret < 0) return ret;

    const struct lp58xx_ae_config green_ae_cfg = {
        .pause_start_msec = 0,
        .pause_end_msec = 0,
        .num_aeu = 2,
        .repeat = 0,
        .aeu = {{
                    .pwm = {start_pwm[LED_GREEN], 0, 0, 50, 50},
                    .time_msec = {LED_FADE_MS, 360, 360, 0},
                    .repeat = 0,
                },
                {
                    .pwm = {50, 20, 2, 20, 50},
                    .time_msec = {540, 800, 800, 540},
                    .repeat = LP58XX_AEU_REPEAT_INFINITE,
                }},
    };

    ret = led_configure_fade_out(LED_BLUE, start_pwm[LED_BLUE]);
    if (ret < 0) return ret;

    ret = lp58xx_ae_configure(cfg->led, LED_GREEN, &green_ae_cfg);
    if (ret < 0) return ret;

    ret = led_configure_fade_out(LED_RED, start_pwm[LED_RED]);
    if (ret < 0) return ret;

    return lp58xx_start(cfg->led);
}

static int led_pattern_battery_full(void) {
    const struct led_config* cfg = &led_config;
    int ret;

    ret = lp58xx_pause(cfg->led);
    if (ret < 0) return ret;

    uint8_t start_pwm[LED_CHANNELS];
    ret = lp58xx_get_auto_pwm(cfg->led, 0, ARRAY_SIZE(start_pwm), start_pwm);
    if (ret < 0) return ret;

    const struct lp58xx_ae_config green_ae_cfg = {
        .pause_start_msec = 0,
        .pause_end_msec = 0,
        .num_aeu = 2,
        .repeat = 0,
        .aeu =
            {
                LED_AEU_FADE(start_pwm[LED_GREEN], 50),
                LED_AEU_CONSTANT(50),
            },
    };

    ret = led_configure_fade_out(LED_BLUE, start_pwm[LED_BLUE]);
    if (ret < 0) return ret;

    ret = lp58xx_ae_configure(cfg->led, LED_GREEN, &green_ae_cfg);
    if (ret < 0) return ret;

    ret = led_configure_fade_out(LED_RED, start_pwm[LED_RED]);
    if (ret < 0) return ret;

    return lp58xx_start(cfg->led);
}

#if IS_ENABLED(CONFIG_ZEUS_NODE_AUDIO)

static int led_pattern_idle_synced(void) {
    const struct led_config* cfg = &led_config;
    int ret;

    ret = lp58xx_pause(cfg->led);
    if (ret < 0) return ret;

    uint8_t start_pwm[LED_CHANNELS];
    ret = lp58xx_get_auto_pwm(cfg->led, 0, ARRAY_SIZE(start_pwm), start_pwm);
    if (ret < 0) return ret;

    const struct lp58xx_ae_config blue_ae_cfg = {
        .pause_start_msec = 0,
        .pause_end_msec = 0,
        .num_aeu = 2,
        .repeat = 0,
        .aeu =
            {
                LED_AEU_FADE(start_pwm[LED_BLUE], 50),
                LED_AEU_CONSTANT(50),
            },
    };

    ret = lp58xx_ae_configure(cfg->led, LED_BLUE, &blue_ae_cfg);
    if (ret < 0) return ret;

    ret = led_configure_fade_out(LED_GREEN, start_pwm[LED_GREEN]);
    if (ret < 0) return ret;

    ret = led_configure_fade_out(LED_RED, start_pwm[LED_RED]);
    if (ret < 0) return ret;

    return lp58xx_start(cfg->led);
}

static int led_pattern_idle_not_synced(void) {
    const struct led_config* cfg = &led_config;
    int ret;

    ret = lp58xx_pause(cfg->led);
    if (ret < 0) return ret;

    uint8_t start_pwm[LED_CHANNELS];
    ret = lp58xx_get_auto_pwm(cfg->led, 0, ARRAY_SIZE(start_pwm), start_pwm);
    if (ret < 0) return ret;

    const struct lp58xx_ae_config blue_ae_cfg = {
        .pause_start_msec = 0,
        .pause_end_msec = 0,
        .num_aeu = 2,
        .repeat = 0,
        .aeu = {LED_AEU_FADE(start_pwm[LED_BLUE], 100),
                {
                    .pwm = {100, 50, 2, 50, 100},
                    .time_msec = {360, 360, 360, 360},
                    .repeat = LP58XX_AEU_REPEAT_INFINITE,
                }},
    };

    ret = lp58xx_ae_configure(cfg->led, LED_BLUE, &blue_ae_cfg);
    if (ret < 0) return ret;

    ret = led_configure_fade_out(LED_GREEN, start_pwm[LED_GREEN]);
    if (ret < 0) return ret;

    ret = led_configure_fade_out(LED_RED, start_pwm[LED_RED]);
    if (ret < 0) return ret;

    return lp58xx_start(cfg->led);
}

#elif IS_ENABLED(CONFIG_ZEUS_NODE_CENTRAL)

static int led_pattern_idle(void) {
    const struct led_config* cfg = &led_config;
    int ret;

    ret = lp58xx_pause(cfg->led);
    if (ret < 0) return ret;

    uint8_t start_pwm[LED_CHANNELS];
    ret = lp58xx_get_auto_pwm(cfg->led, 0, ARRAY_SIZE(start_pwm), start_pwm);
    if (ret < 0) return ret;

    const struct lp58xx_ae_config red_ae_cfg = {
        .pause_start_msec = 0,
        .pause_end_msec = 0,
        .num_aeu = 2,
        .repeat = 0,
        .aeu =
            {
                LED_AEU_FADE(start_pwm[LED_RED], 19),
                LED_AEU_CONSTANT(19),
            },
    };

    const struct lp58xx_ae_config blue_ae_cfg = {
        .pause_start_msec = 0,
        .pause_end_msec = 0,
        .num_aeu = 2,
        .repeat = 0,
        .aeu =
            {
                LED_AEU_FADE(start_pwm[LED_BLUE], 14),
                LED_AEU_CONSTANT(14),
            },
    };

    ret = lp58xx_ae_configure(cfg->led, LED_RED, &red_ae_cfg);
    if (ret < 0) return ret;

    ret = led_configure_fade_out(LED_GREEN, start_pwm[LED_GREEN]);
    if (ret < 0) return ret;

    ret = lp58xx_ae_configure(cfg->led, LED_BLUE, &blue_ae_cfg);
    if (ret < 0) return ret;

    return lp58xx_start(cfg->led);
}

#endif

static int led_pattern_record_waiting(void) {
    const struct led_config* cfg = &led_config;
    int ret;

    ret = lp58xx_pause(cfg->led);
    if (ret < 0) return ret;

    uint8_t start_pwm[LED_CHANNELS];
    ret = lp58xx_get_auto_pwm(cfg->led, 0, ARRAY_SIZE(start_pwm), start_pwm);
    if (ret < 0) return ret;

    const struct lp58xx_ae_config green_ae_cfg = {
        .pause_start_msec = 0,
        .pause_end_msec = 0,
        .num_aeu = 2,
        .repeat = 0,
        .aeu = {{
                    .pwm = {start_pwm[LED_GREEN], 100, 100, 100, 100},
                    .time_msec = {90, 0, 0, 0},
                    .repeat = 0,
                },
                {
                    .pwm = {100, 2, 100, 0, 0},
                    .time_msec = {90, 90, 0, 0},
                    .repeat = LP58XX_AEU_REPEAT_INFINITE,
                }},
    };

    ret = led_configure_fade_out(LED_BLUE, start_pwm[LED_BLUE]);
    if (ret < 0) return ret;

    ret = lp58xx_ae_configure(cfg->led, LED_GREEN, &green_ae_cfg);
    if (ret < 0) return ret;

    ret = led_configure_fade_out(LED_RED, start_pwm[LED_RED]);
    if (ret < 0) return ret;

    return lp58xx_start(cfg->led);
}

static int led_pattern_record_running(void) {
    const struct led_config* cfg = &led_config;
    int ret;

    ret = lp58xx_pause(cfg->led);
    if (ret < 0) return ret;

    uint8_t start_pwm[LED_CHANNELS];
    ret = lp58xx_get_auto_pwm(cfg->led, 0, ARRAY_SIZE(start_pwm), start_pwm);
    if (ret < 0) return ret;

    const struct lp58xx_ae_config red_ae_cfg = {
        .pause_start_msec = 0,
        .pause_end_msec = 0,
        .num_aeu = 1,
        .repeat = 0,
        .aeu = {{
            .pwm = {start_pwm[LED_RED], 150, 0, 0, 0},
            .time_msec = {180, 800, 0, 0},
            .repeat = 0,
        }},
    };

    ret = led_configure_fade_out(LED_BLUE, start_pwm[LED_BLUE]);
    if (ret < 0) return ret;

    ret = led_configure_fade_out(LED_GREEN, start_pwm[LED_GREEN]);
    if (ret < 0) return ret;

    ret = lp58xx_ae_configure(cfg->led, LED_RED, &red_ae_cfg);
    if (ret < 0) return ret;

    return lp58xx_start(cfg->led);
}

static int led_pattern_record_sync_pulse(void) {
    const struct led_config* cfg = &led_config;
    int ret;

    ret = lp58xx_pause(cfg->led);
    if (ret < 0) return ret;

    uint8_t start_pwm[LED_CHANNELS];
    ret = lp58xx_get_auto_pwm(cfg->led, 0, ARRAY_SIZE(start_pwm), start_pwm);
    if (ret < 0) return ret;

    const struct lp58xx_ae_config red_ae_cfg = {
        .pause_start_msec = 0,
        .pause_end_msec = 0,
        .num_aeu = 1,
        .repeat = 0,
        .aeu = {{
            .pwm = {start_pwm[LED_RED], 100, 0, 0, 0},
            .time_msec = {90, 90, 0, 0},
            .repeat = 0,
        }},
    };

    ret = led_configure_fade_out(LED_BLUE, start_pwm[LED_BLUE]);
    if (ret < 0) return ret;

    ret = led_configure_fade_out(LED_GREEN, start_pwm[LED_GREEN]);
    if (ret < 0) return ret;

    ret = lp58xx_ae_configure(cfg->led, LED_RED, &red_ae_cfg);
    if (ret < 0) return ret;

    return lp58xx_start(cfg->led);
}

static int led_set_pattern(enum led_pattern pattern) {
    const struct led_config* cfg = &led_config;
    struct led_data* data = &led_data;

    if (pattern != data->pattern) {
        data->pattern = pattern;
        led_pattern_func* pattern_func = cfg->patterns[pattern];
        if (pattern_func) {
            return pattern_func();
        } else {
            LOG_ERR("undefined LED pattern: %d", pattern);
            return -1;
        }
    } else {
        return 0;
    }
}

#if IS_ENABLED(CONFIG_ZEUS_NODE_AUDIO)
static int led_update(void) {
    struct led_data* data = &led_data;

    enum led_pattern pattern = LED_PATTERN_OFF;
    switch (data->power_state) {
        case LED_POWER_ON:
            switch (data->record_state) {
                case LED_RECORD_WAITING:
                    pattern = LED_PATTERN_RECORD_WAITING;
                    break;
                case LED_RECORD_RUNNING:
                    pattern = LED_PATTERN_RECORD_RUNNING;
                    break;
                case LED_RECORD_IDLE:
                    if (data->synced) {
                        pattern = LED_PATTERN_IDLE_SYNCED;
                    } else {
                        pattern = LED_PATTERN_IDLE_NOT_SYNCED;
                    }
                    break;
            }
            break;
        case LED_POWER_OFF:
            switch (data->battery_state) {
                case LED_BATTERY_CHARGING:
                    pattern = LED_PATTERN_BATTERY_CHARGING;
                    break;
                case LED_BATTERY_FULL:
                    pattern = LED_PATTERN_BATTERY_FULL;
                    break;
                case LED_BATTERY_DISCHARGING:
                case LED_BATTERY_ERROR:
                    pattern = LED_PATTERN_OFF;
                    break;
            }
            break;
        case LED_POWER_UNKNOWN:
            pattern = LED_PATTERN_OFF;
            break;
    }

    return led_set_pattern(pattern);
}
#elif IS_ENABLED(CONFIG_ZEUS_NODE_CENTRAL)
static int led_update(void) {
    struct led_data* data = &led_data;

    enum led_pattern pattern = LED_PATTERN_OFF;
    switch (data->power_state) {
        case LED_POWER_ON:
            switch (data->record_state) {
                case LED_RECORD_WAITING:
                    pattern = LED_PATTERN_RECORD_WAITING;
                    break;
                case LED_RECORD_RUNNING:
                    pattern = LED_PATTERN_RECORD_RUNNING;
                    break;
                case LED_RECORD_IDLE:
                    pattern = LED_PATTERN_IDLE;
                    break;
            }
            break;
        case LED_POWER_OFF:
            switch (data->battery_state) {
                case LED_BATTERY_CHARGING:
                    pattern = LED_PATTERN_BATTERY_CHARGING;
                    break;
                case LED_BATTERY_FULL:
                    pattern = LED_PATTERN_BATTERY_FULL;
                    break;
                case LED_BATTERY_DISCHARGING:
                case LED_BATTERY_ERROR:
                    pattern = LED_PATTERN_OFF;
                    break;
            }
            break;
        case LED_POWER_UNKNOWN:
            pattern = LED_PATTERN_OFF;
            break;
    }

    return led_set_pattern(pattern);
}
#endif

static void led_record_sync_work_handler(struct k_work* work) {
    const struct led_config* config = &led_config;
    struct led_data* data = &led_data;

    K_MUTEX_AUTO_LOCK(config->mutex);
    if (data->record_state == LED_RECORD_RUNNING) {
        led_pattern_record_sync_pulse();
    }
}

int led_boot(void) {
    const struct led_config* cfg = &led_config;
    struct led_data* data = &led_data;
    int ret;

    k_mutex_lock(cfg->mutex, K_FOREVER);
    data->power_state = LED_POWER_ON;
    ret = led_set_pattern(LED_PATTERN_STARTUP);
    k_mutex_unlock(cfg->mutex);
    return ret;
}

int led_battery_charging(void) {
    const struct led_config* cfg = &led_config;
    struct led_data* data = &led_data;
    int ret;

    k_mutex_lock(cfg->mutex, K_FOREVER);
    data->battery_state = LED_BATTERY_CHARGING;
    ret = led_update();
    k_mutex_unlock(cfg->mutex);
    return ret;
}

int led_battery_full(void) {
    const struct led_config* cfg = &led_config;
    struct led_data* data = &led_data;
    int ret;

    k_mutex_lock(cfg->mutex, K_FOREVER);
    data->battery_state = LED_BATTERY_FULL;
    ret = led_update();
    k_mutex_unlock(cfg->mutex);
    return ret;
}

int led_battery_discharging(void) {
    const struct led_config* cfg = &led_config;
    struct led_data* data = &led_data;
    int ret;

    k_mutex_lock(cfg->mutex, K_FOREVER);
    data->battery_state = LED_BATTERY_DISCHARGING;
    ret = led_update();
    k_mutex_unlock(cfg->mutex);
    return ret;
}

#if IS_ENABLED(CONFIG_ZEUS_NODE_AUDIO)
int led_sync_started(void) {
    const struct led_config* cfg = &led_config;
    struct led_data* data = &led_data;
    int ret;

    k_mutex_lock(cfg->mutex, K_FOREVER);
    data->synced = true;
    ret = led_update();
    k_mutex_unlock(cfg->mutex);
    return ret;
}

int led_sync_terminated(void) {
    const struct led_config* cfg = &led_config;
    struct led_data* data = &led_data;
    int ret;

    k_mutex_lock(cfg->mutex, K_FOREVER);
    data->synced = false;
    ret = led_update();
    k_mutex_unlock(cfg->mutex);
    return ret;
}

#endif

int led_record_waiting(void) {
    const struct led_config* cfg = &led_config;
    struct led_data* data = &led_data;
    int ret;

    k_mutex_lock(cfg->mutex, K_FOREVER);
    data->record_state = LED_RECORD_WAITING;
    ret = led_update();
    k_mutex_unlock(cfg->mutex);
    return ret;
}

int led_record_started(void) {
    const struct led_config* cfg = &led_config;
    struct led_data* data = &led_data;
    int ret;

    k_mutex_lock(cfg->mutex, K_FOREVER);
    data->record_state = LED_RECORD_RUNNING;
    ret = led_update();
    k_mutex_unlock(cfg->mutex);
    return ret;
}

int led_record_sync(uint32_t time) {
    const struct led_config* cfg = &led_config;
    struct led_data* data = &led_data;

    K_MUTEX_AUTO_LOCK(cfg->mutex);
    if (data->record_state == LED_RECORD_RUNNING) {
        uint32_t sync_delay =
            LED_RECORD_SYNC_INTERVAL - (time % LED_RECORD_SYNC_INTERVAL);
        uint64_t sync_delay_ms =
            (uint64_t)sync_delay * 1000 / ZEUS_TIME_NOMINAL_FREQ;
        k_work_schedule(cfg->record_sync_work, K_MSEC(sync_delay_ms));
    }
    return 0;
}

int led_record_stopped(void) {
    const struct led_config* cfg = &led_config;
    struct led_data* data = &led_data;
    int ret;

    k_mutex_lock(cfg->mutex, K_FOREVER);
    data->record_state = LED_RECORD_IDLE;
    ret = led_update();
    k_mutex_unlock(cfg->mutex);
    return ret;
}

int led_shutdown(void) {
    const struct led_config* cfg = &led_config;
    struct led_data* data = &led_data;
    int ret;

    k_mutex_lock(cfg->mutex, K_FOREVER);
    data->power_state = LED_POWER_OFF;
    ret = led_update();
    k_mutex_unlock(cfg->mutex);
    return ret;
}
