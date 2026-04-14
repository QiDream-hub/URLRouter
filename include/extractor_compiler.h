#ifndef EXTRACTOR_COMPILER_H
#define EXTRACTOR_COMPILER_H

#include "pattern_compiler.h"
#include <stddef.h>

/* ============================================================
 * URLRouter 提取序列编译器
 *
 * 将操作符序列编译为提取序列
 * 执行两项优化：
 * 1. 匹配操作转换为常量偏移跳过（OP_MATCH → EX_SKIP_LEN）
 * 2. 连续常量移动操作合并
 * 见设计文档 2.2 版本 5.4 节
 * ============================================================ */

/**
 * 提取序列编译：操作符序列 → 提取序列（含优化）
 * @param ops 操作符数组
 * @param op_count 操作符数量
 * @param out_extractors 输出提取操作数组
 * @param out_count 输出提取操作数量
 * @param out_param_count 输出参数数量
 * @return 0 成功，-1 失败
 *
 * 优化规则：
 * 1. OP_MATCH → EX_SKIP_LEN（常量偏移跳过）
 * 2. 连续常量移动操作（EX_SKIP_LEN、EX_JUMP_FWD、EX_JUMP_BACK）合并为一个
 */
int pattern_generate_extractors(const op_t *ops, size_t op_count,
                                extractor_op_t **out_extractors,
                                size_t *out_count, size_t *out_param_count);

#endif /* EXTRACTOR_COMPILER_H */
