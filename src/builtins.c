#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "config.h"
#include "shell.h"
#include "variable.h"
#include "state.h"
#include "jobs.h"
#include "builtins.h"
#include "utils.h"

#include <linc/args.h>

typedef enum {
    SHOW_PIDS,
} jobs_opt_names;

static int arg_to_sig(char *sig_arg) {
    /* - just to be consistent */
    if (STREQ("-HUP", sig_arg))   return SIGHUP;
    if (STREQ("-INT", sig_arg))   return SIGINT;
    if (STREQ("-QUIT", sig_arg))  return SIGQUIT;
    if (STREQ("-KILL", sig_arg))  return SIGKILL;
    if (STREQ("-USR1", sig_arg))  return SIGUSR1;
    if (STREQ("-SEGV", sig_arg))  return SIGSEGV;
    if (STREQ("-USR2", sig_arg))  return SIGUSR2;
    if (STREQ("-PIPE", sig_arg))  return SIGPIPE;
    if (STREQ("-ALRM", sig_arg))  return SIGALRM;
    if (STREQ("-TERM", sig_arg))  return SIGTERM;
    if (STREQ("-CHLD", sig_arg))  return SIGCHLD;
    if (STREQ("-CONT", sig_arg))  return SIGCONT;
    if (STREQ("-STOP", sig_arg))  return SIGSTOP;
    if (STREQ("-TSTP", sig_arg))  return SIGTSTP;
    if (STREQ("-TTIN", sig_arg))  return SIGTTIN;
    if (STREQ("-TTOU", sig_arg))  return SIGTTOU;
    if (STREQ("-WINCH", sig_arg)) return SIGWINCH;

    return -1;
}

