#include "led.h"

#include <zephyr/drivers/led.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zeus/drivers/led/lp58xx.h>

LOG_MODULE_REGISTER(led);

#define LED_CHANNELS 3
#define LED_BLUE 0
#define LED_GREEN 1
#define LED_RED 2

#define LED_FADE_MS 360

static K_MUTEX_DEFINE(led_mutex);

enum led_record_state {
    LED_RECORD_WAITING,
    LED_RECORD_RUNNING,
    LED_RECORD_IDLE,
};

enum led_pattern {
    LED_PATTERN_OFF,
    LED_PATTERN_STARTUP,
    LED_PATTERN_IDLE_SYNCED,
    LED_PATTERN_IDLE_NOT_SYNCED,
    LED_PATTERN_RECORD_WAITING,
    LED_PATTERN_RECORD_RUNNING,
    LED_PATTERN_COUNT,
};

typedef int(led_pattern_func)(void);

static int led_pattern_off(void);
static int led_pattern_startup(void);
static int led_pattern_idle_synced(void);
static int led_pattern_idle_not_synced(void);
static int led_pattern_record_waiting(void);
static int led_pattern_record_running(void);

const struct led_config {
    const struct device* led;
    struct k_mutex* mutex;
    led_pattern_func* patterns[LED_PATTERN_COUNT];
} led_config = {
    .led = DEVICE_DT_GET(DT_NODELABEL(rgb_led)),
    .mutex = &led_mutex,
    .patterns =
        {
            [LED_PATTERN_OFF] = led_pattern_off,
            [LED_PATTERN_STARTUP] = led_pattern_startup,
            [LED_PATTERN_IDLE_SYNCED] = led_pattern_idle_synced,
            [LED_PATTERN_IDLE_NOT_SYNCED] = led_pattern_idle_not_synced,
            [LED_PATTERN_RECORD_WAITING] = led_pattern_record_waiting,
            [LED_PATTERN_RECORD_RUNNING] = led_pattern_record_running,
        },
};

struct led_data {
    bool synced;
    enum led_record_state record_state;
    enum led_pattern pattern;
} led_data = {
    .synced = false,
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
        .aeu = {{
            .pwm = {start_pwm, 0, 0, 0, 0},
            .time_msec = {LED_FADE_MS, 0, 0, 0},
            .repeat = 0,
        }},
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
        .aeu = {{
                    .pwm = {start_pwm[LED_BLUE], 50, 50, 50, 50},
                    .time_msec = {LED_FADE_MS, 8050, 8050, 8050},
                    .repeat = 0,
                },
                {
                    .pwm = {50, 50, 50, 50, 50},
                    .time_msec = {8050, 8050, 8050, 8050},
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
        .aeu = {{
                    .pwm = {start_pwm[LED_BLUE], 100, 100, 100, 100},
                    .time_msec = {LED_FADE_MS, 0, 0, 0},
                    .repeat = 0,
                },
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

    ret = led_configure_fade_out(LED_BLUE, start_pwm[LED_GREEN]);
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

    const struct lp58xx_ae_config green_ae_cfg = {
        .pause_start_msec = 0,
        .pause_end_msec = 0,
        .num_aeu = 2,
        .repeat = 0,
        .aeu = {{
                    .pwm = {start_pwm[LED_GREEN], 0, 0, 0, 0},
                    .time_msec = {1070, 0, 0, 0},
                    .repeat = 0,
                },
                {
                    .pwm = {0, 0, 50, 0, 0},
                    .time_msec = {8050, 90, 90, 0},
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

static int led_update(void) {
    struct led_data* data = &led_data;

    enum led_pattern pattern = LED_PATTERN_OFF;
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

    return led_set_pattern(pattern);
}

int led_boot(void) {
    const struct led_config* cfg = &led_config;
    int ret;

    k_mutex_lock(cfg->mutex, K_FOREVER);
    ret = led_set_pattern(LED_PATTERN_STARTUP);
    k_mutex_unlock(cfg->mutex);
    return ret;
}

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