#include "private/lftpd_inet.h"

#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>

LOG_MODULE_DECLARE(lftpd);

int lftpd_inet_listen(uint16_t port) {
	int s = zsock_socket(AF_INET6, SOCK_STREAM, 0);

	if (s < 0) {
		LOG_ERR("error creating listener socket");
		goto error;
	}

	struct sockaddr_in6 server_addr = {
		.sin6_family = AF_INET6,
		.sin6_addr = in6addr_any,
		.sin6_port = htons(port),
	};

	int err =
		zsock_bind(s, (struct sockaddr*)&server_addr, sizeof(server_addr));
	if (err < 0) {
		LOG_ERR("error binding listener port %d", port);
		goto error;
	}

	err = zsock_listen(s, 1);
	if (err < 0) {
		LOG_ERR("error listening on socket");
		goto error;
	}

	return s;

error:
	if (s >= 0) zsock_close(s);
	return -1;
}

int lftpd_inet_get_socket_port(int socket) {
	struct sockaddr_in6 data_port_addr;
	socklen_t data_port_addr_len = sizeof(struct sockaddr_in6);
	int err = zsock_getsockname(socket, (struct sockaddr*)&data_port_addr,
								&data_port_addr_len);
	if (err != 0) {
		LOG_ERR("error getting listener socket port number");
		return -1;
	}
	return ntohs(data_port_addr.sin6_port);
}

int lftpd_inet_read_line(int socket, char* buffer, size_t buffer_len) {
	if (!buffer || buffer_len < 2) return -EINVAL;

	memset(buffer, 0, buffer_len);
	int total_read_len = 0;
	bool overflow = false;
	while (true) {
		// read up to length - 1 bytes. the - 1 leaves room for the
		// null terminator.
		int read_len = zsock_recv(socket, buffer + total_read_len,
								  buffer_len - total_read_len - 1, 0);
		if (read_len == 0) {
			// end of stream - since we didn't find the end of line in
			// the previous pass we won't find it in this one, so this
			// is an error.
			return -ECONNRESET;
		} else if (read_len < 0) {
			// general error
			return read_len;
		}

		total_read_len += read_len;

		char* p = strstr(buffer, "\r\n");
		if (p) {
			if (overflow) {
				// Found CRLF but the line was too long
				return -EOVERFLOW;
			} else {
				// Null terminate the line and return
				*p = '\0';
				return total_read_len;
			}
		}

		if (total_read_len == buffer_len - 1) {
			// Buffer is full, client probably sent a too long path. In this
			// case, we want to keep reading to flush the rest of the line, but
			// return an error to the caller.
			overflow = true;
			if (buffer[total_read_len - 1] == '\r') {
				// If the last read ended right in the middle of CRLF, we need
				// to preserve the CR.
				total_read_len = 1;
				buffer[0] = '\r';
				buffer[1] = '\0';
			} else {
				total_read_len = 0;
				buffer[0] = '\0';
			}
		}
	}
}

int lftpd_inet_write_string(int socket, const char* message) {
	char* p = (char*)message;
	int length = strlen(message);
	while (length) {
		int write_len = zsock_send(socket, p, length, 0);
		if (write_len < 0) {
			LOG_ERR("write error");
			return write_len;
		}
		p += write_len;
		length -= write_len;
	}
	LOG_DBG("> %s", message);
	return 0;
}
