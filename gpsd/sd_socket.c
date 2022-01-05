/*
 * This file is Copyright 2011 by Eckhart Wörner
 * This file is Copyright 2011 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 */

#include "gpsd_config.h"  /* must be before all includes */

#include <limits.h>
#include <stdlib.h>
#include <unistd.h>

#include "sd_socket.h"

int sd_get_socket_count(void) {
    unsigned long n;
    const char* env;

    env = getenv("LISTEN_PID");
    if (!env)
        return 0;

    n = strtoul(env, NULL, 10);
    if (n == ULONG_MAX || (pid_t)n != getpid())
        return 0;

    env = getenv("LISTEN_FDS");
    if (!env)
        return 0;

    n = strtoul(env, NULL, 10);
    if (n == ULONG_MAX)
        return 0;

    return (int)n;
}
// vim: set expandtab shiftwidth=4
