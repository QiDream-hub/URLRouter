# URLRouter 编译器设计文档

> **本文档描述 URLRouter 的「语法 + 编译器」层**：把单个段的模式字符串
> （`$'…'` / `${…}` / `$[…]`）词法分析为操作符序列，再翻译成对 Stride
> 序列构建函数的调用，产出一个匹配序列与一个提取序列。
>
> **事实来源**：`include/pattern.h`、`src/pattern.c`（词法）、`src/` 中的编译器
> 编译单元（`url_compile` 的实现）；Stride 侧为
> `third_party/Stride/include/stride/{types,sequence,matcher,extractor}.h`
> 与 `third_party/Stride/README.md`。文中所有函数签名、字段名与单位约定均与
> 这些头文件一致。

---

## 一、概述与分工

### 1.1 背景：2026-09-11 的重构

Stride 是独立仓库，以 git submodule 的形式位于 `third_party/Stride`。重构之后，
**Stride 不再含语法、不含编译器**，它是纯粹的**步进式比特串序列库**：

- **序列构建**：函数式的构建函数，每次调用把一个新的偏移或动作追加到**尾节点**，
  能合并就地合并，否则新建节点；没有状态机。
- **通用执行引擎**：`stride_seq_run()`，以及匹配/提取两个便捷入口。

**语法与编译器全部由 URLRouter 承担**（`include/pattern.h`、`src/pattern.c` 与
`src/pattern_compile.c`）：识别 `$'…'` / `${…}` / `$[…]`，并把它们翻译成对
`stride_seq_*()` 构建函数的调用。

| 职责 | Stride | URLRouter |
|------|--------|-----------|
| 段模式语法（词法） | — | ✅ `url_lex()` |
| 模式 → 序列的翻译（编译器） | — | ✅ `url_compile()` |
| 序列构建（函数 + 尾节点合并） | ✅ `stride_seq_*()` | 调用方 |
| 匹配 / 提取执行引擎 | ✅ `stride_seq_run()` / `stride_match_run()` / `stride_extract_run()` | 调用方 |
| 路由树、匹配序列合并、优先级、按分隔符切分 | — | ✅ |

### 1.2 流水线

```
"$'v'${'.'}$'.'${}"
     │  url_lex()        词法分析（URLRouter，src/pattern.c）
     ▼
url_op_t 操作符序列（URLRouter 的 IR）
     │  url_compile()     编译器（URLRouter）
     ▼
Stride 构建函数调用（stride_seq_*）→ stride_seq_t 匹配序列 + stride_seq_t 提取序列
     │  stride_match_run() / stride_extract_run()   ← Stride 通用执行引擎
     ▼
命中判定 / 参数
```

要点：

- 词法与编译是**两个独立阶段**：`url_lex()` 只做切分与转义展开，不关心 Stride；
  `url_compile()` 只做「操作符 → 构建函数调用」的机械翻译，不做任何常量运算——
  常量合并交给 Stride 的尾节点合并（见第七节）。
- **一次编译产出两个序列**：匹配序列供匹配阶段快速判定命中，提取序列在命中之后
  按需执行以取得参数。两者是同一个 `stride_seq_t` 结构的不同用法，共用同一套执行引擎。
- 编译器只编译**单个段**。按 `/` 等分隔符把 URL 切成段由调用方（`router.c`）负责，
  各段分别编译、分别执行。

---

## 二、URLRouter 的方向：字节导向

### 2.1 步长固定为 8

```c
/** URLRouter 的固定步长：1 步 = 1 字节 */
#define URL_PATTERN_STRIDE 8u
```

URLRouter 是**字节导向**的：模式中的长度与偏移单位就是**字节**。步长 8 意味着
**1 步 = 1 字节**，因此：

- `${4}` 捕获 4 个**字节**；`${2}` 捕获 2 个字节；
- `$[3]` 定位到段内第 3 个**字节**处（`HEAD + 3`）；
- `$[END-4]` 定位到段尾前 4 个字节处；
- `$[>2]` / `$[<2]` 前进 / 后退 2 个字节；
- `${n}`、`$[n]`、`$[>n]`、`$[<n]`、`$[END-n]` 中的 `n` 一律按**字节**解释。

字面量仍以比特描述，因为 Stride 的统一目标是**任意比特串**：`stride_blob_t.bit_len`
是比特长度，字节字节串在进入 Stride 之前一律乘以 8。

### 2.2 单位对照

| 量 | 单位 | 说明 |
|----|------|------|
| 步长 `stride` | 比特/步 | 固定为 `URL_PATTERN_STRIDE` = 8 |
| 模式中的长度/偏移 `n` | 字节 | 因为 1 步 = 1 字节，字节数即步数 |
| 字面量 `stride_blob_t.bit_len` | 比特 | 词法层写入 `字节数 × 8` |
| 段长 `segment_bit_len` | 比特 | 执行期传入 `STRIDE_BITS(字节数)` |
| 参数 `stride_param_t.steps` | **步** | Stride 侧以步计；URLRouter 步长恒为 8，故 1 步 = 1 字节 |
| `stride_seq_skip_bits()` 的入参 | 比特 | 唯一以比特计的偏移（提取阶段跳过已验证字面量） |

### 2.3 字节 ↔ 比特换算

Stride 提供换算宏：

```c
/** 字节数 → 比特数 */
#define STRIDE_BITS(nbytes) ((size_t)(nbytes) * 8u)
```

- 词法层：`out->bit_len = n * 8;`（`n` 为转义展开后的字节数）。
- 执行层：段比特长度 = `STRIDE_BITS(段字节数)`；段比特长度必须是步长的整数倍，
  否则 `stride_seq_run()` 直接返回失败（对 URLRouter 而言即字节数天然满足）。
- 匹配阶段的字面量比对在运行期会检查 `bit_len % stride == 0`，所以编译器在编译期
  就对字面量做同样的对齐校验（见 6.11）。

---

## 三、词法分析 `url_lex()`

### 3.1 接口

```c
int url_lex(const void *pattern, size_t pattern_len, url_op_t **out_ops,
            size_t *out_count, size_t *out_capacity);
```

