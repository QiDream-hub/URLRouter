# ============================================================
# URLRouter
#
# 特征序列 / 提取序列的编译与匹配由 Stride 提供
# （git submodule: third_party/Stride）
#
#   make                 构建 example
#   make apps            构建 example 与 test_app
#   make run             运行示例
#   make run-test-app    运行集成测试
#   make test            运行全部测试
#   make test-segment-count  运行段数匹配测试
#   make clean           清理构建产物
# ============================================================

CC = gcc
AR = ar
CFLAGS = -Wall -Wextra -O2 -g -std=c99

# 目录结构
SRC_DIR = src
INCLUDE_DIR = include
BUILD_DIR = build
TEST_DIR = tests

# Stride 依赖（git submodule；可用 make STRIDE_DIR=../Stride 覆盖）
STRIDE_DIR ?= third_party/Stride
STRIDE_LIB := $(STRIDE_DIR)/build/libstride.a

# 头文件搜索路径
CFLAGS += -I$(INCLUDE_DIR) -I$(STRIDE_DIR)/include

# 核心库源文件
LIB_SRCS = $(SRC_DIR)/router.c \
           $(SRC_DIR)/route_tree.c \
           $(SRC_DIR)/pattern.c \
           $(SRC_DIR)/pattern_compile.c

# 头文件
HDRS = $(wildcard $(INCLUDE_DIR)/*.h)

# 应用源文件
EXAMPLE_SRC = example.c
TEST_SRC = test.c
TEST_SEGMENT_COUNT_SRC = $(TEST_DIR)/test_segment_count.c

# 应用二进制文件
EXAMPLE_BIN = $(BUILD_DIR)/example
TEST_APP_BIN = $(BUILD_DIR)/test_app
TEST_SEGMENT_COUNT_BIN = $(BUILD_DIR)/test_segment_count
TEST_PATTERN_BIN = $(BUILD_DIR)/test_pattern

# 创建目录
$(shell mkdir -p $(BUILD_DIR))

# ==================== Stride 子模块 ====================

# 构建 Stride 静态库（缺失子模块时给出明确提示）
$(STRIDE_LIB):
	@if [ ! -f "$(STRIDE_DIR)/Makefile" ]; then \
		echo "错误: 缺少 Stride 子模块。请先执行:"; \
		echo "  git submodule update --init --recursive"; \
		exit 1; \
	fi
	$(MAKE) -C $(STRIDE_DIR) lib

# ==================== 核心库 ====================

# 编译 example
$(EXAMPLE_BIN): $(EXAMPLE_SRC) $(LIB_SRCS) $(HDRS) $(STRIDE_LIB)
	$(CC) $(CFLAGS) -o $@ $(EXAMPLE_SRC) $(LIB_SRCS) $(STRIDE_LIB)

# 编译 test_app
$(TEST_APP_BIN): $(TEST_SRC) $(LIB_SRCS) $(HDRS) $(STRIDE_LIB)
	$(CC) $(CFLAGS) -o $@ $(TEST_SRC) $(LIB_SRCS) $(STRIDE_LIB)

# 编译段数匹配测试
$(TEST_SEGMENT_COUNT_BIN): $(TEST_SEGMENT_COUNT_SRC) $(LIB_SRCS) $(HDRS) $(STRIDE_LIB)
	$(CC) $(CFLAGS) -o $@ $(TEST_SEGMENT_COUNT_SRC) $(LIB_SRCS) $(STRIDE_LIB)

# 编译段模式（词法 + 编译）测试
$(TEST_PATTERN_BIN): $(TEST_DIR)/test_pattern.c $(LIB_SRCS) $(HDRS) $(STRIDE_LIB)
	$(CC) $(CFLAGS) -o $@ $(TEST_DIR)/test_pattern.c $(LIB_SRCS) $(STRIDE_LIB)

# ==================== 目标 ====================

# 默认构建 example
all: $(EXAMPLE_BIN)

# 构建所有应用
apps: $(EXAMPLE_BIN) $(TEST_APP_BIN)

# 运行示例
run: $(EXAMPLE_BIN)
	$(EXAMPLE_BIN)

# 运行 test_app
run-test-app: $(TEST_APP_BIN)
	$(TEST_APP_BIN)

# 运行段数匹配测试
test-segment-count: $(TEST_SEGMENT_COUNT_BIN)
	$(TEST_SEGMENT_COUNT_BIN)

# 运行段模式测试
test-pattern: $(TEST_PATTERN_BIN)
	$(TEST_PATTERN_BIN)

# 运行所有测试（集成测试 + 段数匹配测试）
test: run-test-app test-segment-count test-pattern
	@echo "=== All Tests Complete ==="

# ==================== 清理 ====================

clean:
	rm -rf $(BUILD_DIR)
	@if [ -f "$(STRIDE_DIR)/Makefile" ]; then $(MAKE) -C $(STRIDE_DIR) clean; fi

.PHONY: all apps run run-test-app test test-segment-count test-pattern clean

compile-commands:
	bear -- $(MAKE) clean all
