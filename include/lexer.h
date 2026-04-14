#ifndef LEXER_H
#define LEXER_H

#include "pattern_compiler.h"
#include <stddef.h>

/* ============================================================
 * URLRouter 词法分析器
 *
 * 将模式字符串解析为操作符序列
 * 支持 10 种操作符（设计文档 2.2 版本 2.1 节）
 * ============================================================ */

/**
 * 词法分析：模式字符串 → 操作符序列
 * @param pattern 模式字符串（单个段的内容）
 * @param out_ops 输出操作符数组
 * @param out_count 输出操作符数量
 * @param out_capacity 输出数组容量
 * @return 0 成功，-1 失败
 *
 * 支持的操作符：
 * - $'文本'     -> OP_MATCH
 * - ${数字}     -> OP_CAPTURE_LEN
 * - ${'字符'}   -> OP_CAPTURE_CHR
 * - ${}         -> OP_CAPTURE_END
 * - $[位置]     -> OP_JUMP_ABS
 * - $[END]      -> OP_JUMP_END(is_end=1, offset=0)
 * - $[END-n]    -> OP_JUMP_END(is_end=1, offset=n)
 * - $[>偏移]    -> OP_JUMP_FWD
 * - $[<偏移]    -> OP_JUMP_BACK
 * - $[>'字符']  -> OP_FIND_FWD
 * - $[<'字符']  -> OP_FIND_REV
 */
int pattern_lex(const char *pattern, op_t **out_ops,
                size_t *out_count, size_t *out_capacity);

/**
 * 释放操作符数组
 * @param ops 操作符数组
 */
void op_array_free(op_t *ops);

#endif /* LEXER_H */
