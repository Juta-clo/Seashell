#ifndef INPUT_H
#define INPUT_H

#include "shell_state.h"

#define LINE_BUF 8192

#define PROMPT_SIMPLE 1
#define PROMPT_CWD 2
#define PROMPT_PS 3

typedef struct {
    char *line;
    int start_row;
    int offset;
    int rows, cols;
    int insert_mode;
    int start;
} input_editor;

typedef struct {
    int start, end;
} input_word;

extern input_editor input;

void input_init(shell_env *env);
void input_deinit(shell_env *env);

void drop_line(void);
char *get_line(void);

void display_job_msg(const char *msg);

static inline int llen(void) {
    return (int)(hda_size(input.line) - 1);
}

static inline int line_len(void) {
    int rv = (int)(hda_size(input.line) - 1) - input.start;
    assert(rv >= 0);
    return rv;
}

static inline int next_non_ws(int i) {
    assert(i + 1 >= input.start);

    ++i;

    for (; i < llen(); ++i)
        if (input.line[i] != ' ')
            return i;

    return -1;
}

static inline int next_ws(int i) {
    assert(i + 1 >= input.start);

    ++i;

    for (; i < llen(); ++i)
        if (input.line[i] == ' ')
            return i;

    return -1;
}

static inline int prev_non_ws(int i) {
    assert(i - 1 < llen());

    --i;

    for (; i >= input.start; --i)
        if (input.line[i] != ' ')
            return i;

    return -1;
}

static inline int prev_ws(int i) {
    assert(i - 1 < llen());

    --i;

    for (; i >= input.start; --i)
        if (input.line[i] == ' ')
            return i;

    return -1;
}

static inline int next_word(int start, input_word *word) {
    assert(start >= 0);

    start += input.start;

    if (start == llen())
        return -1;

    int ws = start;

    if (input.line[ws] != ' ') {
        ws = next_ws(ws);
        if (ws == -1)
            return -1;
    }

    word->start = next_non_ws(ws);
    if (word->start == -1)
        return -1;

    word->end = next_ws(word->start);
    if (word->end == -1) {
        word->end = llen() - 1;
    } else
        --word->end;

    assert(word->start >= input.start);
    assert(word->end >= input.start);

    word->start -= input.start;
    word->end -= input.start;

    return 0;
}

static inline int prev_word(int start, input_word *word) {
    start += input.start;

    int ws = start;

    if (input.line[ws] != ' ') {
        ws = prev_ws(ws);
        if (ws == -1)
            return -1;
    }

    word->end = prev_non_ws(ws);
    if (word->end == -1)
        return -1;

    word->start = prev_ws(word->end);
    if (word->start == -1) {
        word->start = input.start;
    } else
        ++word->start;

    assert(word->start >= input.start);
    assert(word->end >= input.start);

    word->start -= input.start;
    word->end -= input.start;

    return 0;
}

static inline int cur_word(int start, input_word *word) {
    start += input.start;

    if (input.line[start] == ' ')
        return -1;

    word->start = prev_ws(start);

    if (word->start == -1) {
        word->start = input.start;
    } else
        ++word->start;

    word->end = next_ws(start);

    if (word->end == -1) {
        word->end = llen() - 1;
    } else
        --word->end;

    assert(word->start >= input.start);
    assert(word->end >= input.start);

    word->start -= input.start;
    word->end -= input.start;

    return 0;
}

#endif
