# URLRouter 编译器设计文档

**版本**：2.2

---

## 一、概述

编译器将路由模式字符串转换为两个独立的数据结构：

1. **特征序列**：用于路由匹配阶段，包含移动操作和关键字
2. **提取序列**：用于参数提取阶段，保留完整的捕获语义

```
模式字符串 → 词法分析 → 操作符序列 → 特征序列编译 → 特征序列
                              ↓
                        提取序列编译 → 提取操作序列（经编译时优化）
```

---

## 二、词法分析

### 2.1 操作符识别规则

词法分析器将模式字符串切分为操作符令牌序列。

| 操作符 | 正则模式 | 示例 |
|--------|---------|------|
| 精确匹配 | `\$'[^']*'` | `$'user'` |
| 定长捕获 | `\$\{[\d]+\}` | `${4}` |
| 捕获到字符 | `\$\{'[^']'\}` | `${'='}` |
| 捕获到结尾 | `\$\{\}` | `${}` |
| 绝对跳转 | `\$\[[\d]+\]` | `$[5]` |
| 绝对跳转(END) | `\$\[END(?:-[\d]+)?\]` | `$[END]`、`$[END-4]` |
| 正向移动 | `\$\[>[\d]+\]` | `$[>3]` |
| 负向移动 | `\$\[<[\d]+\]` | `$[<2]` |
| 正向查找 | `\$\[>'[^']'\]` | `$[>'=']` |
| 负向查找 | `\$\[<'[^']'\]` | `$[<'=']` |

### 2.2 错误处理

| 错误类型 | 示例 | 处理 |
|---------|------|------|
| 未闭合引号 | `$'user` | 编译失败 |
| 无效数字 | `${abc}` | 编译失败 |
| 无效位置 | `$[abc]` | 编译失败 |
| 空模式 | `//` | 编译失败 |
| 不以/开头 | `user/profile` | 编译失败 |

---

## 三、操作符序列

### 3.1 操作符类型定义

```c
typedef enum {
    OP_MATCH,           // 精确匹配 $'text'
    OP_CAPTURE_LEN,     // 定长捕获 ${n}
    OP_CAPTURE_CHR,     // 捕获到字符 ${'c'}
    OP_CAPTURE_END,     // 捕获到结尾 ${}
    OP_JUMP_ABS,        // 绝对跳转 $[n]
    OP_JUMP_END,        // END跳转 $[END] / $[END-n]
    OP_JUMP_FWD,        // 正向移动 $[>n]
    OP_JUMP_BACK,       // 负向移动 $[<n]
    OP_FIND_FWD,        // 正向查找 $[>'c']
    OP_FIND_REV         // 负向查找 $[<'c']
} op_type_t;

typedef struct {
    op_type_t type;
    union {
        struct { const char *text; size_t len; } match;  // OP_MATCH
        size_t length;                                    // OP_CAPTURE_LEN, OP_JUMP_*
        struct { char ch; } find;                        // OP_CAPTURE_CHR, OP_FIND_*
        struct { int is_end; int offset; } jump_end;     // OP_JUMP_END
    } data;
} op_t;
```

### 3.2 编译示例

```
输入: /$'user'/${}

段0: $'user'   → OP_MATCH, text="user"
段1: ${}       → OP_CAPTURE_END
```

---

## 四、特征序列编译

### 4.1 状态机

特征序列编译采用两状态状态机，逐段处理操作符序列。

#### 状态定义

| 状态 | 含义 |
|------|------|
| **IDLE** | 空闲，无持有操作 |
| **HOLD** | 持有一个移动元组，等待合并或输出 |

#### 操作分类

| 类别 | 操作符 | 事件类型 | 说明 |
|------|--------|---------|------|
| 常量操作 | `OP_CAPTURE_LEN`、`OP_JUMP_FWD` | `CONST_POS` | 正向常量移动 |
| 常量操作 | `OP_JUMP_BACK` | `CONST_NEG` | 负向常量移动 |
| 常量操作 | `OP_JUMP_ABS` | `CONST_ABS_HEAD` | 绝对常量移动（基于 HEAD） |
| 常量操作 | `OP_JUMP_END` | `CONST_ABS_END` | 绝对常量移动（基于 END） |
| 动态操作 | `OP_CAPTURE_CHR`、`OP_FIND_FWD` | `DYNAMIC_FIND_FWD` | 动态正向查找 |
| 动态操作 | `OP_FIND_REV` | `DYNAMIC_FIND_REV` | 动态反向查找 |
| 关键字 | `OP_MATCH` | `KEYWORD` | 关键字匹配 |

