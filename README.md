# URLRouter

一个为 C 语言环境设计的轻量级 URL 路由库，专用于将 URL 请求路径映射到对应的处理函数，并从 URL 中提取动态参数。

## 特性

- **轻量级**：零外部依赖，仅使用标准 C 库，适合嵌入式系统和资源受限环境
- **高性能**：时间复杂度与 URL 段数成正比，与注册路由总数无关
- **零拷贝参数提取**：参数直接指向 URL 原始数据
- **灵活的表达式能力**：通过 9 个基础操作符组合，可描述各种 URL 格式
- **HTTP 方法隔离**：支持按 HTTP 方法独立注册路由
- **确定性匹配**：匹配结果不受注册顺序影响
- **匹配与提取分离**：匹配阶段快速定位，提取阶段按需执行

## 项目结构

```
.
├── doc/                        # 设计文档
│   └── URLRouter/
│       ├── URLRouter 综述文档.md
│       ├── URLRouter 路由语法规范.md
│       └── URLRouter 编译器设计文档.md
├── include/                    # 公共头文件
│   ├── router.h                # 路由器 API
│   ├── route_tree.h            # 路由树
│   └── pattern.h               # 段模式：词法（语法）+ 编译（IR）
├── src/                        # 源代码
│   ├── router.c                # 路由器：切分、编译编排、提取
│   ├── route_tree.c            # 路由树：匹配、匹配序列合并、优先级
│   ├── pattern.c               # 词法分析：$'' / ${} / $[] → 操作符 IR
│   └── pattern_compile.c       # 编译器：IR → Stride 序列构建调用
├── third_party/
│   └── Stride/                 # 编译器子模块（git submodule）
├── Makefile                    # 构建配置
├── README.md                   # 项目说明
├── example.c                   # 使用示例
└── test.c                      # 测试用例
```

## 依赖：Stride

[Stride](../Stride) 以 git 子模块 `third_party/Stride` 引入，提供**步进式比特串匹配 / 提取**：

- 序列构建（函数式，尾部合并）与通用执行引擎（`stride_seq_run`）
- 匹配入口 `stride_match_run`、提取入口 `stride_extract_run` / `stride_full_extractor_run`

**语法与编译器在 URLRouter 这一侧**（`pattern.h` / `pattern.c` / `pattern_compile.c`）：

```
"$'v'${'.'}$'.'${}"
     │  url_lex()          词法分析
     ▼
url_op_t 操作符序列
     │  url_compile()      翻译成 Stride 构建函数调用
     ▼
stride_seq_t（匹配序列） + stride_seq_t（提取序列）
```

URLRouter 保留的部分：

- **语法**：`$'文本'`、`${n}`、`${'文本'}`、`${}`、`$[n]`、`$[END]`、`$[END-n]`、`$[>n]`、`$[<n]`、`$[>'文本']`、`$[<'文本']`
- **编译器**：把操作符翻译成 Stride 序列（常量偏移合并、字面量绑定由 Stride 的尾节点合并完成）
- **路由树与匹配**：`route_tree.c`
- **合并匹配序列**：不同路由在同一层若编译出相同匹配序列，共享同一个节点
- **优先级**：同一层多个子节点命中时，比对（关键字）越多者优先
- **HTTP 方法隔离**、分隔符切分、字节↔比特换算与零拷贝提取编排

匹配代价只与 URL 段数（及同层子节点数）相关，与注册的路由总数无关。

## 核心概念

### 段（Segment）

URL 路径按 `/` 分割后的基本单元。

```
URL: /api/v2.0/users/alice
段列表: ["api", "v2.0", "users", "alice"]
```

### 操作符

共九种操作符，用于描述 URL 模式：

| 类别 | 操作符 | 说明 |
|------|--------|------|
| 精确匹配 | `$'文本'` | 匹配固定字符串 |
| 定长捕获 | `${数字}` | 捕获指定长度的字符 |
| 捕获到字符 | `${'字符'}` | 捕获到指定字符前（不包含该字符） |
| 捕获到结尾 | `${}` | 捕获到段尾 |
| 绝对跳转 | `$[位置]` | 跳转到指定位置（支持整数、END、END-n） |
| 向结尾移动 | `$[>偏移]` | 向段尾方向移动 |
| 向开头移动 | `$[<偏移]` | 向段首方向移动 |
| 向结尾查找 | `$[>'字符']` | 向段尾方向查找字符 |
| 向开头查找 | `$[<'字符']` | 向段首方向查找字符 |

