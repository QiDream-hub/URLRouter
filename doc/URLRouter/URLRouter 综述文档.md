# URLRouter 综述文档

---

## 一、项目定位与一句话介绍

**URLRouter 是一个轻量级 C99 URL 路由库，用简洁的段模式语法把 URL 请求路径映射到处理函数，并在匹配成功后以零拷贝方式提取动态参数。**

它面向嵌入式、IoT 与 C 语言微服务等资源受限或追求确定性的场景：库本身只依赖标准 C 库，匹配的代价只与 URL 的段数相关，与注册了多少条路由无关。URLRouter 负责**语法、编译器与路由**三件事，底层把序列构建与匹配/提取执行交给独立的 Stride 库完成。

---

## 二、核心特性

| 特性 | 说明 |
|------|------|
| **轻量零依赖** | 纯 C99，仅使用标准 C 库；不引入正则引擎、不依赖动态分配框架 |
| **匹配与提取分离** | 匹配阶段只判定「命中与否」，提取阶段在需要时才产出参数，两阶段可独立优化 |
| **零拷贝参数** | 参数以 `route_param_t { const char *ptr; size_t len; }` 返回，直接指向原始 URL，不做复制 |
| **树形匹配，与路由数无关** | 路由组织成树，逐段下降；匹配代价只与 URL 段数及同层子节点数相关，与注册路由总数无关 |
| **HTTP 方法隔离** | 每个 HTTP 方法一棵独立路由树（`router_t` 内含 `trees[HTTP_METHOD_COUNT]`），互不干扰 |
| **分隔符可参数化** | `router_create(char sep)` 指定分隔符，URL 用 `'/'`，点分路径可用 `'.'` 等 |
| **合并匹配序列** | 同一层编译出相同匹配序列的不同路由共享同一个节点，树规模只与「不同前缀」数量相关 |
| **优先级确定性** | 同层多个子节点同时命中时，比对（关键字）越多者越具体、越优先，结果不受注册顺序影响 |
| **灵活的表达能力** | 通过 `$'…'` / `${…}` / `$[…]` 三类共九种写法组合，覆盖静态路径、动态捕获、查询串、扩展名、结构化字段等 |

---

## 三、核心概念

### 3.1 URL 与段（Segment）

URL 是待匹配的原始字符串，必须以分隔符开头。URL 按**分隔符**切分后的基本单元称为**段**：

```
URL:    /api/v2.0/users/alice
段列表: ["api", "v2.0", "users", "alice"]
```

切分是**零拷贝**的：`router.c` 内部的 `parse_url_segments()` 只产出「段指针 + 段长度」两个数组，指针直接指向原始 URL 内部。连续出现两个分隔符（空段）被视为非法输入。

### 3.2 段模式（Segment Pattern）

段模式是一个**单段**的匹配/捕获描述，由操作符组成，例如 `$'v'${'.'}$'.'${}`。整条路由模式由分隔符连接各段模式，例如：

```
/$'user'/${}/$'posts'/${}
```

`router_register()` 先按分隔符把整条模式切成段模式，再对每个段模式分别编译。

段模式支持的操作符：

| 类别 | 操作符 | 说明 |
|------|--------|------|
| 精确匹配 | `$'文本'` | 匹配固定字符串 |
| 定长捕获 | `${数字}` | 捕获指定字节数的内容 |
| 捕获到字符 | `${'字符'}` | 捕获到指定文本前（不含该文本） |
| 捕获到结尾 | `${}` | 捕获到段尾 |
| 绝对跳转 | `$[位置]` | 定位到指定位置，位置可为整数、`END`、`END-n` |
| 向结尾移动 | `$[>偏移]` | 向段尾方向移动 |
| 向开头移动 | `$[<偏移]` | 向段首方向移动 |
| 向结尾查找 | `$[>'字符']` | 向段尾方向查找文本 |
| 向开头查找 | `$[<'字符']` | 向段首方向查找文本 |

文本支持 `\\`、`\'`、`\n`、`\t`、`\r`、`\0`、`\xNN` 转义。词法分析由 `url_lex()` 完成，产出以 `url_op_t` 表示的操作符 IR；其中 `url_op_type_t` 共 10 个类别——`$[END]` / `$[END-n]` 在 IR 中与 `$[n]` 区分（`URL_OP_JUMP_END` 与 `URL_OP_JUMP_ABS`）。

### 3.3 分隔符（Separator）

