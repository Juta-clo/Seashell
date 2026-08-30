BIN := pty_test
BLD_DIR := build/pty
COMPCOM := compile_commands.json

SRC_DIR := src
DEP_DIR := deps
TEST_DIR := test/pty
UNITY_DIR := deps/unity

CC := clang

CFLAGS := -g -O0 -std=gnu23 -Wall -Wextra -fcolor-diagnostics

CPPFLAGS := -Iinc -I$(DEP_DIR)/linc_tools/inc -I$(TEST_DIR) -I$(UNITY_DIR)
CPPFLAGS += -DUNITY_FIXTURE_NO_EXTRAS -DUNITY_OUTPUT_COLOR
CPPFLAGS += -DUNITY_REPEAT_TEST_NAME

DEPFLAGS := -MMD -MP

LDFLAGS := -lncurses

SRCS := $(filter-out $(SRC_DIR)/main.c,$(wildcard $(SRC_DIR)/*.c))
SRCS += $(wildcard $(DEP_DIR)/linc_tools/src/*.c)
SRCS += $(wildcard $(TEST_DIR)/*.c)
SRCS += $(wildcard $(UNITY_DIR)/*.c)

OBJS := $(patsubst %.c,$(BLD_DIR)/%.o,$(SRCS))
DEPS := $(patsubst %.c,$(BLD_DIR)/%.d,$(SRCS))

COMP_FLAGS := $(CFLAGS) $(CPPFLAGS) $(DEPFLAGS)

.PHONY: all
all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(COMP_FLAGS) $(LDFLAGS) $^ -o $@

$(BLD_DIR)/%.o: %.c Makefile
	mkdir -p $(dir $@)
	$(CC) $(COMP_FLAGS) -c $< -o $@

-include $(DEPS)
