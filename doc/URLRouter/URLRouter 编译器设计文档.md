# URLRouter 编译器设计文档

---

## 一、概述

编译器将路由模式字符串转换为两个独立的数据结构：

1. **特征序列**：用于路由匹配阶段，包含移动操作和关键字
2. **提取序列**：用于参数提取阶段，保留完整的捕获语义

```
模式字符串 → 词法分析 → 操作符序列 → 特征序列编译 → 特征序列
                              ↓
                        提取序列编译 → 提取操作序列
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
| **HOLD** | 持有一个移动操作，等待合并或输出 |

#### 事件类型

| 事件 | 触发操作符 | 数据 |
|------|-----------|------|
| **CONST** | `OP_CAPTURE_LEN`、`OP_JUMP_ABS`、`OP_JUMP_FWD`、`OP_JUMP_BACK` | 数值（可为负数） |
| **KEY_END** | `OP_JUMP_END` | END 表达式 |
| **VAR** | `OP_CAPTURE_CHR`、`OP_FIND_FWD`、`OP_FIND_REV` | 字符 + 方向 |
| **KW** | `OP_MATCH` | 字符串 |

#### 状态转换表

| 当前状态 | 事件 | 动作 | 下一状态 |
|---------|------|------|---------|
| IDLE | CONST | 持有 = 数值 | HOLD |
| IDLE | KEY_END | 持有 = END | HOLD |
| IDLE | VAR | 持有 = 查询目标 | HOLD |
| IDLE | KW | 输出 `(0, kw)` | IDLE |
| HOLD | CONST | 若可相加：持有 += 值<br>否则：输出 `(持有, NULL)`，持有 = 值 | HOLD |
| HOLD | KEY_END | 若持有无 END：持有 = END + 值<br>若有 END：报错 | HOLD |
| HOLD | VAR | 输出 `(持有, NULL)`，持有 = 查询目标 | HOLD |
| HOLD | KW | 输出 `(持有, kw)`，清空持有 | IDLE |
| 扫描结束 | — | 若在 HOLD：输出 `(持有, NULL)` | IDLE |

#### 相加规则

| 持有类型 | 可相加事件 | 结果 | 约束 |
|---------|-----------|------|------|
| 数值 | CONST | 数值相加 | 无 |
| END | CONST | END + 值 | 值必须为负数 |
| END | CONST（正数） | — | 报错 |
| 查询目标 | 任何 | — | 不可相加，先输出再处理 |

### 4.2 输出元组类型

状态机输出的元组直接对应特征序列：

| 输出形式 | 特征类型 | 说明 |
|---------|---------|------|
| `(n, NULL)` | `FT_ABS_MATCH` | 移动到 HEAD+n |
| `(n, "kw")` | `FT_ABS_MATCH` | 移动到 HEAD+n 后匹配 kw |
| `(END, NULL)` | `FT_END_MATCH` | 移动到 END |
| `(END, "kw")` | `FT_END_MATCH` | 移动到 END 后匹配 kw |
| `(END-n, NULL)` | `FT_END_MATCH` | 移动到 END-n |
| `(END-n, "kw")` | `FT_END_MATCH` | 移动到 END-n 后匹配 kw |
| `('c', NULL)` | `FT_FIND_MATCH` | 向结尾查找 'c' |
| `('c', "kw")` | `FT_FIND_MATCH` | 查找 'c' 后匹配 kw |
| `('<', 'c', NULL)` | `FT_FIND_REV_MATCH` | 向开头查找 'c' |
| `('<', 'c', "kw")` | `FT_FIND_REV_MATCH` | 反向查找 'c' 后匹配 kw |

### 4.3 编译示例

#### 示例1：基础偏移合并

```
输入：${1}${1}${1}$'key'

状态机：
  IDLE + CONST(1) → HOLD(1)
  HOLD + CONST(1) → HOLD(2)
  HOLD + CONST(1) → HOLD(3)
  HOLD + KW("key") → 输出 (3, "key") → IDLE

