CC = gcc
CFLAGS = -Wall -Wextra -O2 -g -std=c99
TARGET = example
SRC_DIR = .
BUILD_DIR = build

# 源文件
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))

# 头文件
HDRS = $(wildcard $(SRC_DIR)/*.h)

# 创建 build 目录
$(shell mkdir -p $(BUILD_DIR))

all: $(BUILD_DIR)/$(TARGET)

$(BUILD_DIR)/$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# 通用编译规则
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

# 测试目标
test: $(BUILD_DIR)/$(TARGET)
	@echo "=== 运行基础测试 ==="
	$(BUILD_DIR)/$(TARGET)

.PHONY: all clean test