#### 基础元组映射

| 操作符 | 基础元组 |
|--------|---------|
| `OP_MATCH` | `(0, text)` |
| `OP_CAPTURE_LEN` | `(length, NULL)` |
| `OP_JUMP_FWD` | `(offset, NULL)` |
| `OP_JUMP_BACK` | `(-offset, NULL)` |
| `OP_JUMP_ABS` | `(HEAD+n, NULL)` |
| `OP_JUMP_END`（END） | `(END, NULL)` |
| `OP_JUMP_END`（END-n） | `(END-n, NULL)` |
| `OP_CAPTURE_CHR` | `(ch, NULL)` |
| `OP_FIND_FWD` | `(ch, NULL)` |
| `OP_FIND_REV` | `('<', ch, NULL)` |

#### 状态转换表

| 当前状态 | 事件 | 动作 | 下一状态 |
|---------|------|------|---------|
| IDLE | 常量操作 | 持有 = 基础元组 | HOLD |
| IDLE | 动态操作 | 持有 = 基础元组 | HOLD |
| IDLE | 关键字 | 输出 `(0, kw)` | IDLE |
| HOLD | 常量操作 | 若可相加：持有 += 常量值<br>否则：输出持有，持有 = 新元组 | HOLD |
| HOLD | 动态操作 | 输出持有，持有 = 动态元组 | HOLD |
| HOLD | 关键字 | 输出 (持有值, kw)，清空持有 | IDLE |
| 扫描结束 | — | 若在 HOLD：输出持有 | IDLE |

#### 常量相加规则

常量操作可以数值相加，但需满足基准兼容性：

| 持有类型 | 可相加事件 | 结果 | 约束 |
|---------|-----------|------|------|
| `(n, NULL)` | `CONST_POS` | `(n+m, NULL)` | 无 |
| `(n, NULL)` | `CONST_NEG` | `(n-m, NULL)` | 结果可为负 |
| `(-n, NULL)` | `CONST_POS` | `(-n+m, NULL)` | 无 |
| `(-n, NULL)` | `CONST_NEG` | `(-n-m, NULL)` | 无 |
| `(HEAD+n, NULL)` | `CONST_POS` | `(HEAD+n+m, NULL)` | 结果 `n+m ≥ 0` |
| `(HEAD+n, NULL)` | `CONST_NEG` | `(HEAD+n-m, NULL)` | 结果 `n-m ≥ 0` |
| `(END-n, NULL)` | `CONST_NEG` | `(END-n-m, NULL)` | 结果 `n+m ≥ 0` |
| `(END-n, NULL)` | `CONST_POS` | **无效** | END 不能加正数 |
| 动态操作 | 任何 | **不可相加** | 先输出再处理 |
| 不同基准 | 任何 | **不可相加** | HEAD 与 END 不能混用 |

#### 关键字合并规则

关键字与当前 HOLD 元组合并，输出 `(元组值, kw)`：

| 持有类型 | 合并结果 |
|---------|---------|
| `(n, NULL)` | `(n, kw)` |
| `(-n, NULL)` | `(-n, kw)` |
| `(HEAD+n, NULL)` | `(HEAD+n, kw)` |
| `(END-n, NULL)` | `(END-n, kw)` |
| `(ch, NULL)` | `(ch, kw)` |
| `('<', ch, NULL)` | `('<', ch, kw)` |

### 4.2 编译示例

#### 示例1：常量操作相加

```
输入：${1}${1}${1}$'key'

操作符序列：OP_CAPTURE_LEN(1), OP_CAPTURE_LEN(1), OP_CAPTURE_LEN(1), OP_MATCH("key")

状态机：
  IDLE + CONST_POS(1) → HOLD(1, NULL)
  HOLD + CONST_POS(1) → HOLD(2, NULL)
  HOLD + CONST_POS(1) → HOLD(3, NULL)
  HOLD + KEYWORD("key") → 输出 (3, "key") → IDLE

输出：[(3, "key")]
```

