#include <assert.h>
#include <signal.h>
#include <unistd.h>

#include <linc/array.h>
#include "t_assert.h"
#include "t_harness.h"
#include "t_pview.h"
#include "t_utils.h"

bool verify_bytes(const char *str) {
    int tfd = start_timerfd((memcheck) ? 1000 : 100);
    size_t start = test->pty_out_data.len;

    while (test_poll(tfd) != -1) {
        if (start >= test->pty_out_data.len)
            continue;

        for (size_t i = start; i < test->pty_out_data.len; ++i) {
            if (strstr(&test->pty_out_data.cstr[i], str)) {
                if (close(tfd) == -1)
                    xtest_fatal("close: %m");
                return true;
            }
        }
    }

    if (close(tfd) == -1)
        xtest_fatal("close: %m");
    return false;
}

bool verify_kill(const char *name) {
    da_pstat pstats;
    size_t loops = 100;

    for (size_t i = 0; i < loops; ++i) {
        if (child_pstat(test->cpid, &pstats) == -1)
            xtest_fatal("child_pstat");

        for (size_t y = 0; y < pstats.size; ++y) {
            if (strcmp(pstats.data[y].name, name) == 0) {
                if (test->cpid == pstats.data[y].pid)
                    xtest_fatal("kill_child: trying to kill seashell");

                if (kill(pstats.data[y].pid, SIGTERM) == -1)
                    xtest_fatal("kill: %m");

                linc_array_free(&pstats);

                return true;
            }
        }

        test_pump_ms(1);
    }

    linc_array_free(&pstats);
    return false;
}

bool verify_pstate(const char *name, const char *state) {
    da_pstat pstats;
    bool state_good = false;
    char pstate[4096];

    size_t checks = (memcheck) ? 1000 : 100;

    for (size_t i = 0; i < checks; ++i) {
        if (child_pstat(test->cpid, &pstats) == -1)
            break;

        for (size_t y = 0; y < pstats.size; ++y) {
            if (strcmp(name, pstats.data[y].name) == 0) {
                strncpy(pstate, pstats.data[y].state, sizeof(pstate));

                if (strncmp(state, pstate, 1) == 0) {
                    state_good = true;
                    break;
                }
            }
        }

        linc_array_free(&pstats);

        if (state_good)
            break;

        test_pump_ms(1);
    }

    return state_good;
}

bool verify_termios(struct termios exp) {
    struct termios tio;

    int checks = (memcheck) ? 1000 : 100;

    for (int i = 0; i < checks; ++i) {
        if (tcgetattr(test->mfd, &tio) == -1)
            xtest_fatal("tcgetattr: %m");
        if (memcmp(exp.c_cc, tio.c_cc, sizeof(tio.c_cc))            == 0 &&
            memcmp(&exp.c_iflag, &tio.c_iflag, sizeof(tio.c_iflag)) == 0 &&
            memcmp(&exp.c_oflag, &tio.c_oflag, sizeof(tio.c_oflag)) == 0 &&
            memcmp(&exp.c_cflag, &tio.c_cflag, sizeof(tio.c_cflag)) == 0 &&
            memcmp(&exp.c_lflag, &tio.c_lflag, sizeof(tio.c_lflag)) == 0)
            return true;

        test_pump_ms(1);
    }

    return false;
}

bool verify_term(char **lines, int crow, int ccol) {
    assert(crow >= 0 && ccol >= 0);
    assert(crow < test->vt.rows && ccol < test->vt.cols);

    vt_lines_to_snap(lines, &test->vt_exp, crow, ccol);

    if (vt_snap_cmp(&test->vt_exp)) {
        vt_free_snap(&test->vt_exp);
        return true;
    }

    int tfd = start_timerfd((memcheck) ? 1000 : 100);

    while (test_poll(tfd) != -1) {
        bool same = vt_snap_cmp(&test->vt_exp);

        if (same) {
            vt_free_snap(&test->vt_exp);

            if (close(tfd) == -1)
                xtest_fatal("close: %m");

            return true;
        }
    }

    test->vt_act = vt_get_snap();
    test->diff_snaps = true;

    if (close(tfd) == -1)
        xtest_fatal("close: %m");

    return false;
}
