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
#include "private/lftpd_path.h"
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
	int (*handler)(struct lftpd_conn* conn, char* arg, size_t arg_buf_len);
} command_t;

static int cmd_cwd(struct lftpd_conn* conn, char* arg, size_t arg_buf_len);
static int cmd_dele(struct lftpd_conn* conn, char* arg, size_t arg_buf_len);
static int cmd_epsv(struct lftpd_conn* conn, char* arg, size_t arg_buf_len);
static int cmd_feat(struct lftpd_conn* conn, char* arg, size_t arg_buf_len);
static int cmd_list(struct lftpd_conn* conn, char* arg, size_t arg_buf_len);
static int cmd_mkd(struct lftpd_conn* conn, char* arg, size_t arg_buf_len);
static int cmd_nlst(struct lftpd_conn* conn, char* arg, size_t arg_buf_len);
static int cmd_noop(struct lftpd_conn* conn, char* arg, size_t arg_buf_len);
static int cmd_pass(struct lftpd_conn* conn, char* arg, size_t arg_buf_len);
static int cmd_pasv(struct lftpd_conn* conn, char* arg, size_t arg_buf_len);
static int cmd_pwd(struct lftpd_conn* conn, char* arg, size_t arg_buf_len);
static int cmd_quit(struct lftpd_conn* conn, char* arg, size_t arg_buf_len);
static int cmd_retr(struct lftpd_conn* conn, char* arg, size_t arg_buf_len);
static int cmd_rmd(struct lftpd_conn* conn, char* arg, size_t arg_buf_len);
static int cmd_size(struct lftpd_conn* conn, char* arg, size_t arg_buf_len);
static int cmd_stor(struct lftpd_conn* conn, char* arg, size_t arg_buf_len);
static int cmd_syst(struct lftpd_conn* conn, char* arg, size_t arg_buf_len);
static int cmd_type(struct lftpd_conn* conn, char* arg, size_t arg_buf_len);
static int cmd_user(struct lftpd_conn* conn, char* arg, size_t arg_buf_len);

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

static int send_response(struct lftpd_conn* conn, int code, bool include_code,
						 bool multiline_start, const char* format, ...) {
	char* buf = conn->buf;
	size_t buf_len = sizeof(conn->buf);
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
	} else if (ret >= sizeof(conn->buf)) {
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

	return lftpd_inet_write_string(conn->socket, conn->buf);
}

