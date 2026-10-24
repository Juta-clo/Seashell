#ifndef SPLITTY_H
#define SPLITTY_H

#include <stddef.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>

#define SP_MOVED 1
#define SP_RESIZED 2
#define _SP_CHILD_MAX 100

typedef struct sp_node sp_node;
typedef struct sp_rect sp_rect;
typedef int (sp_init_fn)(sp_node *node);
typedef void (sp_resize_fn)(sp_node *node, uint32_t flags);
typedef void (sp_free_fn)(sp_node *node);

typedef enum {
    SP_SPLIT_LEFT,
    SP_SPLIT_RIGHT,
    SP_SPLIT_GAP,
    SP_SPLIT_CHILD_MAX,
} sp_split_child;

typedef enum {
    SP_QUAD_TL,
    SP_QUAD_TR,
    SP_QUAD_BL,
    SP_QUAD_BR,

    SP_QUAD_GL,
    SP_QUAD_GR,
    SP_QUAD_GT,
    SP_QUAD_GB,
    SP_QUAD_GJ,
    SP_QUAD_CHILD_MAX,
} sp_quad_child;

typedef enum {
    SP_SPLIT,
    SP_QUAD,
    SP_LEAF,
} sp_node_type;

typedef enum {
    SP_HSPLIT,
    SP_VSPLIT,
} sp_axis;

typedef struct {
    float ratio;
    sp_axis axis;
    int split_gap;
    sp_node *left;
    sp_node *right;
    sp_node *gap;
} sp_split_config;

typedef struct {
    float ratio_y;
    float ratio_x;
    int quad_gap;
    sp_node *tl, *tr, *bl, *br;
    sp_node *gl, *gr, *gt, *gb, *gj;
} sp_quad_config;

struct sp_vec {
    int y;
    int x;
};

struct sp_rect {
    int y;
    int x;
    int rows;
    int cols;
};

struct sp_node {
    sp_rect rect;
    sp_node_type type;

    union {
        /* container */
        struct {
            size_t child_n;
            sp_node *children[_SP_CHILD_MAX];

            union {
                /* split */
                struct {
                    float ratio;
                    sp_axis axis;
                    int split_gap;
                };

                /* quad */
                struct {
                    float ratio_x;
                    float ratio_y;
                    int quad_gap;
                };
            };
        };

        /* leaf */
        struct {
            bool is_data_init;
            sp_init_fn *init_data;
            sp_resize_fn *resize;
            sp_free_fn *free_data;
            void *user_data;
        };
    };
};

static inline void _sp_calc_quad(sp_node *node, sp_rect rects[_SP_CHILD_MAX]) {
    int gap = node->quad_gap;

    if (gap > node->rect.rows)
        gap = node->rect.rows;

    if (gap > node->rect.cols)
        gap = node->rect.cols;

    int avail_rows = node->rect.rows - gap;
    int avail_cols = node->rect.cols - gap;

    int top_side_rows = avail_rows * node->ratio_y;
    int top_side_y = node->rect.y;

    int bottom_side_rows = avail_rows - top_side_rows;
    int bottom_side_y = top_side_y + top_side_rows + gap;

    int left_side_cols = avail_cols * node->ratio_x;
    int left_side_x = node->rect.x;

    int right_side_cols = avail_cols - left_side_cols;
    int right_side_x = left_side_x + left_side_cols + gap;

    rects[SP_QUAD_TL].rows = top_side_rows;
    rects[SP_QUAD_TL].cols = left_side_cols;
    rects[SP_QUAD_TL].y = top_side_y;
    rects[SP_QUAD_TL].x = left_side_x;

    rects[SP_QUAD_TR].rows = top_side_rows;
    rects[SP_QUAD_TR].cols = right_side_cols;
    rects[SP_QUAD_TR].y = top_side_y;
    rects[SP_QUAD_TR].x = right_side_x;

    rects[SP_QUAD_BL].rows = bottom_side_rows;
    rects[SP_QUAD_BL].cols = left_side_cols;
    rects[SP_QUAD_BL].y = bottom_side_y;
    rects[SP_QUAD_BL].x = left_side_x;

    rects[SP_QUAD_BR].rows = bottom_side_rows;
    rects[SP_QUAD_BR].cols = right_side_cols;
    rects[SP_QUAD_BR].y = bottom_side_y;
    rects[SP_QUAD_BR].x = right_side_x;

    int y_gap_y = top_side_y + top_side_rows;
    int x_gap_x = left_side_x + left_side_cols;

    rects[SP_QUAD_GL].rows = gap;
    rects[SP_QUAD_GL].cols = left_side_cols;
    rects[SP_QUAD_GL].y = y_gap_y;
    rects[SP_QUAD_GL].x = left_side_x;

    rects[SP_QUAD_GR].rows = gap;
    rects[SP_QUAD_GR].cols = right_side_cols;
    rects[SP_QUAD_GR].y = y_gap_y;
    rects[SP_QUAD_GR].x = right_side_x;

    rects[SP_QUAD_GT].rows = top_side_rows;
    rects[SP_QUAD_GT].cols = gap;
    rects[SP_QUAD_GT].y = top_side_y;
    rects[SP_QUAD_GT].x = x_gap_x;

    rects[SP_QUAD_GB].rows = bottom_side_rows;
    rects[SP_QUAD_GB].cols = gap;
    rects[SP_QUAD_GB].y = bottom_side_y;
    rects[SP_QUAD_GB].x = x_gap_x;

    rects[SP_QUAD_GJ].rows = gap;
    rects[SP_QUAD_GJ].cols = gap;
    rects[SP_QUAD_GJ].y = y_gap_y;
    rects[SP_QUAD_GJ].x = x_gap_x;
}