#### 示例2：动态操作打断

```
输入：${2}${'a'}${3}$'b'

操作符序列：OP_CAPTURE_LEN(2), OP_CAPTURE_CHR('a'), OP_CAPTURE_LEN(3), OP_MATCH("b")

状态机：
  IDLE + CONST_POS(2) → HOLD(2, NULL)
  HOLD + DYNAMIC('a') → 输出 (2, NULL)，HOLD('a', NULL)
  HOLD + CONST_POS(3) → 输出 ('a', NULL)，HOLD(3, NULL)
  HOLD + KEYWORD("b") → 输出 (3, "b") → IDLE

输出：[(2, NULL), ('a', NULL), (3, "b")]
```

#### 示例3：绝对移动与常量相加

```
输入：$[5]${2}$'key'

操作符序列：OP_JUMP_ABS(5), OP_CAPTURE_LEN(2), OP_MATCH("key")

状态机：
  IDLE + CONST_ABS_HEAD(5) → HOLD(HEAD+5, NULL)
  HOLD + CONST_POS(2) → HOLD(HEAD+7, NULL)
  HOLD + KEYWORD("key") → 输出 (HEAD+7, "key") → IDLE

输出：[(HEAD+7, "key")]
```

#### 示例4：END 合并

```
输入：${}$[<4]$'dddd'

操作符序列：OP_CAPTURE_END, OP_JUMP_BACK(4), OP_MATCH("dddd")

状态机：
  IDLE + CONST_ABS_END(END) → HOLD(END, NULL)
  HOLD + CONST_NEG(4) → HOLD(END-4, NULL)
  HOLD + KEYWORD("dddd") → 输出 (END-4, "dddd") → IDLE

输出：[(END-4, "dddd")]
```

#### 示例5：动态操作与关键字合并

```
输入：${'a'}$'key'

操作符序列：OP_CAPTURE_CHR('a'), OP_MATCH("key")

状态机：
  IDLE + DYNAMIC('a') → HOLD('a', NULL)
  HOLD + KEYWORD("key") → 输出 ('a', "key") → IDLE

输出：[('a', "key")]
```

#### 示例6：混合场景

```
输入：/${2}${'a'}${3}$'b'

输出：[(2, NULL), ('a', NULL), (3, "b")]
```

#### 示例7：连续关键字（不推荐）

```
输入：$'dd'$'aaa'

操作符序列：OP_MATCH("dd"), OP_MATCH("aaa")

状态机：
  IDLE + KEYWORD("dd") → 输出 (0, "dd") → IDLE
  IDLE + KEYWORD("aaa") → 输出 (0, "aaa") → IDLE

输出：[(0, "dd"), (0, "aaa")]
```

**建议**：用户应主动合并相邻关键字为 `$'ddaaa'`，以获得更优性能。

#### 示例8：END 位置约束（报错）

```
输入：$[END]${'a'}

操作符序列：OP_JUMP_END(END), OP_CAPTURE_CHR('a')

状态机：
  IDLE + CONST_ABS_END(END) → HOLD(END, NULL)
  HOLD + DYNAMIC('a') → 尝试输出 (END, NULL) 然后 HOLD('a', NULL)
  
  ❌ 错误：纯 END 元组后不能有其他操作
```

---

## 五、提取序列编译

### 5.1 设计原则

提取序列保留操作语义用于参数提取，并经过编译时优化：

- **保留捕获边界**：每个捕获操作独立存在
- **匹配操作优化**：匹配操作转换为常量偏移跳过（无需重复验证）
- **常量移动合并**：连续的常量移动操作合并为一个
- **保留动态操作**：查找操作无法在编译时优化，保持原样

### 5.2 提取操作类型定义

