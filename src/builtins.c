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

#define use_err(...) \
    do { \
        use_err_imp(argv[0], __VA_ARGS__); \
        return EXIT_FAILURE; \
    } while (false)

__attribute__ ((__format__(printf, 2, 3)))
static void use_err_imp(const char *name, const char *fmt, ...) {
    va_list va;

    fprintf(stderr, "%s: ", name);

    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);

    fprintf(stderr, "\n");
}

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

static int validate_argc(char **argv, size_t min, size_t max) {
    if (!argv || !argv[0])
        fatal("bad builtin argv list");

    size_t arg_n = 0;
    for (char **i = argv + 1; *i != nullptr; ++i)
        ++arg_n;

    if (arg_n < min)
        use_err("not enough arguments");

    if (arg_n > max)
        use_err("too many arguments");

    return arg_n;
}

static int run_fg_builtin(char **argv, shell_env *env) {
    if (env->subsh || !env->job_ctrl)
        use_err("no job control in this shell");

    if (validate_argc(argv, 1, 1) == -1)
        return EXIT_FAILURE;

    if (argv[1][0] != '%')
        use_err("invalid argument: %s", argv[1]);

    int jid;

    if (get_int(&argv[1][1], &jid, GET_INT_NON_NEG) == -1)
        use_err("job not found: %s", argv[1]);

    jc_job *job = job_lookup(jid);

    if (!job)
        use_err("no such job: %s", argv[1]);

    /* Save current tty state then apply saved tty state for the
     * job if one was saved (it was a foreground job) */

    if (tcgetattr(env->tty_fd, &env->tio) == -1)
        use_err("tcgetattr: %m");

    if (tcsetattr(env->tty_fd, TCSANOW, &job->tio) == -1)
        use_err("tcsetattr: %m");

    if (tcsetpgrp(env->tty_fd, job->pgrp.pgid) == -1)
        use_err("tcsetpgrp: %m");

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
    if (env->subsh || !env->job_ctrl)
        use_err("no job control in this shell");

    if (validate_argc(argv, 1, 1) == -1)
        return EXIT_FAILURE;

    if (argv[1][0] != '%')
        use_err("invalid argument: %s", argv[1]);

    int jid;

    if (get_int(&argv[1][1], &jid, GET_INT_NON_NEG) == -1)
        use_err("job not found: %s", argv[1]);

    pid_t pgid = job_lookup_pgrp(jid);

    if (killpg(pgid, SIGCONT) == -1 && errno != ESRCH)
        use_err("killpg: %m");

    job_event jev;
    if (job_set_running(jid, &jev)) {
        print_job_event(&jev, false);
        job_event_free(&jev);
    }

    return EXIT_SUCCESS;
}

/* default sig is TERM */
static int run_kill_builtin(char **argv, shell_env *env) {
    if (env->subsh || !env->job_ctrl)
        use_err("no job control in this shell");

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
        if (sig == -1)
            use_err("unknown signal: %d", sig);
    }

    if (target_arg[0] == '%') {
        if (get_int(&target_arg[1], &jid, GET_INT_NON_NEG) == -1)
            use_err("invalid job: %s", target_arg);

        pid_t pgid = job_lookup_pgrp(jid);
        if (pgid == -1)
            use_err("no such job");

        if (killpg(pgid, sig) == -1 && errno != ESRCH)
            use_err("killpg: %m");
    } else {
        if (get_int(target_arg, &pid, GET_INT_NON_NEG) == -1)
            use_err("illegal pid: %s", target_arg);

        if (kill(pid, sig) == -1 && errno != ESRCH)
            use_err("kill: %m");
    }

    /* Report termination/stop messages before the next prompt */
    nanosleep(&(struct timespec){0, 5000000}, nullptr);
    return EXIT_SUCCESS;
}

static int run_jobs_builtin(char **argv, shell_env *env) {
    if (env->subsh || !env->job_ctrl)
        use_err("no job control in this shell");

    int arg_n = validate_argc(argv, 0, 1);
    if (arg_n == -1)
        return EXIT_FAILURE;

    enum {
        SHOW_PIDS,
    };

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
    int arg_n = validate_argc(argv, 0, 1);
    if (arg_n == -1)
        return EXIT_FAILURE;

    int status = EXIT_SUCCESS;

    if (arg_n == 1) {
        if (get_int(argv[1], &status, GET_INT_NON_NEG) == -1)
            use_err("expected exit status, found '%s'", argv[1]);
    }

    if (env.subsh)
        _exit(status);

    printf("exit\n");
    exit(status);

    ASSERT(false && "unexpected control flow");
    return EXIT_FAILURE;
}

