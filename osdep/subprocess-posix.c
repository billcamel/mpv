/*
 * This file is part of mpv.
 *
 * mpv is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * mpv is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with mpv.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <spawn.h>
#include <poll.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>

#include "osdep/subprocess.h"

#include "osdep/io.h"
#include "common/common.h"
#include "stream/stream.h"

// Normally, this must be declared manually, but glibc is retarded.
#ifndef __GLIBC__
extern char **environ;
#endif

// A silly helper: automatically skips entries with negative FDs
static int sparse_poll(struct pollfd *fds, int num_fds, int timeout)
{
    struct pollfd p_fds[10];
    int map[10];
    if (num_fds > MP_ARRAY_SIZE(p_fds))
        return -1;
    int p_num_fds = 0;
    for (int n = 0; n < num_fds; n++) {
        map[n] = -1;
        if (fds[n].fd < 0)
            continue;
        map[n] = p_num_fds;
        p_fds[p_num_fds++] = fds[n];
    }
    int r = poll(p_fds, p_num_fds, timeout);
    for (int n = 0; n < num_fds; n++)
        fds[n].revents = (map[n] < 0 && r >= 0) ? 0 : p_fds[map[n]].revents;
    return r;
}

int mp_subprocess(char **args, struct mp_cancel *cancel, void *ctx,
                  subprocess_read_cb on_stdout, subprocess_read_cb on_stderr,
                  char **error)
{
    posix_spawn_file_actions_t fa;
    bool fa_destroy = false;
    int status = -1;
    int p_stdout[2] = {-1, -1};
    int p_stderr[2] = {-1, -1};
    pid_t pid = -1;

    if (mp_make_cloexec_pipe(p_stdout) < 0)
        goto done;
    if (mp_make_cloexec_pipe(p_stderr) < 0)
        goto done;

    if (posix_spawn_file_actions_init(&fa))
        goto done;
    fa_destroy = true;
    // redirect stdout and stderr
    if (posix_spawn_file_actions_adddup2(&fa, p_stdout[1], 1))
        goto done;
    if (posix_spawn_file_actions_adddup2(&fa, p_stderr[1], 2))
        goto done;

    if (posix_spawnp(&pid, args[0], &fa, NULL, args, environ)) {
        pid = -1;
        goto done;
    }

    close(p_stdout[1]);
    p_stdout[1] = -1;
    close(p_stderr[1]);
    p_stderr[1] = -1;

    int *read_fds[2] = {&p_stdout[0], &p_stderr[0]};
    subprocess_read_cb read_cbs[2] = {on_stdout, on_stderr};

    while (p_stdout[0] >= 0 || p_stderr[0] >= 0) {
        struct pollfd fds[] = {
            {.events = POLLIN, .fd = *read_fds[0]},
            {.events = POLLIN, .fd = *read_fds[1]},
            {.events = POLLIN, .fd = cancel ? mp_cancel_get_fd(cancel) : -1},
        };
        if (sparse_poll(fds, MP_ARRAY_SIZE(fds), -1) < 0 && errno != EINTR)
            break;
        for (int n = 0; n < 2; n++) {
            if (fds[n].revents) {
                char buf[4096];
                ssize_t r = read(*read_fds[n], buf, sizeof(buf));
                if (r < 0 && errno == EINTR)
                    continue;
                if (r > 0 && read_cbs[n])
                    read_cbs[n](ctx, buf, r);
                if (r <= 0) {
                    close(*read_fds[n]);
                    *read_fds[n] = -1;
                }
            }
        }
        if (fds[2].revents) {
            kill(pid, SIGKILL);
            break;
        }
    }

    // Note: it can happen that a child process closes the pipe, but does not
    //       terminate yet. In this case, we would have to run waitpid() in
    //       a separate thread and use pthread_cancel(), or use other weird
    //       and laborious tricks. So this isn't handled yet.
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}

done:
    if (fa_destroy)
        posix_spawn_file_actions_destroy(&fa);
    close(p_stdout[0]);
    close(p_stdout[1]);
    close(p_stderr[0]);
    close(p_stderr[1]);

    if (WIFEXITED(status) && WEXITSTATUS(status) != 127) {
        *error = NULL;
        status = WEXITSTATUS(status);
    } else {
        *error = WEXITSTATUS(status) == 127 ? "init" : "killed";
        status = -1;
    }

    return status;
}