```c
typedef enum {
    // 捕获操作（产生参数）
    EX_CAPTURE_LEN,     // 定长捕获
    EX_CAPTURE_CHR,     // 捕获到字符
    EX_CAPTURE_END,     // 捕获到结尾
    
    // 移动操作（不产生参数）
    EX_SKIP_LEN,        // 跳过固定长度（由 OP_MATCH 优化而来）
    EX_JUMP_ABS,        // 绝对跳转
    EX_JUMP_END,        // END 跳转
    EX_JUMP_FWD,        // 正向移动
    EX_JUMP_BACK,       // 负向移动
    EX_FIND_FWD,        // 正向查找
    EX_FIND_REV         // 反向查找
} extractor_op_type_t;

typedef struct {
    extractor_op_type_t type;
    union {
        struct { size_t length; } capture_len;      // EX_CAPTURE_LEN
        struct { char ch; } capture_chr;            // EX_CAPTURE_CHR
        struct { size_t length; } skip_len;         // EX_SKIP_LEN
        struct { size_t pos; } jump_abs;            // EX_JUMP_ABS
        struct { int is_end; int offset; } jump_end;// EX_JUMP_END
        struct { size_t offset; } jump_fwd;         // EX_JUMP_FWD
        struct { size_t offset; } jump_back;        // EX_JUMP_BACK
        struct { char ch; } find_fwd;               // EX_FIND_FWD
        struct { char ch; } find_rev;               // EX_FIND_REV
    } data;
} extractor_op_t;
```

### 5.3 基础转换规则

| 操作符 | 基础提取操作 | 产生参数 | 说明 |
|--------|------------|---------|------|
| `OP_MATCH` | `EX_SKIP_LEN` | 否 | 优化为常量偏移，偏移量 = 文本长度 |
| `OP_CAPTURE_LEN` | `EX_CAPTURE_LEN` | 是 | 保持不变 |
| `OP_CAPTURE_CHR` | `EX_CAPTURE_CHR` | 是 | 保持不变 |
| `OP_CAPTURE_END` | `EX_CAPTURE_END` | 是 | 保持不变 |
| `OP_JUMP_ABS` | `EX_JUMP_ABS` | 否 | 保持不变 |
| `OP_JUMP_END` | `EX_JUMP_END` | 否 | 保持不变 |
| `OP_JUMP_FWD` | `EX_JUMP_FWD` | 否 | 保持不变 |
| `OP_JUMP_BACK` | `EX_JUMP_BACK` | 否 | 保持不变 |
| `OP_FIND_FWD` | `EX_FIND_FWD` | 否 | 保持不变 |
| `OP_FIND_REV` | `EX_FIND_REV` | 否 | 保持不变 |

### 5.4 编译时优化

提取序列在基础转换后执行两项优化：匹配关键字转常量偏移、常量移动合并。

#### 5.4.1 匹配关键字 → 常量偏移

由于匹配阶段已验证所有关键字的正确性，提取阶段无需重复验证。

| 原转换 | 优化后转换 |
|--------|-----------|
| `OP_MATCH` → `EX_SKIP_MATCH` | `OP_MATCH` → `EX_SKIP_LEN`（偏移量 = 文本长度） |

优化后提取序列不再包含 `EX_SKIP_MATCH` 类型，匹配操作全部转换为常量偏移跳过。

#### 5.4.2 常量移动合并

连续的常量移动操作（不产生参数）在编译时合并为一个操作。

**可合并的操作**：
- `EX_SKIP_LEN`
- `EX_JUMP_FWD`
- `EX_JUMP_BACK`

**合并规则**：
1. 同类操作直接相加：`EX_JUMP_FWD(a) + EX_JUMP_FWD(b) = EX_JUMP_FWD(a+b)`
2. 正向与负向抵消：`EX_JUMP_FWD(a) + EX_JUMP_BACK(b) = EX_JUMP_FWD(a-b)`（结果可能为负，转换为 `EX_JUMP_BACK`）
3. `EX_SKIP_LEN` 视为正向移动，可与 `EX_JUMP_FWD`、`EX_JUMP_BACK` 合并

**打断条件**：
- 遇到产生参数的操作（`EX_CAPTURE_*`）
- 遇到动态操作（`EX_FIND_FWD`、`EX_FIND_REV`）
- 遇到绝对跳转（`EX_JUMP_ABS`、`EX_JUMP_END`）

**合并示例**：
```
输入序列：EX_JUMP_FWD(3), EX_JUMP_BACK(1), EX_SKIP_LEN(4)
合并后：EX_JUMP_FWD(6)  // 3 - 1 + 4 = 6
```

