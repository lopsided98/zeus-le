#pragma once

#include <stddef.h>

/// Trim trailing slashes from a path. Only leave a trailing slash if the path
/// would be empty otherwise.
void lftpd_io_trim_trailing_slash(char* path);

int lftpd_io_prefix(const char* prefix, char* path, size_t path_buf_len);

int lftpd_io_resolve_path(const char* base_dir, const char* working_dir,
						  char* path, size_t path_buf_len);