输出：[(3, "key")]
```

#### 示例2：查询操作

```
输入：${'a'}${'b'}$'c'

状态机：
  IDLE + VAR('a') → HOLD('a')
  HOLD + VAR('b') → 输出 ('a', NULL)，HOLD('b')
  HOLD + KW("c") → 输出 ('b', "c") → IDLE

输出：[('a', NULL), ('b', "c")]
```

#### 示例3：END 合并

```
输入：${}$[<4]$'dddd'

状态机：
  IDLE + KEY_END(END) → HOLD(END)
  HOLD + CONST(-4) → HOLD(END-4)
  HOLD + KW("dddd") → 输出 (END-4, "dddd") → IDLE

输出：[(END-4, "dddd")]
```

#### 示例4：混合场景

```
输入：${2}${'a'}${3}$'b'

状态机：
  IDLE + CONST(2) → HOLD(2)
  HOLD + VAR('a') → 输出 (2, NULL)，HOLD('a')
  HOLD + CONST(3) → 输出 ('a', NULL)，HOLD(3)
  HOLD + KW("b") → 输出 (3, "b") → IDLE

输出：[(2, NULL), ('a', NULL), (3, "b")]
```

#### 示例5：END 位置约束（报错）

```
输入：$[END]${'a'}

状态机：
  IDLE + KEY_END(END) → HOLD(END)
  HOLD + VAR('a') → 尝试输出 (END, NULL) 然后 HOLD('a')
  
  ❌ 错误：纯 END 元组后不能有其他操作
```

---

## 五、提取序列编译

### 5.1 设计原则

提取序列保留原始操作符的完整语义，用于参数提取：

- **保留捕获边界**：每个捕获操作独立存在
- **转换匹配操作**：匹配操作转换为跳过操作
- **保留移动操作**：移动操作用于指针调整

### 5.2 提取操作类型

```c
typedef enum {
    EX_CAPTURE_LEN,     // 定长捕获
    EX_CAPTURE_CHR,     // 捕获到字符
    EX_CAPTURE_END,     // 捕获到结尾
    EX_SKIP_LEN,        // 跳过固定长度
    EX_SKIP_MATCH,      // 跳过匹配字符串
    EX_JUMP_ABS,        // 绝对跳转
    EX_JUMP_END,        // END 跳转
    EX_JUMP_FWD,        // 正向移动
    EX_JUMP_BACK,       // 负向移动
    EX_FIND_FWD,        // 正向查找
    EX_FIND_REV         // 负向查找
} extractor_op_type_t;
```

### 5.3 转换规则

| 操作符 | 提取操作 | 产生参数 |
|--------|---------|---------|
| `OP_CAPTURE_LEN` | `EX_CAPTURE_LEN` | 是 |
| `OP_CAPTURE_CHR` | `EX_CAPTURE_CHR` | 是 |
| `OP_CAPTURE_END` | `EX_CAPTURE_END` | 是 |
| `OP_MATCH` | `EX_SKIP_MATCH` | 否 |
| `OP_JUMP_ABS` | `EX_JUMP_ABS` | 否 |
| `OP_JUMP_END` | `EX_JUMP_END` | 否 |
| `OP_JUMP_FWD` | `EX_JUMP_FWD` | 否 |
| `OP_JUMP_BACK` | `EX_JUMP_BACK` | 否 |
| `OP_FIND_FWD` | `EX_FIND_FWD` | 否 |
| `OP_FIND_REV` | `EX_FIND_REV` | 否 |

### 5.4 编译示例

#### 示例1：基础捕获

```
输入：${4}$'a'

操作符序列：
  [0] OP_CAPTURE_LEN, length=4
  [1] OP_MATCH, text="a"

提取序列：
  [0] EX_CAPTURE_LEN, length=4  (参数1)
  [1] EX_SKIP_MATCH, text="a"   (不产生参数)