#### 5.4.3 优化流程

提取序列编译采用两阶段处理：

1. **基础转换**：将操作符序列按 5.3 规则转换为提取操作序列（暂不输出）
2. **优化合并**：遍历提取操作序列，按 5.4.2 规则合并连续常量移动

```
操作符序列 → 基础转换 → 临时序列 → 常量合并 → 最终提取序列
```

### 5.5 编译示例

#### 示例1：基础捕获（含匹配优化）

```
输入：${4}$'a'

操作符序列：
  [0] OP_CAPTURE_LEN, length=4
  [1] OP_MATCH, text="a"

基础转换：
  [0] EX_CAPTURE_LEN, length=4
  [1] EX_SKIP_LEN, length=1   // OP_MATCH 优化为 EX_SKIP_LEN

优化合并：无可合并的连续常量移动

最终提取序列：
  [0] EX_CAPTURE_LEN, length=4  (参数1)
  [1] EX_SKIP_LEN, length=1     (不产生参数)

参数数量：1
```

#### 示例2：捕获到字符（无优化）

```
输入：${'='}$'='${}

操作符序列：
  [0] OP_CAPTURE_CHR, ch='='
  [1] OP_MATCH, text="="
  [2] OP_CAPTURE_END

基础转换：
  [0] EX_CAPTURE_CHR, ch='='
  [1] EX_SKIP_LEN, length=1
  [2] EX_CAPTURE_END

优化合并：无可合并的连续常量移动（捕获操作打断）

最终提取序列：
  [0] EX_CAPTURE_CHR, ch='='     (参数1)
  [1] EX_SKIP_LEN, length=1      (不产生参数)
  [2] EX_CAPTURE_END             (参数2)

参数数量：2
```

#### 示例3：常量移动合并

```
输入：${2}$[>3]$'abc'

操作符序列：
  [0] OP_CAPTURE_LEN, length=2
  [1] OP_JUMP_FWD, offset=3
  [2] OP_MATCH, text="abc"

基础转换：
  [0] EX_CAPTURE_LEN, length=2
  [1] EX_JUMP_FWD, offset=3
  [2] EX_SKIP_LEN, length=3

优化合并：EX_JUMP_FWD(3) + EX_SKIP_LEN(3) = EX_JUMP_FWD(6)

最终提取序列：
  [0] EX_CAPTURE_LEN, length=2  (参数1)
  [1] EX_JUMP_FWD, offset=6     (不产生参数)

参数数量：1
运行时操作数：从 3 个减少到 2 个
```

#### 示例4：动态操作打断合并

```
输入：${2}$[>'=']$[>3]$'abc'

操作符序列：
  [0] OP_CAPTURE_LEN, length=2
  [1] OP_FIND_FWD, ch='='
  [2] OP_JUMP_FWD, offset=3
  [3] OP_MATCH, text="abc"

基础转换：
  [0] EX_CAPTURE_LEN, length=2
  [1] EX_FIND_FWD, ch='='
  [2] EX_JUMP_FWD, offset=3
  [3] EX_SKIP_LEN, length=3

优化合并：EX_JUMP_FWD(3) + EX_SKIP_LEN(3) = EX_JUMP_FWD(6)

最终提取序列：
  [0] EX_CAPTURE_LEN, length=2  (参数1)
  [1] EX_FIND_FWD, ch='='       (不产生参数)
  [2] EX_JUMP_FWD, offset=6     (不产生参数)

参数数量：1
运行时操作数：从 4 个减少到 3 个
```

#### 示例5：正向与负向抵消

```
输入：$[>5]$[<3]$'key'

操作符序列：
  [0] OP_JUMP_FWD, offset=5
  [1] OP_JUMP_BACK, offset=3
  [2] OP_MATCH, text="key"

基础转换：
  [0] EX_JUMP_FWD, offset=5
  [1] EX_JUMP_BACK, offset=3
  [2] EX_SKIP_LEN, length=3

优化合并：EX_JUMP_FWD(5) + EX_JUMP_BACK(3) = EX_JUMP_FWD(2)
          EX_JUMP_FWD(2) + EX_SKIP_LEN(3) = EX_JUMP_FWD(5)

最终提取序列：
  [0] EX_JUMP_FWD, offset=5     (不产生参数)

参数数量：0
运行时操作数：从 3 个减少到 1 个
```

