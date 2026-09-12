# Stride 编译器设计文档

> **实现位置（2026-09-11）**：语法与编译器已从 Stride 迁入 URLRouter 本仓库，
> 实现见 `include/pattern.h`、`src/pattern.c`（词法）与 `src/pattern_compile.c`（编译）。
> 编译产物不再是自己定义的特征/提取操作数组，而是 Stride 的步进序列
> （`stride_seq_t`，见 `include/stride/sequence.h`）。本文档描述的模式语法与
> 编译语义仍然有效，类型与函数名请以当前头文件为准。

**文档版本**：2.0
**更新日期**：2026-09-12
**适用模块**：`stride/compiler.h`（词法分析与编译编排）、`stride/matcher.h`、`stride/extractor.h`

---

> **输入模型**：Stride 只编译**单个段（segment，不透明二进制）**的模式；按 `/` 等分隔符切分输入由调用方负责，
> 多个段分别编译、分别执行。文中复合模式示例里的 `/` 即调用方切分边界。
>
> **单位**：步长的单位是**比特**（每步跨越多少比特）；位置与偏移以**步**计。
> 字面量长度、段长、参数长度均以**比特**计（`1 字节 = 8 比特`）。见
> [`Stride 段模式语法规范.md`](Stride%20段模式语法规范.md) 第七节。
>
> **文档划分**：本文档描述**编译器的输入与产出**——词法分析、操作符 IR、编译编排、数据结构与错误码。
> 编译期状态机（匹配序列的 IDLE/HOLD、提取序列的常量合并）见
> [`Stride 状态机设计文档.md`](Stride%20状态机设计文档.md)。
>
> **模块归属**：`stride/compiler.h` 同时承担词法分析与编译编排：`stride_lex()` 提供模式 →
> 操作符序列接口，`stride_compile()` 提供一步编译入口。

---

## 一、概述

编译器将**单个段**的模式转换为两个独立的数据结构：

1. **匹配序列**：用于匹配阶段，包含步进操作与比对字面量
2. **提取序列**：用于参数提取阶段，保留完整的捕获语义

```
段模式 → 词法分析 → 操作符序列 ─┬─ 匹配序列编译（状态机）→ 匹配序列
                              └─ 提取序列编译（状态机）→ 提取序列
```

两个产物彼此独立：调用方既可以只用匹配序列做纯匹配（不关心捕获），也可以只用提取序列在匹配成功之后做参数提取。

编译器**不解释数据内容**：字面量是任意二进制比特串，移动以步（= 步长个比特）为单位。
编译器也**不需要知道步长**：产物中只记录步数与比特长度；步长在执行期传入并校验。

---

## 二、词法分析

### 2.1 操作符识别规则

词法分析器将段模式切分为操作符令牌序列。**语法字符恒为单字节 ASCII**；
引号内的字面量是任意二进制串（含转义），与步长无关。

| 操作符 | 抽象模式 | 示例 |
|--------|---------|------|
| 精确比对 | `\$'(字面量)'` | `$'user'`、`$'用户'` |
| 定步捕获 | `\$\{[\d]+\}` | `${4}` |
| 捕获到定界串 | `\$\{'(字面量)'\}` | `${'='}`、`${'：'}` |
| 捕获到段尾 | `\$\{\}` | `${}` |
| 绝对定位 | `\$\[[\d]+\]` | `$[5]` |
| 绝对定位(END) | `\$\[END(?:-[\d]+)?\]` | `$[END]`、`$[END-4]` |
| 向段尾移动 | `\$\[>[\d]+\]` | `$[>3]` |
| 向段首移动 | `\$\[<[\d]+\]` | `$[<2]` |
| 向段尾查找 | `\$\[>'(字面量)'\]` | `$[>'=']`、`$[>'：']` |
| 向段首查找 | `\$\[<'(字面量)'\]` | `$[<'=']` |

> **`(字面量)` 不是单个字符**：它是引号之间的任意二进制串，可含 `\\`、`\'`、`\xNN`
> 转义（见 2.2）。因此 `$[>'：']`、`${'用户名'}` 都是合法操作符。
>
> 数值操作数（`${n}`、`$[n]`、`$[>n]`、`$[<n]`、`$[END-n]`）的单位一律是**步**。
> 步长仅影响「一步等于多少比特」，词法分析不需要它。

### 2.2 字面量与转义

引号内内容按源码原样取字节，仅识别以下转义：

| 转义 | 结果 |
|------|------|
| `\\` | `0x5C` |
| `\'` | `0x27` |
| `\xNN` | 一个任意字节 |

