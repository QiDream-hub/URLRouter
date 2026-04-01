# URLRouter - C语言轻量级URL路由库

一个基于树形结构的高性能URL路由库，专为C语言环境设计。支持关键字匹配、通配符参数捕获，采用树形存储结构实现O(L)时间复杂度的高效路由匹配。

## 特性

- **树形结构存储**：基于Trie树实现，匹配复杂度O(L)，L为URL段数
- **简洁的注册语法**：`/$'keyword'/${}/$'profile'` 风格的路由定义
- **优先级机制**：关键字匹配优先于通配符匹配
- **参数捕获**：通配符`${}`自动捕获URL段，按顺序存入数组
- **HTTP方法隔离**：支持GET、POST、PUT、DELETE等常见HTTP方法
- **零依赖**：仅使用标准C库，无外部依赖
- **内存安全**：提供完整的资源释放接口

## 快速开始

### 基本使用

```c
#include "router.h"
#include <stdio.h>

void user_handler(route_params_t *params, void *cbdata) {
    printf("User ID: %s\n", params->params[0]);
}

int main() {
    // 创建路由器
    router_t *router = router_create();
    
    // 注册路由
    router_register(router, HTTP_GET, "/$'user'/${}/$'profile'", user_handler, NULL);
    
    // 匹配路由
    route_params_t params;
    route_callback_t cb = router_match(router, HTTP_GET, "/user/alice/profile", &params);
    
    if (cb) {
        cb(&params, NULL);  // 输出: User ID: alice
        route_params_free(&params);
    }
    
    router_destroy(router);
    return 0;
}
```

## 路由语法

### 关键字匹配 `$'keyword'`

关键字用于精确匹配URL段：

```
注册: /$'user'/${}/$'profile'
匹配: /user/alice/profile      ✓
不匹配: /usr/alice/profile     ✗
```

### 通配符参数 `${}`

通配符可以匹配任意URL段，并自动捕获该段的值：

```
注册: /$'user'/${}/$'posts'/${}
匹配: /user/bob/posts/123
捕获: params[0] = "bob", params[1] = "123"
```

### 组合使用

可以任意组合关键字和通配符：

```
/$'api'/${}/$'v1'/${}/$'data'     # API路由
/$'static'/${}                     # 静态文件路由
/$'admin'/$'dashboard'            # 纯关键字路由
```

## API参考

### 数据结构

```c
// HTTP方法枚举
typedef enum {
    HTTP_GET,
    HTTP_POST,
    HTTP_PUT,
    HTTP_DELETE,
    HTTP_PATCH,
    HTTP_HEAD,
    HTTP_OPTIONS,
    HTTP_METHOD_COUNT
} http_method_t;

// 参数结构体
typedef struct {
    char **params;      // 捕获的参数值数组
    int param_count;    // 参数数量
} route_params_t;

// 回调函数类型
typedef void (*route_callback_t)(route_params_t *params, void *cbdata);
```

### 函数说明

#### `router_t* router_create(void)`

创建路由器实例。

- **返回值**：路由器指针，失败返回NULL

#### `void router_destroy(router_t *router)`

销毁路由器，释放所有资源。

- **参数**：`router` - 路由器指针

#### `int router_register(router_t *router, http_method_t method, const char *pattern, route_callback_t callback, void *cbdata)`

注册路由规则。

- **参数**：
  - `router` - 路由器指针
  - `method` - HTTP方法
  - `pattern` - 路由模式（如 `/$'user'/${}/$'profile'`）
  - `callback` - 回调函数
  - `cbdata` - 回调数据（可为NULL）
- **返回值**：0成功，-1失败

#### `route_callback_t router_match(router_t *router, http_method_t method, const char *url, route_params_t *out_params)`

匹配URL路径。

- **参数**：
  - `router` - 路由器指针
  - `method` - HTTP方法
  - `url` - 请求URL路径
  - `out_params` - 输出参数结构体
- **返回值**：匹配到的回调函数，未匹配返回NULL

#### `void route_params_free(route_params_t *params)`

释放参数结构体。

- **参数**：`params` - 参数结构体指针

## 完整示例