分隔符在创建路由器时指定并贯穿全程：

- **注册时**：切分整条模式为段模式；以分隔符开头，不允许空段。
- **匹配时**：切分查询 URL 为段数组；根路径（仅一个分隔符）得到 0 段。
- **提取时**：节点记录了注册时的分隔符（`route_node.sep`），据此重新切分查询 URL，且段数必须与提取器的段数一致。

默认 URL 场景使用 `'/'`；JSON 点路径、层级配置键等场景可使用 `'.'` 等其它字符。分隔符为 `'\0'` 时 `router_create()` 返回 `NULL`。

### 3.4 HEAD 与 END

每个段有两个不可变的**位置基准**：

| 基准 | 含义 | 取值 |
|------|------|------|
| **HEAD** | 段的起始位置 | 固定为 0 |
| **END** | 段的结束位置 | 运行时决定的段长（字节） |

段内所有位置都以 HEAD 为原点的绝对坐标表达。`$[n]` 定位到 `HEAD + n`，`$[END]` / `$[END-n]` 定位到段尾 / 段尾前 n 字节，`${}` 在匹配阶段等价于「移动到 END」。段模式执行完毕后，游标必须**恰好落在段尾**，以此保证模式完整消耗整个段，避免前缀式部分匹配。

### 3.5 游标与步（字节）

执行引擎维护一个沿段前进的**游标**。Stride 的通用单位是「步」，而 URLRouter 是**字节导向**的：它固定使用

```c
#define URL_PATTERN_STRIDE 8u   /* 1 步 = 1 字节 */
```

因此段模式里书写的长度与偏移单位就是**字节**。进入 Stride 时，URLRouter 把段长（字节）换算成比特（`段长 × 8`）；Stride 内部的字面量长度、参数长度均以比特计（`stride_blob_t.bit_len`），偏移与位置均以步计。段比特长度必须是步长的整数倍，否则执行失败。

### 3.6 零拷贝参数

匹配与提取分离的收益集中体现在参数上：

- **匹配阶段**不产生任何参数，只做偏移移动与比对。
- **提取阶段**按需执行，产出的 `route_param_t` 中 `ptr` 指向原始 URL 内部，`len` 是**字节**长度，不复制任何数据。
- 生命周期由调用者负责：原始 URL 在参数使用期间必须保持有效。
- `router_param_to_string()` 提供可选的拷贝式辅助（写入调用者缓冲区），`router_param_is_empty()` 判断参数是否为空。

---

## 四、架构分层

### 4.1 分层结构

```
应用
 └─ router.c       路由器：分隔符切分、HTTP 方法隔离、注册/匹配/提取编排
     └─ route_tree.c  路由树：逐段匹配、相同匹配序列合并、优先级
         └─ pattern.h/.c        语法（词法）：$'…' / ${…} / $[…] → url_op_t
             └─ pattern_compile.c 编译器：url_op_t → Stride 序列构建调用
                 └─ Stride（git submodule third_party/Stride）
                      步进式比特串序列库：函数式构建（尾部合并）+ 通用执行引擎
```

### 4.2 各层职责

| 层 | 文件 | 职责 |
|----|------|------|
| 路由器 | `src/router.c`、`include/router.h` | `router_create()` / `router_destroy()`、`router_register()`、`router_match()`、`router_extract()`；URL 切分（零拷贝段指针 + 段长）、按方法选择路由树、字节↔比特换算、参数编排 |
| 路由树 | `src/route_tree.c`、`include/route_tree.h` | 节点增删与生命周期、注册时的冲突检测、**合并匹配序列**（`match_sequences_equal()` + `find_or_create_child()`）、**优先级选择**（`get_node_priority()`）、逐段执行匹配序列、组装完整提取器 |
| 语法（词法） | `src/pattern.c`、`include/pattern.h` | `url_lex()` 把段模式文本解析为 `url_op_t` 操作符序列，`url_ops_free()` 统一释放文本副本 |
| 编译器 | `src/pattern_compile.c`、`include/pattern.h` | `url_compile()` 把操作符序列翻译成 Stride 的**匹配序列**与**提取序列**，`url_compile_free()` 释放结果 |
| 序列与执行引擎 | `third_party/Stride` | 步进式比特串序列的函数式构建（尾部合并）、通用执行引擎 `stride_seq_run()` 及匹配入口 `stride_match_run()`、多段提取入口 `stride_full_extractor_run()` |

