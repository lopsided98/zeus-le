#include <zephyr/shell/shell.h>

#include "mgr.h"

static int cmd_pair(const struct shell *sh, size_t argc, char **argv) {
    shell_print(sh, "start pairing command");
    return mgr_pair_start();
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_zeus,
                               SHELL_CMD(pair, NULL, "Pair with a central node",
                                         cmd_pair),
                               SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(zeus, &sub_zeus, "Zeus commands", NULL);