static int validate_argc(char **argv, size_t min_argc, size_t max_argc) {
    if (!argv || !argv[0]) {
        LOG_ERR("validate_args: bad argv array");
        fatal("internal error; check logs");
    }

    size_t arg_n = 0;
    for (char **i = argv + 1; *i != nullptr; ++i)
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

static int run_fg_builtin(char **argv, shell_env *env) {
    if (env->subsh) {
        fprintf(stderr, "%s: no job control in this shell\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (validate_argc(argv, 1, 1) == -1)
        return EXIT_FAILURE;

    if (argv[1][0] != '%') {
        fprintf(stderr, "%s: invalid argument: %s\n", argv[0], argv[1]);
        return EXIT_FAILURE;
    }

    int jid;

    if (get_int(&argv[1][1], &jid) == -1) {
        fprintf(stderr, "%s: job not found: %s\n", argv[0], argv[1]);
        return EXIT_FAILURE;
    }

    jc_job *job = job_lookup(jid);

    if (!job) {
        fprintf(stderr, "%s: no such job: %s\n", argv[0], argv[1]);
        return EXIT_FAILURE;
    }

    /* Save current tty state then apply saved tty state for the
     * job if one was saved (it was a foreground job) */

    if (tcgetattr(env->tty_fd, &env->tio) == -1) {
        fprintf(stderr, "%s: tcgetattr: %m", argv[0]);
        return EXIT_FAILURE;
    }

    if (tcsetattr(env->tty_fd, TCSANOW, &job->tio) == -1) {
        fprintf(stderr, "%s: tcsetattr: %m", argv[0]);
        return EXIT_FAILURE;
    }

    if (tcsetpgrp(env->tty_fd, job->pgrp.pgid) == -1) {
        fprintf(stderr, "%s: tcsetpgrp: %m\n", argv[0]);
        return EXIT_FAILURE;
    }

    job_event jev;
    if (job_set_running(jid, &jev)) {
        print_job_event(&jev, false);
        job_event_free(&jev);
    }

    if (killpg(job->pgrp.pgid, SIGCONT) == -1 && errno != ESRCH) {
        tcsetpgrp(env->tty_fd, getpgid(0));
        return EXIT_FAILURE;
    }

    env->fg_jid = jid;

    return EXIT_SUCCESS;
}

static int run_bg_builtin(char **argv, shell_env *env) {
    if (env->subsh) {
        fprintf(stderr, "%s: no job control in this shell\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (validate_argc(argv, 1, 1) == -1)
        return EXIT_FAILURE;

    if (argv[1][0] != '%') {
        fprintf(stderr, "%s: invalid argument: %s\n", argv[0], argv[1]);
        return EXIT_FAILURE;
    }

    int jid;

    if (get_int(&argv[1][1], &jid) == -1) {
        fprintf(stderr, "%s: job not found: %s\n", argv[0], argv[1]);
        return EXIT_FAILURE;
    }

    pid_t pgid = job_lookup_pgrp(jid);

    if (killpg(pgid, SIGCONT) == -1 && errno != ESRCH) {
        fprintf(stderr, "%s: killpg: %m\n", argv[0]);
        return EXIT_FAILURE;
    }

    job_event jev;
    if (job_set_running(jid, &jev)) {
        print_job_event(&jev, false);
        job_event_free(&jev);
    }

    return EXIT_SUCCESS;
}

/* default sig is TERM */
static int run_kill_builtin(char **argv, shell_env *env) {
    if (env->subsh) {
        fprintf(stderr, "%s: no job control in this shell\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (validate_argc(argv, 1, 2) == -1)
        return EXIT_FAILURE;

    int jid;
    pid_t pid;

    char *signal_arg;
    char *target_arg;

    if (!argv[2]) {
        signal_arg = nullptr;
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
        if (get_int(&target_arg[1], &jid) == -1) {
            fprintf(stderr, "%s: invalid job: %s\n", argv[0], target_arg);
            return EXIT_FAILURE;
        }

        pid_t pgid = job_lookup_pgrp(jid);
        if (pgid == -1) {
            fprintf(stderr, "%s: no such job\n", argv[0]);
            return EXIT_FAILURE;
        }

        if (killpg(pgid, sig) == -1 && errno != ESRCH) {
            fprintf(stderr, "%s: killpg failed: %m\n", argv[0]);
            return EXIT_FAILURE;
        }
    } else {
        if (get_int(target_arg, &pid) == -1) {
            fprintf(stderr, "%s: illegal pid: %s\n", argv[0], target_arg);
            return EXIT_FAILURE;
        }

        if (kill(pid, sig) == -1 && errno != ESRCH) {
            fprintf(stderr, "%s: kill failed: %m\n", argv[0]);
            return EXIT_FAILURE;
        }
    }

    /* Report termination/stop messages before the next prompt */
    nanosleep(&(struct timespec){0, 5000000}, nullptr);
    return EXIT_SUCCESS;
}

static int run_jobs_builtin(char **argv, shell_env *env) {
    if (env->subsh) {
        fprintf(stderr, "%s: no job control in this shell\n", argv[0]);
        return EXIT_FAILURE;
    }

    int arg_n = validate_argc(argv, 0, 1);
    if (arg_n == -1)
        return EXIT_FAILURE;

    linc_args jobs_opts[] = {
        [SHOW_PIDS] = {
            .long_name = "long",
            .short_name = 'l',
            .has_arg = LINC_ARGS_ARG_NONE,
        },
        (linc_args){0}
    };

    if (linc_args_parse(arg_n + 1, argv, jobs_opts, LINC_ARGS_NO_NON_OPTS) == -1)
        return EXIT_FAILURE;

    da_jevent jevs = job_get_snapshot();
    for (size_t i = 0; i < jevs.size; ++i) {
        print_job_event(&jevs.data[i], jobs_opts[SHOW_PIDS].found);
        job_event_free(&jevs.data[i]);
    }

    linc_array_free(&jevs);
    return EXIT_SUCCESS;
}

static int run_exit_builtin(char **argv, shell_env *_) {
    if (validate_argc(argv, 0, 1) == -1)
        return EXIT_FAILURE;

    int exit_status = EXIT_SUCCESS;

    if (argv[1]) {
        char *endptr;
        exit_status = strtol(argv[1], &endptr, 10);

        if (STREQ(argv[1], "") || *endptr != '\0') {
            fprintf(stderr, "exit: invalid argument: %s\n", argv[1]);
            return EXIT_FAILURE;
        }
    }

    if (env.subsh) {
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

static int run_export_builtin(char **argv, shell_env *env) {
    if (validate_argc(argv, 2, 2) == -1)
        return EXIT_FAILURE;

    if (add_var(&env->vars, argv[1], argv[2], true) == -1) {
        fprintf(stderr, "%s: failed to add variable\n", argv[0]);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int run_vars_builtin(char **argv, shell_env *env) {
    if (validate_argc(argv, 0, 0) == -1)
        return EXIT_FAILURE;

    for (size_t i = 0; i < env->vars.pairs.size; ++i) {
        mpair *pair = &env->vars.pairs.data[i];
        sh_var *var = pair->value;

        if (var->export)
            printf("(export) ");

        printf("%s=%s\n", (char *)pair->key, var->value);
    }

    return EXIT_SUCCESS;
}

static int run_reload_builtin(char **argv, shell_env *_) {
    if (validate_argc(argv, 0, 0) == -1)
        return EXIT_FAILURE;

    FILE *config = config_file_open();
    if (!config) {
        printf("%s: failed to open config file\n", argv[0]);
        return EXIT_FAILURE;
    }

    int jid = env.fg_jid;
    parse_config();
    env.fg_jid = jid;

    if (fclose(config) == EOF)
        fatal("fclose");

    return EXIT_SUCCESS;
}

static int run_set_builtin(char **argv, shell_env *env) {
    if (validate_argc(argv, 2, 2) == -1)
        return EXIT_FAILURE;

    if (add_var(&env->vars, argv[1], argv[2], false) == -1) {
        fprintf(stderr, "%s: failed to add variable\n", argv[0]);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int run_unset_builtin(char **argv, shell_env *env) {
    if (validate_argc(argv, 1, 1) == -1)
        return EXIT_FAILURE;

    if (delete_var(&env->vars, argv[1])) {
        fprintf(stderr, "%s: failed to delete variable\n", argv[0]);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int run_alias_builtin(char **argv, shell_env *_) {
    if (validate_argc(argv, 2, 2) == -1)
        return EXIT_FAILURE;

    char *key = strdup(argv[1]);
    char *value = strdup(argv[2]);

    if (!key || !value) {
        perror("strdup");
        return EXIT_FAILURE;
    }

    mp_str_add(&env.aliases, key, value);
    free(key);

    return EXIT_SUCCESS;
}

static int run_unalias_builtin(char **argv, shell_env *_) {
    if (validate_argc(argv, 1, 1) == -1)
        return EXIT_FAILURE;

    char *key = strdup(argv[1]);

    if (!key) {
        perror("strdup");
        return EXIT_FAILURE;
    }

    int status = mp_str_delete(&env.aliases, key);

    free(key);

    return status;
}

int run_lua_builtin(char **argv, shell_env *env) {
    if (env->subsh) {
        fprintf(stderr, "%s: no lua in this shell\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (validate_argc(argv, 0, 0) == -1)
        return EXIT_FAILURE;

    env->lua = !env->lua;

    return EXIT_SUCCESS;
}

static sh_builtin builtins[BUILTIN_COUNT] = {
    { .name = "exit", .func = run_exit_builtin },
    { .name = "cd", .func = run_cd_builtin },
    { .name = "export", .func = run_export_builtin },
    { .name = "vars", .func = run_vars_builtin },
    { .name = "jobs", .func = run_jobs_builtin },
    { .name = "kill", .func = run_kill_builtin },
    { .name = "fg", .func = run_fg_builtin },
    { .name = "bg", .func = run_bg_builtin },
    { .name = "reload", .func = run_reload_builtin },
    { .name = "set", .func = run_set_builtin },
    { .name = "unset", .func = run_unset_builtin },
    { .name = "alias", .func = run_alias_builtin },
    { .name = "unalias", .func = run_unalias_builtin },
    { .name = "lua", .func = run_lua_builtin }
};

sh_builtin *get_builtin(const char *arg) {
    const char *name = arg;

    for (size_t i = 0; i < BUILTIN_COUNT; ++i)
        if (STREQ(builtins[i].name, name))
            return &builtins[i];

    return nullptr;
}

bool try_run_builtin(char **argv, int *status) {
    if (!argv || !argv[0]) {
        LOG_ERR("receieved bad argv structure");
        fatal("internal error check logs\n");
    }

    sh_builtin *builtin = get_builtin(argv[0]);

    if (builtin) {
        int builtin_status = builtin->func(argv, &env);

        if (status)
            *status = builtin_status;

        return true;
    }

    return false;
}
