#ifndef PATTERN_COMPILER_H
#define PATTERN_COMPILER_H

#include <stddef.h>

/* ==================== 操作符定义 ==================== */

/**
 * 操作符类型
 * 对应语法规范中的 7 种操作符
 */
typedef enum {
    OP_MATCH,       /* $'文本' - 精确匹配固定字符串 */
    OP_CAPTURE_LEN, /* ${长度} - 捕获指定长度字符 */
    OP_CAPTURE_CHR, /* ${字符} - 捕获到指定字符前 */
    OP_CAPTURE_END, /* ${} - 捕获到段尾 */
    OP_JUMP_POS,    /* $[位置] - 绝对跳转到指定位置 */
    OP_JUMP_FWD,    /* $[>偏移] - 向结尾方向移动 */
    OP_JUMP_BACK    /* $[<偏移] - 向开头方向移动 */
} operator_type_t;

/**
 * 位置表达式类型（用于 OP_JUMP_POS）
 */
typedef enum {
    POS_ABS,    /* 绝对位置：非负整数 */
    POS_END,    /* 段尾位置：END */
    POS_END_OFF /* 段尾偏移：END-n */
} position_type_t;

/**
 * 操作符结构
 */
typedef struct {
    operator_type_t type;
    union {
        /* OP_MATCH */
        struct {
            const char *text;  /* 指向 pattern 内部 */
            size_t len;
        } match;
        
        /* OP_CAPTURE_LEN */
        size_t length;
        
        /* OP_CAPTURE_CHR */
        char ch;
        
        /* OP_JUMP_POS */
        struct {
            position_type_t pos_type;
            int value;  /* 绝对位置值或 END 偏移值 */
        } jump_pos;
        
        /* OP_JUMP_FWD / OP_JUMP_BACK */
        size_t offset;
    } data;
} operator_t;

/* ==================== 特征序列定义 ==================== */

/**
 * 特征元组类型
 */
typedef enum {
    FT_OFFSET_POS,  /* 正向偏移：正整数 */
    FT_OFFSET_NEG,  /* 负向偏移：负整数 */
    FT_CHAR_FIND,   /* 字符查找：单字符 */
    FT_END_OFFSET   /* END 相关偏移 */
} feature_type_t;

/**
 * 特征元组
 * (移动操作，关键字) 对
 */
typedef struct {
    feature_type_t type;
    int value;              /* 偏移量（正/负）或字符 ASCII 值 */
    const char *keyword;    /* 关键字，NULL 表示无验证 */
    size_t keyword_len;
} feature_tuple_t;

/* ==================== 提取器定义 ==================== */

/**
 * 提取器操作类型
 */
typedef enum {
    EX_CAPTURE_LEN,   /* 定长捕获 */
    EX_CAPTURE_CHR,   /* 捕获到字符 */
    EX_CAPTURE_END,   /* 捕获到段尾 */
    EX_SKIP_LEN,      /* 跳过固定长度（优化后的验证） */
    EX_JUMP_POS,      /* 绝对跳转 */
    EX_JUMP_FWD,      /* 向前移动 */
    EX_JUMP_BACK      /* 向后移动 */
} extractor_op_type_t;

/**
 * 提取器操作
 */
typedef struct {
    extractor_op_type_t type;
    union {
        size_t length;
        char ch;
        size_t pos;
        size_t offset;
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
 */
typedef enum {
    COMPILE_OK = 0,
    COMPILE_ERR_SYNTAX,      /* 语法错误 */
    COMPILE_ERR_INVALID_LEN, /* 无效的长度值 */
    COMPILE_ERR_INVALID_POS, /* 无效的位置值 */
    COMPILE_ERR_EMPTY,       /* 空模式 */
    COMPILE_ERR_NO_END_ALIGN /* 无法段尾对齐 */
} compile_status_t;

/**
 * 编译结果
 */
typedef struct {
    compile_status_t status;
    
    /* 特征序列 */
    feature_tuple_t *features;
    size_t feature_count;
    size_t feature_capacity;
    
    /* 提取器 */
    extractor_t *extractor;
    
    /* 错误信息 */
    const char *error_msg;
    size_t error_pos;  /* 错误位置 */
} compile_result_t;

/* ==================== 编译 API ==================== */

/**
 * 编译路由模式
 * @param pattern 模式字符串（单个段的内容）
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
extractor_t *extractor_create(const operator_t *ops, size_t op_count);

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
int pattern_lex(const char *pattern, operator_t **out_ops, 
                size_t *out_count, size_t *out_capacity);

/**
 * 从操作符序列生成特征序列
 * @param ops 操作符数组
 * @param op_count 操作符数量
 * @param out_features 输出特征数组
 * @param out_count 输出特征数量
 * @param out_capacity 输出数组容量
 * @return 0 成功，-1 失败
 */
int pattern_generate_features(const operator_t *ops, size_t op_count,
                              feature_tuple_t **out_features,
                              size_t *out_count, size_t *out_capacity);

#endif /* PATTERN_COMPILER_H */
