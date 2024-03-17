#include "lftpd.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>

#include "private/lftpd_inet.h"
#include "private/lftpd_io.h"
#include "private/lftpd_status.h"
#include "private/lftpd_string.h"

LOG_MODULE_REGISTER(lftpd, CONFIG_LFTPD_LOG_LEVEL);

// https://tools.ietf.org/html/rfc959
// https://tools.ietf.org/html/rfc2389#section-2.2
// https://tools.ietf.org/html/rfc3659
// https://tools.ietf.org/html/rfc5797
// https://tools.ietf.org/html/rfc2428#section-3 EPSV
// https://en.wikipedia.org/wiki/List_of_FTP_commands

typedef struct {
	char* command;
	int (*handler)(lftpd_client_t* client, char* arg, size_t arg_buf_len);
} command_t;

static int cmd_cwd(lftpd_client_t* client, char* arg, size_t arg_buf_len);
static int cmd_dele(lftpd_client_t* client, char* arg, size_t arg_buf_len);
static int cmd_epsv(lftpd_client_t* client, char* arg, size_t arg_buf_len);
static int cmd_feat(lftpd_client_t* client, char* arg, size_t arg_buf_len);
static int cmd_list(lftpd_client_t* client, char* arg, size_t arg_buf_len);
static int cmd_mkd(lftpd_client_t* client, char* arg, size_t arg_buf_len);
static int cmd_nlst(lftpd_client_t* client, char* arg, size_t arg_buf_len);
static int cmd_noop(lftpd_client_t* client, char* arg, size_t arg_buf_len);
static int cmd_pass(lftpd_client_t* client, char* arg, size_t arg_buf_len);
static int cmd_pasv(lftpd_client_t* client, char* arg, size_t arg_buf_len);
static int cmd_pwd(lftpd_client_t* client, char* arg, size_t arg_buf_len);
static int cmd_quit(lftpd_client_t* client, char* arg, size_t arg_buf_len);
static int cmd_retr(lftpd_client_t* client, char* arg, size_t arg_buf_len);
static int cmd_rmd(lftpd_client_t* client, char* arg, size_t arg_buf_len);
static int cmd_size(lftpd_client_t* client, char* arg, size_t arg_buf_len);
static int cmd_stor(lftpd_client_t* client, char* arg, size_t arg_buf_len);
static int cmd_syst(lftpd_client_t* client, char* arg, size_t arg_buf_len);
static int cmd_type(lftpd_client_t* client, char* arg, size_t arg_buf_len);
static int cmd_user(lftpd_client_t* client, char* arg, size_t arg_buf_len);

static const command_t commands[] = {
	// Trailing comments to make clang-format work
	// https://github.com/llvm/llvm-project/issues/61560
	{ "CWD", cmd_cwd },	   //
	{ "DELE", cmd_dele },  //
	{ "EPSV", cmd_epsv },  //
	{ "FEAT", cmd_feat },  //
	{ "LIST", cmd_list },  //
	{ "MKD", cmd_mkd },	   //
	{ "NLST", cmd_nlst },  //
	{ "NOOP", cmd_noop },  //
	{ "PASS", cmd_pass },  //
	{ "PASV", cmd_pasv },  //
	{ "PWD", cmd_pwd },	   //
	{ "QUIT", cmd_quit },  //
	{ "RETR", cmd_retr },  //
	{ "RMD", cmd_rmd },	   //
	{ "SIZE", cmd_size },  //
	{ "STOR", cmd_stor },  //
	{ "SYST", cmd_syst },  //
	{ "TYPE", cmd_type },  //
	{ "USER", cmd_user },  //
	{ NULL, NULL },
};

