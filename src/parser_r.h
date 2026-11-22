#ifndef PARSER_R_H
#define PARSER_R_H

#include "parser.h"

void parse_r_free(ps_ast *ast);
int parse_r(ps_scan *scan, ps_ast *ast);

#endif
