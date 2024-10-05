#include <math.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/shell/shell.h>
#include <zeus/drivers/sensor/bq2515x_adc.h>

#include "mgr.h"

static const struct shell_config {
    const struct device *charger_adc;
} shell_config = {
    .charger_adc = DEVICE_DT_GET(DT_NODELABEL(charger_adc)),
};

static int cmd_pair(const struct shell *sh, size_t argc, char **argv) {
    shell_print(sh, "start pairing command");
    return mgr_pair_start();
}

#define PRIsensor_value(decimals) "%" PRIu32 ".%0" #decimals PRIu32
#define PRIsensor_value_args(val, decimals) \
    ((val).val1),                           \
        DIV_ROUND_CLOSEST((val).val2, (uint32_t)pow(10, 6 - (decimals)))

static int cmd_battery(const struct shell *sh, size_t argc, char **argv) {
    const struct shell_config *config = &shell_config;
    struct sensor_value vin;
    struct sensor_value pmid;
    struct sensor_value vbat;
    struct sensor_value iin;
    struct sensor_value ichg;
    int ret;

    ret = sensor_sample_fetch(config->charger_adc);
    if (ret) {
        shell_print(sh, "failed to fetch ADC (err %d)", ret);
        return ret;
    }

    ret = sensor_channel_get(config->charger_adc,
                             (int)SENSOR_CHAN_BQ2515X_ADC_VIN, &vin);
    if (ret) return ret;

    ret = sensor_channel_get(config->charger_adc,
                             (int)SENSOR_CHAN_BQ2515X_ADC_PMID, &pmid);
    if (ret) return ret;

    ret = sensor_channel_get(config->charger_adc, SENSOR_CHAN_GAUGE_VOLTAGE,
                             &vbat);
    if (ret) return ret;

    ret = sensor_channel_get(config->charger_adc,
                             (int)SENSOR_CHAN_BQ2515X_ADC_IIN, &iin);
    if (ret) return ret;

    ret = sensor_channel_get(config->charger_adc,
                             (int)SENSOR_CHAN_BQ2515X_ADC_ICHG, &ichg);
    if (ret) return ret;

    shell_print(sh, "Input voltage:   " PRIsensor_value(3) " V",
                PRIsensor_value_args(vin, 3));
    shell_print(sh, "Input current:   " PRIsensor_value(4) " A",
                PRIsensor_value_args(iin, 4));
    shell_print(sh, "System voltage:  " PRIsensor_value(3) " V",
                PRIsensor_value_args(pmid, 3));
    shell_print(sh, "Battery voltage: " PRIsensor_value(3) " V",
                PRIsensor_value_args(vbat, 3));
    shell_print(sh, "Charge rate:     " PRIsensor_value(2) "%%",
                PRIsensor_value_args(ichg, 2));
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
    sub_zeus, SHELL_CMD(pair, NULL, "Pair with a central node", cmd_pair),
    SHELL_CMD(battery, NULL, "Print battery status", cmd_battery),
    SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(zeus, &sub_zeus, "Zeus commands", NULL);
