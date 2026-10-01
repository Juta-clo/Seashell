BIN := seashell
BLD_DIR := build/seashell
COMPCOM := compile_commands.json

SRC_DIR := src
INC_DIR := inc
DEP_DIR := deps
LT_DIR  := $(DEP_DIR)/linc_tools
PR_DIR  := $(DEP_DIR)/Printty

CC := clang

CFLAGS := -g -O0 -std=gnu23 -Wall -Wextra -fcolor-diagnostics
CPPFLAGS := -I$(INC_DIR) -I$(LT_DIR) -I$(PR_DIR)/include
CPPFLAGS += -D_GNU_SOURCE
DEPFLAGS := -MMD -MP
CC_FLAGS := $(CFLAGS) $(CPPFLAGS) $(DEPFLAGS)

LDLIBS := -lunibilium -rdynamic
LD_FLAGS := $(LDLIBS)

SRCS := $(wildcard $(SRC_DIR)/*.c)
SRCS += $(wildcard $(PR_DIR)/src/*.c)
OBJS := $(patsubst %.c,$(BLD_DIR)/%.o,$(SRCS))
DEPS := $(patsubst %.c,$(BLD_DIR)/%.d,$(SRCS))

.PHONY: all
all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(LD_FLAGS) $^ -o $@

$(BLD_DIR)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CC_FLAGS) -c $< -o $@

-include $(DEPS)
