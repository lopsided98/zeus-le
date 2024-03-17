#pragma once

#include <stdint.h>
#include <zephyr/kernel.h>

typedef struct {
	const char* base_dir;
	char cwd[CONFIG_LFTPD_MAX_PATH_LEN + 1];
	/// Buffer to store received control command
	char cmd_buf[4 /* CMD */ + 1 /* SP */ + CONFIG_LFTPD_MAX_PATH_LEN +
				 2 /* CRLF */ + 1 /* NUL */];
	char buf[1024];
	int socket;
	int data_socket;
} lftpd_client_t;

typedef struct {
	const char* base_dir;
	int server_socket;

	struct k_mbox conn_mbox;
} lftpd_t;

int lftpd_init(lftpd_t* lftpd, const char* base_dir, uint16_t port);

/**
 * @brief Create a server on port and start listening for client
 * connections. This function blocks for the life of the server and
 * only returns when lftpd_stop() is called with the same lftpd_t.
 */
int lftpd_run(lftpd_t* lftpd);

int lftpd_client_run(lftpd_t* lftpd, lftpd_client_t* client);