```c
#include <stdio.h>
#include "router.h"

// 用户资料处理器
void user_profile_handler(route_params_t *params, void *cbdata) {
    printf("=== User Profile ===\n");
    printf("User ID: %s\n", params->params[0]);
    if (cbdata) {
        printf("Extra data: %s\n", (char*)cbdata);
    }
}

// 用户帖子处理器
void user_posts_handler(route_params_t *params, void *cbdata) {
    printf("=== User Posts ===\n");
    printf("User ID: %s\n", params->params[0]);
    printf("Post ID: %s\n", params->params[1]);
}

// 设置页面处理器
void settings_handler(route_params_t *params, void *cbdata) {
    printf("=== Settings ===\n");
    printf("Settings page accessed\n");
}

// API处理器
void api_handler(route_params_t *params, void *cbdata) {
    printf("=== API Handler ===\n");
    printf("Version: %s\n", params->params[0]);
    printf("Resource: %s\n", params->params[1]);
}

int main() {
    // 创建路由器
    router_t *router = router_create();
    if (!router) {
        fprintf(stderr, "Failed to create router\n");
        return 1;
    }
    
    // 注册路由
    router_register(router, HTTP_GET, "/$'user'/${}/$'profile'", 
                    user_profile_handler, (void*)"from cache");
    
    router_register(router, HTTP_GET, "/$'user'/${}/$'posts'/${}", 
                    user_posts_handler, NULL);
    
    router_register(router, HTTP_GET, "/$'settings'", 
                    settings_handler, NULL);
    
    router_register(router, HTTP_GET, "/$'api'/${}/$'data'", 
                    api_handler, NULL);
    
    // 测试路由匹配
    route_params_t params;
    route_callback_t cb;
    
    // 测试1: 用户资料
    printf("\n--- Test 1: User Profile ---\n");
    cb = router_match(router, HTTP_GET, "/user/alice/profile", &params);
    if (cb) {
        cb(&params, (void*)"user data");
        route_params_free(&params);
    }
    
    // 测试2: 用户帖子
    printf("\n--- Test 2: User Posts ---\n");
    cb = router_match(router, HTTP_GET, "/user/bob/posts/123", &params);
    if (cb) {
        cb(&params, NULL);
        route_params_free(&params);
    }
    
    // 测试3: 设置页面
    printf("\n--- Test 3: Settings ---\n");
    cb = router_match(router, HTTP_GET, "/settings", &params);
    if (cb) {
        cb(&params, NULL);
        route_params_free(&params);
    }
    
    // 测试4: API路由
    printf("\n--- Test 4: API Route ---\n");
    cb = router_match(router, HTTP_GET, "/api/v1/data", &params);
    if (cb) {
        cb(&params, NULL);
        route_params_free(&params);
    }
    
    // 测试5: 不存在的路由
    printf("\n--- Test 5: Not Found ---\n");
    cb = router_match(router, HTTP_GET, "/nonexistent/path", &params);
    if (!cb) {
        printf("404 - Route not found\n");
    }
    
    // 清理
    router_destroy(router);
    
    return 0;
}
```

## 匹配规则

### 优先级

关键字匹配优先级高于通配符匹配。当存在多个可能的匹配时，系统优先选择关键字匹配。

```c
注册规则A: /$'user'/${}/$'profile'
注册规则B: /$'user'/$'admin'/$'profile'
请求URL: /user/admin/profile

结果: 匹配规则B（关键字优先级更高）
```

### 段数匹配

URL路径的段数必须与路由规则完全一致：

```
路由规则: /$'user'/${}/$'profile'  (3段)
匹配: /user/alice/profile          ✓ (3段)
不匹配: /user/alice                 ✗ (2段)
不匹配: /user/alice/profile/extra  ✗ (4段)
```

## 性能特点

- **匹配时间复杂度**：O(L)，L为URL段数
- **空间复杂度**：O(N×M)，N为路由数量，M为平均段长
- **无哈希冲突**：基于树结构的确定性匹配
- **内存友好**：每个路由段只存储一次

## 编译与安装

### 编译静态库

```bash
# 编译目标文件
gcc -c -O2 router.c -o router.o

# 创建静态库
ar rcs librouter.a router.o
```

### 使用示例编译

```bash
gcc -o example example.c router.c -O2
./example
```

### Makefile示例

```makefile
CC = gcc
CFLAGS = -Wall -Wextra -O2
TARGET = example
OBJS = router.o example.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c router.h
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f $(OBJS) $(TARGET)
```

## 注意事项

1. **路由模式必须以`/`开头**
2. **关键字必须使用`$'keyword'`格式**，单引号不可省略
3. **通配符必须使用`${}`格式**
4. **捕获的参数按出现顺序存入数组**，无命名参数
5. **匹配后必须调用`route_params_free`释放参数内存**
6. **路由注册顺序不影响匹配结果**（基于树结构，关键字优先）

## 线程安全

本库不包含内部同步机制。如果需要在多线程环境中使用，请自行添加互斥锁保护`router_match`和`router_register`操作。

## 许可证

MIT License

## 贡献

欢迎提交Issue和Pull Request。

## 作者

URLRouter - 轻量级C语言URL路由库