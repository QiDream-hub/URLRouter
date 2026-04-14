#ifndef PATTERN_COMPILER_H
#define PATTERN_COMPILER_H

#include <stddef.h>

/* ============================================================
 * URLRouter 模式编译器 - 头文件
 * 
 * 根据设计文档 2.2 版本实现
 * 支持两阶段编译：特征序列（匹配）和提取序列（参数提取）
 * ============================================================ */

/* ==================== 操作符定义 ==================== */

/**
 * 操作符类型
 * 对应语法规范中的 10 种操作符
 */
typedef enum {
    OP_MATCH,           /* $'文本' - 精确匹配固定字符串 */
    OP_CAPTURE_LEN,     /* ${长度} - 捕获指定长度字符 */
    OP_CAPTURE_CHR,     /* ${'字符'} - 捕获到指定字符前 */
    OP_CAPTURE_END,     /* ${} - 捕获到段尾 */
    OP_JUMP_ABS,        /* $[位置] - 绝对跳转（基于 HEAD）*/
    OP_JUMP_END,        /* $[END] / $[END-n] - END 跳转 */
    OP_JUMP_FWD,        /* $[>偏移] - 向结尾方向移动 */
    OP_JUMP_BACK,       /* $[<偏移] - 向开头方向移动 */
    OP_FIND_FWD,        /* $[>'字符'] - 向结尾方向查找字符 */
    OP_FIND_REV         /* $[<'字符'] - 向开头方向查找字符 */
} op_type_t;

/**
 * 操作符结构
 */
typedef struct {
    op_type_t type;
    union {
        /* OP_MATCH */
        struct {
            const char *text;  /* 指向 pattern 内部 */
            size_t len;
        } match;

        /* OP_CAPTURE_LEN, OP_JUMP_ABS, OP_JUMP_FWD, OP_JUMP_BACK */
        size_t length;
        size_t pos;
        size_t offset;

        /* OP_CAPTURE_CHR, OP_FIND_FWD, OP_FIND_REV */
        struct {
            char ch;
        } find;

        /* OP_JUMP_END */
        struct {
            int is_end;    /* 是否为 END */
            int offset;    /* END-n 中的 n，0 表示纯 END */
        } jump_end;
    } data;
} op_t;

/* ==================== 特征序列定义 ==================== */

/**
 * 特征元组类型（6 种）
 * 根据设计文档 2.2 版本 7.1 节
 */
typedef enum {
    FT_CONST_REL_FWD,   /* 常量相对向结尾移动：(n, kw) */
    FT_CONST_REL_BACK,  /* 常量相对向开头移动：(-n, kw) */
    FT_CONST_ABS_HEAD,  /* 常量绝对位置（基于 HEAD）：(HEAD+n, kw) */
    FT_CONST_ABS_END,   /* 常量绝对位置（基于 END）：(END-n, kw) */
    FT_DYNAMIC_FIND_FWD,/* 动态向结尾查找：('c', kw) */
    FT_DYNAMIC_FIND_REV /* 动态向开头查找：('<', 'c', kw) */
} feature_type_t;

/**
 * 特征元组
 * (移动操作，关键字) 对
 */
typedef struct {
    feature_type_t type;
    int value;              /* 偏移量或字符 ASCII 值 */
    const char *keyword;    /* 关键字，可为 NULL */
    size_t keyword_len;
} feature_tuple_t;

/* ==================== 提取序列定义 ==================== */

/**
 * 提取器操作类型（10 种）
 * 根据设计文档 2.2 版本 5.4 节
 */
typedef enum {
    /* 捕获操作（产生参数）*/
    EX_CAPTURE_LEN,     /* 定长捕获 */
    EX_CAPTURE_CHR,     /* 捕获到字符 */
    EX_CAPTURE_END,     /* 捕获到结尾 */

    /* 移动操作（不产生参数）*/
    EX_SKIP_LEN,        /* 跳过固定长度（由 OP_MATCH 优化而来）*/
    EX_JUMP_ABS,        /* 绝对跳转（基于 HEAD）*/
    EX_JUMP_END,        /* END 跳转 */
    EX_JUMP_FWD,        /* 正向移动 */
    EX_JUMP_BACK,       /* 负向移动 */
    EX_FIND_FWD,        /* 正向查找 */
    EX_FIND_REV         /* 反向查找 */
} extractor_op_type_t;

/**
 * 提取器操作
 */
