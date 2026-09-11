#define _GNU_SOURCE

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "variable.h"
#include "shell_state.h"
#include "job_state.h"
#include "builtins.h"
#include "llog.h"
#include "utils.h"
#include "args.h"

typedef enum {
    SHOW_PIDS,
} jobs_opt_names;

static int get_int(const char *arg) {
    if (arg[0] == '\0')
        return -1;

    char *endptr;
    pid_t num = (int) strtol(&arg[0], &endptr, 10);

    if (*endptr != '\0')
        return -1;

    return num;
}

static int arg_to_sig(char *sig_arg) {
    /* - just to be consistent */
    if (strcmp("-HUP", sig_arg) == 0)   return SIGHUP;
    if (strcmp("-INT", sig_arg) == 0)   return SIGINT;
    if (strcmp("-QUIT", sig_arg) == 0)  return SIGQUIT;
    if (strcmp("-KILL", sig_arg) == 0)  return SIGKILL;
    if (strcmp("-USR1", sig_arg) == 0)  return SIGUSR1;
    if (strcmp("-SEGV", sig_arg) == 0)  return SIGSEGV;
    if (strcmp("-USR2", sig_arg) == 0)  return SIGUSR2;
    if (strcmp("-PIPE", sig_arg) == 0)  return SIGPIPE;
    if (strcmp("-ALRM", sig_arg) == 0)  return SIGALRM;
    if (strcmp("-TERM", sig_arg) == 0)  return SIGTERM;
    if (strcmp("-CHLD", sig_arg) == 0)  return SIGCHLD;
    if (strcmp("-CONT", sig_arg) == 0)  return SIGCONT;
    if (strcmp("-STOP", sig_arg) == 0)  return SIGSTOP;
    if (strcmp("-TSTP", sig_arg) == 0)  return SIGTSTP;
    if (strcmp("-TTIN", sig_arg) == 0)  return SIGTTIN;
    if (strcmp("-TTOU", sig_arg) == 0)  return SIGTTOU;
    if (strcmp("-WINCH", sig_arg) == 0) return SIGWINCH;

    return -1;
}
static int validate_argc(char **argv, size_t min_argc, size_t max_argc) {
    if (!argv || !argv[0]) {
        LOG_ERR("validate_args: bad argv array");
        fatal("internal error; check logs");
    }

    size_t arg_n = 0;
    for (char **i = argv + 1; *i != NULL; ++i)
        ++arg_n;

    if (arg_n < min_argc) {
        fprintf(stderr, "%s: not enough arguments\n", argv[0]);
        return -1;
    }

    if (arg_n > max_argc) {
        fprintf(stderr, "%s: too many arguments\n", argv[0]);
        return -1;
    }

    return arg_n;
}

static int run_echo_builtin(char **argv, shell_env *_) {
    if (validate_argc(argv, 1, 1) == -1)
        return EXIT_FAILURE;

    printf("%s\n", argv[1]);

    return EXIT_SUCCESS;
}