- 未转义的字节直接进入字面量，因此源码中的 `$'中文'` 天然是 24 比特字面量（UTF-8）。
- 词法器**不解释编码**，只做比特切片；字面量的 `bit_len = 8 × 字节数`。
- 需要匹配含 `0x00` 的模式时，使用长度化入口：`stride_lex(pattern, pattern_len, ...)`
  与 `stride_compile_ex(pattern, pattern_len, stride)`；`pattern_len == 0` 时按 `'\0'` 结尾处理。

### 2.3 错误处理

| 错误类型 | 示例 | 处理 |
|---------|------|------|
| 未闭合引号 | `$'user` | 编译失败 |
| 空定界串 | `${''}`、`$[>'']` | 编译失败 |
| 无效数字 | `${abc}` | 编译失败 |
| 无效位置 | `$[abc]` | 编译失败 |
| 空模式 | `""` | 编译失败（`STRIDE_E_EMPTY_SEGMENT`） |
| 游离数据（不以 `$` 起始，且不在引号内） | `$'a'/${}` 中的 `/` | 编译失败 |
| 字面量比特长度不是步长整数倍（编译期已知步长时） | 步长 16 时 `$'abc'`（24 比特） | 编译失败（`STRIDE_E_ALIGN`） |

> **`/` 不是非法字符，“分隔符”也不是语法概念**：模式中的每一条指令都必须以 `$` 起始。
> `/` 出现在引号内时只是字面量数据，例如 `$'////'` 是**合法模式**（比对 4 个斜杠数据）；
> 而 `$'a'/${}` 失败的原因是 `/` 是**游离数据**、不成其为一条指令，与它是不是“分隔符”无关。
> 所谓“与分隔符无关”指的是**调用方如何切分输入**，并不是禁止 `/` 出现在模式里。

---

## 三、操作符序列

### 3.1 操作符类型定义

```c
/* stride/types.h */

/* 二进制数据 + 比特长度（1 字节 = 8 比特） */
typedef struct {
    const void *data;    /* 数据：指向 pattern 内部，不拥有 */
    size_t      bit_len; /* 比特长度 */
} stride_blob_t;

/* 字节数 → 比特数 */
#define STRIDE_BITS(nbytes) ((size_t)(nbytes) * 8u)

typedef enum {
    STRIDE_OP_MATCH = 0,     /* 精确比对 $'字面量' */
    STRIDE_OP_CAPTURE_STEPS, /* 定步捕获 ${n} */
    STRIDE_OP_CAPTURE_UNTIL, /* 捕获到定界串 ${'S'} */
    STRIDE_OP_CAPTURE_END,   /* 捕获到段尾 ${} */
    STRIDE_OP_JUMP_ABS,      /* 绝对定位 $[n] */
    STRIDE_OP_JUMP_END,      /* END 定位 $[END] / $[END-n] */
    STRIDE_OP_JUMP_FWD,      /* 向段尾移动 $[>n] */
    STRIDE_OP_JUMP_BACK,     /* 向段首移动 $[<n] */
    STRIDE_OP_FIND_FWD,      /* 向段尾查找 $[>'S'] */
    STRIDE_OP_FIND_REV       /* 向段首查找 $[<'S'] */
} stride_op_type_t;

typedef struct {
    stride_op_type_t type;
    union {
        stride_blob_t literal;   /* MATCH / CAPTURE_UNTIL / FIND_FWD / FIND_REV */
        size_t        steps;     /* CAPTURE_STEPS / JUMP_ABS / JUMP_FWD / JUMP_BACK */
        struct { int is_end; size_t back_steps; } jump_end; /* JUMP_END */
    } data;
} stride_op_t;
```

**所有权说明**：携带字面量的操作符（`MATCH` / `CAPTURE_UNTIL` / `FIND_FWD` / `FIND_REV`）
**拥有**词法阶段解码后的比特串（`\\`、`\'`、`\xNN` 转义已展开）；
操作符数组由调用方通过 `stride_ops_free(ops, count)` 释放，`pattern` 本身不要求继续有效。

**单位说明**：`steps` 一律为**步数**（非负）；`literal.bit_len` 一律为**比特数**。

### 3.2 编译示例

```
输入: /$'user'/${}
      （以 '/' 为调用方切分边界）

段0: $'user'   → STRIDE_OP_MATCH, literal.bit_len = 32
段1: ${}       → STRIDE_OP_CAPTURE_END
```

---

## 四、完整编译流程

### 4.1 编译入口

词法分析（`stride/compiler.h` 的 `stride_lex`）负责模式 → 操作符序列：

```c
/* stride/compiler.h */

/**
 * 词法分析：段模式 → 操作符序列
 * @param pattern       段模式（单个段）
 * @param pattern_len   模式字节长度；0 表示按 '\0' 结尾
 * @param out_ops       输出操作符数组（调用方通过 stride_ops_free(ops, count) 释放）
 * @param out_count     输出操作符数量
 * @param out_capacity  输出数组容量
 * @return 0 成功，-1 失败
 */
int stride_lex(const void *pattern, size_t pattern_len,
               stride_op_t **out_ops, size_t *out_count, size_t *out_capacity);

/** 释放操作符数组 */
void stride_ops_free(stride_op_t *ops, size_t count);
```

