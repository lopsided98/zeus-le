#include "net_audio.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>

LOG_MODULE_REGISTER(net_audio);

static K_FIFO_DEFINE(net_audio_fifo);

static struct net_audio {
    int socket;
    struct k_fifo *const fifo;

    bool init;
    const char *addr_str;
    uint16_t mtu;
} net_audio = {
    .socket = -1,
    .fifo = &net_audio_fifo,

    .addr_str = "fe80::8854:88ff:fea9:23a6",
};

int net_audio_init(void) {
    int ret;
    struct net_audio *n = &net_audio;
    if (n->init) return -EALREADY;

    struct net_if *iface = net_if_get_default();
    if (!iface) {
        LOG_WRN("no network interface available");
        return 0;
    }

    struct in6_addr *ll_addr = net_if_ipv6_get_ll(iface, NET_ADDR_ANY_STATE);
    if (ll_addr) {
        char ll_addr_str[INET6_ADDRSTRLEN] = {0};
        zsock_inet_ntop(AF_INET6, ll_addr, ll_addr_str,
                        sizeof(struct in6_addr));
        LOG_INF("link-local address: %s", ll_addr_str);
    } else {
        LOG_WRN("no IPv6 link local address");
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

    n->mtu = net_if_get_mtu(iface) - 48;

    n->init = true;
    return 0;
}

int net_audio_send(const uint8_t *buf, size_t len) {
    struct net_audio *n = &net_audio;
    if (!n->init) return -EINVAL;

    while (len > 0) {
        size_t pkt_len = MIN(n->mtu, len);
        zsock_send(n->socket, buf, pkt_len, 0);
        len -= pkt_len;
        buf += pkt_len;
    }

    return 0;
}