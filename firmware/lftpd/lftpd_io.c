#define _GNU_SOURCE

#include "private/lftpd_io.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(lftpd, LOG_LEVEL_DBG);

void lftpd_io_trim_trailing_slash(char* path) {
	size_t path_len = strlen(path);
	if (path_len == 0) return;
	for (size_t i = path_len - 1; i > 0 && path[i] == '/'; --i) {
		path[i] = '\0';
	}
}

int lftpd_io_prefix(const char* prefix, char* path, size_t path_buf_len) {
	if (!prefix || !path || path_buf_len == 0) return -EINVAL;

	char* path_buf = path;
	size_t prefix_len = strlen(prefix);
	size_t path_len = strlen(path);
	if (prefix_len == 0) {
		// Empty prefix, return path as is
		return 0;
	} else if (path_len == 0) {
		// Empty path, copy prefix to path
		if (prefix_len + 1 > path_buf_len) {
			return -ENAMETOOLONG;
		}
		memcpy(path, prefix, prefix_len + 1);
		return 0;
	}

	bool prefix_trailing_slash = prefix[prefix_len - 1] == '/';
	bool path_leading_slash = path[0] == '/';
	// Index of slash joining prefix to path
	size_t slash_index;
	// Offset to move path to leave room for prefix and slash (if necessary)
	size_t path_offset;
	if (prefix_trailing_slash && path_leading_slash) {
		// Remove leading slash from path to avoid double slash
		++path;
		--path_len;
		slash_index = prefix_len - 1;
		path_offset = prefix_len;
	} else if (prefix_trailing_slash) {
		// Slash is copied from prefix
		slash_index = prefix_len - 1;
		path_offset = prefix_len;
	} else if (path_leading_slash) {
		// Slash is already path of path
		slash_index = prefix_len;
		path_offset = prefix_len;
	} else {
		// Neither has trailing/leading slash, so leave extra space to add slash
		slash_index = prefix_len;
		path_offset = prefix_len + 1;
	}

	if (path_offset + path_len + 1 > path_buf_len) {
		return -ENAMETOOLONG;
	}

	memmove(path_buf + path_offset, path, path_len + 1);
	memcpy(path_buf, prefix, prefix_len);
	path_buf[slash_index] = '/';

	return 0;
}

int lftpd_io_resolve_path(const char* base_dir, const char* working_dir,
						  char* path, size_t path_buf_len) {
	int ret;
	if (!base_dir || !working_dir || !path || path_buf_len == 0) {
		return -EINVAL;
	}

	// FIXME: resolve .. and prevent base dir escape

	const char* prefix;
	if (path[0] == '/') {
		// Absolute path, append to base directory
		prefix = base_dir;
	} else {
		// Relative path, append to working directory
		prefix = working_dir;
	}

	ret = lftpd_io_prefix(prefix, path, path_buf_len);
	if (ret < 0) return ret;

	lftpd_io_trim_trailing_slash(path);

	return 0;
}