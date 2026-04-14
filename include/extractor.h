#ifndef EXTRACTOR_H
#define EXTRACTOR_H

#include "pattern_compiler.h"
#include "router.h"

/* ============================================================
 * URLRouter 参数提取器 - 头文件
 *
 * 根据设计文档 2.2 版本实现
 * 支持 10 种提取操作类型，包括常量移动合并优化
 * ============================================================ */

/* ==================== 类型定义 ==================== */

/**
 * 段提取器（单段）
 * 与 extractor_t 相同，使用别名便于理解
 */
typedef extractor_t segment_extractor_t;

/**
 * 完整提取器
 * 包含所有段的提取操作
 */
typedef struct {
    segment_extractor_t **segments;  /* 每段的提取器 */
    size_t segment_count;
    size_t total_params;             /* 总参数数量 */
} full_extractor_t;

/* ==================== 段提取器 API ==================== */

/**
 * 创建段提取器（别名函数）
 * @param ops 提取操作数组
 * @param op_count 操作数量
 * @return 段提取器指针
 */
static inline segment_extractor_t *segment_extractor_create(const extractor_op_t *ops, size_t op_count) {
    return extractor_create(ops, op_count);
}

/**
 * 释放段提取器（别名函数）
 * @param seg_ext 段提取器指针
 */
static inline void segment_extractor_destroy(segment_extractor_t *seg_ext) {
    extractor_destroy(seg_ext);
}

/**
 * 执行单段提取
 * @param seg_ext 段提取器
 * @param segment 段内容
 * @param segment_len 段长度
 * @param params 参数数组
 * @param param_capacity 参数数组容量
 * @param param_count 输出参数数量
 * @return 0 成功，-1 失败
 */
int segment_extractor_execute(const segment_extractor_t *seg_ext,
                              const char *segment, size_t segment_len,
                              route_param_t *params, size_t param_capacity,
                              size_t *param_count);

/* ==================== 完整提取器 API ==================== */

/**
 * 创建完整提取器
 * @param segment_extractors 每段的提取器数组
 * @param segment_count 段数
 * @return 完整提取器指针
 */
full_extractor_t *full_extractor_create(segment_extractor_t **segment_extractors,
                                         size_t segment_count);

/**
 * 释放完整提取器
 * @param full_ext 完整提取器指针
 */
void full_extractor_destroy(full_extractor_t *full_ext);

/**
 * 执行完整提取（多段）
 * @param full_ext 完整提取器
 * @param segments 段数组
 * @param seg_lens 每段长度数组
 * @param segment_count 段数
 * @param params 参数数组
 * @param param_capacity 参数数组容量
 * @param out_count 输出参数总数
 * @return 0 成功，-1 失败
 */
int full_extractor_execute(const full_extractor_t *full_ext,
                           const char **segments, size_t *seg_lens,
                           size_t segment_count,
                           route_param_t *params, size_t param_capacity,
                           size_t *out_count);

#endif /* EXTRACTOR_H */
