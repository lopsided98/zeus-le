#pragma once

#include <stdint.h>
#include <zephyr/kernel.h>

struct lftpd_conn {
	const char* base_dir;
	char cwd[CONFIG_LFTPD_MAX_PATH_LEN + 1];
	/// Buffer to store received control command
	char cmd_buf[4 /* CMD */ + 1 /* SP */ + CONFIG_LFTPD_MAX_PATH_LEN +
				 2 /* CRLF */ + 1 /* NUL */];
	char buf[1024];
	int socket;
	int data_socket;
};

struct lftpd {
	const char* base_dir;
	int server_socket;

	struct k_mbox conn_mbox;
};

int lftpd_init(struct lftpd* lftpd, const char* base_dir, uint16_t port);

/**
 * @brief Create a server on port and start listening for client
 * connections. This function blocks for the life of the server and
 * only returns when lftpd_stop() is called with the same struct lftpd.
 */
int lftpd_run(struct lftpd* lftpd);

int lftpd_conn_run(struct lftpd* lftpd, struct lftpd_conn* client);