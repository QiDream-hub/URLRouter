#ifndef FEATURE_COMPILER_H
#define FEATURE_COMPILER_H

#include "pattern_compiler.h"
#include <stddef.h>

/* ============================================================
 * URLRouter 特征序列编译器
 *
 * 将操作符序列编译为特征序列
 * 采用状态机驱动（IDLE/HOLD 两状态）
 * 见设计文档 2.2 版本 4.1 节
 * ============================================================ */

/**
 * 特征序列编译：操作符序列 → 特征序列
 * @param ops 操作符数组
 * @param op_count 操作符数量
 * @param out_features 输出特征数组
 * @param out_count 输出特征数量
 * @param out_capacity 输出数组容量
 * @return 0 成功，-1 失败
 *
 * 状态机规则：
 * - IDLE + 常量操作 → HOLD
 * - IDLE + 动态操作 → HOLD
 * - IDLE + 关键字 → 输出 (0, kw)，保持 IDLE
 * - HOLD + 常量操作 → 尝试相加，否则输出持有
 * - HOLD + 动态操作 → 输出持有，持有新元组
 * - HOLD + 关键字 → 输出 (持有值，kw)，清空持有
 */
int pattern_generate_features(const op_t *ops, size_t op_count,
                              feature_tuple_t **out_features,
                              size_t *out_count, size_t *out_capacity);

/**
 * 释放特征数组
 * @param features 特征数组
 * @param count 特征数量
 */
void feature_array_free(feature_tuple_t *features, size_t count);

#endif /* FEATURE_COMPILER_H */
