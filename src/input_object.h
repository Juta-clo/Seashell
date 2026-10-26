#ifndef INPUT_OBJECT_H
#define INPUT_OBJECT_H

#include "utils.h"

typedef struct {
    int start, end;
} input_word;

static inline int next_non_ws(const char *line, int i) {
    ASSERT(line);
    ASSERT_GE_INT(i, 0);

    ++i;

    for (; i < (int)strlen(line); ++i)
        if (line[i] != ' ')
            return i;

    return -1;
}

static inline int next_ws(const char *line, int i) {
    ASSERT(line);
    ASSERT_GE_INT(i, 0);

    ++i;

    for (; i < (int)strlen(line); ++i)
        if (line[i] == ' ')
            return i;

    return -1;
}

static inline int prev_non_ws(const char *line, int i) {
    ASSERT(line);
    ASSERT_GE_INT(i, 0);

    --i;

    for (; i >= 0; --i)
        if (line[i] != ' ')
            return i;

    return -1;
}

static inline int prev_ws(const char *line, int i) {
    ASSERT(line);
    ASSERT_GE_INT(i, 0);

    --i;

    for (; i >= 0; --i)
        if (line[i] == ' ')
            return i;

    return -1;
}

static inline int next_word(const char *line, int start, input_word *word) {
    ASSERT(line && word);
    ASSERT(start >= 0);
    ASSERT(start < (int)strlen(line) + 1);

    if (start == (int)strlen(line))
        return -1;

    int ws = start;

    if (line[ws] != ' ') {
        ws = next_ws(line, ws);
        if (ws == -1)
            return -1;
    }

    word->start = next_non_ws(line, ws);
    if (word->start == -1)
        return -1;

    word->end = next_ws(line, word->start);
    if (word->end == -1) {
        word->end = strlen(line) - 1;
    } else
        --word->end;

    ASSERT_GE_INT(word->start, 0);
    ASSERT_GE_INT(word->end, 0);
    ASSERT_G_INT((int)strlen(line), word->start);
    ASSERT_G_INT((int)strlen(line), word->end);

    return 0;
}

static inline int prev_word(const char *line, int start, input_word *word) {
    ASSERT(line && word);
    ASSERT(start >= 0);
    ASSERT(start < (int)strlen(line) + 1);

    if (start == (int)strlen(line))
        return -1;

    int ws = start;

    if (line[ws] != ' ') {
        ws = prev_ws(line, ws);
        if (ws == -1)
            return -1;
    }

    word->end = prev_non_ws(line, ws);
    if (word->end == -1)
        return -1;

    word->start = prev_ws(line, word->end);
    if (word->start == -1) {
        word->start = 0;
    } else
        ++word->start;

    ASSERT_GE_INT(word->start, 0);
    ASSERT_GE_INT(word->end, 0);
    ASSERT_G_INT((int)strlen(line), word->start);
    ASSERT_G_INT((int)strlen(line), word->end);

    return 0;
}

static inline int cur_word(const char *line, int start, input_word *word) {
    ASSERT(line && word);
    ASSERT(start >= 0);
    ASSERT(start < (int)strlen(line) + 1);

    if (start == (int)strlen(line))
        return -1;

    if (line[start] == ' ')
        return -1;

    word->start = prev_ws(line, start);

    if (word->start == -1) {
        word->start = 0;
    } else
        ++word->start;

    word->end = next_ws(line, start);

    if (word->end == -1) {
        word->end = strlen(line) - 1;
    } else
        --word->end;

    ASSERT_GE_INT(word->start, 0);
    ASSERT_GE_INT(word->end, 0);
    ASSERT_G_INT((int)strlen(line), word->start);
    ASSERT_G_INT((int)strlen(line), word->end);

    return 0;
}

#endif
