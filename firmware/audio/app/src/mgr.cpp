#include "mgr.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/logging/log.h>

#include "coroutine_zephyr.hpp"
#include "record.h"
#include "sync_timer.h"
#include "zeus/protocol.h"

LOG_MODULE_REGISTER(mgr, LOG_LEVEL_DBG);

namespace {

enum class mgr_cmd {
    NONE,
    PAIR,
};

template <typename T>
CO_DECLARE(T*, mgr_loan_get_conn, co_loan<T>& loan, struct bt_conn* conn,
           bool cancel)
CO_DECLARE_END;

CO_DECLARE(int, mgr_scan_for_central, bt_addr_le_t& addr, bool cancel)
CO_DECLARE_END;

CO_DECLARE(int, mgr_scan_for_sync, const bt_addr_le_t& addr, uint8_t& sid,
           bool cancel)
CO_DECLARE_END;

CO_DECLARE(int, mgr_pair, bool cancel)
bt_conn* conn;
char addr_str[BT_ADDR_LE_STR_LEN];
union {
    mgr_loan_get_conn<const struct mgr_connection_event>
        loan_get_connection_evt;
    mgr_loan_get_conn<const struct mgr_auth_event> loan_get_auth_evt;
    CO_CALLEE(mgr_scan_for_central);
};
CO_DECLARE_END;

CO_DECLARE(int, mgr_sync, const bt_addr_le_t& addr, bool cancel)
bt_le_per_adv_sync* sync;
uint8_t sid;
union {
    CO_CALLEE(mgr_scan_for_sync);
};
CO_DECLARE_END;

CO_DECLARE(int, mgr_run)
bt_addr_le_t addr;
union {
    CO_CALLEE(mgr_sync);
    CO_CALLEE(mgr_pair);
};
CO_DECLARE_END;

struct mgr_scan_recv_event {
    const struct bt_le_scan_recv_info* info;
    struct net_buf_simple* buf;
};

struct mgr_per_adv_sync_event {
    enum { SYNCED, TERM, RECV } type;
    struct bt_le_per_adv_sync* sync;

    union {
        struct bt_le_per_adv_sync_synced_info* synced;
        const struct bt_le_per_adv_sync_term_info* term;
        struct {
            const struct bt_le_per_adv_sync_recv_info* info;
            struct net_buf_simple* buf;
        } recv;
    } info;
};

struct mgr_connection_event {
    struct bt_conn* conn;
    uint8_t err;
};

struct mgr_auth_event {
    enum {
        SECURITY_CHANGED,
        PAIRING_COMPLETE,
        PAIRING_FAILED,
    } type;
    struct bt_conn* conn;

    struct security_changed {
        bt_security_t level;
        enum bt_security_err err;
    };
    struct pairing_complete {
        bool bonded;
    };
    struct pairing_failed {
        enum bt_security_err reason;
    };

    union {
        struct security_changed security_changed;
        struct pairing_complete pairing_complete;
        struct pairing_failed pairing_failed;
    } event;
};

struct mgr {
    co_loan<const struct mgr_scan_recv_event> scan_recv;
    co_loan<const struct mgr_per_adv_sync_event> per_adv_sync;
    co_loan<const struct mgr_connection_event> connected;
    co_loan<const struct mgr_connection_event> disconnected;
    co_loan<const struct mgr_auth_event> auth;
    co_sync<mgr_run> run;

