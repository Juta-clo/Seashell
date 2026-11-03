#ifndef INPUT_INTERNAL_H
#define INPUT_INTERNAL_H

#include "utils.h"

typedef struct {
    int start, end;
} input_word;

static inline int next_non_ws(const char *line, int i) {
    ASSERT(line);
    ASSERT_(GEQ, i, 0);

    ++i;

    for (; i < (int)strlen(line); ++i)
        if (line[i] != ' ')
            return i;

    return -1;
}

static inline int next_ws(const char *line, int i) {
    ASSERT(line);
    ASSERT_(GEQ, i, 0);

    ++i;

    for (; i < (int)strlen(line); ++i)
        if (line[i] == ' ')
            return i;

    return -1;
}

static inline int prev_non_ws(const char *line, int i) {
    ASSERT(line);
    ASSERT_(GEQ, i, 0);

    --i;

    for (; i >= 0; --i)
        if (line[i] != ' ')
            return i;

    return -1;
}

static inline int prev_ws(const char *line, int i) {
    ASSERT(line);
    ASSERT_(GEQ, i, 0);

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

    ASSERT_(GEQ, word->start, 0);
    ASSERT_(GEQ, word->end, 0);
    ASSERT_(G, (int)strlen(line), word->start);
    ASSERT_(G, (int)strlen(line), word->end);

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

    ASSERT_(GEQ, word->start, 0);
    ASSERT_(GEQ, word->end, 0);
    ASSERT_(G, (int)strlen(line), word->start);
    ASSERT_(G, (int)strlen(line), word->end);

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

    ASSERT_(GEQ, word->start, 0);
    ASSERT_(GEQ, word->end, 0);
    ASSERT_(G, (int)strlen(line), word->start);
    ASSERT_(G, (int)strlen(line), word->end);

    return 0;
}

#define xprintty_cursor_move(pt, row, col) \
    do { if (printty_cursor_move(pt, row, col) == PRINTTY_ERR) \
            fatal("printty_cursor_move"); } while (false)

#define xprintty_write(pt, uc) \
    do { if (printty_write(pt, uc) == PRINTTY_ERR) \
            fatal("printty_write"); } while (false)

#define xprintty_write_name(pt, name) \
    do { if (printty_write_name(pt, name) == PRINTTY_ERR) \
            fatal("printty_write_name"); } while (false)

#define xprintty_get_cursor_pos(pt, row, col) \
    do { if (printty_get_cursor_pos(pt, row, col) == PRINTTY_ERR) \
            fatal("printty_get_cursor_pos"); } while (false)

#define xprintty_get_term_size(pt, rows, cols) \
    do { if (printty_get_term_size(pt, rows, cols) == PRINTTY_ERR) \
            fatal("printty_get_term_size"); } while (false)

#define xprintty_set_color(pt, fg, bg) \
    do { if (printty_set_color(pt, fg, bg) == PRINTTY_ERR) \
            fatal("printty_set_color"); } while (false)

#define xprintty_cstyle(pt, style) \
    do { if (printty_cstyle(pt, style) == PRINTTY_ERR) \
            fatal("printty_cstyle"); } while (false)

#endif