static int run_cd_builtin(char **argv, shell_env *env) {
    if (validate_argc(argv, 0, 1) == -1)
        return EXIT_FAILURE;

    char *path;

    if (!argv[1]) {
        sh_var *var = lookup_var(&env->vars, "HOME");
        path = (var) ? var->value : ".";
    } else {
        path = argv[1];
    }

    if (chdir(path) == -1)
        use_err("%m");

    char cwd_buf[PATH_MAX];

    if (getcwd(cwd_buf, PATH_MAX))
        add_var(&env->vars, "PWD", cwd_buf, false);

    return EXIT_SUCCESS;
}

static int run_export_builtin(char **argv, shell_env *env) {
    if (validate_argc(argv, 2, 2) == -1)
        return EXIT_FAILURE;

    if (add_var(&env->vars, argv[1], argv[2], true) == -1)
        use_err("failed to add variable");

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
    if (!config)
        use_err("failed to open config file");

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

    if (add_var(&env->vars, argv[1], argv[2], false) == -1)
        use_err("failed to add variable");

    return EXIT_SUCCESS;
}

static int run_unset_builtin(char **argv, shell_env *env) {
    int arg_n = validate_argc(argv, 1, 2);
    if (arg_n == -1)
        return EXIT_FAILURE;

    enum {
        OPT_FUNC,
    };

    linc_args opts[] = {
        [OPT_FUNC] = {
            .long_name = "function",
            .short_name = 'f',
            .has_arg = LINC_ARGS_ARG_OPT,
            .arg_type = LINC_ARGS_ARG_NONE
        },
        (linc_args){0}
    };

    if (linc_args_parse(arg_n + 1, argv, opts, 0) == -1)
        return EXIT_FAILURE;

    if (!argv[optind])
        use_err("missing target");

    if (opts[OPT_FUNC].found) {
        void *func;

        if (mp_str_lookup(&env->funcs, argv[optind], &func) == 0) {
            ast_release(((ps_cmd *)func)->ast);

            if (mp_str_delete(&env->funcs, argv[optind], false) == -1)
                return EXIT_FAILURE;
        }
    } else {
        if (delete_var(&env->vars, argv[optind]) == -1)
            use_err("failed to delete variable");
    }

    return EXIT_SUCCESS;
}

static int run_alias_builtin(char **argv, shell_env *_) {
    if (validate_argc(argv, 2, 2) == -1)
        return EXIT_FAILURE;

    char *key = strdup(argv[1]);
    char *value = strdup(argv[2]);

    if (!key || !value)
        use_err("strdup: %m");

    mp_str_add(&env.aliases, key, value);
    free(key);

    return EXIT_SUCCESS;
}

static int run_unalias_builtin(char **argv, shell_env *_) {
    if (validate_argc(argv, 1, 1) == -1)
        return EXIT_FAILURE;

    char *key = strdup(argv[1]);

    if (!key)
        use_err("strdup: %m");

    int status = mp_str_delete(&env.aliases, key, true);

    free(key);

    return status;
}

int run_lua_builtin(char **argv, shell_env *env) {
    if (env->subsh)
        use_err("no lua in this shell");

    if (validate_argc(argv, 0, 0) == -1)
        return EXIT_FAILURE;

    env->lua = !env->lua;

    return EXIT_SUCCESS;
}

int run_break_builtin(char **argv, shell_env *env) {
    if (validate_argc(argv, 0, 0) == -1)
        return EXIT_FAILURE;

    if (env->loop_level == 0)
        use_err("can only break from inside a loop");

    env->breaking = true;

    return EXIT_SUCCESS;
}

int run_continue_builtin(char **argv, shell_env *env) {
    if (validate_argc(argv, 0, 0) == -1)
        return EXIT_FAILURE;

    if (env->loop_level == 0)
        use_err("can only continue from inside a loop");

    env->continuing = true;

    return EXIT_SUCCESS;
}

int run_return_builtin(char **argv, shell_env *env) {
    int arg_n = validate_argc(argv, 0, 1);
    if (arg_n == -1)
        return EXIT_FAILURE;

    if (env->func_level == 0)
        use_err("can only return from a function or script");

    env->returning = true;

    int status = EXIT_SUCCESS;

    if (arg_n == 1) {
        if (get_int(argv[1], &status, GET_INT_NON_NEG) == -1)
            use_err("expected exit status, found '%s'", argv[1]);

        status %= 256;
    }

    return status;
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
    { .name = "lua", .func = run_lua_builtin },
    { .name = "break", .func = run_break_builtin },
    { .name = "continue", .func = run_continue_builtin },
    { .name = "return", .func = run_return_builtin }
};

sh_builtin *get_builtin(const char *arg) {
    for (size_t i = 0; i < BUILTIN_COUNT; ++i)
        if (STREQ(builtins[i].name, arg))
            return &builtins[i];

    return nullptr;
}
