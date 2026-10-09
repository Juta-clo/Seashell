BIN := unit_test
BLD_DIR := build/unit
COMPCOM := compile_commands.json

SRC_DIR := src
DEP_DIR := deps
TEST_DIR := test/unit
UNITY_DIR := deps/unity
LT_DIR := $(DEP_DIR)/linc_tools
PR_DIR := $(DEP_DIR)/Printty

CC := clang

CFLAGS := -g -O0 -std=gnu23 -Wall -Wextra -fcolor-diagnostics
CPPFLAGS := -Iinc -I$(TEST_DIR) -I$(UNITY_DIR) -I$(LT_DIR) -I$(PR_DIR)/include
CPPFLAGS += -DUNITY_OUTPUT_COLOR -DUNITY_FIXTURE_NO_EXTRAS -D_GNU_SOURCE
DEPFLAGS := -MMD -MP

SRCS := $(filter-out $(SRC_DIR)/main.c,$(wildcard $(SRC_DIR)/*.c))
SRCS := $(filter-out $(SRC_DIR)/llog_sinks.c,$(SRCS))
SRCS += $(wildcard $(TEST_DIR)/*.c)
SRCS += $(wildcard $(UNITY_DIR)/*.c)
SRCS += $(wildcard $(PR_DIR)/src/*.c)

OBJS := $(patsubst %.c,$(BLD_DIR)/%.o,$(SRCS))
DEPS := $(patsubst %.c,$(BLD_DIR)/%.d,$(SRCS))

COMP_FLAGS := $(CFLAGS) $(CPPFLAGS) $(DEPFLAGS)

.PHONY: all
all: $(BIN)

$(BIN): $(OBJS)
	$(CC) -lunibilium $^ -o $@

$(BLD_DIR)/%.o: %.c Makefile
	mkdir -p $(dir $@)
	$(CC) $(COMP_FLAGS) -c $< -o $@

-include $(DEPS)