一步编译入口（`stride/compiler.h`）负责 模式 → 两份产物：

```c
/* stride/compiler.h */

typedef struct {
    stride_match_op_t     *match;         /* 匹配序列（匹配阶段用） */
    size_t                 match_count;
    stride_extractor_op_t *extract;       /* 提取序列（已优化，参数提取用） */
    size_t                 extract_count;
    size_t                 param_count;   /* 参数数量 */
    stride_status_t        status;
    const char            *error_msg;
    size_t                 error_pos;
} stride_compile_result_t;

/**
 * 编译入口：词法分析 → 操作符序列 → 匹配序列 + 提取序列
 * @param pattern     段模式（单个段）
 * @param pattern_len 模式字节长度；0 表示按 '\0' 结尾
 * @param stride      编译期步长（比特/步）；0 表示未知（不校验对齐），≥1 表示已知并校验
 * @return 编译结果；失败时 status != STRIDE_OK
 */
stride_compile_result_t stride_compile_ex(const void *pattern, size_t pattern_len,
                                          size_t stride);

/** 便捷入口：等价于 stride_compile_ex(pattern, 0, 0) */
stride_compile_result_t stride_compile(const void *pattern);

/** 释放编译结果（匹配序列与提取序列） */
void stride_compile_free(stride_compile_result_t *result);
```

状态码使用统一命名空间 `stride_status_t`：成功为 `STRIDE_OK`，各类失败为 `STRIDE_E_*`；
可调用 `stride_status_str()` 取得可读描述。

### 4.2 完整示例

#### 输入
```
模式: /$'api'/$'v'${'.'}$'.'${}
输入:  /api/v2.0
      （以 '/' 为调用方切分边界，此处仅编译段1；步长 8 = 1 字节/步）
```

#### 词法分析
```
段0: $'api'
段1: $'v' ${'.'} $'.' ${}
```

#### 操作符序列
```
段0:
  [0] STRIDE_OP_MATCH, literal="api" (24 比特)

段1:
  [0] STRIDE_OP_MATCH, literal="v" (8 比特)
  [1] STRIDE_OP_CAPTURE_UNTIL, literal="." (8 比特)
  [2] STRIDE_OP_MATCH, literal="." (8 比特)
  [3] STRIDE_OP_CAPTURE_END
```

#### 匹配序列编译（段1）
状态机细节见[状态机设计文档](Stride%20状态机设计文档.md)第二节。

```
  IDLE + LITERAL("v") → 输出 (0, "v")
  IDLE + FIND_FWD(".") → HOLD(".", NULL)
  HOLD + LITERAL(".") → 输出 (".", ".")
  IDLE + STEP_ABS_END(END) → HOLD(END, NULL)
  扫描结束 → 输出 (END, NULL)

输出：[(0, "v"), (".", "."), (END, NULL)]
```

#### 提取序列编译（段1）
状态机细节见[状态机设计文档](Stride%20状态机设计文档.md)第四节。

```
基础转换：
  [0] STRIDE_EX_SKIP_STEPS, bit_len=8    // STRIDE_OP_MATCH "v"
  [1] STRIDE_EX_CAPTURE_UNTIL, "."       // STRIDE_OP_CAPTURE_UNTIL
  [2] STRIDE_EX_SKIP_STEPS, bit_len=8    // STRIDE_OP_MATCH "."
  [3] STRIDE_EX_CAPTURE_END

常量合并：无可合并（捕获操作打断）

最终提取序列：
  [0] STRIDE_EX_SKIP_STEPS, bit_len=8    (不产生参数)
  [1] STRIDE_EX_CAPTURE_UNTIL, "."       (参数1)
  [2] STRIDE_EX_SKIP_STEPS, bit_len=8    (不产生参数)
  [3] STRIDE_EX_CAPTURE_END              (参数2)
```

#### 最终结果
```
匹配序列:
  段0: [(0, "api")]
  段1: [(0, "v"), (".", "."), (END, NULL)]

提取序列:
  段0: [STRIDE_EX_SKIP_STEPS(bit_len=24)]
  段1: [STRIDE_EX_SKIP_STEPS(8), STRIDE_EX_CAPTURE_UNTIL("."),
        STRIDE_EX_SKIP_STEPS(8), STRIDE_EX_CAPTURE_END]

参数数量: 2
```

---

## 五、数据结构定义汇总

### 5.1 共享层（`stride/types.h`）

