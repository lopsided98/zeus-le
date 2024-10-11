#include <zephyr/shell/shell.h>

#include "mgr.h"

static int cmd_pair(const struct shell *sh, size_t argc, char **argv) {
    shell_print(sh, "start pairing command");
    return mgr_pair_start();
}

SHELL_SUBCMD_ADD((zeus), pair, NULL, "Pair with a central node", cmd_pair, 1,
                 0);

