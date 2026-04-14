CC = gcc
CFLAGS = -Wall -Wextra -O2 -g -std=c99

SRC_DIR = .
BUILD_DIR = build

# 核心库源文件
LIB_SRCS = router.c route_tree.c pattern_compiler.c extractor.c

# 头文件
HDRS = $(wildcard $(SRC_DIR)/*.h)

# 创建 build 目录
$(shell mkdir -p $(BUILD_DIR))

# 编译 example
$(BUILD_DIR)/example: example.c $(LIB_SRCS) $(HDRS)
	$(CC) $(CFLAGS) -o $@ example.c $(LIB_SRCS)

# 编译 test
$(BUILD_DIR)/test_runner: test.c $(LIB_SRCS) $(HDRS)
	$(CC) $(CFLAGS) -o $@ test.c $(LIB_SRCS)

# 默认构建 example
all: $(BUILD_DIR)/example

# 运行示例
run: $(BUILD_DIR)/example
	$(BUILD_DIR)/example

# 运行测试
test: $(BUILD_DIR)/test_runner
	$(BUILD_DIR)/test_runner

# 清理
clean:
	rm -rf $(BUILD_DIR)

.PHONY: all run test clean