```c
typedef struct {
    const void *data;    /* 数据 */
    size_t      bit_len; /* 比特长度 */
} stride_blob_t;

#define STRIDE_BITS(nbytes) ((size_t)(nbytes) * 8u)
```

### 5.2 匹配序列（`stride/matcher.h`）

```c
typedef enum {
    STRIDE_MT_STEP_FWD = 0, /* 步进向段尾：(n, expect) */
    STRIDE_MT_STEP_BACK,    /* 步进向段首：(−n, expect) */
    STRIDE_MT_ABS_HEAD,     /* 绝对步位置（基于 HEAD）：(HEAD+n, expect) */
    STRIDE_MT_ABS_END,      /* 绝对步位置（基于 END）：(END−n, expect) */
    STRIDE_MT_FIND_FWD,     /* 向段尾查找比特串：("S", expect) */
    STRIDE_MT_FIND_REV      /* 向段首查找比特串：("<", "S", expect) */
} stride_match_type_t;

typedef struct {
    stride_match_type_t type;
    size_t              steps;     /* 非负步数 */
    stride_blob_t       delimiter; /* 查找目标（拥有） */
    stride_blob_t       expect;    /* 比对字面量（拥有） */
} stride_match_op_t;
```

### 5.3 提取序列（`stride/extractor.h`）

```c
typedef enum {
    STRIDE_EX_CAPTURE_STEPS, /* 定步捕获（产生参数） */
    STRIDE_EX_CAPTURE_UNTIL, /* 捕获到定界串（产生参数） */
    STRIDE_EX_CAPTURE_END,   /* 捕获到段尾（产生参数） */
    STRIDE_EX_SKIP_STEPS,    /* 跳过固定步数（不产生参数） */
    STRIDE_EX_JUMP_ABS,      /* 绝对定位（不产生参数） */
    STRIDE_EX_JUMP_END,      /* END 定位（不产生参数） */
    STRIDE_EX_JUMP_FWD,      /* 向段尾移动（不产生参数） */
    STRIDE_EX_JUMP_BACK,     /* 向段首移动（不产生参数） */
    STRIDE_EX_FIND_FWD,      /* 向段尾查找（不产生参数） */
    STRIDE_EX_FIND_REV       /* 向段首查找（不产生参数） */
} stride_extractor_op_type_t;

typedef struct {
    stride_extractor_op_type_t type;
    union {
        struct { size_t steps; }        capture_steps; /* 定步捕获 */
        stride_blob_t                   capture_until; /* 捕获到定界串 */
        struct { size_t steps; }        skip_steps;    /* 跳过固定步数 */
        struct { size_t steps; }        jump_abs;      /* 绝对定位 */
        struct { int is_end; size_t back_steps; } jump_end; /* END 定位 */
        struct { size_t steps; }        jump_fwd;      /* 向段尾移动 */
        struct { size_t steps; }        jump_back;     /* 向段首移动 */
        stride_blob_t                   find_fwd;      /* 向段尾查找 */
        stride_blob_t                   find_rev;      /* 向段首查找 */
    } data;
} stride_extractor_op_t;

/* 提取参数：零拷贝的（指针，比特长度）对 */
typedef struct {
    const void *ptr;
    size_t      bit_len;
} stride_param_t;
```

---

## 六、错误码定义

状态码类型为 `stride_status_t`，可通过 `stride_status_str()` 获取可读字符串。

| 状态码 | 含义 |
|--------|------|
| `STRIDE_OK` | 编译成功 |
| `STRIDE_E_INVALID_PATTERN` | 模式格式无效（词法 / 语法错误）|
| `STRIDE_E_EMPTY_SEGMENT` | 空的段模式 |
| `STRIDE_E_ALIGN` | 步长对齐错误：段比特长度或字面量比特长度不是步长的整数倍 |

---

## 七、设计原则总结

1. **两阶段编译**：匹配（匹配序列）与提取（提取序列）分离
2. **字节无关性**：数据与字面量都是不透明二进制，编译器不解释内容
3. **比特为单位**：步长是「每步比特数」；长度与偏移以比特 / 步计量
4. **步长无关性**：产物只记录步数与比特长度，步长在编译期可选、在执行期生效
5. **步进与查找分离**：步进可相加，查找不可相加（详见状态机文档）
6. **HEAD 与 END 为符号**：必须显式出现，不可省略
7. **字面量驱动**：只有字面量参与序列区分
8. **比对优化**：提取阶段把比对字面量转为定步跳过，避免重复验证
9. **常量合并**：连续常量步进在提取序列中合并，减少运行时操作
10. **对齐约束**：段长与字面量长度必须是步长整数倍
11. **单段编译**：一次编译只针对一个段；跨段的切分与组织属于调用方职责

---

**文档版本**：2.0
**更新日期**：2026-09-12
