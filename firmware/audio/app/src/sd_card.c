#include "sd_card.h"

#include <ff.h>
#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/disk_access.h>

LOG_MODULE_REGISTER(sd_card);

static struct sd_card {
    // Config
    const char* name;

    // Resources
    struct fs_mount_t mount;
    FATFS fat_fs;

    // State
    bool init;
} sd_card = {
    .name = "SD",
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

int sd_card_init(void) {
    struct sd_card* s = &sd_card;
    if (s->init) return -EALREADY;

    int ret = disk_access_init(s->name);
    if (ret < 0) {
        LOG_ERR("failed to initialize SD card: (err %d)", ret);
        return ret;
    }

    ret = sd_card_print_info(s->name);
    if (ret < 0) return ret;

    ret = fs_mount(&s->mount);
    if (ret != FR_OK) {
        LOG_ERR("failed to mount SD card: (err %d)", ret);
        return -ENODEV;
    }

    s->init = true;
    return 0;
}