#### 示例6：完整路由示例

```
输入：/$'api'/$'v'${'.'}$'.'${}/$'users'

操作符序列（段1）：
  [0] OP_MATCH, text="v"
  [1] OP_CAPTURE_CHR, ch='.'
  [2] OP_MATCH, text="."
  [3] OP_CAPTURE_END

基础转换：
  [0] EX_SKIP_LEN, length=1
  [1] EX_CAPTURE_CHR, ch='.'
  [2] EX_SKIP_LEN, length=1
  [3] EX_CAPTURE_END

优化合并：无可合并（捕获操作打断）

最终提取序列（段1）：
  [0] EX_SKIP_LEN, length=1     (不产生参数)
  [1] EX_CAPTURE_CHR, ch='.'    (参数1)
  [2] EX_SKIP_LEN, length=1     (不产生参数)
  [3] EX_CAPTURE_END            (参数2)

参数数量：2
```

---

## 六、完整编译流程

### 6.1 路由编译入口

```c
typedef struct {
    feature_tuple_t *features;      // 特征序列
    size_t feature_count;
    extractor_op_t *extractors;     // 提取序列（已优化）
    size_t extractor_count;
    size_t param_count;              // 参数数量
} compiled_route_t;

compiled_route_t* compile_route(const char *pattern);
```

### 6.2 完整示例

#### 输入
```
模式: /$'api'/$'v'${'.'}$'.'${}
URL:  /api/v2.0
```

#### 词法分析
```
段0: $'api'
段1: $'v' ${'.'} $'.' ${}
```

#### 操作符序列
```
段0:
  [0] OP_MATCH, text="api"

段1:
  [0] OP_MATCH, text="v"
  [1] OP_CAPTURE_CHR, ch='.'
  [2] OP_MATCH, text="."
  [3] OP_CAPTURE_END
```

#### 特征序列编译（段1）
```
状态机：
  IDLE + KEYWORD("v") → 输出 (0, "v") → IDLE
  IDLE + DYNAMIC('.') → HOLD('.', NULL)
  HOLD + KEYWORD(".") → 输出 ('.', ".") → IDLE
  IDLE + CONST_ABS_END(END) → HOLD(END, NULL)
  扫描结束 → 输出 (END, NULL)

输出：[(0, "v"), ('.', "."), (END, NULL)]
```

#### 提取序列编译（段1）
```
基础转换：
  [0] EX_SKIP_LEN, length=1    // OP_MATCH "v"
  [1] EX_CAPTURE_CHR, ch='.'   // OP_CAPTURE_CHR
  [2] EX_SKIP_LEN, length=1    // OP_MATCH "."
  [3] EX_CAPTURE_END           // OP_CAPTURE_END

优化合并：无可合并的连续常量移动

最终提取序列：
  [0] EX_SKIP_LEN, length=1    (不产生参数)
  [1] EX_CAPTURE_CHR, ch='.'   (参数1)
  [2] EX_SKIP_LEN, length=1    (不产生参数)
  [3] EX_CAPTURE_END           (参数2)
```

#### 最终结果
```
特征序列:
  段0: [(0, "api")]
  段1: [(0, "v"), ('.', "."), (END, NULL)]

提取序列:
  段0: [EX_SKIP_LEN(3)]
  段1: [EX_SKIP_LEN(1), EX_CAPTURE_CHR('.'), EX_SKIP_LEN(1), EX_CAPTURE_END]

参数数量: 2
```

---

## 七、数据结构定义汇总

### 7.1 特征序列

```c
typedef enum {
    FT_CONST_REL_FWD,   // 常量相对向结尾移动: (n, kw)
    FT_CONST_REL_BACK,  // 常量相对向开头移动: (-n, kw)
    FT_CONST_ABS_HEAD,  // 常量绝对位置（基于 HEAD）: (HEAD+n, kw)
    FT_CONST_ABS_END,   // 常量绝对位置（基于 END）: (END-n, kw)
    FT_DYNAMIC_FIND_FWD,// 动态向结尾查找: ('c', kw)
    FT_DYNAMIC_FIND_REV // 动态向开头查找: ('<', 'c', kw)
} feature_type_t;

typedef struct {
    feature_type_t type;
    int value;              // 偏移量或字符 ASCII
    const char *keyword;    // 关键字，可为 NULL
    size_t keyword_len;
} feature_tuple_t;
```