    mgr_cmd cmd = mgr_cmd::NONE;
    bool cancel_command = false;
} mgr;

void mgr_scan_recv(const struct bt_le_scan_recv_info* info,
                   struct net_buf_simple* buf) {
    struct mgr* m = &mgr;
    struct mgr_scan_recv_event evt = {.info = info, .buf = buf};
    auto guard = m->scan_recv.loan(evt);
    m->run(K_FOREVER);
}

struct bt_le_scan_cb mgr_scan_cb = {
    .recv = mgr_scan_recv,
};

void mgr_per_adv_sync_synced(struct bt_le_per_adv_sync* sync,
                             struct bt_le_per_adv_sync_synced_info* info) {
    struct mgr* m = &mgr;
    struct mgr_per_adv_sync_event evt = {
        .type = mgr_per_adv_sync_event::SYNCED,
        .sync = sync,
        .info = {.synced = info},
    };
    auto guard = m->per_adv_sync.loan(evt);
    m->run(K_FOREVER);
}

void mgr_per_adv_sync_term(struct bt_le_per_adv_sync* sync,
                           const struct bt_le_per_adv_sync_term_info* info) {
    struct mgr* m = &mgr;
    struct mgr_per_adv_sync_event evt = {
        .type = mgr_per_adv_sync_event::TERM,
        .sync = sync,
        .info = {.term = info},
    };
    auto guard = m->per_adv_sync.loan(evt);
    m->run(K_FOREVER);
}

void mgr_per_adv_sync_recv(struct bt_le_per_adv_sync* sync,
                           const struct bt_le_per_adv_sync_recv_info* info,
                           struct net_buf_simple* buf) {
    struct mgr* m = &mgr;
    struct mgr_per_adv_sync_event evt = {
        .type = mgr_per_adv_sync_event::RECV,
        .sync = sync,
        .info = {.recv = {.info = info, .buf = buf}},
    };
    auto guard = m->per_adv_sync.loan(evt);
    m->run(K_FOREVER);
}

struct bt_le_per_adv_sync_cb mgr_per_adv_sync_cb = {
    .synced = mgr_per_adv_sync_synced,
    .term = mgr_per_adv_sync_term,
    .recv = mgr_per_adv_sync_recv,
};

void mgr_connected(struct bt_conn* conn, uint8_t err) {
    struct mgr* m = &mgr;
    struct mgr_connection_event evt {
        .conn = conn, .err = err,
    };
    auto guard = m->connected.loan(evt);
    m->run(K_FOREVER);
}

void mgr_disconnected(struct bt_conn* conn, uint8_t err) {
    struct mgr* m = &mgr;
    struct mgr_connection_event evt {
        .conn = conn, .err = err,
    };
    auto guard = m->disconnected.loan(evt);
    m->run(K_FOREVER);
}

void mgr_security_changed(struct bt_conn* conn, bt_security_t level,
                          enum bt_security_err err) {
    struct mgr* m = &mgr;
    struct mgr_auth_event evt = {
        .type = mgr_auth_event::SECURITY_CHANGED,
        .conn = conn,
        .event = {.security_changed = {.level = level, .err = err}},
    };
    auto guard = m->auth.loan(evt);
    m->run(K_FOREVER);
}

BT_CONN_CB_DEFINE(mgr_conn_cb) = {
    .connected = mgr_connected,
    .disconnected = mgr_disconnected,
    .security_changed = mgr_security_changed,
};

void mgr_pairing_complete(struct bt_conn* conn, bool bonded) {
    struct mgr* m = &mgr;
    struct mgr_auth_event evt = {
        .type = mgr_auth_event::PAIRING_COMPLETE,
        .conn = conn,
        .event = {.pairing_complete = {.bonded = bonded}},
    };
    auto guard = m->auth.loan(evt);
    m->run(K_FOREVER);
}

void mgr_pairing_failed(struct bt_conn* conn, enum bt_security_err reason) {
    struct mgr* m = &mgr;
    struct mgr_auth_event evt = {
        .type = mgr_auth_event::PAIRING_FAILED,
        .conn = conn,
        .event = {.pairing_failed = {.reason = reason}},
    };
    auto guard = m->auth.loan(evt);
    m->run(K_FOREVER);
}

struct bt_conn_auth_info_cb mgr_auth_info_cb = {
    .pairing_complete = mgr_pairing_complete,
    .pairing_failed = mgr_pairing_failed,
};

void mgr_switch_to_work_queue_handler(struct k_work* work) {
    struct mgr* m = &mgr;
    m->run(K_FOREVER);
}

CO_DECLARE(int, mgr_switch_to_work_queue)
k_work work;
CO_DECLARE_END;

CO_DEFINE(int, mgr_switch_to_work_queue, void) {
    CO_BEGIN;
    k_work_init(&work, mgr_switch_to_work_queue_handler);
    {
        int ret = k_work_submit(&work);
        if (ret) CO_RETURN(ret);
    }
    do {
        CO_YIELD;
        // Make sure we are actually running in workqueue, not woken by
        // something else
    } while (!(k_work_busy_get(&work) & K_WORK_RUNNING));
    CO_RETURN(0);
    // FIXME: work item goes out of scope, but needs to be accessed by work
    // queue
}

bt_addr_le_t mgr_get_paired_address(void) {
    bt_addr_le_t addr = bt_addr_le_none;
    bt_foreach_bond(
        BT_ID_DEFAULT,
        [](const struct bt_bond_info* info, void* user_data) {
            bt_addr_le_t* addr = static_cast<bt_addr_le_t*>(user_data);
            *addr = info->addr;
        },
        &addr);
    return addr;
}

int mgr_set_auto_connect(void) {
    int ret;
    bt_addr_le_t addr = mgr_get_paired_address();

    if (bt_addr_le_cmp(&addr, BT_ADDR_LE_NONE) == 0) {
        // No bond, disable auto-connection
        bt_le_set_auto_conn(&addr, NULL);
        return 0;
    }

    char addr_str[BT_ADDR_STR_LEN];
    bt_addr_le_to_str(&addr, addr_str, sizeof(addr_str));
    LOG_DBG("auto-connecting to %s", addr_str);

    auto conn_param = BT_LE_CONN_PARAM_DEFAULT[0];
    ret = bt_le_set_auto_conn(&addr, &conn_param);
    if (ret) {
        LOG_WRN("failed to enable auto-connect to %s (err %d)", addr_str, ret);
    }
    return ret;
}

template <typename T>
CO_DEFINE(T*, mgr_loan_get_conn<T>, co_loan<T>& loan, struct bt_conn* conn,
          bool cancel) {
    T* ret;
    do {
        ret = CO_AWAIT(loan.get(cancel));
        if (!ret) CO_RETURN(nullptr);
    } while (ret->conn != conn);
    CO_RETURN(ret);
}

CO_DEFINE(int, mgr_scan_for_central, bt_addr_le_t& addr, bool cancel) {
    struct mgr* m = &mgr;
    CO_BEGIN;
    int ret;

    {
        const auto& param = BT_LE_SCAN_PASSIVE;
        ret = bt_le_scan_start(param, NULL);
        if (ret) {
            goto exit;
        }
    }

    const struct mgr_scan_recv_event* info;
    while (true) {
        info = CO_AWAIT(m->scan_recv.get(cancel));
        if (!info) {
            ret = -ECANCELED;
            goto exit;
        }

        if (!(info->info->adv_props & BT_GAP_ADV_PROP_CONNECTABLE)) {
            continue;
        }

        auto match_central_uuid = [](struct bt_data* data, void* user_data) {
            bool* matched = static_cast<bool*>(user_data);

            switch (data->type) {
                case BT_DATA_UUID128_SOME:
                case BT_DATA_UUID128_ALL:
                    if (data->data_len % 16 != 0) {
                        LOG_WRN("invalid AD UUIDs: length=%u", data->data_len);
                        return true;
                    }

                    const struct bt_uuid_128 uuid_expected =
                        BT_UUID_INIT_128(ZEUS_BT_UUID_VAL);

                    for (uint8_t i = 0; i < data->data_len; i += 16) {
                        struct bt_uuid_128 uuid = BT_UUID_INIT_128();
                        memcpy(uuid.val, data->data, sizeof(uuid.val));

                        if (0 != bt_uuid_cmp(&uuid.uuid, &uuid_expected.uuid))
                            continue;

                        *matched = true;
                        return false;
                    }
                    break;
            }

            return true;
        };
        bool matched = false;
        bt_data_parse(info->buf, match_central_uuid, &matched);
        if (!matched) continue;

        break;
    }

    addr = *info->info->addr;
    ret = 0;

exit:
    bt_le_scan_stop();
    CO_RETURN(ret);
}

CO_DEFINE(int, mgr_scan_for_sync, const bt_addr_le_t& addr, uint8_t& sid,
          bool cancel) {
    struct mgr* m = &mgr;
    CO_BEGIN;
    int ret;

    {
        const auto& param = BT_LE_SCAN_PASSIVE;
        ret = bt_le_scan_start(param, NULL);
        if (ret) {
            goto exit;
        }
    }

    const struct mgr_scan_recv_event* info;
    while (true) {
        info = CO_AWAIT(m->scan_recv.get(cancel));
        if (!info) {
            ret = -ECANCELED;
            goto exit;
        }

        // Ignore non-periodic advertisments
        if (info->info->interval == 0) continue;

        if (bt_addr_le_cmp(info->info->addr, &addr) != 0) {
            continue;
        }

        break;
    }

    sid = info->info->sid;

    ret = 0;

exit:
    bt_le_scan_stop();
    CO_RETURN(ret);
}

CO_DEFINE(int, mgr_pair, bool cancel) {
    struct mgr* m = &mgr;
    CO_BEGIN;
    int ret;

    LOG_INF("pairing");

    bt_addr_le_t addr;
    ret = CO_AWAIT(mgr_scan_for_central(addr, cancel));
    if (ret) {
        if (ret != -ECANCELED) {
            LOG_WRN("failed to scan (err %d)", ret);
        }
        goto error;
    }

    bt_addr_le_to_str(&addr, addr_str, sizeof(addr_str));
    LOG_INF("device found: %s", addr_str);

    {
        auto create_param = BT_CONN_LE_CREATE_CONN[0];
        auto conn_param = BT_LE_CONN_PARAM_DEFAULT[0];
        ret = bt_conn_le_create(&addr, &create_param, &conn_param, &conn);
    }
    if (ret && ret != -EALREADY) {
        LOG_WRN("failed to create connection (err %d)", ret);
        goto error;
    }

    {
        const struct mgr_connection_event* conn_evt;
        conn_evt =
            CO_AWAIT(loan_get_connection_evt(m->connected, conn, cancel));
        if (!conn_evt) {
            ret = -ECANCELED;
            goto error;
        }

        if (conn_evt->err) {
            ret = conn_evt->err;
            LOG_WRN("connection failed (err %d)", ret);
            goto error;
        }
    }

    LOG_INF("connected to %s", addr_str);

    ret = bt_conn_set_security(conn, BT_SECURITY_L2);
    if (ret) {
        LOG_WRN("failed to enable security (err %d)", ret);
        goto error;
    }

    {
        bool paired;
        do {
            const struct mgr_auth_event* auth_evt;
            auth_evt = CO_AWAIT(loan_get_auth_evt(m->auth, conn, cancel));
            if (!auth_evt) {
                ret = -ECANCELED;
                goto error;
            }

            paired = false;
            switch (auth_evt->type) {
                case mgr_auth_event::SECURITY_CHANGED:
                    ret = auth_evt->event.security_changed.err;
                    if (ret) {
                        LOG_WRN("failed to set security (err %d)", ret);
                        goto error;
                    }
                    paired = true;
                    break;
                case mgr_auth_event::PAIRING_COMPLETE:
                    LOG_INF("paired successfully, bonded: %u",
                            auth_evt->event.pairing_complete.bonded);
                    break;
                case mgr_auth_event::PAIRING_FAILED:
                    ret = auth_evt->event.pairing_failed.reason;
                    LOG_WRN("pairing failed (err %d)", ret);
                    goto error;
            }
        } while (!paired);

        {
            struct bt_conn_info info;
            ret = bt_conn_get_info(conn, &info);
            if (ret) goto error;

            LOG_INF("secure connection established, key size: %u, flags: %u",
                    info.security.enc_key_size, info.security.flags);
        }
    }

    ret = 0;
    goto exit;

error:
    if (conn) {
        bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    }

exit:
    if (conn) {
        bt_conn_unref(conn);
    }
    CO_RETURN(ret);
}

bool mgr_parse_adv_data(const struct bt_data& adv, struct zeus_adv_data& data) {
    if (adv.type != BT_DATA_MANUFACTURER_DATA) return false;
    uint8_t len = adv.data_len;
    if (len < sizeof(data.hdr)) return false;
    if (len > sizeof(data)) return false;

    memcpy(&data, adv.data, len);

    size_t body_len = len - sizeof(data.hdr);
    if (body_len == 0) {
        // Empty body means no command
        data.cmd = {.hdr = {.id = ZEUS_ADV_CMD_NONE}};
        return true;
    }

    size_t cmd_len = body_len - sizeof(data.cmd.hdr);
    switch (data.cmd.hdr.id) {
        case ZEUS_ADV_CMD_NONE:
        case ZEUS_ADV_CMD_STOP:
            if (cmd_len != 0) return false;
            break;
        case ZEUS_ADV_CMD_START:
            if (cmd_len != sizeof(data.cmd.start)) return false;
            break;
        default:
            // Unknown command
            return false;
    }

    return true;
}

void mgr_handle_adv_data(const struct zeus_adv_data& data) {
    sync_timer_recv_adv(&data.hdr);

    if (data.cmd.hdr.id != ZEUS_ADV_CMD_NONE) {
        LOG_DBG("received command: %d", data.cmd.hdr.id);
    }

    switch (data.cmd.hdr.id) {
        case ZEUS_ADV_CMD_NONE:
            break;
        case ZEUS_ADV_CMD_START:
            record_start(data.cmd.start.time);
            break;
        case ZEUS_ADV_CMD_STOP:
            record_stop();
            break;
    }
}

CO_DEFINE(int, mgr_sync, const bt_addr_le_t& addr, bool cancel) {
    auto& m = mgr;
    CO_BEGIN;
    int ret;

    // ret = mgr_set_auto_connect();
    // if (ret) goto exit;

    ret = CO_AWAIT(mgr_scan_for_sync(addr, sid, cancel));
    if (ret) {
        LOG_WRN("failed to scan (err %d)", ret);
        CO_RETURN(ret);
    }

    {
        struct bt_le_per_adv_sync_param param = {
            .sid = sid,
            .skip = 0,
            .timeout = 100,
        };
        bt_addr_le_copy(&param.addr, &addr);

        ret = bt_le_per_adv_sync_create(&param, &sync);
        if (ret) {
            sync = NULL;
            LOG_ERR("failed to start adv. sync (err %d)", ret);
            goto exit;
        }
    }

    while (true) {
        const struct mgr_per_adv_sync_event* evt;
        evt = CO_AWAIT(m.per_adv_sync.get(cancel));
        if (!evt) {
            ret = -ECANCELED;
            goto exit;
        }

        switch (evt->type) {
            case mgr_per_adv_sync_event::SYNCED:
                LOG_INF("sync started");
                break;
            case mgr_per_adv_sync_event::TERM:
                LOG_INF("sync terminated");
                break;
            case mgr_per_adv_sync_event::RECV:
                bt_data_parse(
                    evt->info.recv.buf,
                    [](struct bt_data* adv, void* user_data) {
                        struct zeus_adv_data data;
                        if (!mgr_parse_adv_data(*adv, data)) {
                            return true;
                        }

                        mgr_handle_adv_data(data);
                        return false;
                    },
                    NULL);

                break;
        }
    }

exit:
    bt_le_set_auto_conn(&addr, NULL);
    if (sync) bt_le_per_adv_sync_delete(sync);

    CO_RETURN(ret);
}

CO_DEFINE(int, mgr_run) {
    auto& m = mgr;
    CO_BEGIN;
    int ret;

    bt_le_scan_cb_register(&mgr_scan_cb);
    bt_le_per_adv_sync_cb_register(&mgr_per_adv_sync_cb);
    ret = bt_conn_auth_info_cb_register(&mgr_auth_info_cb);
    if (ret) {
        LOG_ERR("failed to register auth info callbacks (err %d)", ret);
        CO_RETURN(ret);
    }

    while (true) {
        m.cancel_command = false;
        auto cmd = m.cmd;
        m.cmd = mgr_cmd::NONE;
        switch (cmd) {
            case mgr_cmd::NONE:
                addr = mgr_get_paired_address();
                if (bt_addr_le_cmp(&addr, BT_ADDR_LE_NONE) == 0) {
                    // No paired device, just wait
                    CO_YIELD;
                } else {
                    CO_AWAIT(mgr_sync(addr, m.cancel_command));
                }
                break;
            case mgr_cmd::PAIR:
                CO_AWAIT(mgr_pair(m.cancel_command));
                break;
        }
    }
}

}  // namespace

extern "C" {

int mgr_init() {
    struct mgr* m = &mgr;
    m->run(K_FOREVER);
    return 0;
}

int mgr_pair_start() {
    auto& m = mgr;
    m.cmd = mgr_cmd::PAIR;
    m.cancel_command = true;
    m.run(K_FOREVER);
    return 0;
}
}