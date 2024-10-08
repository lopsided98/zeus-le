#include "ftp.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "lftpd.h"

LOG_MODULE_REGISTER(ftp);

#define FTP_MAX_CONN 3

static K_THREAD_STACK_DEFINE(ftp_server_stack, 512);
static K_THREAD_STACK_ARRAY_DEFINE(ftp_conn_stacks, FTP_MAX_CONN, 2048);

static struct ftp {
    struct k_thread server_thread;
    struct k_thread conn_threads[FTP_MAX_CONN];
    struct lftpd lftp;
    struct lftpd_conn conn[FTP_MAX_CONN];

    bool init;
} ftp;

static void ftp_server_run(void *p1, void *p2, void *p3) {
    struct ftp *f = &ftp;
    lftpd_run(&f->lftp);
}

static void ftp_conn_run(void *p1, void *p2, void *p3) {
    struct ftp *f = &ftp;
    struct lftpd_conn *client = p1;
    lftpd_conn_run(&f->lftp, client);
}

int ftp_init(void) {
    struct ftp *f = &ftp;
    if (f->init) return -EALREADY;

    int ret = lftpd_init(&f->lftp, "/", 21);
    if (ret < 0) {
        LOG_ERR("failed to initialize FTP server (err %d)", ret);
        return ret;
    }

    k_thread_create(&f->server_thread, ftp_server_stack,
                    K_THREAD_STACK_SIZEOF(ftp_server_stack), ftp_server_run,
                    NULL, NULL, NULL, K_PRIO_PREEMPT(9), 0, K_NO_WAIT);
    k_thread_name_set(&f->server_thread, "lftpd server");

    for (size_t i = 0; i < ARRAY_SIZE(ftp_conn_stacks); ++i) {
        k_thread_create(&f->conn_threads[i], ftp_conn_stacks[i],
                        K_THREAD_STACK_SIZEOF(ftp_conn_stacks[i]), ftp_conn_run,
                        &f->conn[i], NULL, NULL, K_PRIO_PREEMPT(8), 0,
                        K_NO_WAIT);
        k_thread_name_set(&f->conn_threads[i], "lftpd conn");
    }

    f->init = true;
    return 0;
}