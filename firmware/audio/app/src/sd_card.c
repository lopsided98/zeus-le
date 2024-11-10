#include "sd_card.h"

#include <ff.h>
#include <zephyr/drivers/sdhc.h>
#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/disk_access.h>

#include "record.h"

LOG_MODULE_REGISTER(sd_card);

#define HAS_SDHC DT_NODE_EXISTS(DT_NODELABEL(sdhc0))

#if HAS_SDHC
static void sd_card_work_handler(struct k_work* work);
static K_WORK_DELAYABLE_DEFINE(sd_card_work, sd_card_work_handler);
#endif

static const struct sd_card_config {
    const char* name;
#if HAS_SDHC
    const struct device* sd;
    struct k_work_delayable* work;
#endif
} sd_card_config = {
    .name = "SD",
#if HAS_SDHC
    .sd = DEVICE_DT_GET(DT_NODELABEL(sdhc0)),
    .work = &sd_card_work,
#endif
};

static struct sd_card_data {
    struct fs_mount_t mount;
    FATFS fat_fs;

    // State
    bool init;
    bool disk_init;
    bool mounted;
} sd_card = {
    .mount =
        {
            .type = FS_FATFS,
            .fs_data = &sd_card.fat_fs,
            .mnt_point = "/SD:",
        },
};

static int sd_card_print_info(const char* disk_pdrv) {
    uint32_t sector_count;
    int ret = disk_access_ioctl(disk_pdrv, DISK_IOCTL_GET_SECTOR_COUNT,
                                &sector_count);
    if (ret < 0) {
        LOG_ERR("failed to get sector count (err %d)", ret);
        return ret;
    }

    uint32_t sector_size;
    ret =
        disk_access_ioctl(disk_pdrv, DISK_IOCTL_GET_SECTOR_SIZE, &sector_size);
    if (ret < 0) {
        LOG_ERR("failed to get sector size (err %d)", ret);
        return ret;
    }

    uint64_t memory_size_bytes = (uint64_t)sector_count * sector_size;
    uint32_t memory_size_mib = memory_size_bytes / 1024 / 1024;
    if (memory_size_mib >= 1024) {
        uint32_t memory_size_gib_whole = memory_size_mib / 1024;
        uint32_t memory_size_gib_frac =
            DIV_ROUND_CLOSEST((memory_size_mib % 1024) * 10, 1024);
        LOG_INF("Found %" PRIu32 ".%" PRIu32 " GiB SD card",
                memory_size_gib_whole, memory_size_gib_frac);
    } else {
        LOG_INF("Found %" PRIu32 " MiB SD card", memory_size_mib);
    }

    return 0;
}

static int sd_card_inserted(void) {
    const struct sd_card_config* config = &sd_card_config;
    struct sd_card_data* data = &sd_card;
    int ret;

    LOG_INF("SD card inserted");

    if (!data->disk_init) {
        ret = disk_access_ioctl(config->name, DISK_IOCTL_CTRL_INIT, NULL);
        if (ret < 0) {
            LOG_ERR("failed to initialize SD card: (err %d)", ret);
            return ret;
        }

        data->disk_init = true;

        ret = sd_card_print_info(config->name);
        if (ret < 0) return ret;
    }

    if (!data->mounted) {
        ret = fs_mount(&data->mount);
        if (ret != FR_OK) {
            LOG_ERR("failed to mount SD card: (err %d)", ret);
            return -ENODEV;
        }

        data->mounted = true;
    }

    ret = record_card_inserted();
    if (ret) return ret;

    return 0;
}

#if HAS_SDHC
static int sd_card_removed(void) {
    const struct sd_card_config* config = &sd_card_config;
    struct sd_card_data* data = &sd_card;
    int ret;

    LOG_INF("SD card removed");

    if (data->mounted) {
        ret = fs_unmount(&data->mount);
        if (ret != FR_OK) {
            LOG_ERR("failed to unmount SD card: (err %d)", ret);
            return -ENODEV;
        }

        data->mounted = false;
    }

    if (data->disk_init) {
        ret = disk_access_ioctl(config->name, DISK_IOCTL_CTRL_DEINIT, NULL);
        if (ret < 0) {
            LOG_ERR("failed to deinitialize SD card: (err %d)", ret);
            return ret;
        }

        data->disk_init = false;
    }

    return 0;
}

static void sd_card_work_handler(struct k_work* work) {
    const struct sd_card_config* config = &sd_card_config;
    if (sdhc_card_present(config->sd)) {
        sd_card_inserted();
    } else {
        sd_card_removed();
    }
}

static void sd_card_interrupt(const struct device* dev, int reason,
                              const void* user_data) {
    const struct sd_card_config* config = &sd_card_config;

    // Wait 100ms to allow bouncing to settle
    // TODO: probably shouldn't run this on the global workqueue
    k_work_reschedule(config->work, K_MSEC(100));
}
#endif

int sd_card_init(void) {
    const struct sd_card_config* config = &sd_card_config;
    struct sd_card_data* data = &sd_card;
    int ret;
    if (data->init) return -EALREADY;

#if HAS_SDHC
    ret = sdhc_enable_interrupt(config->sd, sd_card_interrupt,
                                SDHC_INT_INSERTED | SDHC_INT_REMOVED, NULL);
    if (ret) return ret;
    k_work_schedule(config->work, K_NO_WAIT);
#else
    ret = sd_card_inserted();
    if (ret) return ret;
#endif

    data->init = true;
    return 0;
}