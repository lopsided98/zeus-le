/*
 * Copyright (c) 2016 Intel Corporation.
 * Copyright (c) 2021,2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <zephyr/types.h>
#include <zephyr/drivers/disk.h>
#include <zephyr/storage/disk_access.h>
#include <errno.h>
#include <zephyr/init.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(disk_cache, CONFIG_DISK_CACHE_LOG_LEVEL);

struct disk_cache_entry {
	sys_dnode_t node;
	uint32_t sector;
	uint8_t data[];
};

struct disk_cache_config {
	const char *const disk_name;
	const size_t sector_size;
	struct k_mem_slab *const entries;
	sys_dlist_t *const lru_list;
};

#define NODE_TO_ENTRY(node) CONTAINER_OF(node, struct disk_cache_entry, node)

/// Check that the cache config is compatible with the underlying disk.
static int disk_cache_check_config(const struct device *dev)
{
	const struct disk_cache_config *config = dev->config;
	int ret;
	uint32_t sector_size;

	ret = disk_access_ioctl(config->disk_name, DISK_IOCTL_GET_SECTOR_SIZE, &sector_size);
	if (ret < 0) {
		return ret;
	}

	if (sector_size != config->sector_size) {
		LOG_ERR("Cache sector size (%u) does not match underlying disk (%u)",
			config->sector_size, sector_size);
		return -EINVAL;
	}
	return 0;
}

/// Flush all entries from the cache.
static void disk_cache_flush(const struct device *dev)
{
	const struct disk_cache_config *config = dev->config;

	sys_dnode_t *node;
	while ((node = sys_dlist_get(config->lru_list))) {
		struct disk_cache_entry *entry = NODE_TO_ENTRY(node);
		entry->sector = 0;
		k_mem_slab_free(config->entries, entry);
	}
}

/// Lookup the cache entry for the specified sector. Return NULL if the sector is not cached.
static struct disk_cache_entry *disk_cache_lookup(const struct device *dev, uint32_t sector)
{
	const struct disk_cache_config *config = dev->config;

	sys_dnode_t *node;
	SYS_DLIST_FOR_EACH_NODE(config->lru_list, node) {
		struct disk_cache_entry *entry = NODE_TO_ENTRY(node);
		if (entry->sector == sector) {
			return entry;
		}
	}

	return NULL;
}

/// Add a sector to the cache. The sector must not be already present in the cache.
static void disk_cache_add(const struct device *dev, uint32_t sector, uint8_t *data)
{
	const struct disk_cache_config *config = dev->config;
	int ret;

	__ASSERT(NULL == disk_cache_lookup(dev, sector), "Sector is already cached");

	void *entry_ptr;
	ret = k_mem_slab_alloc(config->entries, &entry_ptr, K_NO_WAIT);
	if (ret < 0) {
		entry_ptr = NULL;
	}
	struct disk_cache_entry *entry;
	if (entry_ptr) {
		// Allocated new entry
		entry = entry_ptr;
		LOG_DBG("allocate: sector %u", sector);
	} else {
		// Cache full, replace oldest entry
		sys_dnode_t *node = sys_dlist_get(config->lru_list);
		__ASSERT(node, "No free entries and no allocated entries");
		entry = NODE_TO_ENTRY(node);
		LOG_DBG("replace: sector %u->%u", entry->sector, sector);
	}

	entry->sector = sector;
	memcpy(entry->data, data, config->sector_size);
	sys_dlist_append(config->lru_list, &entry->node);
}

/// Bump a cache entry to the most recent position in the LRU list.
static void disk_cache_bump(const struct device *dev, struct disk_cache_entry *entry)
{
	const struct disk_cache_config *config = dev->config;

	sys_dlist_remove(&entry->node);
	sys_dlist_append(config->lru_list, &entry->node);
	LOG_DBG("bump: sector %u", entry->sector);
}

static int disk_cache_populate(const struct device *dev, uint8_t *buf, uint32_t start_sector,
			       uint32_t num_sector)
{
	const struct disk_cache_config *config = dev->config;
	int ret;

	ret = disk_access_read(config->disk_name, buf, start_sector, num_sector);
	if (ret < 0) {
		return ret;
	}

	for (uint32_t i = 0; i < num_sector; ++i) {
		disk_cache_add(dev, start_sector + i, buf + i * config->sector_size);
	}
	return 0;
}

static int disk_cache_access_status(struct disk_info *disk)
{
	const struct device *dev = disk->dev;
	const struct disk_cache_config *config = dev->config;

	return disk_access_status(config->disk_name);
}

static int disk_cache_access_read(struct disk_info *disk, uint8_t *buff, uint32_t start_sector,
				  uint32_t num_sector)
{
	const struct device *dev = disk->dev;
	const struct disk_cache_config *config = dev->config;
	int ret;

	uint32_t cache_miss_start_sector = start_sector;
	for (uint32_t i = 0; i < num_sector; ++i) {
		uint32_t sector = start_sector + i;
		struct disk_cache_entry *entry = disk_cache_lookup(dev, sector);
		if (entry) {
			if (cache_miss_start_sector != sector) {
				ret = disk_cache_populate(
					dev,
					buff + (cache_miss_start_sector - start_sector) *
							config->sector_size,
					cache_miss_start_sector, sector - cache_miss_start_sector);
				if (ret < 0) {
					return ret;
				}
			}
			cache_miss_start_sector = sector + 1;

			LOG_DBG("hit: sector %u", sector);
			memcpy(buff + i * config->sector_size, entry->data, config->sector_size);
			disk_cache_bump(dev, entry);
		}
	}

	uint32_t last_sector = start_sector + num_sector;
	if (cache_miss_start_sector != last_sector) {
		ret = disk_cache_populate(
			dev, buff + (cache_miss_start_sector - start_sector) * config->sector_size,
			cache_miss_start_sector, last_sector - cache_miss_start_sector);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

static int disk_cache_access_write(struct disk_info *disk, const uint8_t *data_buf,
				   uint32_t start_sector, uint32_t num_sector)
{
	const struct device *dev = disk->dev;
	const struct disk_cache_config *config = dev->config;
	int ret;

	LOG_DBG("write: sector %u, count %u", start_sector, num_sector);
	ret = disk_access_write(config->disk_name, data_buf, start_sector, num_sector);
	if (ret < 0) {
		return ret;
	}

	for (uint32_t i = 0; i < num_sector; ++i) {
		struct disk_cache_entry *entry = disk_cache_lookup(dev, start_sector + i);
		if (entry) {
			memcpy(entry->data, data_buf + i * config->sector_size,
			       config->sector_size);
		}
	}

	return 0;
}

static int disk_cache_access_ioctl(struct disk_info *disk, uint8_t cmd, void *buff)
{
	const struct device *dev = disk->dev;
	const struct disk_cache_config *config = dev->config;
	int ret;

	ret = disk_access_ioctl(config->disk_name, cmd, buff);
	if (ret < 0) {
		return ret;
	}

	switch (cmd) {
	case DISK_IOCTL_CTRL_INIT:
		ret = disk_cache_check_config(dev);
		if (ret < 0) {
			return ret;
		}
		disk_cache_flush(dev);
		break;
	case DISK_IOCTL_CTRL_DEINIT:
		disk_cache_flush(dev);
		break;
	}

	return 0;
}

static int disk_cache_access_init(struct disk_info *disk)
{
	const struct disk_cache_config *config = disk->dev->config;
	return disk_access_init(config->disk_name);
}

static int disk_cache_init(const struct device *dev)
{
	struct disk_info *info = dev->data;

	info->dev = dev;

	return disk_access_register(info);
}

static const struct disk_operations disk_cache_ops = {
	.init = disk_cache_access_init,
	.status = disk_cache_access_status,
	.read = disk_cache_access_read,
	.write = disk_cache_access_write,
	.ioctl = disk_cache_access_ioctl,
};

#define DT_DRV_COMPAT zephyr_disk_cache

#define DISK_CACHE_DEVICE_DEFINE(n)                                                                \
	K_MEM_SLAB_DEFINE_STATIC(disk_entries_##n,                                                 \
				 sizeof(struct disk_cache_entry) + DT_INST_PROP(n, sector_size),   \
				 DT_INST_PROP(n, sector_count), 4);                                \
                                                                                                   \
	static sys_dlist_t disk_lru_list_##n = SYS_DLIST_STATIC_INIT(&disk_lru_list_##n);          \
                                                                                                   \
	static struct disk_info disk_info_##n = {                                                  \
		.name = DT_INST_PROP(n, disk_name),                                                \
		.ops = &disk_cache_ops,                                                            \
	};                                                                                         \
                                                                                                   \
	static const struct disk_cache_config disk_config_##n = {                                  \
		.disk_name = DT_INST_PROP(n, backing_disk_name),                                   \
		.sector_size = DT_INST_PROP(n, sector_size),                                       \
		.entries = &disk_entries_##n,                                                      \
		.lru_list = &disk_lru_list_##n,                                                    \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, disk_cache_init, NULL, &disk_info_##n, &disk_config_##n,          \
			      POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &disk_cache_ops);

DT_INST_FOREACH_STATUS_OKAY(DISK_CACHE_DEVICE_DEFINE)