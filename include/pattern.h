#ifndef PATTERN_H
#define PATTERN_H

#include "stride/stride.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * URLRouter 段模式：词法 + 编译
 *
 * 这是从 Stride 迁入的「语法 + 编译器」层：
 *
 *     "$'v'${'.'}$'.'${}"
 *          │  url_lex()       词法分析
 *          ▼
 *     url_op_t 操作符序列
 *          │  url_compile()   翻译成 Stride 构建函数调用
 *          ▼
 *     stride_seq_t（匹配序列） + stride_seq_t（提取序列）
 *
 * URLRouter 是字节导向的：段长以字节计，编译/执行统一使用步长 8
 * （1 步 = 1 字节）。表达式中的长度/偏移单位因此就是字节。
 * ============================================================ */

/** URLRouter 的固定步长：1 步 = 1 字节 */
#define URL_PATTERN_STRIDE 8u

/* ==================== 操作符 IR ==================== */

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

/**
 * 操作符
 *
 * 携带文本的操作符（MATCH / CAPTURE_UNTIL / FIND_FWD / FIND_REV）
 * 拥有 data.literal.data 指向的、已展开转义的字节串，
 * 由 url_ops_free() 统一释放。
 */
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

/* ==================== 词法分析 ==================== */

/**
 * 词法分析：段模式 → 操作符序列
 * @param pattern       模式（单个段的内容）
 * @param pattern_len   字节长度；0 表示按 '\0' 结尾
 * @param out_ops       输出操作符数组（用 url_ops_free 释放）
 * @param out_count     输出数量
 * @param out_capacity  输出容量
 * @return 0 成功，-1 失败（语法错误）
 *
 * 支持：
 *   $'文本'    ${n}    ${'文本'}    ${}
 *   $[n]      $[END]   $[END-n]    $[>n]    $[<n]
 *   $[>'文本']  $[<'文本']
 * 文本支持 \\、\'、\n、\t、\r、\0、\xNN 转义。
 */
int url_lex(const void *pattern, size_t pattern_len, url_op_t **out_ops,
            size_t *out_count, size_t *out_capacity);

/** 释放操作符数组（含各文本副本） */
void url_ops_free(url_op_t *ops, size_t count);

/* ==================== 编译 ==================== */

typedef struct {
    stride_status_t status;

    stride_seq_t *match;   /* 匹配序列（调用方通过 url_compile_free 释放）*/
    stride_seq_t *extract; /* 提取序列（同上）*/
    size_t param_count;    /* 捕获个数 */

    const char *error_msg;
} url_compile_result_t;

/**
 * 编译单个段模式，一次产出匹配序列与提取序列
 * @param pattern     模式（单个段的内容）
 * @param pattern_len 字节长度；0 表示按 '\0' 结尾
 * @return 编译结果；status == STRIDE_OK 表示成功。始终应调用 url_compile_free
 */
url_compile_result_t url_compile(const void *pattern, size_t pattern_len);

/** 释放编译结果并清零 */
void url_compile_free(url_compile_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* PATTERN_H */