| 参数 | 方向 | 说明 |
|------|------|------|
| `pattern` | 入 | 模式（单个段的内容），不拥有 |
| `pattern_len` | 入 | 字节长度；`0` 表示按 `'\0'` 结尾 |
| `out_ops` | 出 | 操作符数组（用 `url_ops_free` 释放） |
| `out_count` | 出 | 操作符数量 |
| `out_capacity` | 出 | 数组容量（≥ `out_count`） |

返回值：`0` 成功，`-1` 失败（语法错误或分配失败）。

实现使用一个 `cursor_t { const unsigned char *p; const unsigned char *end; }`
维护读取位置，逐条指令识别。数组初始容量为 `URL_LEX_INITIAL_CAPACITY`（16），
每次按 2 倍增长；新槽位以 `memset` 清零，保证未被赋值的 union 成员为 0
（例如无文本操作符的 `data.literal` 是 `{NULL, 0}`）。解析失败时以
`url_ops_free(ops, count + 1)` 释放（`count` 槽位可能已分配了文本但尚未计数），
并把三个输出参数清为 `NULL` / `0` / `0`。

### 3.2 词法表

每条指令**必须以 `$` 起始**。共 11 种书写形式，映射到 10 个 `url_op_type_t` 枚举值：

| 语法 | 操作符 | 语义 |
|------|--------|------|
| `$'文本'` | `URL_OP_MATCH` | 精确比对文本（文本可为多字节） |
| `${n}` | `URL_OP_CAPTURE_STEPS` | 捕获 n 字节 |
| `${'文本'}` | `URL_OP_CAPTURE_UNTIL` | 捕获到该文本之前（不含该文本） |
| `${}` | `URL_OP_CAPTURE_END` | 捕获到段尾 |
| `$[n]` | `URL_OP_JUMP_ABS` | 绝对定位到第 n 字节 |
| `$[END]` | `URL_OP_JUMP_END` | 定位到段尾（`is_end = 1`，`back_steps = 0`） |
| `$[END-n]` | `URL_OP_JUMP_END` | 定位到段尾前 n 字节（`is_end = 1`，`back_steps = n`） |
| `$[>n]` | `URL_OP_JUMP_FWD` | 向段尾前进 n 字节 |
| `$[<n]` | `URL_OP_JUMP_BACK` | 向段首后退 n 字节 |
| `$[>'文本']` | `URL_OP_FIND_FWD` | 向段尾查找文本，落点为其首字节 |
| `$[<'文本']` | `URL_OP_FIND_REV` | 向段首查找文本，落点为其首字节 |

识别细节（与实现一致）：

- `$` 之后紧跟 `'` → `MATCH`，走引号扫描。
- `$` 之后是 `{`：再看下一个字符——`}` → `CAPTURE_END`；`'` → `CAPTURE_UNTIL`
  （文本后必须紧跟 `}`）；十进制数字 → `CAPTURE_STEPS`（数值不得为 0，后必须紧跟 `}`）。
- `$` 之后是 `[`：先处理 `>` 与 `<` 两个前缀——
  - `>` 后是 `'` → `FIND_FWD`；`>` 后是数字 → `JUMP_FWD`；末尾必须紧跟 `]`。
  - `<` 后是 `'` → `FIND_REV`；`<` 后是数字 → `JUMP_BACK`；末尾必须紧跟 `]`。
  - 否则若剩余长度 ≥ 3 且为字面 `END` → `JUMP_END`（`is_end = 1`）；其后可跟
    `-数字` 写入 `back_steps`；末尾必须紧跟 `]`。
  - 否则以十进制数字开头 → `JUMP_ABS`；末尾必须紧跟 `]`。
- `$[>0]`、`$[<0]`、`$[0]`、`$[END-0]` 均为合法（数值 0 被接受）；
  只有 `${0}` 被拒绝——长度为 0 的定长捕获没有意义（`CAPTURE_END` 已覆盖该语义）。
- 语法字符（`$ ' { } [ ] < > -`、`END`）恒为单字节 ASCII；数字以十进制累加，
  实现不做上界与溢出检查。

### 3.3 `pattern_len` 的两种模式

- `pattern_len == 0`：按 `'\0'` 结尾处理，用 `strlen()` 求出长度。适合普通 C 字符串。
- `pattern_len != 0`：严格按给定字节数扫描，**允许模式中含 `'\0'` 字节**——此时
  `'\0'` 属于游离数据（不以 `$` 起始）会导致词法失败，但模式整体是长度化的，
  不依赖结尾符。
- 此外，转义 `\0` 与 `\x00` 可以把 NUL 字节写进**字面量内部**，因此即使使用
  `'\0'` 结尾模式，也能匹配含 NUL 的文本。

### 3.4 字面量与转义

引号内按**字节**读取，遇到 `'` 结束。转义表：

| 转义 | 结果字节 |
|------|----------|
| `\\` | `0x5C` |
| `\'` | `0x27` |
| `\n` | `0x0A` |
| `\t` | `0x09` |
| `\r` | `0x0D` |
| `\0` | `0x00` |
| `\xNN` | 一个任意字节（`NN` 必须是两个十六进制字符） |

- 未转义的字节**原样进入**字面量，因此 `$'中文'` 的字面量是 3 个 UTF-8 字节
  （`bit_len = 24`）；词法器不解释任何编码，只做字节切片。
- 文本缓冲区以 `malloc(16)` 起始，容量不足时按 2 倍 `realloc`。
- 引号内允许出现 `$`、`/`、`{` 等字符，一律是普通数据；例如 `$'////'`、
  `$'${}'` 都是合法模式。
- 空文本（`$''`、`${''}`、`$[>'']`）在词法层**是合法的**：`bit_len = 0`。
  注意其后效不在编译期体现，而在运行期：查找类偏移
  （`FIND_FWD` / `FIND_REV`）与 `CAPTURE_UNTIL` 的内部查找要求目标
  `bit_len != 0`，空目标永远查找失败（`CAPTURE_UNTIL` 因此退化为“捕获到段尾”，
  `FIND_*` 节点直接失败）。若需要“捕获到段尾”的语义，应显式书写 `${}`。