### 7.2 提取序列

```c
typedef enum {
    EX_CAPTURE_LEN,     // 定长捕获（产生参数）
    EX_CAPTURE_CHR,     // 捕获到字符（产生参数）
    EX_CAPTURE_END,     // 捕获到结尾（产生参数）
    EX_SKIP_LEN,        // 跳过固定长度（不产生参数）
    EX_JUMP_ABS,        // 绝对跳转（不产生参数）
    EX_JUMP_END,        // END 跳转（不产生参数）
    EX_JUMP_FWD,        // 正向移动（不产生参数）
    EX_JUMP_BACK,       // 负向移动（不产生参数）
    EX_FIND_FWD,        // 正向查找（不产生参数）
    EX_FIND_REV         // 反向查找（不产生参数）
} extractor_op_type_t;

typedef struct {
    extractor_op_type_t type;
    union {
        struct { size_t length; } capture_len;      // EX_CAPTURE_LEN
        struct { char ch; } capture_chr;            // EX_CAPTURE_CHR
        struct { size_t length; } skip_len;         // EX_SKIP_LEN
        struct { size_t pos; } jump_abs;            // EX_JUMP_ABS
        struct { int is_end; int offset; } jump_end;// EX_JUMP_END
        struct { size_t offset; } jump_fwd;         // EX_JUMP_FWD
        struct { size_t offset; } jump_back;        // EX_JUMP_BACK
        struct { char ch; } find_fwd;               // EX_FIND_FWD
        struct { char ch; } find_rev;               // EX_FIND_REV
    } data;
} extractor_op_t;
```

---

## 八、错误码定义

| 错误码 | 含义 |
|--------|------|
| `E_INVALID_PATTERN` | 模式格式无效 |
| `E_UNCLOSED_QUOTE` | 未闭合的引号 |
| `E_INVALID_NUMBER` | 无效的数字 |
| `E_INVALID_POSITION` | 无效的位置表达式 |
| `E_EMPTY_SEGMENT` | 空的段模式 |
| `E_NO_LEADING_SLASH` | 模式不以 / 开头 |
| `E_END_CONFLICT` | END 后存在其他操作 |
| `E_END_POSITIVE_OFFSET` | END 与正数相加 |
| `E_END_DUPLICATE` | 同一持有单元内 END 重复 |
| `E_ROUTE_CONFLICT` | 路由特征序列冲突 |

---

## 九、设计原则总结

1. **两阶段编译**：匹配（特征序列）与提取（提取序列）分离
2. **常量与动态分离**：常量操作可相加，动态操作不可相加
3. **HEAD 与 END 为符号**：必须显式出现，不可省略
4. **状态机驱动**：特征序列编译使用简洁的两状态状态机
5. **关键字合并**：关键字与当前 HOLD 元组合并后输出
6. **动态打断**：动态操作触发当前 HOLD 输出
7. **END 约束**：只能与负数相加，纯 END 只能在末尾
8. **HEAD 约束**：最终结果必须为 `HEAD + n`，`n ≥ 0`
9. **保留语义**：提取序列保留完整操作语义用于参数提取
10. **匹配优化**：匹配操作转换为常量偏移，避免重复验证
11. **常量合并**：连续常量移动在提取序列中合并，减少运行时操作

---

**文档版本**：2.2  
**更新日期**：2026-04-14  

**主要变更**：
- 区分常量操作与动态操作
- HEAD 和 END 作为显式符号标志，不可省略
- 明确常量相加规则与动态打断规则
- 明确关键字合并规则
- 更新数据结构定义以支持六种特征元组类型
- 增加连续关键字的处理说明
- **5.3 转换规则优化**：匹配操作转换为 `EX_SKIP_LEN`（常量偏移），移除 `EX_SKIP_MATCH`
- **5.4 新增编译时优化**：常量移动合并规则与示例
- **更新提取操作类型**：精简为 10 种，明确标注是否产生参数