#include <string.h>
#include <assert.h>

#include "xfuncs.h"
#include "variable.h"
#include "map.h"
#include "utils.h"

static bool verify_key(const void *key) {
    assert(key);

    if (strlen(key) >= VAR_KEY_SIZE)
        return false;

    for (const char *c = key; *c != '\0'; ++c) {
        bool valid_key =
                ((*c >= 'a' && *c <= 'z') ||
                (*c >= 'A' && *c <= 'Z') ||
                (*c >= '0' && *c <= '9') ||
                *c == '-' || *c == '_');

        if (!valid_key)
            return false;
    }

    return true;
}

int add_var(map_t *vars, char *key, char *value, bool export) {
    if (!verify_key(key))
        return -1;

    sh_var *var = lookup_var(vars, key);
    if (var)
        free(var->value);

    var = xmalloc(sizeof(sh_var));
    var->value = xmalloc(strlen(value) + 1);
    strcpy(var->value, value);
    var->export = export;

    if (mp_str_add(vars, key, var) == -1)
        xfatal("mp_str_add");

    return 0;
}

sh_var *lookup_var(map_t *vars, const char *key) {
    void *value;

    if (mp_str_lookup(vars, key, &value) == -1)
        xfatal("mp_str_lookup");

    return value;
}

sh_var *lookup_var_n(map_t *vars, const char *key, size_t key_len) {
    void *value;

    char key_buf[VAR_KEY_SIZE];
    strncpy(key_buf, key, key_len);
    key_buf[key_len] = '\0';

    if (mp_str_lookup(vars, key_buf, &value) == -1)
        xfatal("mp_str_lookup");

    return value;
}

int delete_var(map_t *vars, char *key) {
    if (!verify_key(key))
        return -1;

    sh_var *var = lookup_var(vars, key);
    if (var) {
        free(var->value);
        mp_str_delete(vars, key);
    }

    return 0;
}

void export_vars(map_t *vars) {
    for (size_t i = 0; i < vars->pairs.size; ++i) {
        mpair *pair = &vars->pairs.data[i];
        sh_var *var = pair->value;

        if (var->export)
            if (setenv(pair->key, var->value, 1) == -1)
                err_exit("setenv");
    }
}