参数数量：1
```

#### 示例2：捕获到字符

```
输入：${'='}$'='${}

操作符序列：
  [0] OP_CAPTURE_CHR, ch='='
  [1] OP_MATCH, text="="
  [2] OP_CAPTURE_END

提取序列：
  [0] EX_CAPTURE_CHR, ch='='    (参数1)
  [1] EX_SKIP_MATCH, text="="   (不产生参数)
  [2] EX_CAPTURE_END            (参数2)

参数数量：2
```

#### 示例3：带移动操作

```
输入：${2}$[>'=']$'='${}

操作符序列：
  [0] OP_CAPTURE_LEN, length=2
  [1] OP_FIND_FWD, ch='='
  [2] OP_MATCH, text="="
  [3] OP_CAPTURE_END

提取序列：
  [0] EX_CAPTURE_LEN, length=2  (参数1)
  [1] EX_FIND_FWD, ch='='       (不产生参数)
  [2] EX_SKIP_MATCH, text="="   (不产生参数)
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
    extractor_op_t *extractors;     // 提取序列
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
  IDLE + KW("v") → 输出 (0, "v") → IDLE
  IDLE + VAR('.') → HOLD('.')
  HOLD + KW(".") → 输出 ('.', ".") → IDLE
  IDLE + KEY_END(END) → HOLD(END)
  扫描结束 → 输出 (END, NULL)

输出：[(0, "v"), ('.', "."), (END, NULL)]
```

#### 提取序列编译（段1）
```
[0] EX_SKIP_MATCH, text="v"
[1] EX_CAPTURE_CHR, ch='.'    (参数1)
[2] EX_SKIP_MATCH, text="."
[3] EX_CAPTURE_END            (参数2)
```

#### 最终结果
```
特征序列:
  段0: [(0, "api")]
  段1: [(0, "v"), ('.', "."), (END, NULL)]

提取序列:
  段0: [EX_SKIP_MATCH("api")]
  段1: [EX_SKIP_MATCH("v"), EX_CAPTURE_CHR('.'), EX_SKIP_MATCH("."), EX_CAPTURE_END]

参数数量: 2
```

---

## 七、数据结构定义汇总

### 7.1 特征序列

```c
typedef enum {
    FT_ABS_MATCH,       // 绝对位置匹配: (value, keyword)  HEAD + value
    FT_END_MATCH,       // END 偏移匹配: (value, keyword)  value = -n 表示 END-n
    FT_FIND_MATCH,      // 查找匹配: (ch, keyword) 向结尾查找
    FT_FIND_REV_MATCH   // 反向查找匹配: (ch, keyword) 向开头查找
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
    EX_CAPTURE_LEN,
    EX_CAPTURE_CHR,
    EX_CAPTURE_END,
    EX_SKIP_LEN,
    EX_SKIP_MATCH,
    EX_JUMP_ABS,
    EX_JUMP_END,
    EX_JUMP_FWD,
    EX_JUMP_BACK,
    EX_FIND_FWD,
    EX_FIND_REV
} extractor_op_type_t;

typedef struct {
    extractor_op_type_t type;
    union {
        struct { size_t length; } capture_len;
        struct { char ch; } capture_chr;
        struct { const char *text; size_t len; } skip_match;
        struct { size_t pos; } jump_abs;
        struct { int is_end; int offset; } jump_end;
        struct { size_t offset; } jump_fwd;
        struct { size_t offset; } jump_back;
        struct { char ch; } find_fwd;
        struct { char ch; } find_rev;
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
2. **状态机驱动**：特征序列编译使用简洁的两状态状态机
3. **偏移合并**：连续的常量移动合并为单一绝对位置
4. **查询独立**：查询操作作为独立锚点，不可合并
5. **END 约束**：只能与负数相加，纯 END 只能在末尾
6. **保留语义**：提取序列保留完整操作语义用于参数提取