### 4.3 URLRouter ↔ Stride 分工

| 关注点 | URLRouter | Stride |
|--------|-----------|--------|
| 语法（`$''` / `${}` / `$[]` 词法） | ✅ `url_lex()` | — |
| 模式 → 序列的翻译（编译器） | ✅ `url_compile()` | — |
| 序列构建（函数 + 尾部合并） | 调用方 | ✅ |
| 匹配 / 提取执行引擎 | 调用方 | ✅ |
| 路由树、匹配序列合并、优先级 | ✅ | — |
| HTTP 方法隔离、分隔符切分 | ✅ | — |
| 比特串表示与步长语义 | 固定 `URL_PATTERN_STRIDE = 8` | ✅ 步长任意，`stride_blob_t` 为任意比特串 |

一句话概括：**URLRouter 负责「语法 + 编译器 + 路由」，Stride 负责「序列构建 + 匹配/提取执行」。** Stride 不含语法、也不含编译器，它是与 URL 无关的通用步进式比特串库；URLRouter 内部没有保留任何 Stride 已经提供的编译器副本，编译器只此一份，位于 `pattern_compile.c`。

序列构建的**尾部合并**由 Stride 完成（同单位的常量偏移相加、字面量/动作绑定到当前尾节点），URLRouter 的编译器层因此保持为无状态机的直译循环：一个操作符对应一到两次构建调用（例如 `URL_OP_MATCH` 在匹配序列上调用 `stride_seq_compare()`，在提取序列上调用 `stride_seq_skip_bits()`——匹配阶段已验证过的字面量，提取时只需跳过）。

---

## 五、一次请求的完整流程

### 5.1 匹配阶段：`router_match()`

```c
route_node_t *router_match(router_t *router, http_method_t method, const char *url);
```

1. **校验与选树**：校验 `router` / `url` 及 `method` 合法性，取出 `router->trees[method]`，实现 HTTP 方法隔离。
2. **切分 URL**：`parse_url_segments()` 按 `router->sep` 把 URL 切成「段指针数组 + 段长数组」，零拷贝；拒绝不以分隔符开头的 URL 与空段；根路径（0 段）直接返回 `NULL`。
3. **逐段下降**：调用 `route_tree_match(&router->trees[method], segments, seg_lens, segment_count)`。
4. **同层候选匹配**：在每一层，对当前节点的所有子节点依次执行
   `stride_match_run(child->match, URL_PATTERN_STRIDE, segment, seg_len * 8)`；
   返回 `0` 表示命中。段长由字节换算为比特后传入。
5. **优先级决胜**：若同层有多个子节点命中，用 `get_node_priority(child)` 计算优先级并选择最高者，保证结果确定且与注册顺序无关。
6. **命中节点**：任一层无子节点命中即返回 `NULL`；所有段走完后，当前节点必须同时满足 `is_leaf` 且有回调，才作为命中节点返回，否则返回 `NULL`（例如某个中间节点只是前缀，不是已注册的完整路由）。
7. **释放临时数组**：段指针/长度数组在此释放；返回的 `route_node_t *` 仍留在树中，无需调用者管理。

### 5.2 提取阶段：`router_extract()`

```c
int router_extract(route_node_t *node, const char *url, route_param_t *params,
                   size_t param_capacity, size_t *out_count);
```

1. **快速短路**：若节点没有提取器或提取器 `segment_count == 0`（模式中没有任何捕获），`*out_count = 0` 并返回 `0`。
2. **切分 URL**：用节点记录的 `node->sep` 再次调用 `parse_url_segments()`；段数必须与 `extractor->segment_count` 完全一致，否则返回 `-1`。
3. **字节 → 比特换算**：把每个段的字节长度乘以 8，得到 `seg_bit_lens`，因为 Stride 以比特计量。
4. **执行完整提取**：调用
   `stride_full_extractor_run(extractor, URL_PATTERN_STRIDE, segments, seg_bit_lens, segment_count, tmp, param_capacity, &n)`，
   其中 `tmp` 为 `stride_param_t` 临时缓冲，参数按段顺序依次连接。
5. **比特 → 字节换算**：把每个 `tmp[i].bit_len` 换算回 `params[i].len`（字节）并转移 `ptr`；若长度不是 8 的整数倍则返回 `-1`（URLRouter 的参数以字节为单位）。
6. **收尾**：`*out_count = n`，释放临时缓冲与段数组。

