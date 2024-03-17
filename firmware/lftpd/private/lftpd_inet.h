#pragma once

#include <stddef.h>
#include <stdint.h>

int lftpd_inet_listen(uint16_t port);
int lftpd_inet_get_socket_port(int socket);

/**
 * @brief Read a line from the client, terminating when CRLF is received
 * or the buffer length is reached.
 */
int lftpd_inet_read_line(int socket, char* buffer, size_t buffer_len);

int lftpd_inet_write_string(int socket, const char* message);