typedef struct {
    extractor_op_type_t type;
    union {
        struct { size_t length; } capture_len;      /* EX_CAPTURE_LEN */
        struct { char ch; } capture_chr;            /* EX_CAPTURE_CHR */
        struct { size_t length; } skip_len;         /* EX_SKIP_LEN */
        struct { size_t pos; } jump_abs;            /* EX_JUMP_ABS */
        struct { int is_end; int offset; } jump_end;/* EX_JUMP_END */
        struct { size_t offset; } jump_fwd;         /* EX_JUMP_FWD */
        struct { size_t offset; } jump_back;        /* EX_JUMP_BACK */
        struct { char ch; } find_fwd;               /* EX_FIND_FWD */
        struct { char ch; } find_rev;               /* EX_FIND_REV */
    } data;
} extractor_op_t;

/**
 * 提取器
 * 保存原始模式的完整操作序列，用于参数提取
 */
typedef struct {
    extractor_op_t *ops;
    size_t op_count;
    size_t param_count;  /* 该提取器产生的参数数量 */
} extractor_t;

/* ==================== 编译结果 ==================== */

/**
 * 编译结果状态
 * 根据设计文档 2.2 版本 八、错误码定义
 */
typedef enum {
    COMPILE_OK = 0,
    E_INVALID_PATTERN,      /* 模式格式无效 */
    E_UNCLOSED_QUOTE,       /* 未闭合的引号 */
    E_INVALID_NUMBER,       /* 无效的数字 */
    E_INVALID_POSITION,     /* 无效的位置表达式 */
    E_EMPTY_SEGMENT,        /* 空的段模式 */
    E_NO_LEADING_SLASH,     /* 模式不以 / 开头 */
    E_END_CONFLICT,         /* END 后存在其他操作 */
    E_END_POSITIVE_OFFSET,  /* END 与正数相加 */
    E_END_DUPLICATE,        /* 同一持有单元内 END 重复 */
    E_ROUTE_CONFLICT        /* 路由特征序列冲突 */
} compile_status_t;

/**
 * 编译结果
 */
typedef struct {
    compile_status_t status;

    /* 特征序列 */
    feature_tuple_t *features;
    size_t feature_count;

    /* 提取序列（已优化）*/
    extractor_op_t *extractors;
    size_t extractor_count;
    size_t param_count;              /* 参数数量 */

    /* 错误信息 */
    const char *error_msg;
    size_t error_pos;  /* 错误位置 */
} compile_result_t;

/* ==================== 编译 API ==================== */

/**
 * 编译路由模式（单个段）
 * @param pattern 模式字符串（单个段的内容，不包含前导/）
 * @return 编译结果
 *
 * 注意：调用者负责释放编译结果
 */
compile_result_t pattern_compile(const char *pattern);

/**
 * 释放编译结果
 * @param result 编译结果指针
 */
void pattern_compile_free(compile_result_t *result);

/**
 * 创建提取器
 * @param ops 操作符数组
 * @param op_count 操作符数量
 * @return 提取器指针
 */
extractor_t *extractor_create(const extractor_op_t *ops, size_t op_count);

/**
 * 释放提取器
 * @param extractor 提取器指针
 */
void extractor_destroy(extractor_t *extractor);

/* ==================== 内部工具函数 ==================== */

/**
 * 解析操作符序列（词法分析）
 * @param pattern 模式字符串
 * @param out_ops 输出操作符数组
 * @param out_count 输出操作符数量
 * @param out_capacity 输出数组容量
 * @return 0 成功，-1 失败
 */
int pattern_lex(const char *pattern, op_t **out_ops,
                size_t *out_count, size_t *out_capacity);

/**
 * 从操作符序列生成特征序列（状态机驱动）
 * @param ops 操作符数组
 * @param op_count 操作符数量
 * @param out_features 输出特征数组
 * @param out_count 输出特征数量
 * @param out_capacity 输出数组容量
 * @return 0 成功，-1 失败
 */
int pattern_generate_features(const op_t *ops, size_t op_count,
                              feature_tuple_t **out_features,
                              size_t *out_count, size_t *out_capacity);

/**
 * 从操作符序列生成提取序列（含编译时优化）
 * @param ops 操作符数组
 * @param op_count 操作符数量
 * @param out_extractors 输出提取操作数组
 * @param out_count 输出提取操作数量
 * @param out_param_count 输出参数数量
 * @return 0 成功，-1 失败
 */
int pattern_generate_extractors(const op_t *ops, size_t op_count,
                                extractor_op_t **out_extractors,
                                size_t *out_count, size_t *out_param_count);

#endif /* PATTERN_COMPILER_H */