```c
/* 端到端：注册 → 匹配 → 提取 */
router_t *router = router_create('/');
router_register(router, HTTP_GET, "/$'user'/${}", user_handler, NULL);

route_node_t *node = router_match(router, HTTP_GET, "/user/alice");
if (node) {
    route_param_t params[16];
    size_t count = 0;
    if (router_extract(node, "/user/alice", params, 16, &count) == 0) {
        /* params[0].ptr 指向 "/user/alice" 中的 "alice"，count == 1 */
        route_params_t rp = { params, count };
        route_callback_t cb = router_get_callback(node);
        void *userdata = router_get_userdata(node);
        if (cb) {
            cb(&rp, userdata);   /* 回调签名为 (void *request, void *response) */
        }
    }
}
router_destroy(router);
```

回调类型为 `typedef int (*route_callback_t)(void *request, void *response)`：`request` 指向 `route_params_t`（含参数列表），`response` 为保留参数（当前版本未使用），返回 `0` 表示成功、非 `0` 表示失败；`userdata` 通过 `router_get_userdata(node)` 取得。

### 5.3 API 速览

| 函数 | 说明 |
|------|------|
| `router_t *router_create(char sep)` | 创建路由器，指定分隔符；`sep` 为 `'\0'` 时返回 `NULL` |
| `void router_destroy(router_t *router)` | 销毁路由器及所有路由树 |
| `int router_register(router_t *router, http_method_t method, const char *pattern, route_callback_t callback, void *userdata)` | 注册路由；成功返回 `0`，模式语法错误 / 冲突 / 内存不足返回 `-1` |
| `route_node_t *router_match(router_t *router, http_method_t method, const char *url)` | 匹配 URL，返回命中节点或 `NULL` |
| `int router_extract(route_node_t *node, const char *url, route_param_t *params, size_t param_capacity, size_t *out_count)` | 提取参数；成功返回 `0` |
| `route_callback_t router_get_callback(route_node_t *node)` | 取得节点回调 |
| `void *router_get_userdata(route_node_t *node)` | 取得节点用户数据 |
| `size_t router_param_to_string(route_param_t param, char *buf, size_t buf_size)` | 参数转 null 结尾字符串（拷贝式辅助） |
| `int router_param_is_empty(route_param_t param)` | 判断参数是否为空 |

底层直接可用的接口：`url_lex()`、`url_ops_free()`、`url_compile()`、`url_compile_free()`（`pattern.h`）；`route_tree_init()`、`route_tree_destroy()`、`route_tree_register()`、`route_tree_match()`、`match_sequences_equal()`（`route_tree.h`）。

线程安全：路由树在注册完成后进入只读状态，匹配与提取无需加锁；动态注册请由调用者在外部同步。

---

## 六、两个关键优化

### 6.1 合并匹配序列

路由树的每个节点持有一段**匹配序列**（`stride_seq_t *match`）与其子节点数组。注册一条路由时，逐段从根节点下降：对每个段，先在当前节点的子节点中查找是否存在**相同匹配序列**的子节点；存在则复用（合并），不存在才新建。

相同性判定由 `match_sequences_equal()` 完成，逐节点比较 `move` / `move_value` / `move_target` 与 `act` / `act_value` / `act_target`，比特串按 `bit_len` 与内容比较。

**为什么不同的模式会编译出相同的匹配序列？** 因为匹配阶段只关心「怎么走、在哪里比什么」，不关心捕获边界。典型例子：

| 模式 | 匹配序列（步长 8，1 步 = 1 字节） | 提取序列 |
|------|-----------------------------------|----------|
| `${2}` | 前进 2 步（`stride_seq_step_fwd(m, 2)`） | 捕获 2 步（`stride_seq_capture_steps`） |
| `$[>2]` | 前进 2 步（`stride_seq_step_fwd(m, 2)`） | 同样前进 2 步（`stride_seq_step_fwd`） |

两者的匹配序列**完全相同**，因此若它们出现在同一层，`find_or_create_child()` 会让它们共享同一个节点；差异只体现在各自叶子上的提取序列。类似地，`$'text'` 在匹配序列中是「比对 `text`」，而连续常量偏移的相加、字面量绑定到当前节点等**尾部合并**由 Stride 在构建时完成，进一步提高了序列相同的概率。

效果：树的规模取决于「不同匹配前缀」的数量，而不是路由条数。大量结构相似的路由（例如同一方法下成百上千个 `/${}/$'…'` 形态的接口）会大幅共享节点。