static int run_fg_builtin(char **argv, shell_env *sh_env) {
    if (sh_env->subshell) {
        fprintf(stderr, "%s: no job control in this shell\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (validate_argc(argv, 1, 1) == -1)
        return EXIT_FAILURE;

    if (argv[1][0] != '%') {
        fprintf(stderr, "%s: invalid argument: %s\n", argv[0], argv[1]);
        return EXIT_FAILURE;
    }

    pid_t jid = get_int(&argv[1][1]);
    if (jid == -1) {
        fprintf(stderr, "%s: job not found: %s\n", argv[0], argv[1]);
        return EXIT_FAILURE;
    }

    /* Always a race so lets "lie" here. It's this or risking
     * writing to the terminal with a process that expects to
     * be the only one that can write to it. Think interactive
     * programs like vim. */
    printf("[%d] Continued\n", jid);

    /* Save current tty state then apply saved tty state for the
     * job if one was saved (it was a foreground job) */
    job_plan *plan = lookup_plan(jid, NULL);

    if (tcgetattr(sh_env->tty_fd, &sh_env->tty_state) == -1) {
        fprintf(stderr, "%s: tcgetattr: %m", argv[0]);
        return EXIT_FAILURE;
    }

    /* a jobs terminal state wouldn't be saved if it is a bg job. If
     * a bg job is stopped it never owned the terminal so it doesn't
     * make sense to save it */
    if (plan && plan->tty_state_saved) {
        if (tcsetattr(sh_env->tty_fd, TCSANOW, &plan->tty_state) == -1) {
            fprintf(stderr, "%s: tcsetattr: %m", argv[0]);
            return EXIT_FAILURE;
        }

        plan->tty_state_saved = false;
    }

    pid_t pgid = lookup_job_pgrp(jid);

    if (tcsetpgrp(sh_env->tty_fd, pgid) == -1) {
        fprintf(stderr, "%s: tcsetpgrp: %m\n", argv[0]);
        return EXIT_FAILURE;
    }

    sh_env->fg_jid = jid;

    /* best effort, must report success when tcsetpgrp succeeds */
    if (killpg(pgid, SIGCONT) == -1 && errno != ESRCH)
        LOG_ERR("killpg: %m");

    if (nanosleep(&(struct timespec){0, 5000000}, NULL) == -1)
        LOG_ERR("nanosleep: %m");

    return EXIT_SUCCESS;
}

static int run_bg_builtin(char **argv, shell_env *sh_env) {
    if (sh_env->subshell) {
        fprintf(stderr, "%s: no job control in this shell\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (validate_argc(argv, 1, 1) == -1)
        return EXIT_FAILURE;

    if (argv[1][0] != '%') {
        fprintf(stderr, "%s: invalid argument: %s\n", argv[0], argv[1]);
        return EXIT_FAILURE;
    }

    pid_t jid = get_int(&argv[1][1]);
    if (jid == -1) {
        fprintf(stderr, "%s: job not found: %s\n", argv[0], argv[1]);
        return EXIT_FAILURE;
    }

    pid_t pgid = lookup_job_pgrp(jid);

    if (killpg(pgid, SIGCONT) == -1 && errno != ESRCH) {
        fprintf(stderr, "%s: killpg: %m\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (nanosleep(&(struct timespec){0, 5000000}, NULL) == -1) {
        fprintf(stderr, "%s: nanosleep: %m\n", argv[0]);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/* default sig is TERM */
static int run_kill_builtin(char **argv, shell_env *sh_env) {
    if (sh_env->subshell) {
        fprintf(stderr, "%s: no job control in this shell\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (validate_argc(argv, 1, 2) == -1)
        return EXIT_FAILURE;

    pid_t jid;
    pid_t pid;

    char *signal_arg;
    char *target_arg;

    if (!argv[2]) {
        signal_arg = NULL;
        target_arg = argv[1];
    } else {
        signal_arg = argv[1];
        target_arg = argv[2];
    }

    int sig = SIGTERM;

    if (signal_arg) {
        sig = arg_to_sig(signal_arg);
        if (sig == -1) {
            fprintf(stderr, "%s: unknown signal: %d\n", argv[0], sig);
            return EXIT_FAILURE;
        }
    }

    if (target_arg[0] == '%') {
        jid = get_int(&target_arg[1]);
        if (jid == -1) {
            fprintf(stderr, "%s: invalid job: %s\n", argv[0], target_arg);
            return EXIT_FAILURE;
        }

        pid_t pgid = lookup_job_pgrp(jid);
        if (pgid == -1) {
            fprintf(stderr, "%s: no such job\n", argv[0]);
            return EXIT_FAILURE;
        }

        if (killpg(pgid, sig) == -1 && errno != ESRCH) {
            fprintf(stderr, "%s: killpg failed: %m\n", argv[0]);
            return EXIT_FAILURE;
        }

    } else {
        pid = get_int(target_arg);
        if (pid == -1) {
            fprintf(stderr, "%s: illegal pid: %s\n", argv[0], target_arg);
            return EXIT_FAILURE;
        }

        if (kill(pid, sig) == -1 && errno != ESRCH) {
            fprintf(stderr, "%s: kill failed: %m\n", argv[0]);
            return EXIT_FAILURE;
        }
    }

    /* Report the signal more consistently when delivered immediately.
     *
     * This achieves that by allowing the wait event to be queued by
     * the time we arrive at process_signals().
     *
     * This does not mean that the wait event will be queued before
     * kill's builtin exit event, but that is fine because a builtin
     * exit event does not cause any job control messaging or notis.
     * It simply asks for the next job or the terminal if there aren't
     * any left */

    if (nanosleep(&(struct timespec){0, 5000000}, NULL) == -1) {
        fprintf(stderr, "%s: nanosleep failed: %m\n", argv[0]);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int run_jobs_builtin(char **argv, shell_env *sh_env) {
    if (sh_env->subshell) {
        fprintf(stderr, "%s: no job control in this shell\n", argv[0]);
        return EXIT_FAILURE;
    }

    int arg_n = validate_argc(argv, 0, 1);
    if (arg_n == -1)
        return EXIT_FAILURE;

    opt_data jobs_opts[] = {
        [SHOW_PIDS] = {
            .long_name = "pid",
            .short_name = 'p',
            .has_arg = NO_ARG,
        },
        (opt_data){0}
    };

    if (arg_parse(arg_n + 1, argv, jobs_opts, ARG_NO_NON_OPTS) == -1)
        return EXIT_FAILURE;

    da_jevent jevs = get_job_info();

    for (size_t i = 0; i < jevs.size; ++i) {
        job_event *jev = &jevs.data[i];
        printf("[%d] ", jev->jid);

        switch (jev->stat) {
        case JOB_RUN:  printf("Running\n"); break;
        case JOB_EXIT: printf("Exited\n");  break;
        case JOB_STOP: printf("Stopped\n"); break;
        }

        for (size_t i = 0; i < jev->pviews.size; ++i) {
            proc_view *pview = &jev->pviews.data[i];

            printf("   ");

            if (jobs_opts[SHOW_PIDS].found)
                printf("%d ", pview->pid);

            for (char **arg = pview->argv; *arg != NULL; ++arg)
                printf("%s ", *arg);

            switch(pview->stat) {
                case PROC_RUN:
                    printf("Running\n");
                    break;

                case PROC_EXIT:
                    printf("Exited with status %d\n", pview->exit_stat);
                    break;

                case PROC_SIG:
                    printf("%s by signal %d\n", strsignal(pview->term_sig),
                            pview->term_sig);
                    break;

                case PROC_STOP:
                    printf("%s by signal %d\n", strsignal(pview->stop_sig),
                            pview->stop_sig);
                    break;
            }
        }

        free_job_event(&jevs.data[i]);
    }

    da_free(&jevs);

    return EXIT_SUCCESS;
}

static int run_exit_builtin(char **argv, shell_env *_) {
    if (validate_argc(argv, 0, 1) == -1)
        return EXIT_FAILURE;

    int exit_status = EXIT_SUCCESS;

    if (argv[1]) {
        char *endptr;
        exit_status = strtol(argv[1], &endptr, 10);

        if (strcmp(argv[1], "") == 0 || *endptr != '\0') {
            fprintf(stderr, "exit: invalid argument: %s\n", argv[1]);
            return EXIT_FAILURE;
        }
    }

    if (sh_env.subshell) {
        _exit(exit_status);
    } else {
        printf("exit\n");
        exit(exit_status);
    }

    return EXIT_SUCCESS;
}

static int run_cd_builtin(char **argv, shell_env *_) {
    if (validate_argc(argv, 1, 1) == -1)
        return EXIT_FAILURE;

    if (chdir(argv[1]) == -1) {
        fprintf(stderr, "%s: chdir: %s\n", argv[0], strerror(errno));
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int run_set_builtin(char **argv, shell_env *sh_env) {
    if (validate_argc(argv, 2, 2) == -1)
        return EXIT_FAILURE;

    if (add_var(&sh_env->vars, argv[1], argv[2], false) == -1) {
        fprintf(stderr, "%s: failed to add variable\n", argv[0]);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int run_unset_builtin(char **argv, shell_env *sh_env) {
    if (validate_argc(argv, 1, 1) == -1)
        return EXIT_FAILURE;

    if (delete_var(&sh_env->vars, argv[1])) {
        fprintf(stderr, "%s: failed to delete variable\n", argv[0]);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int run_export_builtin(char **argv, shell_env *sh_env) {
    if (validate_argc(argv, 2, 2) == -1)
        return EXIT_FAILURE;

    if (add_var(&sh_env->vars, argv[1], argv[2], true) == -1) {
        fprintf(stderr, "%s: failed to add variable\n", argv[0]);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int run_vars_builtin(char **argv, shell_env *sh_env) {
    if (validate_argc(argv, 0, 0) == -1)
        return EXIT_FAILURE;

    for (size_t i = 0; i < sh_env->vars.pairs.size; ++i) {
        mpair *pair = &sh_env->vars.pairs.data[i];
        sh_var *var = pair->value;

        if (var->export)
            printf("(export) ");

        printf("%s=%s\n", (char *)pair->key, var->value);
    }

    return EXIT_SUCCESS;
}

static sh_builtin builtins[BUILTIN_COUNT] = {
    { .name = "exit", .func = run_exit_builtin },
    { .name = "cd", .func = run_cd_builtin },
    { .name = "set", .func = run_set_builtin },
    { .name = "unset", .func = run_unset_builtin },
    { .name = "export", .func = run_export_builtin },
    { .name = "vars", .func = run_vars_builtin },
    { .name = "jobs", .func = run_jobs_builtin },
    { .name = "kill", .func = run_kill_builtin },
    { .name = "fg", .func = run_fg_builtin },
    { .name = "bg", .func = run_bg_builtin },
    { .name = "echo", .func = run_echo_builtin }
};

sh_builtin *get_builtin(const char *arg) {
    const char *name = arg;

    for (size_t i = 0; i < BUILTIN_COUNT; ++i)
        if (strcmp(builtins[i].name, name) == 0)
            return &builtins[i];

    return NULL;
}

bool try_run_builtin(char **argv, int *status) {
    if (!argv || !argv[0]) {
        LOG_ERR("receieved bad argv structure");
        fatal("internal error check logs\n");
    }

    sh_builtin *builtin = get_builtin(argv[0]);

    if (builtin) {
        int builtin_status = builtin->func(argv, &sh_env);

        if (status)
            *status = builtin_status;

        return true;
    }

    return false;
}
