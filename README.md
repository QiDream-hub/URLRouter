# URLRouter

一个为 C 语言环境设计的轻量级 URL 路由库，专用于将 URL 请求路径映射到对应的处理函数，并从 URL 中提取动态参数。

## 特性

- **轻量级**：零外部依赖，仅使用标准 C 库，适合嵌入式系统和资源受限环境
- **高性能**：时间复杂度与 URL 段数成正比，与注册路由总数无关
- **零拷贝参数提取**：参数直接指向 URL 原始数据
- **灵活的表达式能力**：通过 7 个基础操作符组合，可描述各种 URL 格式
- **HTTP 方法隔离**：支持按 HTTP 方法独立注册路由
- **确定性匹配**：匹配结果不受注册顺序影响

## 项目结构

```
.
├── doc/                        # 设计文档
│   └── URLRouter/
│       ├── URL 路由语法规范.md
│       ├── URLRouter 路由匹配设计文档.md
│       └── URLRouter 综述文档.md
├── Makefile                    # 构建配置
├── README.md                   # 项目说明
├── example.c                   # 使用示例
├── router.h                    # 公共 API 头文件
├── router.c                    # 路由器实现
├── route_tree.h                # 路由树头文件
├── route_tree.c                # 路由树实现
├── pattern_compiler.h          # 模式编译器头文件
├── pattern_compiler.c          # 模式编译器实现
├── extractor.h                 # 提取器头文件
└── extractor.c                 # 提取器实现
```

## 核心概念

### 段（Segment）

URL 路径按 `/` 分割后的基本单元。

```
URL: /api/v2.0/users/alice
段列表: ["api", "v2.0", "users", "alice"]
```

### 操作符

共七种操作符，用于描述 URL 模式：

| 类别 | 操作符 | 说明 |
|------|--------|------|
| 精确匹配 | `$'文本'` | 匹配固定字符串 |
| 定长捕获 | `${数字}` | 捕获指定长度的字符 |
| 捕获到字符 | `${'字符'}` | 捕获到指定字符前 |
| 捕获到结尾 | `${}` | 捕获到段尾 |
| 绝对跳转 | `$[位置]` | 跳转到指定位置（支持整数、END、END-n） |
| 向结尾移动 | `$[>偏移]` | 向段尾方向移动 |
| 向开头移动 | `$[<偏移]` | 向段首方向移动 |

## 快速开始

### 编译

```bash
make
```

### 基本使用

```c
#include "router.h"

// 创建路由器
router_t *router = router_create();

// 注册路由
router_register(router, HTTP_GET, "/$'user'/${}", user_handler, NULL);

// 匹配路由
route_node_t *node = router_match(router, HTTP_GET, "/user/alice");

if (node) {
    // 提取参数
    route_param_t params[16];
    size_t param_count = 16;
    
    if (router_extract(node, "/user/alice", params, 16, &param_count) == 0) {
        // 调用处理函数
        route_params_t rp = {params, param_count};
        router_get_callback(node)(&rp, router_get_userdata(node));
    }
}

// 销毁路由器
router_destroy(router);
```

### 处理函数示例

```c
static int user_handler(const route_params_t *params, void *userdata) {
    char buf[256];
    
    if (params && params->count > 0) {
        router_param_to_string(params->params[0], buf, sizeof(buf));
        printf("User ID: %s\n", buf);
    }
    
    return 0;
}
```

## 模式示例

| 场景 | 模式 | URL 示例 | 捕获结果 |
|------|------|----------|----------|
| 用户主页 | `/$'user'/${}` | `/user/alice` | `["alice"]` |
| 用户帖子 | `/$'user'/${}/$'posts'/${}` | `/user/bob/posts/42` | `["bob", "42"]` |
| 搜索查询 | `/$'search'$'?'${'='}$'='${}` | `/search?q=router` | `["q", "router"]` |
| 版本化 API | `/$'api'/$'v'${'.'}$'.'${}/$'users'` | `/api/v2.0/users` | `["2", "0"]` |
| 静态文件 | `/$'files'/${'.'}$'.'${}` | `/files/document.pdf` | `["document", "pdf"]` |
| 日期日志 | `/$'logs'/${4}$'-'${2}$'-'${2}` | `/logs/2024-03-15` | `["2024", "03", "15"]` |

## API 参考

### 路由器管理

| 函数 | 说明 |
|------|------|
| `router_t *router_create(void)` | 创建路由器实例 |
| `void router_destroy(router_t *router)` | 销毁路由器 |
| `void router_clear(router_t *router)` | 清除所有路由 |

### 路由注册

| 函数 | 说明 |
|------|------|
| `int router_register(router_t *router, http_method_t method, const char *pattern, route_callback_t callback, void *userdata)` | 注册路由 |

### 路由匹配

| 函数 | 说明 |
|------|------|
| `route_node_t *router_match(const router_t *router, http_method_t method, const char *url)` | 匹配 URL，返回路由节点 |

### 参数提取

| 函数 | 说明 |
|------|------|
| `int router_extract(const route_node_t *node, const char *url, route_param_t *out_params, size_t max_params, size_t *out_count)` | 从 URL 提取参数 |
| `void router_param_to_string(route_param_t param, char *buf, size_t buf_size)` | 将参数转换为字符串 |

### 回调访问

| 函数 | 说明 |
|------|------|
| `route_callback_t router_get_callback(const route_node_t *node)` | 获取处理函数 |
| `void *router_get_userdata(const route_node_t *node)` | 获取用户数据 |

## 编译选项

```bash
# 编译库
make

# 运行示例
./router_example

# 清理
make clean
```

## 设计文档

详细设计请参阅 `doc/URLRouter/` 目录下的文档：

- [URLRouter 综述文档](doc/URLRouter/URLRouter%20综述文档.md) - 整体设计理念
- [URL 路由语法规范](doc/URLRouter/URL%20路由语法规范.md) - 模式语法详解
- [URLRouter 路由匹配设计文档](doc/URLRouter/URLRouter%20路由匹配设计文档.md) - 编译器设计

## 适用场景

- 嵌入式 Web 服务器
- IoT 设备 HTTP API
- C 语言编写的微服务
- 网络库的 URL 分发模块
- 学习路由实现原理

## 不适用场景

- 需要正则表达式匹配的复杂路由
- 需要基于请求头、IP 等条件的路由
- 全功能 Web 框架（本库只做路由）

## 许可证

本项目采用 MIT 许可证。