合并的所有权约定：`route_tree_register()` 无论成功与否都接管传入序列的所有权，命中已有节点时释放传入的重复序列。

### 6.2 优先级

同一层可能存在**多个**子节点的匹配序列同时命中。此时 URLRouter 用 `get_node_priority()` 计算候选优先级并取最高者：

```
priority = Σ (每个节点)  act == STRIDE_ACT_COMPARE ? 100 : 1
```

即**比对（关键字）越多越优先**：一条带更多字面量约束的模式比一条几乎全靠偏移的模式更具体，理应胜出。例如在同一层注册 `/$'user'/${}` 与 `/${}` 后，请求 `/user` 会优先命中前者；请求 `/alice` 则只有后者命中。当优先级完全相同时，`route_tree_match()` 保留先出现的候选，因此同一次运行中的结果始终确定；而匹配序列完全相同的模式在注册时就已经被合并、其叶子冲突也会被 `check_conflict()` 拒绝，所以不存在「两条等价路由互相抢答」的情况。

---

## 七、HTTP 方法隔离与分隔符参数化

### 7.1 HTTP 方法隔离

`router_t` 内部是一个方法到路由树的数组：

```c
struct router {
  route_tree_t trees[HTTP_METHOD_COUNT];
  char sep;                 /* 路径分隔符 */
};
```

`http_method_t` 定义 `HTTP_GET`、`HTTP_POST`、`HTTP_PUT`、`HTTP_DELETE`、`HTTP_PATCH`、`HTTP_HEAD`、`HTTP_OPTIONS` 共 7 个方法，`HTTP_METHOD_COUNT` 为哨兵值。注册与匹配都带 `method` 参数，直接索引对应的树：

- 同一 URL 模式可以为不同方法注册不同回调，彼此完全独立；
- 方法之间不共享节点，因此某一方法的匹配代价不受其它方法路由数量的影响。

### 7.2 分隔符参数化

分隔符在 `router_create(char sep)` 时确定，并保存在路由器中：

- 注册时用于切分整条模式，匹配时用于切分 URL；
- 注册成功后，每个叶子节点把分隔符记录到 `route_node.sep`，提取时用它重新切分 URL，因此提取不依赖路由器实例，只依赖节点；
- 典型的非 URL 用法是以 `'.'` 处理点分路径（如配置键 `service.db.host`）、以 `':'` 处理冒号分隔的结构化标识等。此时所有模式与查询串都必须使用同一种分隔符书写。

---

## 八、依赖与构建

### 8.1 依赖：Stride 子模块

Stride 以 **git submodule** 引入到 `third_party/Stride`，其仓库地址（SSH）为：

```
git@github.com:QiDream-hub/Stride.git
```

首次克隆后需要拉取子模块：

```bash
git submodule update --init --recursive
```

URLRouter 通过 `-I$(STRIDE_DIR)/include` 引入 `stride/stride.h` 等头文件，并链接 Stride 构建出的静态库 `$(STRIDE_DIR)/build/libstride.a`。当子模块缺失时，Makefile 会给出「请先执行 `git submodule update --init --recursive`」的明确提示后停止。

### 8.2 构建命令

```bash
make                     # 构建示例（自动构建并链接 Stride 静态库）
make apps                # 构建 example 与 test_app
make run                 # 运行示例
make run-test-app        # 运行集成测试
make test                # 运行全部测试（集成测试 + 段数匹配测试 + 段模式测试）
make test-segment-count  # 仅运行段数匹配测试
make test-pattern        # 仅运行段模式（词法 + 编译）测试
make clean               # 清理构建产物（含 Stride 子模块）
make compile-commands    # bear -- make clean all，生成 compile_commands.json
```

编译选项为 `-Wall -Wextra -O2 -g -std=c99`，核心库源文件是 `src/router.c`、`src/route_tree.c`、`src/pattern.c`、`src/pattern_compile.c`。

指定本地 Stride 检出（例如把 URLRouter 与 Stride 并排克隆时）：

```bash
make STRIDE_DIR=../Stride
```

`STRIDE_DIR` 使用 `?=` 默认值 `third_party/Stride`，可在命令行覆盖。

### 8.3 构建系统

项目**只提供 Makefile，不提供 CMake**。集成到其它构建系统时，需自行加入四个核心源文件、两个头文件搜索路径（`include/` 与 Stride 的 `include/`）以及 Stride 静态库。