### 3.5 错误处理表

`url_lex()` 对所有失败情形统一返回 `-1`（并清空输出）。词法层**不区分**语法错误与
内存不足；`url_compile()` 把 `url_lex()` 的失败一律映射为
`STRIDE_E_INVALID_PATTERN`。

| 情形 | 触发示例 | 结果 |
|------|----------|------|
| 必需参数为 `NULL` | `url_lex(NULL, …)` | -1 |
| 字节不以 `$` 起始（游离数据） | `abc`、`$'a'/`、`$'a'${}x` | -1 |
| 孤立 `$`（模式以 `$` 结尾） | `$'a'$` | -1 |
| `$` 后跟非法引导字符 | `$x`、`$1`、`$-` | -1 |
| 引号未闭合 | `$'abc` | -1 |
| 转义不完整 | `$'a\` | -1 |
| 非法转义字符 | `$'\q'`、`$'\a'` | -1 |
| `\x` 不足两位或非十六进制 | `$'\x0'`、`$'\xZZ'` | -1 |
| `${` 后模式结束 | `${` | -1 |
| `${0}`（定长捕获长度为 0） | `${0}` | -1 |
| `${}` 内既非 `}`、`'`、数字 | `${a}`、`${ }`、`${-1}` | -1 |
| `${n` 缺 `}` | `${2` | -1 |
| `${'文本'` 缺 `}` | `${'x'` | -1 |
| `$[` 后模式结束 | `$[` | -1 |
| `$[>` / `$[<` 后既非 `'` 也非数字 | `$[>]`、`$[>END]` | -1 |
| 位置表达式缺 `]` | `$[3`、`$[>3`、`$[<3` | -1 |
| 查找文本后缺 `]` | `$[>'x'`、`$[<'x'` | -1 |
| `END` 后 `-` 无数字 | `$[END-]` | -1 |
| `$[END` 缺 `]` | `$[END` | -1 |
| `$[` 后为其它内容 | `$[abc]`、`$[-1]`、`$[]` | -1 |
| 分配/扩容失败 | —（`calloc` / `realloc` / `malloc` 返回 NULL） | -1 |

### 3.6 所有权与释放

```c
void url_ops_free(url_op_t *ops, size_t count);
```

- 携带文本的操作符（`URL_OP_MATCH`、`URL_OP_CAPTURE_UNTIL`、`URL_OP_FIND_FWD`、
  `URL_OP_FIND_REV`）**拥有**已展开转义的字节串所有权。
- `url_ops_free()` 遍历 `count` 个操作符，只对上述 4 种类型释放
  `data.literal.data`，然后释放 `ops` 数组本身；`ops == NULL` 时直接返回。
- 数值型操作符（`CAPTURE_STEPS`、`JUMP_*`）不持有堆内存，其 `data.literal.data`
  恒为 `NULL`，无需单独处理。
- 编译结束后 `url_compile()` 立即调用 `url_ops_free(ops, op_count)`：因为
  Stride 的构建函数会**复制**（`blob_dup`）目标比特串，序列不引用操作符的内存。

---

## 四、操作符 IR

### 4.1 `url_op_type_t`（全部 10 个枚举值）

```c
typedef enum {
    URL_OP_MATCH = 0,        /* $'文本'     精确比对 */
    URL_OP_CAPTURE_STEPS,    /* ${n}        捕获 n 字节 */
    URL_OP_CAPTURE_UNTIL,    /* ${'文本'}   捕获到该文本前 */
    URL_OP_CAPTURE_END,      /* ${}         捕获到段尾 */
    URL_OP_JUMP_ABS,         /* $[n]        定位到第 n 字节 */
    URL_OP_JUMP_END,         /* $[END]/$[END-n] 定位到段尾 / 段尾前 n 字节 */
    URL_OP_JUMP_FWD,         /* $[>n]       前进 n 字节 */
    URL_OP_JUMP_BACK,        /* $[<n]       后退 n 字节 */
    URL_OP_FIND_FWD,         /* $[>'文本']  向段尾查找 */
    URL_OP_FIND_REV          /* $[<'文本']  向段首查找 */
} url_op_type_t;
```

`URL_OP_MATCH == 0`，而 `calloc`/`memset` 会把操作符槽位清零，因此“未写入类型”
与 `MATCH` 取值相同；实现中每个槽位在使用前都显式赋值 `type`，不依赖该巧合。

### 4.2 `url_op_t`

```c
typedef struct {
    url_op_type_t type;
    union {
        stride_blob_t literal; /* MATCH / CAPTURE_UNTIL / FIND_FWD / FIND_REV */
        size_t steps;          /* CAPTURE_STEPS / JUMP_ABS / JUMP_FWD / JUMP_BACK */
        struct {
            int is_end;
            size_t back_steps;
        } jump_end;
    } data;
} url_op_t;
```

| 字段 | 类型 | 语义 |
|------|------|------|
| `type` | `url_op_type_t` | 操作符种类，决定 union 的活跃成员 |
| `data.literal` | `stride_blob_t` | `{ const void *data; size_t bit_len; }`；`bit_len = 字节数 × 8`；`data` 指向已展开转义的堆副本，由操作符拥有 |
| `data.steps` | `size_t` | 字节数（= 步数），用于定长捕获与数值型跳转 |
| `data.jump_end.is_end` | `int` | 标记来自 `$[END]` / `$[END-n]` 的 END 形式（值为 1） |
| `data.jump_end.back_steps` | `size_t` | 段尾回退的字节数；`$[END]` 为 0 |

union 成员与类型的对应关系是**排他的**：

| 操作符 | 活跃成员 |
|--------|----------|
| `URL_OP_MATCH`、`URL_OP_CAPTURE_UNTIL`、`URL_OP_FIND_FWD`、`URL_OP_FIND_REV` | `data.literal` |
| `URL_OP_CAPTURE_STEPS`、`URL_OP_JUMP_ABS`、`URL_OP_JUMP_FWD`、`URL_OP_JUMP_BACK` | `data.steps` |
| `URL_OP_JUMP_END` | `data.jump_end` |

`is_end` 只用于区分书写来源，**编译器只读 `back_steps`**：`$[END]`（`back_steps = 0`）
与 `$[END-0]` 编译结果完全一致。

---

## 五、编译器 `url_compile()`

### 5.1 接口

```c
url_compile_result_t url_compile(const void *pattern, size_t pattern_len);
void url_compile_free(url_compile_result_t *result);
```

```c
typedef struct {
    stride_status_t status;

    stride_seq_t *match;   /* 匹配序列（调用方通过 url_compile_free 释放）*/
    stride_seq_t *extract; /* 提取序列（同上）*/
    size_t param_count;    /* 捕获个数 */

    const char *error_msg;
} url_compile_result_t;
```

`pattern_len` 的语义与 `url_lex()` 相同：`0` 表示按 `'\0'` 结尾。返回结构体按值返回，
其中两个序列是**堆对象**，必须交给 `url_compile_free()` 释放。

### 5.2 编译流程

1. **空模式检查**：`pattern == NULL`，或 `pattern_len == 0` 且首字节为 `'\0'` →
   `status = STRIDE_E_EMPTY_SEGMENT`，`error_msg = "empty pattern"`，直接返回。
2. **词法分析**：调用 `url_lex(pattern, pattern_len, &ops, &op_count, &op_capacity)`。
   失败 → `status = STRIDE_E_INVALID_PATTERN`，`error_msg = "syntax error in pattern"`。
3. **空操作符检查**：`op_count == 0` → 释放 `ops`，返回
   `STRIDE_E_EMPTY_SEGMENT` / `"empty pattern"`（防御性分支：正常路径下空模式
   已在上一步被拦截）。
4. **建序列**：`stride_seq_new()` 各创建一个匹配序列 `m` 与提取序列 `e`；
   任一失败 → 释放两者与 `ops`，返回 `STRIDE_E_NOMEM` / `"out of memory"`。
5. **逐操作符翻译**：按第六节的规则表把每个操作符追加到 `m` 与 `e`。
   - 任一构建函数返回非 0 → `err = STRIDE_E_NOMEM`；
   - 字面量对齐校验失败 → `err = STRIDE_E_ALIGN`；
   - 未知操作符类型 → `err = STRIDE_E_INVALID_PATTERN`（防御性分支）。
   出现错误立即 `break`，不再处理后续操作符。
6. **收尾**：`url_ops_free(ops, op_count)`；若 `err != STRIDE_OK`，释放 `m`、`e`，
   返回 `status = err`、`error_msg = stride_status_str(err)`；否则返回
   `status = STRIDE_OK`、`match = m`、`extract = e`、
   `param_count = stride_seq_param_count(e)`。

### 5.3 `url_compile_free()`

```c
void url_compile_free(url_compile_result_t *result) {
    /* 释放 match 与 extract，然后 memset 整个结构体 */
}
```

- `result == NULL` 时直接返回。
- 依次 `stride_seq_free(result->match)` 与 `stride_seq_free(result->extract)`
  （两者内部对 `NULL` 安全），随后把结构体整体清零，避免重复释放。
- `error_msg` 指向静态字符串（字符串字面量或 `stride_status_str()` 的返回值），
  **不被释放**。
- 编译成功与失败两种情形都应调用 `url_compile_free()`：失败结果的
  `match` / `extract` 已是 `NULL`，调用是无害的。

---

## 六、翻译规则表（核心）

对每个操作符，编译器向匹配序列 `m` 与提取序列 `e` 各追加一次调用。
`lit` 指 `&op->data.literal`，构建函数会复制其比特串。

### 6.1 规则总表

| 操作符 | 匹配序列构建调用 | 提取序列构建调用 |
|--------|------------------|------------------|
| `URL_OP_MATCH` | `stride_seq_compare(m, lit)` | `stride_seq_skip_bits(e, lit->bit_len)` |
| `URL_OP_CAPTURE_STEPS` | `stride_seq_step_fwd(m, op->data.steps)` | `stride_seq_capture_steps(e, op->data.steps)` |
| `URL_OP_CAPTURE_UNTIL` | `stride_seq_find_fwd(m, lit)` | `stride_seq_capture_until(e, lit)` |
| `URL_OP_CAPTURE_END` | `stride_seq_abs_end(m, 0)` | `stride_seq_capture_end(e)` |
| `URL_OP_JUMP_ABS` | `stride_seq_abs_head(m, op->data.steps)` | `stride_seq_abs_head(e, op->data.steps)` |
| `URL_OP_JUMP_END` | `stride_seq_abs_end(m, op->data.jump_end.back_steps)` | `stride_seq_abs_end(e, op->data.jump_end.back_steps)` |
| `URL_OP_JUMP_FWD` | `stride_seq_step_fwd(m, op->data.steps)` | `stride_seq_step_fwd(e, op->data.steps)` |
| `URL_OP_JUMP_BACK` | `stride_seq_step_back(m, op->data.steps)` | `stride_seq_step_back(e, op->data.steps)` |
| `URL_OP_FIND_FWD` | `stride_seq_find_fwd(m, lit)` | `stride_seq_find_fwd(e, lit)` |
| `URL_OP_FIND_REV` | `stride_seq_find_rev(m, lit)` | `stride_seq_find_rev(e, lit)` |

### 6.2 逐条说明与理由

**`URL_OP_MATCH`（`$'文本'`）**

- 匹配：`stride_seq_compare(m, lit)`。动作 `STRIDE_ACT_COMPARE` 在游标处逐比特
  比对字面量，成功则游标前进其步数（`bit_len / stride` 字节）。
- 提取：`stride_seq_skip_bits(e, lit->bit_len)`。**匹配阶段已经验证过这段文本**，
  提取阶段无需重复验证，只需跳过它；`STRIDE_MOVE_SKIP_BITS` 是唯一以**比特**计的
  偏移，正好与字面量的 `bit_len` 单位一致，不必做单位换算。
- 这就是“匹配与提取分离”的收益：比对只在匹配阶段发生一次。

**`URL_OP_CAPTURE_STEPS`（`${n}`）**

- 匹配：`stride_seq_step_fwd(m, n)`。捕获在匹配阶段**只等于前进**：匹配不产出参数，
  游标需要越过这 n 字节，后续的 `COMPARE` / `FIND_*` 才能落在正确位置。
- 提取：`stride_seq_capture_steps(e, n)`。动作 `STRIDE_ACT_CAPTURE_STEPS` 记录
  `[游标, 游标 + n)` 为参数并前进 n 字节；同时使提取序列的 `param_count` 加一。

**`URL_OP_CAPTURE_UNTIL`（`${'文本'}`）**

- 匹配：`stride_seq_find_fwd(m, lit)`。匹配阶段要确认定界文本**存在**，并把游标停在
  它的首字节（而不是自己的结尾）——这正是向前查找的语义，因此可以直接复用。
- 提取：`stride_seq_capture_until(e, lit)`。动作 `STRIDE_ACT_CAPTURE_UNTIL` 从游标起
  向前查找定界文本，把 `[游标, 定界文本首字节)` 记为参数，游标停在定界文本首字节；
  未找到则捕获到段尾（游标移到段尾）。

**`URL_OP_CAPTURE_END`（`${}`）**

- 匹配：`stride_seq_abs_end(m, 0)`。动作侧无需动作，只需把游标定位到段尾；
  这样一来，若该段之前的部分没有恰好消费完整个段，执行引擎的**段尾对齐**检查
  （结束要求游标恰好等于段尾）就会失败——这与“模式必须完整描述一个段”的约定一致。
- 提取：`stride_seq_capture_end(e)`。动作 `STRIDE_ACT_CAPTURE_END` 把
  `[游标, 段尾)` 记为参数并把游标移到段尾。

**`URL_OP_JUMP_ABS` / `URL_OP_JUMP_END` / `URL_OP_JUMP_FWD` / `URL_OP_JUMP_BACK`**

定位与相对移动在匹配、提取两个阶段的语义**完全相同**：两阶段都必须把游标真的移到
同一位置，否则后续动作的落点会不一致。因此两个序列使用同一个构建函数：

- `$[n]` → `stride_seq_abs_head(seq, n)`（`STRIDE_MOVE_ABS_HEAD`，定位到第 n 字节）；
- `$[END]` / `$[END-n]` → `stride_seq_abs_end(seq, back_steps)`
  （`STRIDE_MOVE_ABS_END`，定位到 `段尾 − back_steps`；`$[END]` 即 0）；
- `$[>n]` → `stride_seq_step_fwd(seq, n)`（`STRIDE_MOVE_STEP_FWD`）；
- `$[<n]` → `stride_seq_step_back(seq, n)`（`STRIDE_MOVE_STEP_BACK`）。

运行时这些偏移各自带边界检查：前进/后退越界、绝对定位超出段长都会使该节点失败。

**`URL_OP_FIND_FWD` / `URL_OP_FIND_REV`**

- 两序列都用对应的查找构建函数：`$[>'文本']` →
  `stride_seq_find_fwd(seq, lit)`（`STRIDE_MOVE_FIND_FWD`），`$[<'文本']` →
  `stride_seq_find_rev(seq, lit)`（`STRIDE_MOVE_FIND_REV`）。
- 查找失败（目标不存在、目标 `bit_len == 0`、目标比特长度不是步长整数倍）会使该节点
  失败；成功时游标落在目标**首字节**，不消费目标本身。
- 查找是**步对齐**的：只在整字节边界上比对，不做任意比特错位搜索。

### 6.3 字面量对齐校验

```c
static int literal_aligned(const stride_blob_t *b) {
    return (b->bit_len % URL_PATTERN_STRIDE) == 0;
}
```

- 在翻译 `MATCH`、`CAPTURE_UNTIL`、`FIND_FWD`、`FIND_REV` 之前校验其字面量
  比特长度是否为 8 的整数倍；不满足则 `err = STRIDE_E_ALIGN`，编译失败。
- 理由：运行期 `COMPARE` 需要 `bit_len / stride` 得到整步数，查找也要求
  `bit_len % stride == 0`；与其让模式在运行期因对齐问题失败，不如在编译期报错。
- 由于当前词法层的时间 `lit->bit_len` 恒为 `字节数 × 8`，该分支在步长为 8 时是
  **不变量守卫**（尤其对 `\xNN` 与多字节 UTF-8 文本，长度天然是 8 的倍数）：
  一旦步长约定或词法层实现改变，它会立刻把问题挡在编译期。
- `CAPTURE_STEPS`、`CAPTURE_END`、`JUMP_*` 不含字面量，不做该校验。

---

## 七、合并不再由状态机完成

### 7.1 职责转移

在 2026-09-11 的重构之前，连续常量偏移的相加/抵消、关键字（字面量）与当前偏移的
绑定，由编译器内部的一台状态机完成。重构后**这层逻辑整体消失**：

> 构建是**函数式**的：每次调用把一个新的「偏移」或「动作」追加到**尾节点**，
> **能与尾节点合并就地合并**，否则新建尾节点。没有状态机。
> —— `third_party/Stride/include/stride/sequence.h`

因此 `url_compile()` 只是把操作符**逐个翻译**为构建函数调用；合并完全发生在
Stride 的构建函数内部，URLRouter 侧不做任何偏移运算，也不维护任何编译期状态。
这带来的直接好处是：合并规则只存在于一处（Stride），URLRouter 的编译器成为
无状态的机械翻译。

### 7.2 尾节点合并规则（Stride 侧）

| 尾节点已有偏移 \ 新增偏移 | `STEP_FWD` | `STEP_BACK` | `SKIP_BITS` | `FIND_*` |
|---|---|---|---|---|
| `STEP_FWD` | 相加 | 抵消（可为负 → `STEP_BACK`） | 不合并（单位不同） | 不合并 |
| `STEP_BACK` | 抵消 | 相加 | 不合并（单位不同） | 不合并 |
| `ABS_HEAD` | 相加 | 结果非负时相加 | 不合并 | 不合并 |
| `ABS_END` | 不合并 | 相加 | 不合并 | 不合并 |
| `SKIP_BITS` | 不合并 | 不合并 | 相加 | 不合并 |
| `NONE` / `FIND_*` | 不合并 | 不合并 | 不合并 | 不合并 |

两条附加规则：

1. **尾节点已有动作时不合并偏移**——直接新建节点。动作是把序列切成节点的主要边界。
2. **动作绑定**：追加动作时，若尾节点尚无动作，就绑定到该节点（例如把“比对 `v`”
   绑到刚追加的偏移上）；否则新建节点。捕获动作（`CAPTURE_STEPS` /
   `CAPTURE_UNTIL` / `CAPTURE_END`）会递增序列的 `param_count`，`COMPARE` 不会。

### 7.3 合并示例

**示例 A：连续常量偏移相加，字面量绑定到当前偏移**

模式 `${1}${1}${1}$'key'`（匹配序列）：

| 步骤 | 调用 | 结果 |
|------|------|------|
| 1 | `stride_seq_step_fwd(m, 1)` | 新建节点 A：`move = STEP_FWD(1)` |
| 2 | `stride_seq_step_fwd(m, 1)` | 尾部无动作且同为 `STEP_FWD` → 就地相加 → `STEP_FWD(2)` |
| 3 | `stride_seq_step_fwd(m, 1)` | 就地相加 → `STEP_FWD(3)` |
| 4 | `stride_seq_compare(m, "key")` | 尾部 A 尚无动作 → 绑定动作 → `{ STEP_FWD(3), COMPARE("key") }` |

匹配序列最终**只有 1 个节点**：前进 3 字节，然后比对 `key`。三个 `${1}` 与一次
`MATCH` 在编译产物里退化为一次移动 + 一次比对。

> 同样的合并也跨操作符种类发生：`${1}$[>1]` 的匹配序列同样是
> `STEP_FWD(2)`，因为合并规则只看“同单位的常量相加”，不看操作符来自哪个语法。

**示例 B：`${}$[<4]$'dddd'` → `ABS_END(4)`**

| 步骤 | 调用 | 结果 |
|------|------|------|
| 1 | `stride_seq_abs_end(m, 0)` | 新建节点 A：`move = ABS_END(0)`（`${}`） |
| 2 | `stride_seq_step_back(m, 4)` | `ABS_END` + `STEP_BACK` → 相加 → `ABS_END(4)`（`$[<4]`） |
| 3 | `stride_seq_compare(m, "dddd")` | 尾部无动作 → 绑定 → `{ ABS_END(4), COMPARE("dddd") }` |

匹配序列**只有 1 个节点**：定位到“段尾前 4 字节”，再比对 `dddd`。
这里能合并的关键是 `ABS_END + STEP_BACK` 允许相加（`$[<4]` 是从段尾往回 4 字节，
语义上仍是相对段尾的定位）；而 `ABS_END + STEP_FWD` **不合并**——越过段尾没有意义，
只能新建节点并在运行期由边界检查判定失败。

**示例 C：提取序列中「跳过 + 捕获」绑定到同一节点**

模式 `$'ab'${2}`（提取序列）：

| 步骤 | 调用 | 结果 |
|------|------|------|
| 1 | `stride_seq_skip_bits(e, 16)` | 新建节点 A：`move = SKIP_BITS(16)`（`$'ab'` 的两个字节） |
| 2 | `stride_seq_capture_steps(e, 2)` | 尾部 A 尚无动作 → 绑定 → `{ SKIP_BITS(16), CAPTURE_STEPS(2) }` |

提取序列**只有 1 个节点**：跳过 `ab`，然后捕获 2 字节。跳过（比特单位）与捕获
（步单位）绑定在同一个节点上，一次“偏移 → 动作”就完成了全部工作。

**示例 D：单位不同则不合并**

模式 `$'ab'$[>1]`（提取序列）：`skip_bits(16)` 生成 `SKIP_BITS(16)` 节点后，
`step_fwd(1)` 因**单位不同**（比特 vs 步）无法与尾节点合并，于是新建节点
`STEP_FWD(1)` → 2 个节点。合并只发生在单位相同的常量之间，绝不隐式换算。

**示例 E：动作打断合并**

模式 `${1}${2}`（提取序列）：`capture_steps(1)` 建节点 A（带捕获动作）；
`capture_steps(2)` 见尾节点已有动作，只能新建节点 B → 2 个节点、`param_count = 2`。
而在匹配序列中，两者都是 `step_fwd`，合并为 `STEP_FWD(3)` 的 1 个节点。

---

## 八、编译示例：`$'v'${'.'}$'.'${}`

这是一个完整的、逐步展开的例子：模式在段内匹配“`v` + 任意内容 + `.` + 到段尾”，
例如 URL 段 `v2.0`。

### 8.1 词法分析结果

| 序 | 源码 | 操作符 | 负载 |
|----|------|--------|------|
| 0 | `$'v'` | `URL_OP_MATCH` | `literal = { "v", bit_len = 8 }` |
| 1 | `${'.'}` | `URL_OP_CAPTURE_UNTIL` | `literal = { ".", bit_len = 8 }` |
| 2 | `$'.'` | `URL_OP_MATCH` | `literal = { ".", bit_len = 8 }` |
| 3 | `${}` | `URL_OP_CAPTURE_END` | 无 |

### 8.2 匹配序列（3 个节点）

| 节点 | move（偏移） | act（动作） | 来源 |
|------|--------------|-------------|------|
| 1 | `NONE`（原地） | `COMPARE("v")`，8 比特 | 操作符 0 `$'v'` → `stride_seq_compare(m, lit)` |
| 2 | `FIND_FWD(".")` | `COMPARE(".")`，8 比特 | 操作符 1 `${'.'}` → `stride_seq_find_fwd(m, lit)`；随后操作符 2 `$'.'` 的 `stride_seq_compare(m, lit)` **绑定**到该节点 |
| 3 | `ABS_END(0)` | `NONE` | 操作符 3 `${}` → `stride_seq_abs_end(m, 0)` |

逐节点解释：

- **节点 1**：匹配序列的第一个动作没有前置偏移，因此 `move = STRIDE_MOVE_NONE`，
  游标留在段首原地比对 `v`；比对成功后游标前进 1 字节。
- **节点 2**：`${'.'}` 在匹配阶段翻译成向前查找 `.`，游标落到 `.` 的首字节；
  紧接着的 `$'.'` 产生 `COMPARE`，而尾节点（节点 2）此时还没有动作，于是被绑定上去。
  “查找 + 就地比对”合并为一个节点，等价于“找到 `.` 并确认它就是 `.`”。
- **节点 3**：`${}` 在匹配阶段只需定位到段尾；执行后游标等于段尾，
  段尾对齐检查通过。若节点 2 的查找落点不是段尾前 1 字节，节点 3 之后的
  对齐检查仍会通过（因为 `ABS_END(0)` 强制跳到段尾），但**此前的比对内容**
  已经决定了匹配是否成立。

对段 `"v2.0"`（4 字节，`segment_bit_len = 32`）的执行过程：节点 1 比对 `v` → 游标 1；
节点 2 从 1 起向前查找 `.`，命中偏移 1，比对 `.` 成功 → 游标 2；
节点 3 定位到段尾 → 游标 4 = 段尾 → 返回 0（命中）。

### 8.3 提取序列（2 个节点）

| 节点 | move（偏移） | act（动作） | 来源 |
|------|--------------|-------------|------|
| 1 | `SKIP_BITS(8)`（1 字节） | `CAPTURE_UNTIL(".")` | 操作符 0 `$'v'` → `stride_seq_skip_bits(e, lit->bit_len)`，其尾部尚无动作，于是操作符 1 `${'.'}` 的 `stride_seq_capture_until(e, lit)` **绑定**到同一节点 |
| 2 | `SKIP_BITS(8)`（1 字节） | `CAPTURE_END` | 操作符 2 `$'.'` → `stride_seq_skip_bits(e, lit->bit_len)`（尾节点已有动作，新建节点）；随后操作符 3 `${}` 的 `stride_seq_capture_end(e)` 绑定到该节点 |

逐节点解释：

- **节点 1**：`$'v'` 在提取阶段被优化为“跳过 1 字节”；`${'.'}` 的捕获动作可以绑定到
  这个刚建的偏移上，于是两个操作符合并成一个节点：跳过 `v`，然后捕获到 `.` 之前。
- **节点 2**：`$'.'` 再次跳过 1 字节（匹配阶段已验证过这个 `.`，无需重复比对）；
  `${}` 的 `CAPTURE_END` 绑定上去，捕获到段尾。
- 提取序列的 `param_count = 2`（两个捕获动作），与 `url_compile_result_t.param_count`
  一致。

对段 `"v2.0"` 的执行过程：节点 1 跳过 1 字节 → 游标 1，捕获 `[1, 2)` = `"2"` → 游标 2；
节点 2 跳过 1 字节 → 游标 3，捕获 `[3, 4)` = `"0"` → 游标 4 = 段尾。
两次执行参数总数均为 2，参数以 `stride_param_t` 零拷贝返回（`ptr` 指向段内、`steps = 1`；`steps × 8 = 8` 比特 = 1 字节）。

### 8.4 与 Stride README 的手工翻译对照

Stride README 中手工书写的等价代码是：

```
stride_seq_compare(m, &v);     /* $'v'  */
stride_seq_find_fwd(m, &dot);  /* ${'.'} 匹配阶段 = 查找 */
stride_seq_compare(m, &dot);   /* $'.'  绑到上一节点 */
stride_seq_abs_end(m, 0);      /* ${}   */
```

与 8.2 的 3 个节点一一对应——这正是 `url_compile()` 产出的东西，
说明编译器只是把操作符机械地转写为这些调用，不含额外加工。

### 8.5 执行入口

```c
stride_match_run(result.match, URL_PATTERN_STRIDE, seg, STRIDE_BITS(seg_len));
stride_extract_run(result.extract, URL_PATTERN_STRIDE, seg, STRIDE_BITS(seg_len),
                   params, param_capacity, &param_count);
```

- 步长恒为 `URL_PATTERN_STRIDE`；段比特长度用 `STRIDE_BITS()` 从字节数换算。
- `stride_match_run()` 与 `stride_extract_run()` 都委托给通用引擎
  `stride_seq_run()`，区别在于提取入口带参数缓冲。
- 返回值：`0` 成功；负数失败中 `-(i+1)` 表示第 `i` 个节点失败，
  `-(count+1)` 表示游标未落在段尾（段尾未对齐）。
- 参数起始位置必须字节对齐（比特偏移为 8 的整数倍），否则捕获失败；
  步长 8 且所有偏移都是整字节时该条件自然满足。

---

## 九、状态码表

`url_compile_result_t.status` 的取值全部来自 `stride_status_t`：

| 状态码 | 何时产生 | `error_msg` |
|--------|----------|-------------|
| `STRIDE_OK` | 编译成功：两个序列均构建完成 | `NULL`（结构体被 `memset` 清零，编译成功路径不写入 `error_msg`） |
| `STRIDE_E_INVALID_PATTERN` | `url_lex()` 返回 -1（词法/语法错误，含词法层分配失败）；或翻译循环遇到未知的 `url_op_type_t` 值 | `"syntax error in pattern"`；防御性分支走 `stride_status_str()` → `"invalid pattern"` |
| `STRIDE_E_EMPTY_SEGMENT` | `pattern == NULL`；或 `pattern_len == 0` 且首字节为 `'\0'`；或词法产出 0 个操作符 | `"empty pattern"` |
| `STRIDE_E_ALIGN` | `MATCH` / `CAPTURE_UNTIL` / `FIND_FWD` / `FIND_REV` 的字面量比特长度不是 `URL_PATTERN_STRIDE`（8）的整数倍 | `stride_status_str()` → `"alignment error"` |
| `STRIDE_E_NOMEM` | `stride_seq_new()` 返回 `NULL`；或任一 `stride_seq_*()` 构建函数返回非 0（内部比特串复制 `malloc` 失败） | `"out of memory"` |

失败路径的固定形态：`status != STRIDE_OK` 时 `match` 与 `extract` 必定是 `NULL`，
且两个已建的序列都已被释放（不存在泄漏），调用方仍应统一调用
`url_compile_free()`。

---

## 十、数据结构汇总

### 10.1 编译器的输入、中间表示与产出

```
pattern（字节串 + 长度）
   │ url_lex()
   ▼
url_op_t[]（操作符 IR，含转义展开后的文本副本）
   │ url_compile()  ← 机械翻译，无状态机
   ▼
url_compile_result_t { status, match, extract, param_count, error_msg }
                              │
                              ▼
                     stride_seq_t —— 单链表，节点 stride_step_t
```

### 10.2 `url_op_t` 与 `stride_step_t` 的字段映射

```c
typedef struct stride_step {
    /* 偏移 */
    stride_move_t move;
    size_t move_value;
    stride_blob_t move_target;

    /* 动作 */
    stride_act_t act;
    stride_blob_t act_target;
    size_t act_value;

    struct stride_step *next;
} stride_step_t;
```

| `url_op_t` 的字段 | 匹配序列落点 | 提取序列落点 |
|-------------------|--------------|--------------|
| `data.literal`（`MATCH`） | `act = COMPARE`，`act_target = literal` | `move = SKIP_BITS`，`move_value = bit_len` |
| `data.literal`（`CAPTURE_UNTIL`） | `move = FIND_FWD`，`move_target = literal` | `act = CAPTURE_UNTIL`，`act_target = literal` |
| `data.literal`（`FIND_FWD` / `FIND_REV`） | `move = FIND_FWD` / `FIND_REV`，`move_target = literal` | 同左 |
| `data.steps`（`CAPTURE_STEPS`） | `move = STEP_FWD`，`move_value = steps` | `act = CAPTURE_STEPS`，`act_value = steps` |
| `data.steps`（`JUMP_ABS`） | `move = ABS_HEAD`，`move_value = steps` | 同左 |
| `data.steps`（`JUMP_FWD`） | `move = STEP_FWD`，`move_value = steps` | 同左 |
| `data.steps`（`JUMP_BACK`） | `move = STEP_BACK`，`move_value = steps` | 同左 |
| `data.jump_end.back_steps` | `move = ABS_END`，`move_value = back_steps` | 同左 |
| （无 `CAPTURE_END` 负载） | `move = ABS_END`，`move_value = 0` | `act = CAPTURE_END`，无值 |

说明：

- 节点上的 `move_value` / `act_value` 单位是**步**；
  `act_target.bit_len` / `move_target.bit_len` 单位是**比特**；
  `SKIP_BITS` 的 `move_value` 是**比特**。这与 `stride/types.h` 的单位约定一致。
- 构建时 `blob_dup()` 会把 `move_target` / `act_target` 指向的比特串**复制**到节点
  自己的缓冲区，因此节点拥有目标数据，`url_ops_free()` 之后仍然有效；
  `stride_seq_free()` / `stride_seq_clear()` 负责释放这些副本。

### 10.3 `stride_seq_t`

```c
typedef struct {
    stride_step_t *head;
    stride_step_t *tail;
    size_t count;       /* 节点数 */
    size_t param_count; /* 捕获动作个数（提取序列用）*/
} stride_seq_t;
```

| 字段 | 说明 |
|------|------|
| `head` / `tail` | 单链表首尾；构建只用 `tail`（尾部合并），执行从 `head` 顺序走 |
| `count` | 节点数；`stride_seq_run()` 用它在段尾未对齐时返回 `-(count+1)` |
| `param_count` | 捕获动作个数。匹配序列恒为 0；提取序列的值被 `url_compile()` 复制到 `url_compile_result_t.param_count` |

### 10.4 `url_compile_result_t`

| 字段 | 类型 | 所有权与生命周期 |
|------|------|------------------|
| `status` | `stride_status_t` | 值；`STRIDE_OK` 表示成功 |
| `match` | `stride_seq_t *` | 堆对象，由调用方通过 `url_compile_free()` 释放 |
| `extract` | `stride_seq_t *` | 同上 |
| `param_count` | `size_t` | 值；等于 `stride_seq_param_count(extract)` |
| `error_msg` | `const char *` | 静态字符串，**不释放**；成功时为 `NULL` |

### 10.5 生命周期一览

| 对象 | 创建 | 释放 |
|------|------|------|
| 操作符数组及其文本副本 | `url_lex()` | `url_ops_free(ops, count)`（`url_compile()` 内部已调用） |
| 匹配序列 / 提取序列 | `stride_seq_new()`（在 `url_compile()` 内） | `url_compile_free(&result)` → `stride_seq_free()` |
| 节点上的比特串副本 | 构建函数内部的 `blob_dup()` | `stride_seq_free()` / `stride_seq_clear()` |

---

## 十一、设计约束小结

1. **语法与编译器在 URLRouter，序列构建与执行在 Stride**——边界清晰，Stride 不认识
   `$` 语法，URLRouter 不实现链表与合并。
2. **字节导向**：`URL_PATTERN_STRIDE = 8`，模式中的长度与偏移单位就是字节，
   进入 Stride 时按 `× 8` 换算为比特。
3. **无状态机**：编译器不缓存、不合并、不排序；所有合并由 Stride 的尾节点合并在
   构建时完成，规则只有一份。
4. **匹配与提取分离**：一次编译产出两个序列；`MATCH` 在提取序列退化为跳过，
   `${n}` 在匹配序列退化为前进，各自只做本阶段必要的工作。
5. **编译期能查的错不留给运行期**：空模式、语法错误、字面量对齐都在编译期报出
   状态码；越界与段尾对齐由执行引擎在运行期判定。
6. **零拷贝参数**：捕获只记录（指针，比特长度），参数直接指向段数据，且起始位置
   满足字节对齐。

---

**文档版本**：1.0
**更新日期**：2026-09-11
