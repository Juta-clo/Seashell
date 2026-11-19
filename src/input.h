#ifndef INPUT_H
#define INPUT_H

#include <sys/signal.h>
#include "lexer.h"
#include "linc/array.h"

typedef void (*input_cb)(void);
extern void (*input_sig_cb)(void);

typedef struct {
    char *data;
    int i;
    int n;
    bool hl;
    bool attr_off;
    int fg;
    int bg;
    bool bold;
    bool under;
} input_cell;

LINC_ARRAY_TYPE(da_cell, input_cell);

typedef struct {
    da_cell cells;
    int prompt_len;
} input_buf;


void input_init(int input_fd);
void input_deinit(void);

input_buf input_get_cells(const char *line, int start_i);
void input_draw_cell(input_cell *cell, input_cell *hl);
void input_free_cells(input_buf *ibuf);
void input_highlight(input_buf *ibuf, char *line, size_t n, int start_i);
void input_highlight_tok(int start, int end, lx_kind kind, input_buf *ibuf,
        int start_i);

void input_opt_completion_shadow(bool set);
void input_set_suspend_mask(sigset_t suspend_mask);
void input_clear_history(void);
void input_reset_line_state(void);
void input_on_new_line(void);
char *input_get_line(const char *prompt);

#endif
