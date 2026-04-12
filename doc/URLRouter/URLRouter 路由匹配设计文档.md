# URLRouter 编译器设计文档

## 一、编译流程概述

URLRouter 将路由模式编译为两个独立的数据结构：

1. **特征序列（Feature Sequence）** - 用于路由匹配阶段
2. **提取操作序列（Extractor Operations）** - 用于参数提取阶段

```
模式字符串 → 词法分析 → 操作符序列 → 特征序列生成 → 特征序列
                              ↓
                        提取器创建 → 提取操作序列
```

## 二、操作符定义

### 2.1 操作符类型

| 类型 | 语法 | 说明 |
|------|------|------|
| OP_MATCH | `$'文本'` | 精确匹配固定字符串 |
| OP_CAPTURE_LEN | `${数字}` | 捕获指定长度字符 |
| OP_CAPTURE_CHR | `${'字符'}` | 捕获到指定字符前 |
| OP_CAPTURE_END | `${}` | 捕获到段尾 |
| OP_JUMP_POS | `$[位置]` | 绝对跳转到指定位置 |
| OP_JUMP_FWD | `$[>偏移]` | 向结尾方向移动 |
| OP_JUMP_BACK | `$[<偏移]` | 向开头方向移动 |

## 三、特征序列生成规则

### 3.1 设计原则

特征序列的设计目标是**高效匹配**，采用（偏移，关键字）元组形式：

- **偏移合并**：连续的捕获/移动操作合并为一个偏移值
- **关键字验证**：在偏移后的位置验证关键字
- **段尾对齐**：执行完毕后指针必须位于段尾

### 3.2 编译规则

| 操作符 | 特征序列输出 | 说明 |
|--------|-------------|------|
| `${数字}` | 累加偏移 | 不立即输出，等待与关键字合并 |
| `$[>偏移]` | 累加偏移 | 不立即输出，等待与关键字合并 |
| `$[<偏移]` | 累加偏移（负值） | 不立即输出，等待与关键字合并 |
| `${'字符'}` | 输出累计偏移 + 字符查找元组 | 字符查找单独成元组 |
| `${}` | 输出 END 偏移元组 | 移动到段尾 |
| `$[位置]` | 输出 END 相关元组 | 绝对位置转换 |
| `$'文本'` | 输出（累计偏移，文本）元组 | 偏移与关键字合并 |

### 3.3 编译示例

#### 示例 1：`${4}$'a'`

```
操作符序列：
  [0] OP_CAPTURE_LEN, length=4
  [1] OP_MATCH, text="a"

编译过程：
  i=0: OP_CAPTURE_LEN → cumulative_offset = 4
  i=1: OP_MATCH → 输出 (OFFSET_POS, 4, "a")

特征序列：
  [(type=OFFSET_POS, value=4, keyword="a")]
```

**匹配语义**：先偏移 4 个字符，然后匹配"a"，指针必须到达段尾。

#### 示例 2：`${2}$'key'${3}$'end'`

```
操作符序列：
  [0] OP_CAPTURE_LEN, length=2
  [1] OP_MATCH, text="key"
  [2] OP_CAPTURE_LEN, length=3
  [3] OP_MATCH, text="end"

编译过程：
  i=0: OP_CAPTURE_LEN → cumulative_offset = 2
  i=1: OP_MATCH → 输出 (OFFSET_POS, 2, "key"), cumulative_offset = 0
  i=2: OP_CAPTURE_LEN → cumulative_offset = 3
  i=3: OP_MATCH → 输出 (OFFSET_POS, 3, "end")

特征序列：
  [(OFFSET_POS, 2, "key"), (OFFSET_POS, 3, "end")]
```

**匹配语义**：
1. 偏移 2，匹配"key"
2. 偏移 3，匹配"end"
3. 指针必须到达段尾

#### 示例 3：`${'='}$'='`

```
操作符序列：
  [0] OP_CAPTURE_CHR, ch='='
  [1] OP_MATCH, text="="

编译过程：
  i=0: OP_CAPTURE_CHR → 输出 (CHAR_FIND, '=', NULL)
  i=1: OP_MATCH → 输出 (OFFSET_POS, 0, "=")

特征序列：
  [(CHAR_FIND, '=', NULL), (OFFSET_POS, 0, "=")]
```

**匹配语义**：
1. 查找'='字符
2. 匹配"="
3. 指针必须到达段尾

#### 示例 4：`${}$[<2]$'aa'`

```
操作符序列：
  [0] OP_CAPTURE_END
  [1] OP_JUMP_BACK, offset=2
  [2] OP_MATCH, text="aa"

编译过程：
  i=0: OP_CAPTURE_END → 输出 (END_OFFSET, 0, NULL)
  i=1: OP_JUMP_BACK → cumulative_offset = -2
  i=2: OP_MATCH → 输出 (OFFSET_NEG, -2, "aa")

特征序列：
  [(END_OFFSET, 0, NULL), (OFFSET_NEG, -2, "aa")]
```

**匹配语义**：
1. 移动到段尾
2. 向开头回退 2 个字符
3. 匹配"aa"
4. 指针必须到达段尾

## 四、提取操作序列生成规则

### 4.1 设计原则

提取操作序列保留原始操作的完整语义，用于参数提取：

- **保留捕获边界**：每个捕获操作独立存在
- **优化验证操作**：匹配操作转换为跳过操作
- **保留参数计数**：明确记录产生的参数数量

### 4.2 编译规则

