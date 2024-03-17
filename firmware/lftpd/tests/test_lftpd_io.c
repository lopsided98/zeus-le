#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "lftpd_io.h"

#define zassert_equal_string(a, b, ...) \
	zassert(strcmp(a, b) == 0, #a " not equal to " #b, ##__VA_ARGS__)

ZTEST_SUITE(lftpd_io, NULL, NULL, NULL, NULL, NULL);

static void assert_lftpd_io_trim_trailing_slash(const char* path,
												const char* expected) {
	char path_buf[32];
	zassert_between_inclusive(strlen(path), 0, sizeof(path_buf) - 1);
	strcpy(path_buf, path);
	lftpd_io_trim_trailing_slash(path_buf);
	zassert_equal_string(
		path_buf, expected,
		"lftpd_io_trim_trailing_slash(\"%s\") == \"%s\" != \"%s\"", path,
		path_buf, expected);
}

ZTEST(lftpd_io, test_lftpd_io_trim_trailing_slash) {
	assert_lftpd_io_trim_trailing_slash("name", "name");
	assert_lftpd_io_trim_trailing_slash("", "");
	assert_lftpd_io_trim_trailing_slash("/", "/");
	assert_lftpd_io_trim_trailing_slash("///", "/");
	assert_lftpd_io_trim_trailing_slash("/name", "/name");
	assert_lftpd_io_trim_trailing_slash("/name/", "/name");
	assert_lftpd_io_trim_trailing_slash("/name/a/b/c/", "/name/a/b/c");
	assert_lftpd_io_trim_trailing_slash("/name///", "/name");
}

static void assert_lftpd_io_prefix(const char* prefix, const char* path,
								   const char* expected) {
	char path_buf[32];
	zassert_between_inclusive(strlen(path), 0, sizeof(path_buf) - 1);
	strcpy(path_buf, path);
	zassert_equal(lftpd_io_prefix(prefix, path_buf, sizeof(path_buf)), 0);
	zassert_equal_string(path_buf, expected,
						 "lftpd_io_prefix(\"%s\", \"%s\") == \"%s\" != \"%s\"",
						 prefix, path, path_buf, expected);
}

ZTEST(lftpd_io, test_lftpd_io_prefix) {
	assert_lftpd_io_prefix("", "", "");
	assert_lftpd_io_prefix("/", "", "/");
	assert_lftpd_io_prefix("/", "/", "/");
	assert_lftpd_io_prefix("", "/", "/");
	assert_lftpd_io_prefix("a", "", "a");
	assert_lftpd_io_prefix("", "b", "b");
	assert_lftpd_io_prefix("/prefix/", "/path/", "/prefix/path/");
	assert_lftpd_io_prefix("prefix/", "/path", "prefix/path");
	assert_lftpd_io_prefix("prefix/", "path", "prefix/path");
	assert_lftpd_io_prefix("prefix", "/path", "prefix/path");
	assert_lftpd_io_prefix("a", "b", "a/b");
	assert_lftpd_io_prefix("prefix/", "", "prefix/");
	assert_lftpd_io_prefix("prefix", "/", "prefix/");
	assert_lftpd_io_prefix("/", "path", "/path");
	assert_lftpd_io_prefix("/", "/path", "/path");
}

ZTEST(lftpd_io, test_lftpd_io_prefix_too_long) {
	{
		char path[8] = "abc";
		zassert_equal(-ENAMETOOLONG,
					  lftpd_io_prefix("asdf", path, sizeof(path)));
	}
	{
		char path[8] = "";
		zassert_equal(-ENAMETOOLONG,
					  lftpd_io_prefix("asdfghjk", path, sizeof(path)));
	}
}

static void assert_lftpd_io_canonicalize_path(const char* base,
											  const char* name,
											  const char* expected) {
	char* path;
	zassert_equal_string(path = lftpd_io_canonicalize_path(base, name),
						 expected,
						 "lftpd_io_canonicalize_path(\"%s\", \"%s\") != \"%s\"",
						 base, name, expected);
	free(path);
}

ZTEST(lftpd_io, test_lftpd_io_canonicalize_path) {
	assert_lftpd_io_canonicalize_path("/", "name", "/name");
	assert_lftpd_io_canonicalize_path("/base", "name", "/base/name");
	assert_lftpd_io_canonicalize_path("/base/", "/name", "/name");
	assert_lftpd_io_canonicalize_path("/base//", "/name/", "/name");
	assert_lftpd_io_canonicalize_path("/base", ".", "/base");
	assert_lftpd_io_canonicalize_path("/base/", "..", "/");
	assert_lftpd_io_canonicalize_path("/base/base1/base2", "..", "/base/base1");
	assert_lftpd_io_canonicalize_path("/base/base1/", "name/name2/..",
									  "/base/base1/name");
	assert_lftpd_io_canonicalize_path(
		"/one/./two/../three/four//five/.././././..", "name",
		"/one/three/name");
	assert_lftpd_io_canonicalize_path("/", "/", "/");
	assert_lftpd_io_canonicalize_path("/", NULL, "/");
	assert_lftpd_io_canonicalize_path(NULL, "/", "/");
	assert_lftpd_io_canonicalize_path("/", "", "/");
	assert_lftpd_io_canonicalize_path("", "/", "/");
	assert_lftpd_io_canonicalize_path("", "", "/");
}