static int send_response(lftpd_client_t* client, int code, bool include_code,
						 bool multiline_start, const char* format, ...) {
	char* buf = client->buf;
	size_t buf_len = sizeof(client->buf);
	int ret;
	if (include_code) {
		if (multiline_start) {
			ret = snprintf(buf, buf_len, "%d-", code);
		} else {
			ret = snprintf(buf, buf_len, "%d ", code);
		}
		if (ret < 0) {
			return ret;
		} else if (ret >= buf_len) {
			return -EOVERFLOW;
		}
		buf += ret;
		buf_len -= ret;
	}

	va_list args;
	va_start(args, format);
	ret = vsnprintf(buf, buf_len, format, args);
	va_end(args);
	if (ret < 0) {
		return ret;
	} else if (ret >= sizeof(client->buf)) {
		return -EOVERFLOW;
	}
	buf += ret;
	buf_len -= ret;

	if (buf_len < 3) {
		return -EOVERFLOW;
	}
	buf[0] = '\r';
	buf[1] = '\n';
	buf[2] = '\0';
	buf_len -= 3;

	return lftpd_inet_write_string(client->socket, client->buf);
}

#define send_simple_response(conn, code, format, ...) \
	send_response(conn, code, true, false, format, ##__VA_ARGS__)

#define send_multiline_response_begin(conn, code, format, ...) \
	send_response(conn, code, true, true, format, ##__VA_ARGS__)

#define send_multiline_response_line(conn, format, ...) \
	send_response(conn, 0, false, false, format, ##__VA_ARGS__)

#define send_multiline_response_end(conn, code, format, ...) \
	send_response(conn, code, true, false, format, ##__VA_ARGS__)

static int accept_data_connection(lftpd_client_t* client) {
	if (client->data_socket < 0) {
		return -EBADF;
	}

	// get the port from the new socket, which is random
	int port = lftpd_inet_get_socket_port(client->data_socket);

	// wait for the connection to the data port
	LOG_DBG("waiting for data port connection on port %d...", port);
	int client_socket = zsock_accept(client->data_socket, NULL, NULL);
	if (client_socket < 0) {
		LOG_ERR("failed to accept data connection (err %d)", errno);
		zsock_close(client->data_socket);
		client->data_socket = -1;
		return client_socket;
	}
	LOG_DBG("data port connection received...");
	return client_socket;
}

static int send_list(lftpd_client_t* client, int data_socket,
					 const char* path) {
	// https://files.stairways.com/other/ftp-list-specs-info.txt
	// http://cr.yp.to/ftp/list/binls.html
	static const char* directory_format =
		"drw-rw-rw- 1 owner group %13zu Jan 01  1970 %s" CRLF;
	static const char* file_format =
		"-rw-rw-rw- 1 owner group %13zu Jan 01  1970 %s" CRLF;

	int ret;
	struct fs_dir_t dir;
	fs_dir_t_init(&dir);

	ret = fs_opendir(&dir, path);
	if (ret < 0) return ret;

	while (true) {
		struct fs_dirent entry;
		ret = fs_readdir(&dir, &entry);
		if (ret < 0) goto close;

		if (entry.name[0] == 0) break;

		switch (entry.type) {
			case FS_DIR_ENTRY_DIR:
				ret = snprintf(client->buf, sizeof(client->buf),
							   directory_format, entry.size, entry.name);
				break;
			case FS_DIR_ENTRY_FILE:
				ret = snprintf(client->buf, sizeof(client->buf), file_format,
							   entry.size, entry.name);
				break;
		}
		if (ret < 0) {
			goto close;
		} else if (ret >= sizeof(client->buf)) {
			ret = -ENAMETOOLONG;
			goto close;
		}

		ret = lftpd_inet_write_string(data_socket, client->buf);
		if (ret < 0) goto close;
	}

	ret = 0;
close:
	fs_closedir(&dir);
	return ret;
}

static int send_nlst(int data_socket, const char* path) {
	int ret;
	struct fs_dir_t dir;
	fs_dir_t_init(&dir);

	ret = fs_opendir(&dir, path);
	if (ret < 0) return ret;

	while (true) {
		struct fs_dirent entry;
		ret = fs_readdir(&dir, &entry);
		if (ret < 0) goto close;

		if (entry.name[0] == 0) break;

		if (entry.type == FS_DIR_ENTRY_FILE) {
			ret = lftpd_inet_write_string(data_socket, entry.name);
			if (ret < 0) goto close;
			ret = lftpd_inet_write_string(data_socket, CRLF);
			if (ret < 0) goto close;
		}
	}

	ret = 0;
close:
	fs_closedir(&dir);
	return ret;
}

static int send_file(int socket, const char* path, char* buf, size_t buf_len) {
	int ret;
	struct fs_file_t file;
	fs_file_t_init(&file);

	ret = fs_open(&file, path, FS_O_READ);
	if (ret < 0) {
		LOG_ERR("failed to open file for read");
		return ret;
	}

	int read_len;
	while ((read_len = fs_read(&file, buf, buf_len)) > 0) {
		unsigned char* p = buf;
		while (read_len) {
			int write_len = zsock_send(socket, p, read_len, 0);
			if (write_len < 0) {
				ret = write_len;
				goto close;
			}
			p += write_len;
			read_len -= write_len;
		}
	}
	if (read_len < 0) {
		ret = read_len;
		goto close;
	}

	ret = 0;
close:
	fs_close(&file);
	return ret;
}

/// Receive file contents from socket and store them into a file at the
/// specified path. Use buf as temporary receive buffer. The path and buf
/// arguments may overlap.
static int receive_file(int socket, const char* path, char* buf,
						size_t buf_len) {
	int ret;
	struct fs_file_t file;
	fs_file_t_init(&file);

	LOG_DBG("receive into: %s", path);
	ret = fs_open(&file, path, FS_O_WRITE | FS_O_CREATE);
	if (ret < 0) {
		LOG_ERR("failed to open file for write");
		return ret;
	}

	while ((ret = zsock_recv(socket, buf, buf_len, 0)) > 0) {
		ret = fs_write(&file, buf, ret);
		if (ret < 0) goto close;
	}
	if (ret < 0) goto close;

	ret = 0;
close:
	fs_close(&file);
	return ret;
}

static int cmd_cwd(lftpd_client_t* client, char* arg, size_t arg_buf_len) {
	int ret;

	if (!arg || strlen(arg) == 0) {
		return send_simple_response(client, 550, STATUS_550);
	}

	ret =
		lftpd_io_resolve_path(client->base_dir, client->cwd, arg, arg_buf_len);
	if (ret < 0) {
		return send_simple_response(client, 553, STATUS_553);
	}
	const char* cwd = arg;
	size_t cwd_len = strlen(cwd);

	if (strcmp(cwd, "/") == 0) {
		// Root directory, always valid
	} else if (strrchr(cwd, '/') == cwd) {
		// Only a single path element, represents a mount point
		struct fs_statvfs entry;
		ret = fs_statvfs(cwd, &entry);
		if (ret < 0) {
			return send_simple_response(client, 550, STATUS_550);
		}
	} else {
		// make sure the path exists
		struct fs_dirent entry;
		ret = fs_stat(cwd, &entry);
		if (ret < 0) {
			return send_simple_response(client, 550, STATUS_550);
		}

		// make sure the path is a directory
		if (entry.type != FS_DIR_ENTRY_DIR) {
			return send_simple_response(client, 550, STATUS_550);
		}
	}

	if (cwd_len + 1 > sizeof(client->cwd)) {
		return send_simple_response(client, 553, STATUS_553);
	}
	memcpy(client->cwd, cwd, cwd_len + 1);

	return send_simple_response(client, 250, STATUS_250);
}

static int cmd_dele(lftpd_client_t* client, char* arg, size_t arg_buf_len) {
	if (!arg || strlen(arg) == 0) {
		return send_simple_response(client, 501, STATUS_501);
	}

	int ret =
		lftpd_io_resolve_path(client->base_dir, client->cwd, arg, arg_buf_len);
	if (ret < 0) {
		return send_simple_response(client, 500, STATUS_500);
	}

	ret = fs_unlink(arg);
	if (ret < 0) {
		return send_simple_response(client, 550, STATUS_550);
	}

	return send_simple_response(client, 250, STATUS_250);
}

static int cmd_epsv(lftpd_client_t* client, char* arg, size_t arg_buf_len) {
	// We disable TIME_WAIT to avoid resource exhaustion, so we can just reuse
	// an existing socket if available.
	if (client->data_socket < 0) {
		// open a data port
		client->data_socket = lftpd_inet_listen(0);
		if (client->data_socket < 0) {
			return send_simple_response(client, 425, STATUS_425);
		}
	}

	// get the port from the socket, which is random
	int port = lftpd_inet_get_socket_port(client->data_socket);

	// format the response
	return send_simple_response(client, 229, STATUS_229, port);
}

static int cmd_feat(lftpd_client_t* client, char* arg, size_t arg_buf_len) {
	send_multiline_response_begin(client, 211, STATUS_211);
	send_multiline_response_line(client, " EPSV");
	send_multiline_response_line(client, " PASV");
	send_multiline_response_line(client, " SIZE");
	send_multiline_response_line(client, " NLST");
	send_multiline_response_line(client, " UTF8");
	send_multiline_response_end(client, 211, "End");
	return 0;
}

static int cmd_list(lftpd_client_t* client, char* arg, size_t arg_buf_len) {
	int data_socket = accept_data_connection(client);
	if (data_socket < 0) {
		return send_simple_response(client, 425, STATUS_425);
	}

	send_simple_response(client, 150, STATUS_150);

	int err = send_list(client, data_socket, client->cwd);
	zsock_close(data_socket);
	if (err == 0) {
		return send_simple_response(client, 226, STATUS_226);
	} else {
		return send_simple_response(client, 550, STATUS_550);
	}
}

static int cmd_mkd(lftpd_client_t* client, char* arg, size_t arg_buf_len) {
	if (!arg) {
		return send_simple_response(client, 501, STATUS_501);
	}

	int ret =
		lftpd_io_resolve_path(client->base_dir, client->cwd, arg, arg_buf_len);
	if (ret < 0) {
		return send_simple_response(client, 500, STATUS_500);
	}

	ret = fs_mkdir(arg);
	if (ret < 0) {
		return send_simple_response(client, 550, STATUS_550);
	}

	return send_simple_response(client, 257, STATUS_257, arg);
}

static int cmd_nlst(lftpd_client_t* client, char* arg, size_t arg_buf_len) {
	int ret;
	int data_socket = accept_data_connection(client);
	if (data_socket < 0) {
		return send_simple_response(client, 425, STATUS_425);
	}

	const char* path = client->cwd;
	if (arg) {
		ret = lftpd_io_resolve_path(client->base_dir, client->cwd, arg,
									arg_buf_len);
		if (ret < 0) {
			ret = send_simple_response(client, 553, STATUS_500);
			goto exit;
		}
		path = arg;
	}

	ret = send_simple_response(client, 150, STATUS_150);
	if (ret < 0) goto exit;

	ret = send_nlst(data_socket, client->cwd);
	if (ret < 0) {
		ret = send_simple_response(client, 550, STATUS_550);
		goto exit;
	}

	ret = send_simple_response(client, 226, STATUS_226);
exit:
	if (data_socket >= 0) {
		zsock_close(data_socket);
	}
	return ret;
}

static int cmd_noop(lftpd_client_t* client, char* arg, size_t arg_buf_len) {
	return send_simple_response(client, 200, STATUS_200);
}

static int cmd_pass(lftpd_client_t* client, char* arg, size_t arg_buf_len) {
	return send_simple_response(client, 230, STATUS_230);
}

static int cmd_pasv(lftpd_client_t* client, char* arg, size_t arg_buf_len) {
	// We disable TIME_WAIT to avoid resource exhaustion, so we can just reuse
	// an existing socket if available.
	if (client->data_socket < 0) {
		// open a data port
		client->data_socket = lftpd_inet_listen(0);
		if (client->data_socket < 0) {
			return send_simple_response(client, 425, STATUS_425);
		}
	}

	// get the port from the socket, which is random
	int port = lftpd_inet_get_socket_port(client->data_socket);

	// get our IP by reading our side of the client's control channel
	// socket connection
	struct sockaddr_storage client_addr;
	socklen_t client_addr_len = sizeof(client_addr);
	int err = zsock_getsockname(client->socket, (struct sockaddr*)&client_addr,
								&client_addr_len);
	if (err != 0) {
		LOG_ERR("error getting client IP info");
		zsock_close(client->data_socket);
		return send_simple_response(client, 425, STATUS_425);
	}

	if (client_addr.ss_family != AF_INET) {
		LOG_ERR("client not connected over IPv4");
		zsock_close(client->data_socket);
		return send_simple_response(client, 425, STATUS_425);
	}

	// format the response
	uint32_t ip = htonl(((struct sockaddr_in*)&client_addr)->sin_addr.s_addr);
	send_simple_response(client, 227, STATUS_227, (ip >> 24) & 0xff,
						 (ip >> 16) & 0xff, (ip >> 8) & 0xff, (ip >> 0) & 0xff,
						 (port >> 8) & 0xff, (port >> 0) & 0xff);

	return 0;
}

static int cmd_pwd(lftpd_client_t* client, char* arg, size_t arg_buf_len) {
	return send_simple_response(client, 257, "\"%s\" ", client->cwd);
}

static int cmd_quit(lftpd_client_t* client, char* arg, size_t arg_buf_len) {
	send_simple_response(client, 221, STATUS_221);
	return -1;
}

static int cmd_retr(lftpd_client_t* client, char* arg, size_t arg_buf_len) {
	int ret;
	int data_socket = accept_data_connection(client);
	if (data_socket < 0) {
		return send_simple_response(client, 425, STATUS_425);
	}

	ret = send_simple_response(client, 150, STATUS_150);
	if (ret < 0) goto exit;

	ret =
		lftpd_io_resolve_path(client->base_dir, client->cwd, arg, arg_buf_len);
	if (ret < 0) {
		ret = send_simple_response(client, 500, STATUS_500);
		goto exit;
	}

	ret = send_file(data_socket, arg, client->buf, sizeof(client->buf));
	if (ret < 0) {
		ret = send_simple_response(client, 450, STATUS_450);
		goto exit;
	}

	ret = send_simple_response(client, 226, STATUS_226);

exit:
	if (data_socket >= 0) {
		zsock_close(data_socket);
	}
	return ret;
}

static int cmd_rmd(lftpd_client_t* client, char* arg, size_t arg_buf_len) {
	if (!arg) {
		return send_simple_response(client, 550, STATUS_501);
	}

	int ret =
		lftpd_io_resolve_path(client->base_dir, client->cwd, arg, arg_buf_len);
	if (ret < 0) {
		return send_simple_response(client, 553, STATUS_500);
	}

	struct fs_dirent entry;
	ret = fs_stat(arg, &entry);
	if (ret < 0) {
		return send_simple_response(client, 550, STATUS_550);
	}

	if (entry.type != FS_DIR_ENTRY_DIR) {
		return send_simple_response(client, 550, STATUS_550);
	}

	ret = fs_unlink(arg);
	if (ret < 0) {
		return send_simple_response(client, 550, STATUS_550);
	}

	return send_simple_response(client, 250, STATUS_250);
}

static int cmd_size(lftpd_client_t* client, char* arg, size_t arg_buf_len) {
	if (!arg) {
		return send_simple_response(client, 501, STATUS_501);
	}

	int ret =
		lftpd_io_resolve_path(client->base_dir, client->cwd, arg, arg_buf_len);
	if (ret < 0) {
		return send_simple_response(client, 500, STATUS_500);
	}

	struct fs_dirent entry;
	ret = fs_stat(arg, &entry);
	if (ret < 0) {
		return send_simple_response(client, 550, STATUS_550);
	}

	return send_simple_response(client, 213, "%zu", entry.size);
}

static int cmd_stor(lftpd_client_t* client, char* arg, size_t arg_buf_len) {
	int ret;
	int data_socket = accept_data_connection(client);
	if (data_socket < 0) {
		return send_simple_response(client, 425, STATUS_425);
	}

	ret = send_simple_response(client, 150, STATUS_150);
	if (ret < 0) goto exit;

	LOG_DBG("before resolve: %s", arg);
	ret =
		lftpd_io_resolve_path(client->base_dir, client->cwd, arg, arg_buf_len);
	if (ret < 0) {
		ret = send_simple_response(client, 500, STATUS_500);
		goto exit;
	}

	ret = receive_file(data_socket, arg, client->buf, sizeof(client->buf));
	if (ret < 0) {
		ret = send_simple_response(client, 450, STATUS_450);
		goto exit;
	}

	ret = send_simple_response(client, 226, STATUS_226);

exit:
	if (data_socket >= 0) {
		zsock_close(data_socket);
	}
	return ret;
}

static int cmd_syst(lftpd_client_t* client, char* arg, size_t arg_buf_len) {
	return send_simple_response(client, 215, "UNIX Type: L8");
}

static int cmd_type(lftpd_client_t* client, char* arg, size_t arg_buf_len) {
	return send_simple_response(client, 200, STATUS_200);
}

static int cmd_user(lftpd_client_t* client, char* arg, size_t arg_buf_len) {
	return send_simple_response(client, 230, STATUS_230);
}

static int handle_control_channel(lftpd_client_t* client) {
	struct sockaddr_in6 client_addr;
	socklen_t client_addr_len = sizeof(struct sockaddr_in6);
	int ret = zsock_getpeername(client->socket, (struct sockaddr*)&client_addr,
								&client_addr_len);
	if (ret != 0) {
		LOG_ERR("error getting client IP info");
		LOG_INF("connection received...");
	} else {
		char ip[INET6_ADDRSTRLEN];
		inet_ntop(AF_INET6, &client_addr.sin6_addr, ip, INET6_ADDRSTRLEN);
		int port = lftpd_inet_get_socket_port(client->socket);
		LOG_INF("connection received from [%s]:%d...", ip, port);
	}

	ret = send_simple_response(client, 220, STATUS_220);
	if (ret < 0) {
		LOG_ERR("error sending welcome message");
		goto exit;
	}

	while (ret >= 0) {
		ret = lftpd_inet_read_line(client->socket, client->cmd_buf,
								   sizeof(client->cmd_buf));
		if (ret == -EOVERFLOW) {
			LOG_DBG("command too long");
			ret = send_simple_response(client, 500, STATUS_500);
			continue;
		} else if (ret == -ECONNRESET) {
			LOG_DBG("client disconnected");
			ret = 0;
			goto exit;
		} else if (ret < 0) {
			LOG_ERR("error reading next command");
			goto exit;
		}

		// find the index of the first space
		size_t split_index;
		char* p = strchr(client->cmd_buf, ' ');
		if (p != NULL) {
			split_index = p - client->cmd_buf;
		}
		// if no space, use the whole string
		else {
			split_index = strlen(client->cmd_buf);
		}

		// if the index is 5 or greater the command is too long
		if (split_index >= 5) {
			ret = send_simple_response(client, 500, STATUS_500);
			continue;
		}

		client->cmd_buf[split_index] = 0;
		char* command = client->cmd_buf;
		char* arg = client->cmd_buf + split_index + 1;
		size_t arg_buf_len = sizeof(client->cmd_buf) - split_index - 1;

		// upper case the command
		for (int i = 0; command[i]; i++) {
			command[i] = toupper(command[i]);
		}

		arg = lftpd_string_trim(arg);
		if (strlen(arg) == 0) {
			arg = NULL;
		}

		// see if we have a matching function for the command, and if
		// so, dispatch it
		bool matched = false;
		for (int i = 0; commands[i].command; i++) {
			if (strcmp(commands[i].command, command) == 0) {
				if (arg) {
					LOG_DBG("cmd: %s %s", command, arg);
				} else {
					LOG_DBG("cmd: %s", command);
				}
				ret = commands[i].handler(client, arg, arg_buf_len);
				matched = true;
				break;
			}
		}
		if (!matched) {
			if (arg) {
				LOG_DBG("unknown command: %s %s", command, arg);
			} else {
				LOG_DBG("unknown command: %s", command);
			}
			ret = send_simple_response(client, 502, STATUS_502);
		}
	}

exit:
	if (client->data_socket >= 0) {
		zsock_close(client->data_socket);
		client->data_socket = -1;
	}
	zsock_close(client->socket);
	client->socket = -1;

	return ret;
}

int lftpd_init(lftpd_t* lftpd, const char* base_dir, uint16_t port) {
	*lftpd = (lftpd_t){
		.base_dir = base_dir,
		.server_socket = -1,
	};

	k_mbox_init(&lftpd->conn_mbox);

	lftpd->server_socket = lftpd_inet_listen(port);
	if (lftpd->server_socket < 0) {
		LOG_ERR("error creating listener");
		return lftpd->server_socket;
	}

	return 0;
}

int lftpd_run(lftpd_t* lftpd) {
	if (!lftpd->base_dir || lftpd->server_socket < 0) {
		return -EINVAL;
	}
	int ret;

	struct sockaddr_in6 server_addr;
	socklen_t server_addr_len = sizeof(struct sockaddr_in6);
	ret = zsock_getsockname(lftpd->server_socket,
							(struct sockaddr*)&server_addr, &server_addr_len);
	if (ret < 0) {
		LOG_ERR("error getting server IP info");
	} else {
		char ip[INET6_ADDRSTRLEN];
		inet_ntop(AF_INET6, &server_addr.sin6_addr, ip, INET6_ADDRSTRLEN);
		int port = lftpd_inet_get_socket_port(lftpd->server_socket);
		LOG_INF("listening on [%s]:%d...", ip, port);
	}

	while (true) {
		LOG_DBG("waiting for connection...");

		int client_socket = zsock_accept(lftpd->server_socket, NULL, NULL);
		if (client_socket < 0) {
			ret = client_socket;
			LOG_ERR("error accepting client socket");
			break;
		}

		struct k_mbox_msg msg = {
			.info = (uint32_t)client_socket,
			.size = 0,
			.tx_data = NULL,
			.tx_target_thread = K_ANY,
		};

		ret = k_mbox_put(&lftpd->conn_mbox, &msg, K_NO_WAIT);
		if (ret < 0) {
			LOG_WRN("no thread available to handle connection");
			zsock_close(client_socket);
		}
	}

	zsock_close(lftpd->server_socket);
	return ret;
}

int lftpd_client_run(lftpd_t* lftpd, lftpd_client_t* client) {
	*client = (lftpd_client_t){
		.base_dir = lftpd->base_dir,
		.socket = -1,
		.data_socket = -1,
	};

	int ret = lftpd_io_resolve_path(client->base_dir, client->base_dir,
									client->cwd, sizeof(client->cwd));
	if (ret < 0) return ret;

	while (true) {
		struct k_mbox_msg msg = {
			.size = 0,
			.rx_source_thread = K_ANY,
		};
		ret = k_mbox_get(&lftpd->conn_mbox, &msg, NULL, K_FOREVER);
		if (ret < 0) return ret;

		client->socket = msg.info;
		handle_control_channel(client);
	}

	return 0;
}