| 操作符 | 提取操作 | 产生参数 |
|--------|---------|---------|
| OP_CAPTURE_LEN | EX_CAPTURE_LEN | 是 |
| OP_CAPTURE_CHR | EX_CAPTURE_CHR | 是 |
| OP_CAPTURE_END | EX_CAPTURE_END | 是 |
| OP_MATCH | EX_SKIP_LEN | 否 |
| OP_JUMP_POS | EX_JUMP_POS | 否 |
| OP_JUMP_FWD | EX_JUMP_FWD | 否 |
| OP_JUMP_BACK | EX_JUMP_BACK | 否 |

### 4.3 编译示例

#### 示例 1：`${4}$'a'`

```
操作符序列：
  [0] OP_CAPTURE_LEN, length=4
  [1] OP_MATCH, text="a"

提取操作序列：
  [0] EX_CAPTURE_LEN, length=4  (产生参数 1)
  [1] EX_SKIP_LEN, length=1     (不产生参数)

参数数量：1
```

#### 示例 2：`${'='}$'='${}`

```
操作符序列：
  [0] OP_CAPTURE_CHR, ch='='
  [1] OP_MATCH, text="="
  [2] OP_CAPTURE_END

提取操作序列：
  [0] EX_CAPTURE_CHR, ch='='   (产生参数 1)
  [1] EX_SKIP_LEN, length=1    (不产生参数)
  [2] EX_CAPTURE_END           (产生参数 1)

参数数量：2
```

## 五、完整编译示例

### 5.1 模式：`/${4}$'-'${2}$'-'${2}`

**URL 示例**：`/2024-03-15`

**段解析**：`2024-03-15`

**操作符序列**：
```
[0] OP_CAPTURE_LEN, length=4
[1] OP_MATCH, text="-"
[2] OP_CAPTURE_LEN, length=2
[3] OP_MATCH, text="-"
[4] OP_CAPTURE_LEN, length=2
```

**特征序列**：
```
[0] (OFFSET_POS, 4, "-")
[1] (OFFSET_POS, 2, "-")
[2] (OFFSET_POS, 2, NULL)
```

**提取操作序列**：
```
[0] EX_CAPTURE_LEN, length=4  (参数："2024")
[1] EX_SKIP_LEN, length=1
[2] EX_CAPTURE_LEN, length=2  (参数："03")
[3] EX_SKIP_LEN, length=1
[4] EX_CAPTURE_LEN, length=2  (参数："15")

参数数量：3
```

**匹配过程**：
```
segment = "2024-03-15", len = 10
cursor = 0

特征 [0]: 偏移 4 → cursor=4, 匹配"-" → cursor=5
特征 [1]: 偏移 2 → cursor=7, 匹配"-" → cursor=8
特征 [2]: 偏移 2 → cursor=10

段尾检查：cursor(10) == len(10) ✓
```

**提取过程**：
```
segment = "2024-03-15", cursor = 0

操作 [0]: 捕获 [0..4] = "2024", cursor=4
操作 [1]: 跳过 [4..5], cursor=5
操作 [2]: 捕获 [5..7] = "03", cursor=7
操作 [3]: 跳过 [7..8], cursor=8
操作 [4]: 捕获 [8..10] = "15", cursor=10

参数：["2024", "03", "15"]
```

### 5.2 模式：`/${'.'}$'.'${}`

**URL 示例**：`/files/document.pdf`

**段解析**：`document.pdf`

**操作符序列**：
```
[0] OP_CAPTURE_CHR, ch='.'
[1] OP_MATCH, text="."
[2] OP_CAPTURE_END
```

**特征序列**：
```
[0] (CHAR_FIND, '.', NULL)
[1] (OFFSET_POS, 0, ".")
[2] (END_OFFSET, 0, NULL)
```

**提取操作序列**：
```
[0] EX_CAPTURE_CHR, ch='.'  (参数："document")
[1] EX_SKIP_LEN, length=1
[2] EX_CAPTURE_END          (参数："pdf")

参数数量：2
```

## 六、特征序列类型定义

```c
typedef enum {
    FT_OFFSET_POS,  /* 正向偏移：value > 0 */
    FT_OFFSET_NEG,  /* 负向偏移：value < 0 */
    FT_CHAR_FIND,   /* 字符查找：value = 字符 ASCII */
    FT_END_OFFSET   /* END 偏移：value = 0 表示 END, < 0 表示 END-n */
} feature_type_t;

typedef struct {
    feature_type_t type;
    int value;              /* 偏移量或字符 ASCII */
    const char *keyword;    /* 关键字，NULL 表示无 */
    size_t keyword_len;
} feature_tuple_t;
```

## 七、提取操作类型定义

```c
typedef enum {
    EX_CAPTURE_LEN,   /* 定长捕获 */
    EX_CAPTURE_CHR,   /* 捕获到字符 */
    EX_CAPTURE_END,   /* 捕获到段尾 */
    EX_SKIP_LEN,      /* 跳过固定长度 */
    EX_JUMP_POS,      /* 绝对跳转 */
    EX_JUMP_FWD,      /* 向前移动 */
    EX_JUMP_BACK      /* 向后移动 */
} extractor_op_type_t;

typedef struct {
    extractor_op_type_t type;
    union {
        size_t length;
        char ch;
        size_t pos;
        size_t offset;
    } data;
} extractor_op_t;
```

## 八、设计要点总结

1. **匹配与提取分离**：特征序列用于快速匹配，提取操作序列用于精确提取
2. **偏移合并优化**：连续的捕获/移动操作在特征序列中合并为一个偏移
3. **段尾对齐检查**：确保模式完整消耗整个段
4. **零拷贝提取**：参数以（指针，长度）形式返回，直接指向原始数据
5. **HTTP 方法隔离**：每个 HTTP 方法有独立的路由树
