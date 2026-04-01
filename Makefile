CC = gcc
CFLAGS = -Wall -Wextra -O2 -g
TARGET = example
SRC_DIR = .
BUILD_DIR = build
OBJS = $(BUILD_DIR)/router.o $(BUILD_DIR)/example.o

# 自动查找源文件
SRCS = $(wildcard $(SRC_DIR)/*.c)
# 生成对应的目标文件列表
OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))

# 创建build目录
$(shell mkdir -p $(BUILD_DIR))

all: $(BUILD_DIR) $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(BUILD_DIR)/$@ $^

# 通用编译规则
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# 添加头文件依赖
$(BUILD_DIR)/router.o: $(SRC_DIR)/router.c $(SRC_DIR)/router.h
$(BUILD_DIR)/example.o: $(SRC_DIR)/example.c $(SRC_DIR)/router.h

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean