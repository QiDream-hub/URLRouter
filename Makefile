CC = gcc
CFLAGS = -Wall -Wextra -O2 -g -std=c99

# 目录结构
SRC_DIR = src
INCLUDE_DIR = include
BUILD_DIR = build
TEST_DIR = tests

# 头文件搜索路径
CFLAGS += -I$(INCLUDE_DIR)

# 核心库源文件
LIB_SRCS = $(SRC_DIR)/router.c \
           $(SRC_DIR)/route_tree.c \
           $(SRC_DIR)/pattern_compiler.c \
           $(SRC_DIR)/lexer.c \
           $(SRC_DIR)/feature_compiler.c \
           $(SRC_DIR)/extractor_compiler.c \
           $(SRC_DIR)/extractor.c

# 头文件
HDRS = $(wildcard $(INCLUDE_DIR)/*.h)

# 测试源文件
TEST_LEXER_SRC = $(TEST_DIR)/test_lexer.c
TEST_FEATURE_SRC = $(TEST_DIR)/test_feature_compiler.c
TEST_EXTRACTOR_SRC = $(TEST_DIR)/test_extractor_compiler.c
TEST_RUNNER_SRC = $(TEST_DIR)/test_runner.c

# 测试二进制文件
TEST_LEXER_BIN = $(BUILD_DIR)/test_lexer
TEST_FEATURE_BIN = $(BUILD_DIR)/test_feature_compiler
TEST_EXTRACTOR_BIN = $(BUILD_DIR)/test_extractor_compiler
TEST_RUNNER_BIN = $(BUILD_DIR)/test_runner

# 应用源文件
EXAMPLE_SRC = example.c
TEST_SRC = test.c

# 应用二进制文件
EXAMPLE_BIN = $(BUILD_DIR)/example
TEST_APP_BIN = $(BUILD_DIR)/test_app

# 创建目录
$(shell mkdir -p $(BUILD_DIR))

# ==================== 核心库 ====================

# 编译 example
$(EXAMPLE_BIN): $(EXAMPLE_SRC) $(LIB_SRCS) $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(EXAMPLE_SRC) $(LIB_SRCS)

# 编译 test_app
$(TEST_APP_BIN): $(TEST_SRC) $(LIB_SRCS) $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(TEST_SRC) $(LIB_SRCS)

# ==================== 单元测试 ====================

# 词法分析器测试
$(TEST_LEXER_BIN): $(TEST_LEXER_SRC) $(SRC_DIR)/lexer.c $(INCLUDE_DIR)/pattern_compiler.h
	$(CC) $(CFLAGS) -o $@ $(TEST_LEXER_SRC) $(SRC_DIR)/lexer.c

# 特征序列编译器测试
$(TEST_FEATURE_BIN): $(TEST_FEATURE_SRC) $(SRC_DIR)/lexer.c $(SRC_DIR)/feature_compiler.c $(INCLUDE_DIR)/pattern_compiler.h
	$(CC) $(CFLAGS) -o $@ $(TEST_FEATURE_SRC) $(SRC_DIR)/lexer.c $(SRC_DIR)/feature_compiler.c

# 提取序列编译器测试
$(TEST_EXTRACTOR_BIN): $(TEST_EXTRACTOR_SRC) $(SRC_DIR)/lexer.c $(SRC_DIR)/extractor_compiler.c $(INCLUDE_DIR)/pattern_compiler.h
	$(CC) $(CFLAGS) -o $@ $(TEST_EXTRACTOR_SRC) $(SRC_DIR)/lexer.c $(SRC_DIR)/extractor_compiler.c

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

# ==================== 测试目标 ====================

# 所有单元测试
unit-tests: $(TEST_LEXER_BIN) $(TEST_FEATURE_BIN) $(TEST_EXTRACTOR_BIN)
	@echo "=== Running Lexer Tests ==="
	@$(TEST_LEXER_BIN) && echo "" || true
	@echo "=== Running Feature Compiler Tests ==="
	@$(TEST_FEATURE_BIN) && echo "" || true
	@echo "=== Running Extractor Compiler Tests ==="
	@$(TEST_EXTRACTOR_BIN) && echo "" || true
	@echo "=== Unit Tests Complete ==="

# 单独运行各测试
test-lexer: $(TEST_LEXER_BIN)
	$(TEST_LEXER_BIN)

test-feature: $(TEST_FEATURE_BIN)
	$(TEST_FEATURE_BIN)

test-extractor: $(TEST_EXTRACTOR_BIN)
	$(TEST_EXTRACTOR_BIN)

# 运行所有测试（单元测试 + 集成测试）
test: unit-tests run-test-app

# ==================== 清理 ====================

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all apps run run-test-app unit-tests test-lexer test-feature test-extractor test clean
