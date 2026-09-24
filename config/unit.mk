BIN := unit_test
BLD_DIR := build/unit
COMPCOM := compile_commands.json

SRC_DIR := src
DEP_DIR := deps
TEST_DIR := test/unit
UNITY_DIR := deps/unity

CC := clang

CFLAGS := -g -O0 -std=gnu23 -Wall -Wextra -fcolor-diagnostics
CPPFLAGS := -Iinc -I$(DEP_DIR)/linc_tools -I$(TEST_DIR) -I$(UNITY_DIR)
CPPFLAGS += -DUNITY_OUTPUT_COLOR -DUNITY_FIXTURE_NO_EXTRAS -D_GNU_SOURCE
DEPFLAGS := -MMD -MP

SRCS := $(filter-out $(SRC_DIR)/main.c,$(wildcard $(SRC_DIR)/*.c))
SRCS += $(wildcard $(TEST_DIR)/*.c)
SRCS += $(wildcard $(UNITY_DIR)/*.c)

OBJS := $(patsubst %.c,$(BLD_DIR)/%.o,$(SRCS))
DEPS := $(patsubst %.c,$(BLD_DIR)/%.d,$(SRCS))

COMP_FLAGS := $(CFLAGS) $(CPPFLAGS) $(DEPFLAGS)

.PHONY: all
all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(COMP_FLAGS) $^ -o $@

$(BLD_DIR)/%.o: %.c Makefile
	mkdir -p $(dir $@)
	$(CC) $(COMP_FLAGS) -c $< -o $@

-include $(DEPS)