---

## 九、适用场景与不适用场景

### 9.1 适用场景

- **嵌入式 Web 服务器**：资源受限，无法承载正则引擎或全功能框架。
- **IoT 设备 HTTP API**：接口数量固定、结构规整，需要确定性的分发表。
- **C 语言编写的微服务**：已有自有网络层，只需要一个高效的 URL 分发组件。
- **网络库的 URL 分发模块**：以库的形式嵌入，由宿主负责 I/O 与生命周期。
- **路由实现的学习参考**：语法层、编译器层、执行引擎三层清晰分离，便于研究与裁剪。

### 9.2 不适用场景

- **需要正则表达式匹配**的复杂路由（URLRouter 刻意不引入正则，只提供 URL 特化的操作符）。
- **需要基于请求头、IP、Cookie 等条件的路由**（本库只看方法与 URL 路径）。
- **全功能 Web 框架**（不含模板、会话、静态文件服务、连接管理等）。
- **需要从任意比特偏移捕获参数**的场景：URLRouter 固定步长为 8（字节），参数以字节为单位返回；比特级能力请直接使用 Stride。

---

## 十、目录结构

```
URLRouter/
├── include/
│   ├── router.h            # 路由器 API、HTTP 方法、参数与回调类型
│   ├── route_tree.h        # 路由树、节点结构、匹配序列合并接口
│   └── pattern.h           # 段模式：词法（url_lex）+ 编译（url_compile）
├── src/
│   ├── router.c            # 切分、注册/匹配/提取编排、字节↔比特换算
│   ├── route_tree.c        # 逐段匹配、匹配序列合并、优先级
│   ├── pattern.c           # 词法分析：$'' / ${} / $[] → url_op_t
│   └── pattern_compile.c   # 编译器：url_op_t → Stride 序列构建调用
├── tests/
│   ├── test_segment_count.c  # 段数与匹配代价测试
│   └── test_pattern.c        # 词法与编译测试
├── doc/
│   └── URLRouter/          # 设计文档（本综述、语法规范、编译器设计等）
├── third_party/
│   └── Stride/             # 依赖：步进式比特串序列库（git submodule）
├── build/                  # 构建产物
├── example.c               # 使用示例
├── test.c                  # 集成测试
├── Makefile                # 构建配置
└── README.md               # 项目说明
```

---

## 十一、与旧版本的差异要点

2026-09-11 重构后，URLRouter 与旧版的主要差别可概括为几句话：

- **语法与编译器从 Stride 迁入 URLRouter**：`$'…'` / `${…}` / `$[…]` 的词法与翻译现在位于 `pattern.c` / `pattern_compile.c`，Stride 只保留通用的序列构建与执行引擎；旧版「URLRouter 内部还保存编译器副本」的说法不再成立。
- **「特征序列」更名为「匹配序列」**：概念仍是「偏移 + 比对」的精简序列，但名字与 Stride 的 `stride_seq_t` 一致，避免与提取序列混淆。
- **序列实现改为 Stride 的步进序列**：单链表结构 + 函数式尾部合并，取代旧版自有的元组/偏移合并实现。

除上述三点外，调用侧的 API 形态（注册、匹配、提取三段式）与零拷贝参数的语义保持不变。

---

## 十二、总结

URLRouter 用极少的代码把「URL 怎么切、段怎么匹配、参数怎么取」三件事拆成清晰的三层：**语法（词法）→ 编译器 → 路由树**，而把与业务无关的「比特串怎么走、怎么比、怎么捕获」下沉给 Stride。由此得到一组互相支撑的性质：

- **匹配与提取分离**：匹配阶段只做偏移与比对，提取阶段按需执行；
- **零拷贝参数**：参数直接指向原始 URL，长度以字节计；
- **段尾对齐**：模式必须完整消耗整个段，杜绝前缀式误匹配；
- **树形匹配、与路由数无关**：代价只与段数及同层子节点数相关；
- **合并匹配序列 + 优先级**：相似路由共享节点，具体者优先，结果确定；
- **HTTP 方法隔离、分隔符参数化**：一套机制适配多方法 URL 与点分等其它层级路径。

设计上遵循最小化与确定性原则：不引入正则、不做框架、不做运行期动态分配之外的魔法，把内存所有权与生命周期明确交给调用者。

---

**文档版本：1.0**
**更新日期：2026-09-11**