#define send_simple_response(conn, code, format, ...) \
	send_response(conn, code, true, false, format, ##__VA_ARGS__)

#define send_multiline_response_begin(conn, code, format, ...) \
	send_response(conn, code, true, true, format, ##__VA_ARGS__)

#define send_multiline_response_line(conn, format, ...) \
	send_response(conn, 0, false, false, format, ##__VA_ARGS__)

#define send_multiline_response_end(conn, code, format, ...) \
	send_response(conn, code, true, false, format, ##__VA_ARGS__)

static int accept_data_connection(struct lftpd_conn* conn) {
	if (conn->data_socket < 0) {
		return -EBADF;
	}

	// get the port from the new socket, which is random
	int port = lftpd_inet_get_socket_port(conn->data_socket);

	// wait for the connection to the data port
	LOG_DBG("waiting for data port connection on port %d...", port);
	int conn_socket = zsock_accept(conn->data_socket, NULL, NULL);
	if (conn_socket < 0) {
		LOG_ERR("failed to accept data connection (err %d)", errno);
		zsock_close(conn->data_socket);
		conn->data_socket = -1;
		return conn_socket;
	}
	LOG_DBG("data port connection received...");
	return conn_socket;
}

static int send_list(struct lftpd_conn* conn, int data_socket,
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
				ret = snprintf(conn->buf, sizeof(conn->buf), directory_format,
							   entry.size, entry.name);
				break;
			case FS_DIR_ENTRY_FILE:
				ret = snprintf(conn->buf, sizeof(conn->buf), file_format,
							   entry.size, entry.name);
				break;
		}
		if (ret < 0) {
			goto close;
		} else if (ret >= sizeof(conn->buf)) {
			ret = -ENAMETOOLONG;
			goto close;
		}

		ret = lftpd_inet_write_string(data_socket, conn->buf);
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
		size_t len = ret;
		ret = fs_write(&file, buf, len);
		if (ret < 0) {
			goto close;
		} else if (ret != len) {
			// Zephyr never does partial writes except on failure, usually out
			// of space.
			ret = -errno;
			// Sometimes errno is zero, unclear why. Assume no space.
			if (ret == 0) ret = -ENOSPC;
			goto close;
		}
	}
	if (ret < 0) goto close;

	ret = 0;
close:
	fs_close(&file);
	return ret;
}

static int cmd_cwd(struct lftpd_conn* conn, char* arg, size_t arg_buf_len) {
	int ret;

	if (!arg || strlen(arg) == 0) {
		return send_simple_response(conn, 550, STATUS_550);
	}

	ret = lftpd_path_resolve(conn->base_dir, conn->cwd, arg, arg_buf_len);
	if (ret < 0) {
		return send_simple_response(conn, 553, STATUS_553);
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
			return send_simple_response(conn, 550, STATUS_550);
		}
	} else {
		// make sure the path exists
		struct fs_dirent entry;
		ret = fs_stat(cwd, &entry);
		if (ret < 0) {
			return send_simple_response(conn, 550, STATUS_550);
		}

		// make sure the path is a directory
		if (entry.type != FS_DIR_ENTRY_DIR) {
			return send_simple_response(conn, 550, STATUS_550);
		}
	}

	if (cwd_len + 1 > sizeof(conn->cwd)) {
		return send_simple_response(conn, 553, STATUS_553);
	}
	memcpy(conn->cwd, cwd, cwd_len + 1);

	return send_simple_response(conn, 250, STATUS_250);
}

static int cmd_dele(struct lftpd_conn* conn, char* arg, size_t arg_buf_len) {
	if (!arg || strlen(arg) == 0) {
		return send_simple_response(conn, 501, STATUS_501);
	}

	int ret = lftpd_path_resolve(conn->base_dir, conn->cwd, arg, arg_buf_len);
	if (ret < 0) {
		return send_simple_response(conn, 500, STATUS_500);
	}

	ret = fs_unlink(arg);
	if (ret < 0) {
		return send_simple_response(conn, 550, STATUS_550);
	}

	return send_simple_response(conn, 250, STATUS_250);
}

static int cmd_epsv(struct lftpd_conn* conn, char* arg, size_t arg_buf_len) {
	// We disable TIME_WAIT to avoid resource exhaustion, so we can just reuse
	// an existing socket if available.
	if (conn->data_socket < 0) {
		// open a data port
		conn->data_socket = lftpd_inet_listen(0);
		if (conn->data_socket < 0) {
			return send_simple_response(conn, 425, STATUS_425);
		}
	}

	// get the port from the socket, which is random
	int port = lftpd_inet_get_socket_port(conn->data_socket);

	// format the response
	return send_simple_response(conn, 229, STATUS_229, port);
}

static int cmd_feat(struct lftpd_conn* conn, char* arg, size_t arg_buf_len) {
	send_multiline_response_begin(conn, 211, STATUS_211);
	send_multiline_response_line(conn, " EPSV");
	send_multiline_response_line(conn, " PASV");
	send_multiline_response_line(conn, " SIZE");
	send_multiline_response_line(conn, " NLST");
	send_multiline_response_line(conn, " UTF8");
	send_multiline_response_end(conn, 211, "End");
	return 0;
}

static int cmd_list(struct lftpd_conn* conn, char* arg, size_t arg_buf_len) {
	int data_socket = accept_data_connection(conn);
	if (data_socket < 0) {
		return send_simple_response(conn, 425, STATUS_425);
	}

	send_simple_response(conn, 150, STATUS_150);

	int err = send_list(conn, data_socket, conn->cwd);
	zsock_close(data_socket);
	if (err == 0) {
		return send_simple_response(conn, 226, STATUS_226);
	} else {
		return send_simple_response(conn, 550, STATUS_550);
	}
}

static int cmd_mkd(struct lftpd_conn* conn, char* arg, size_t arg_buf_len) {
	if (!arg) {
		return send_simple_response(conn, 501, STATUS_501);
	}

	int ret = lftpd_path_resolve(conn->base_dir, conn->cwd, arg, arg_buf_len);
	if (ret < 0) {
		return send_simple_response(conn, 500, STATUS_500);
	}

	ret = fs_mkdir(arg);
	if (ret < 0) {
		return send_simple_response(conn, 550, STATUS_550);
	}

	return send_simple_response(conn, 257, STATUS_257, arg);
}

static int cmd_nlst(struct lftpd_conn* conn, char* arg, size_t arg_buf_len) {
	int ret;
	int data_socket = accept_data_connection(conn);
	if (data_socket < 0) {
		return send_simple_response(conn, 425, STATUS_425);
	}

	const char* path = conn->cwd;
	if (arg) {
		ret = lftpd_path_resolve(conn->base_dir, conn->cwd, arg, arg_buf_len);
		if (ret < 0) {
			ret = send_simple_response(conn, 553, STATUS_500);
			goto exit;
		}
		path = arg;
	}

	ret = send_simple_response(conn, 150, STATUS_150);
	if (ret < 0) goto exit;

	ret = send_nlst(data_socket, conn->cwd);
	if (ret < 0) {
		ret = send_simple_response(conn, 550, STATUS_550);
		goto exit;
	}

	ret = send_simple_response(conn, 226, STATUS_226);
exit:
	if (data_socket >= 0) {
		zsock_close(data_socket);
	}
	return ret;
}

static int cmd_noop(struct lftpd_conn* conn, char* arg, size_t arg_buf_len) {
	return send_simple_response(conn, 200, STATUS_200);
}

static int cmd_pass(struct lftpd_conn* conn, char* arg, size_t arg_buf_len) {
	return send_simple_response(conn, 230, STATUS_230);
}

static int cmd_pasv(struct lftpd_conn* conn, char* arg, size_t arg_buf_len) {
	// We disable TIME_WAIT to avoid resource exhaustion, so we can just reuse
	// an existing socket if available.
	if (conn->data_socket < 0) {
		// open a data port
		conn->data_socket = lftpd_inet_listen(0);
		if (conn->data_socket < 0) {
			return send_simple_response(conn, 425, STATUS_425);
		}
	}

	// get the port from the socket, which is random
	int port = lftpd_inet_get_socket_port(conn->data_socket);

	// get our IP by reading our side of the conn's control channel
	// socket connection
	struct sockaddr_storage conn_addr;
	socklen_t conn_addr_len = sizeof(conn_addr);
	int err = zsock_getsockname(conn->socket, (struct sockaddr*)&conn_addr,
								&conn_addr_len);
	if (err != 0) {
		LOG_ERR("error getting client IP info");
		zsock_close(conn->data_socket);
		return send_simple_response(conn, 425, STATUS_425);
	}

	if (conn_addr.ss_family != AF_INET) {
		LOG_ERR("client not connected over IPv4");
		zsock_close(conn->data_socket);
		return send_simple_response(conn, 425, STATUS_425);
	}

	// format the response
	uint32_t ip = htonl(((struct sockaddr_in*)&conn_addr)->sin_addr.s_addr);
	send_simple_response(conn, 227, STATUS_227, (ip >> 24) & 0xff,
						 (ip >> 16) & 0xff, (ip >> 8) & 0xff, (ip >> 0) & 0xff,
						 (port >> 8) & 0xff, (port >> 0) & 0xff);

	return 0;
}

static int cmd_pwd(struct lftpd_conn* conn, char* arg, size_t arg_buf_len) {
	return send_simple_response(conn, 257, "\"%s\" ", conn->cwd);
}

static int cmd_quit(struct lftpd_conn* conn, char* arg, size_t arg_buf_len) {
	send_simple_response(conn, 221, STATUS_221);
	return -1;
}

static int cmd_retr(struct lftpd_conn* conn, char* arg, size_t arg_buf_len) {
	int ret;
	int data_socket = accept_data_connection(conn);
	if (data_socket < 0) {
		return send_simple_response(conn, 425, STATUS_425);
	}

	ret = send_simple_response(conn, 150, STATUS_150);
	if (ret < 0) goto exit;

	ret = lftpd_path_resolve(conn->base_dir, conn->cwd, arg, arg_buf_len);
	if (ret < 0) {
		ret = send_simple_response(conn, 500, STATUS_500);
		goto exit;
	}

	ret = send_file(data_socket, arg, conn->buf, sizeof(conn->buf));
	if (ret < 0) {
		ret = send_simple_response(conn, 450, STATUS_450);
		goto exit;
	}

	ret = send_simple_response(conn, 226, STATUS_226);

exit:
	if (data_socket >= 0) {
		zsock_close(data_socket);
	}
	return ret;
}

static int cmd_rmd(struct lftpd_conn* conn, char* arg, size_t arg_buf_len) {
	if (!arg) {
		return send_simple_response(conn, 550, STATUS_501);
	}

	int ret = lftpd_path_resolve(conn->base_dir, conn->cwd, arg, arg_buf_len);
	if (ret < 0) {
		return send_simple_response(conn, 553, STATUS_500);
	}

	struct fs_dirent entry;
	ret = fs_stat(arg, &entry);
	if (ret < 0) {
		return send_simple_response(conn, 550, STATUS_550);
	}

	if (entry.type != FS_DIR_ENTRY_DIR) {
		return send_simple_response(conn, 550, STATUS_550);
	}

	ret = fs_unlink(arg);
	if (ret < 0) {
		return send_simple_response(conn, 550, STATUS_550);
	}

	return send_simple_response(conn, 250, STATUS_250);
}

static int cmd_size(struct lftpd_conn* conn, char* arg, size_t arg_buf_len) {
	if (!arg) {
		return send_simple_response(conn, 501, STATUS_501);
	}

	int ret = lftpd_path_resolve(conn->base_dir, conn->cwd, arg, arg_buf_len);
	if (ret < 0) {
		return send_simple_response(conn, 500, STATUS_500);
	}

	struct fs_dirent entry;
	ret = fs_stat(arg, &entry);
	if (ret < 0) {
		return send_simple_response(conn, 550, STATUS_550);
	}

	return send_simple_response(conn, 213, "%zu", entry.size);
}

static int cmd_stor(struct lftpd_conn* conn, char* arg, size_t arg_buf_len) {
	int ret;
	int data_socket = accept_data_connection(conn);
	if (data_socket < 0) {
		return send_simple_response(conn, 425, STATUS_425);
	}

	ret = send_simple_response(conn, 150, STATUS_150);
	if (ret < 0) goto exit;

	LOG_DBG("before resolve: %s", arg);
	ret = lftpd_path_resolve(conn->base_dir, conn->cwd, arg, arg_buf_len);
	if (ret < 0) {
		ret = send_simple_response(conn, 500, STATUS_500);
		goto exit;
	}

	ret = receive_file(data_socket, arg, conn->buf, sizeof(conn->buf));
	if (ret < 0) {
		ret = send_simple_response(conn, 450, STATUS_450);
		goto exit;
	}

	ret = send_simple_response(conn, 226, STATUS_226);

exit:
	if (data_socket >= 0) {
		zsock_close(data_socket);
	}
	return ret;
}

static int cmd_syst(struct lftpd_conn* conn, char* arg, size_t arg_buf_len) {
	return send_simple_response(conn, 215, "UNIX Type: L8");
}

static int cmd_type(struct lftpd_conn* conn, char* arg, size_t arg_buf_len) {
	return send_simple_response(conn, 200, STATUS_200);
}

static int cmd_user(struct lftpd_conn* conn, char* arg, size_t arg_buf_len) {
	return send_simple_response(conn, 230, STATUS_230);
}

static int handle_control_channel(struct lftpd_conn* conn) {
	struct sockaddr_in6 conn_addr;
	socklen_t conn_addr_len = sizeof(struct sockaddr_in6);
	int ret = zsock_getpeername(conn->socket, (struct sockaddr*)&conn_addr,
								&conn_addr_len);
	if (ret != 0) {
		LOG_ERR("error getting client IP info");
		LOG_INF("connection received...");
	} else {
		char ip[INET6_ADDRSTRLEN];
		zsock_inet_ntop(AF_INET6, &conn_addr.sin6_addr, ip, INET6_ADDRSTRLEN);
		int port = lftpd_inet_get_socket_port(conn->socket);
		LOG_INF("connection received from [%s]:%d...", ip, port);
	}

	ret = send_simple_response(conn, 220, STATUS_220);
	if (ret < 0) {
		LOG_ERR("error sending welcome message");
		goto exit;
	}

	while (ret >= 0) {
		ret = lftpd_inet_read_line(conn->socket, conn->cmd_buf,
								   sizeof(conn->cmd_buf));
		if (ret == -EOVERFLOW) {
			LOG_DBG("command too long");
			ret = send_simple_response(conn, 500, STATUS_500);
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
		char* p = strchr(conn->cmd_buf, ' ');
		if (p != NULL) {
			split_index = p - conn->cmd_buf;
		}
		// if no space, use the whole string
		else {
			split_index = strlen(conn->cmd_buf);
		}

		// if the index is 5 or greater the command is too long
		if (split_index >= 5) {
			ret = send_simple_response(conn, 500, STATUS_500);
			continue;
		}

		conn->cmd_buf[split_index] = 0;
		char* command = conn->cmd_buf;
		char* arg = conn->cmd_buf + split_index + 1;
		size_t arg_buf_len = sizeof(conn->cmd_buf) - split_index - 1;

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
				ret = commands[i].handler(conn, arg, arg_buf_len);
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
			ret = send_simple_response(conn, 502, STATUS_502);
		}
	}

exit:
	if (conn->data_socket >= 0) {
		zsock_close(conn->data_socket);
		conn->data_socket = -1;
	}
	zsock_close(conn->socket);
	conn->socket = -1;

	return ret;
}

int lftpd_init(struct lftpd* lftpd, const char* base_dir, uint16_t port) {
	*lftpd = (struct lftpd){
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

int lftpd_run(struct lftpd* lftpd) {
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
		zsock_inet_ntop(AF_INET6, &server_addr.sin6_addr, ip, INET6_ADDRSTRLEN);
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

int lftpd_conn_run(struct lftpd* lftpd, struct lftpd_conn* conn) {
	*conn = (struct lftpd_conn){
		.base_dir = lftpd->base_dir,
		.socket = -1,
		.data_socket = -1,
	};

	int ret = lftpd_path_resolve(conn->base_dir, conn->base_dir, conn->cwd,
								 sizeof(conn->cwd));
	if (ret < 0) return ret;

	while (true) {
		struct k_mbox_msg msg = {
			.size = 0,
			.rx_source_thread = K_ANY,
		};
		ret = k_mbox_get(&lftpd->conn_mbox, &msg, NULL, K_FOREVER);
		if (ret < 0) return ret;

		conn->socket = msg.info;
		handle_control_channel(conn);
	}

	return 0;
}