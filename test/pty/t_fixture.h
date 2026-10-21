#ifndef T_FIXTURE_H
#define T_FIXTURE_H

#include <sys/wait.h>

__attribute__ ((__format__(printf, 1, 2)))
void test_push_err(const char *fmt, ...);

void test_setup(void);
void test_teardown(void);

#endif