static inline void _sp_calc_split(sp_node *node, sp_rect rects[_SP_CHILD_MAX]) {
    int gap = node->split_gap;

    rects[SP_SPLIT_LEFT] = node->rect;
    rects[SP_SPLIT_RIGHT] = node->rect;
    rects[SP_SPLIT_GAP] = node->rect;

    if (node->axis == SP_HSPLIT) {
        if (node->split_gap > node->rect.rows)
            gap = node->rect.rows;

        int avail = node->rect.rows - gap;

        rects[SP_SPLIT_LEFT].rows = avail * node->ratio;

        rects[SP_SPLIT_GAP].rows = gap;
        rects[SP_SPLIT_GAP].y += rects[SP_SPLIT_LEFT].rows;

        rects[SP_SPLIT_RIGHT].rows = avail - rects[SP_SPLIT_LEFT].rows;
        rects[SP_SPLIT_RIGHT].y += rects[SP_SPLIT_LEFT].rows + gap;
    }

    if (node->axis == SP_VSPLIT) {
        if (node->split_gap > node->rect.cols)
            gap = node->rect.cols;

        int avail = node->rect.cols - node->split_gap;

        rects[SP_SPLIT_LEFT].cols = avail * node->ratio;

        rects[SP_SPLIT_GAP].cols = gap;
        rects[SP_SPLIT_GAP].x += rects[SP_SPLIT_LEFT].cols;

        rects[SP_SPLIT_RIGHT].cols = avail - rects[SP_SPLIT_LEFT].cols;
        rects[SP_SPLIT_RIGHT].x += rects[SP_SPLIT_LEFT].cols + node->split_gap;
    }
}

static inline sp_node *sp_new_quad(sp_quad_config cfg) {
    assert(cfg.ratio_x > 0 && cfg.ratio_x <= 1.0F);
    assert(cfg.ratio_y > 0 && cfg.ratio_y <= 1.0F);
    assert(cfg.quad_gap >= 0);
    assert(cfg.tl && cfg.tr && cfg.bl && cfg.br);
    assert(cfg.gl && cfg.gr && cfg.gt && cfg.gb && cfg.gj);

    sp_node *quad = malloc(sizeof(sp_node));
    if (!quad)
        exit(EXIT_FAILURE);

    *quad = (sp_node){
        .type = SP_QUAD,
        .ratio_x = cfg.ratio_x,
        .ratio_y = cfg.ratio_y,
        .quad_gap = cfg.quad_gap,
        .child_n = SP_QUAD_CHILD_MAX,
    };

    quad->children[SP_QUAD_TL] = cfg.tl;
    quad->children[SP_QUAD_TR] = cfg.tr;
    quad->children[SP_QUAD_BL] = cfg.bl;
    quad->children[SP_QUAD_BR] = cfg.br;
    quad->children[SP_QUAD_GL] = cfg.gl;
    quad->children[SP_QUAD_GR] = cfg.gr;
    quad->children[SP_QUAD_GT] = cfg.gt;
    quad->children[SP_QUAD_GB] = cfg.gb;
    quad->children[SP_QUAD_GJ] = cfg.gj;

    return quad;
}

static inline sp_node *sp_new_split(sp_split_config cfg) {
    assert(cfg.ratio > 0 && cfg.ratio <= 1.0F);
    assert(cfg.left && cfg.right);
    assert(cfg.split_gap >= 0);

    sp_node *split = malloc(sizeof(sp_node));
    if (!split)
        exit(EXIT_FAILURE);

    *split = (sp_node){
        .type = SP_SPLIT,
        .ratio = cfg.ratio,
        .axis = cfg.axis,
        .split_gap = cfg.split_gap,
        .child_n = SP_SPLIT_CHILD_MAX,
    };

    split->children[SP_SPLIT_LEFT] = cfg.left;
    split->children[SP_SPLIT_RIGHT] = cfg.right;
    split->children[SP_SPLIT_GAP] = cfg.gap;

    return split;
}

static inline sp_node *sp_new_leaf(sp_init_fn *fn1, sp_resize_fn *fn2,
        sp_free_fn *fn3)
{
    assert(fn1 && fn2 && fn3);

    sp_node *leaf = malloc(sizeof(sp_node));
    if (!leaf)
        exit(EXIT_FAILURE);

    *leaf = (sp_node){
        .type = SP_LEAF,
        .is_data_init = false,
        .init_data = fn1,
        .resize = fn2,
        .free_data = fn3,
    };

    return leaf;
}

static inline void sp_refresh(sp_node *node, sp_rect rect) {
    assert(node);

    uint32_t change_flags = 0;

    if (rect.y != node->rect.y || rect.x != node->rect.x)
        change_flags |= SP_MOVED;

    if (rect.rows != node->rect.rows || rect.cols != node->rect.cols)
        change_flags |= SP_RESIZED;

    node->rect = rect;

    if (node->type == SP_LEAF) {
        if (!node->is_data_init) {
            if (node->init_data(node) == 0)
                node->is_data_init = true;

        } else if (change_flags != 0)
            node->resize(node, change_flags);

    } else {
        sp_rect rects[_SP_CHILD_MAX];

        switch (node->type) {
        case SP_SPLIT: _sp_calc_split(node, rects); break;
        case SP_QUAD:  _sp_calc_quad(node, rects);  break;
        default: exit(EXIT_FAILURE);
        }

        for (size_t i = 0; i < node->child_n; ++i)
            sp_refresh(node->children[i], rects[i]);
    }
}

static inline void sp_free(sp_node *node) {
    if (node->type == SP_LEAF) {
        node->free_data(node);

    } else {
        for (size_t i = 0; i < node->child_n; ++i)
            sp_free(node->children[i]);
    }

    free(node);
}

#undef _SP_CHILD_MAX
#endif
