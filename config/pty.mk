BIN := pty_test
BLD_DIR := build/pty
COMPCOM := compile_commands.json

SRC_DIR := src
DEP_DIR := deps
TEST_DIR := test/pty
UNITY_DIR := deps/unity
TEST_BIN_DIR := test/pty/test_bins

CC := clang

CFLAGS := -g -O0 -std=gnu23 -Wall -Wextra -fcolor-diagnostics

CPPFLAGS := -Iinc -I$(DEP_DIR)/linc_tools -I$(TEST_DIR) -I$(UNITY_DIR)
CPPFLAGS += -DUNITY_FIXTURE_NO_EXTRAS -DUNITY_OUTPUT_COLOR
CPPFLAGS += -DUNITY_REPEAT_TEST_NAME -D_GNU_SOURCE

DEPFLAGS := -MMD -MP

LDFLAGS := -lncurses -lvterm -lm

SRCS += $(wildcard $(DEP_DIR)/linc_tools/src/*.c)
SRCS += $(wildcard $(TEST_DIR)/*.c)
SRCS += $(wildcard $(UNITY_DIR)/*.c)

TEST_BINS = $(addprefix $(TEST_BIN_DIR)/, \
	change_tio interrupt_me self_kill write_sleep)

OBJS := $(patsubst %.c,$(BLD_DIR)/%.o,$(SRCS))
DEPS := $(patsubst %.c,$(BLD_DIR)/%.d,$(SRCS))

COMP_FLAGS := $(CFLAGS) $(CPPFLAGS) $(DEPFLAGS)

.PHONY: all
all: $(BIN) $(TEST_BINS)

$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@

$(BLD_DIR)/%.o: %.c Makefile
	mkdir -p $(dir $@)
	$(CC) $(COMP_FLAGS) -c $< -o $@

$(TEST_BIN_DIR)/%: $(TEST_BIN_DIR)/%.c
	$(CC) $(COMP_FLAGS) $< -o $@

-include $(DEPS)
