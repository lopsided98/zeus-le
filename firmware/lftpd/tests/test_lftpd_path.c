#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "lftpd_path.h"

#define zassert_equal_string(a, b, ...) \
	zassert(strcmp(a, b) == 0, #a " not equal to " #b, ##__VA_ARGS__)

ZTEST_SUITE(lftpd_path, NULL, NULL, NULL, NULL, NULL);

static void assert_lftpd_path_trim_trailing_slash(const char* path,
												  const char* expected) {
	char path_buf[32];
	zassert_between_inclusive(strlen(path), 0, sizeof(path_buf) - 1);
	strcpy(path_buf, path);
	lftpd_path_trim_trailing_slash(path_buf);
	zassert_equal_string(
		path_buf, expected,
		"lftpd_path_trim_trailing_slash(\"%s\") == \"%s\" != \"%s\"", path,
		path_buf, expected);
}

ZTEST(lftpd_path, test_lftpd_path_trim_trailing_slash) {
	assert_lftpd_path_trim_trailing_slash("name", "name");
	assert_lftpd_path_trim_trailing_slash("", "");
	assert_lftpd_path_trim_trailing_slash("/", "/");
	assert_lftpd_path_trim_trailing_slash("///", "/");
	assert_lftpd_path_trim_trailing_slash("/name", "/name");
	assert_lftpd_path_trim_trailing_slash("/name/", "/name");
	assert_lftpd_path_trim_trailing_slash("/name/a/b/c/", "/name/a/b/c");
	assert_lftpd_path_trim_trailing_slash("/name///", "/name");
}

static void assert_lftpd_path_prefix(const char* prefix, const char* path,
									 const char* expected) {
	char path_buf[32];
	zassert_between_inclusive(strlen(path), 0, sizeof(path_buf) - 1);
	strcpy(path_buf, path);
	zassert_equal(lftpd_path_prefix(prefix, path_buf, sizeof(path_buf)), 0);
	zassert_equal_string(
		path_buf, expected,
		"lftpd_path_prefix(\"%s\", \"%s\") == \"%s\" != \"%s\"", prefix, path,
		path_buf, expected);
}

ZTEST(lftpd_path, test_lftpd_path_prefix) {
	assert_lftpd_path_prefix("", "", "");
	assert_lftpd_path_prefix("/", "", "/");
	assert_lftpd_path_prefix("/", "/", "/");
	assert_lftpd_path_prefix("", "/", "/");
	assert_lftpd_path_prefix("a", "", "a");
	assert_lftpd_path_prefix("", "b", "b");
	assert_lftpd_path_prefix("/prefix/", "/path/", "/prefix/path/");
	assert_lftpd_path_prefix("prefix/", "/path", "prefix/path");
	assert_lftpd_path_prefix("prefix/", "path", "prefix/path");
	assert_lftpd_path_prefix("prefix", "/path", "prefix/path");
	assert_lftpd_path_prefix("a", "b", "a/b");
	assert_lftpd_path_prefix("prefix/", "", "prefix/");
	assert_lftpd_path_prefix("prefix", "/", "prefix/");
	assert_lftpd_path_prefix("/", "path", "/path");
	assert_lftpd_path_prefix("/", "/path", "/path");
}

ZTEST(lftpd_path, test_lftpd_path_prefix_too_long) {
	{
		char path[8] = "abc";
		zassert_equal(-ENAMETOOLONG,
					  lftpd_path_prefix("asdf", path, sizeof(path)));
	}
	{
		char path[8] = "";
		zassert_equal(-ENAMETOOLONG,
					  lftpd_path_prefix("asdfghjk", path, sizeof(path)));
	}
}