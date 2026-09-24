BIN := seashell
BLD_DIR := build/seashell
COMPCOM := compile_commands.json

SRC_DIR := src
INC_DIR := inc
DEP_DIR := deps

LT_DIR  := $(DEP_DIR)/linc_tools

CC := clang

CFLAGS := -g -O0 -std=gnu23 -Wall -Wextra -fcolor-diagnostics
CPPFLAGS := -I$(INC_DIR) -I$(LT_DIR) -D_GNU_SOURCE
DEPFLAGS := -MMD -MP

SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst %.c,$(BLD_DIR)/%.o,$(SRCS))
DEPS := $(patsubst %.c,$(BLD_DIR)/%.d,$(SRCS))

COMP_FLAGS := $(CFLAGS) $(CPPFLAGS) $(DEPFLAGS)

.PHONY: all
all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(COMP_FLAGS) $^ -o $@

$(BLD_DIR)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(COMP_FLAGS) -c $< -o $@

-include $(DEPS)