### 特征序列（Feature Sequence）

编译时将段模式转换为元组序列，每个元组描述一个**移动操作**和在该位置需要验证的**关键字**。

- **匹配阶段**：仅使用特征序列，不关心捕获内容
- **提取阶段**：使用提取器，保留完整操作和捕获边界

### 提取器（Extractor）

保存模式的完整操作序列，在路由匹配成功后执行参数提取，返回（指针，长度）形式的参数列表，零拷贝。

## 快速开始

### 编译

```bash
# 首次克隆后需拉取 Stride 子模块
git submodule update --init --recursive

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
    size_t param_count = 0;

    if (router_extract(node, "/user/alice", params, 16, &param_count) == 0) {
        // 调用处理函数
        route_params_t rp = {params, param_count};
        route_callback_t callback = router_get_callback(node);
        void *userdata = router_get_userdata(node);
        if (callback) {
            callback(&rp, userdata);
        }
    }
}

// 销毁路由器
router_destroy(router);
```

### 处理函数示例

```c
static int user_handler(const route_params_t *params, void *userdata) {
    (void)userdata;
    
    if (params && params->count > 0) {
        char buf[256];
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
| 定长限制 | `/${2}/$'key'` | `/ab/key` | `["ab"]` |
| 回溯捕获 | `/$'file'/${}$[0]${'.'}$'.'${}` | `/file/document.pdf` | `["document.pdf", "document", "pdf"]` |

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

### 回调函数签名

```c
typedef int (*route_callback_t)(void *request, void *response);
```

- `request`：指向 `route_params_t` 结构体的指针，包含参数列表
- `response`：保留参数（当前版本未使用）
- 返回值：0 表示成功，非 0 表示失败

**示例**：
```c
static int user_handler(void *request, void *response) {
    (void)response;
    route_params_t *params = (route_params_t *)request;
    
    if (params && params->count > 0) {
        // 处理参数...
    }
    return 0;
}
```

## 编译选项

```bash
# 构建（会自动构建并链接 Stride 子模块）
make

# 运行示例
make run

# 运行所有测试
make test

# 清理（含 Stride 子模块）
make clean
```

## 设计文档

详细设计请参阅 `doc/URLRouter/`：

- [URLRouter 综述文档](doc/URLRouter/URLRouter%20综述文档.md) - 架构分层、核心概念、URLRouter ↔ Stride 分工
- [URLRouter 路由语法规范](doc/URLRouter/URLRouter%20路由语法规范.md) - 操作符语法、执行模型、边界检查、完整示例
- [URLRouter 编译器设计文档](doc/URLRouter/URLRouter%20编译器设计文档.md) - 词法分析、编译流程、翻译与合并规则

步进序列与执行引擎的设计在 Stride 仓库：
[Stride 序列设计文档](../Stride/doc/Stride%20序列设计文档.md)。

## 适用场景

- 嵌入式 Web 服务器
- IoT 设备 HTTP API
- C 语言编写的微服务
- 网络库的 URL 分发模块
- 学习路由实现原理

## 不适用场景

- 需要正则表达式匹配的复杂路由
- 需要基于请求头、IP 等条件的路由
- 全功能 Web 框架（本库只做路由，不包含模板、会话等功能）

## 核心设计理念

- **匹配与提取分离**：匹配阶段快速定位，提取阶段按需执行
- **特征序列驱动**：通过（移动操作，关键字）元组实现高效匹配
- **偏移合并优化**：连续的纯偏移操作在编译时合并，减少运行时操作数量
- **段尾对齐**：确保模式完整消耗整个段，避免部分匹配
- **零拷贝参数**：参数直接指向 URL 原始数据，不进行复制

## 许可证

本项目采用 MIT 许可证。