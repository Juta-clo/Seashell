#ifndef T_FIXTURE_H
#define T_FIXTURE_H

#include <sys/wait.h>

__attribute__ ((__format__(printf, 1, 2)))
void test_push_err(const char *fmt, ...);

#define test_setup(...) \
    test_setup_imp((char *[]){ __VA_ARGS__, nullptr })

void test_setup_imp(char **argv);
void test_teardown(void);

#endif
