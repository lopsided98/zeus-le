#include "net_audio.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>

LOG_MODULE_REGISTER(net_audio);

static K_FIFO_DEFINE(net_audio_fifo);

static struct net_audio {
    int socket;
    struct k_fifo *const fifo;

    const char *addr_str;
    uint16_t mtu;
} net_audio = {
    .fifo = &net_audio_fifo,

    .addr_str = "fe80::cf73:fd64:8977:ea79",
};

int net_audio_init(void) {
    int ret;
    struct net_audio *n = &net_audio;

    if (!net_if_get_default()) {
        LOG_WRN("no network interface available");
        return 0;
    }

    struct sockaddr_in6 addr = {
        .sin6_family = AF_INET6,
        .sin6_port = htons(54321),
    };
    ret = zsock_inet_pton(addr.sin6_family, n->addr_str, &addr.sin6_addr);
    if (ret == 0) {
        LOG_ERR("invalid IPv6 address: %s", n->addr_str);
        return -EINVAL;
    } else if (ret < 0) {
        LOG_ERR("failed to convert address (err %d)", errno);
        return -errno;
    }

    n->socket = zsock_socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (n->socket < 0) {
        LOG_ERR("failed to create UDP socket (err %d)", errno);
        return -errno;
    }

    ret = zsock_connect(n->socket, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0) {
        LOG_ERR("connect failed (err %d)", errno);
        return -errno;
    }

    struct net_if *iface = net_if_get_default();
    if (!iface) {
        LOG_ERR("no network interfaces");
        return -ENODEV;
    }
    n->mtu = net_if_get_mtu(iface) - 48;

    return 0;
}

int net_audio_send(struct net_buf *buf) {
    struct net_audio *n = &net_audio;

    while (buf->len > 0) {
        size_t len = MIN(n->mtu, buf->len);
        void *pkt = net_buf_pull_mem(buf, len);
        zsock_send(n->socket, pkt, len, 0);
    }

    return 0;
}