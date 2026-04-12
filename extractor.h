#ifndef EXTRACTOR_H
#define EXTRACTOR_H

#include "pattern_compiler.h"
#include "router.h"

/* ==================== 段提取器 ==================== */

/* segment_extractor_t 与 extractor_t 结构相同，可以直接类型转换 */
typedef extractor_t segment_extractor_t;

/* 函数声明 */
segment_extractor_t *segment_extractor_create(const operator_t *ops, size_t op_count);
void segment_extractor_destroy(segment_extractor_t *seg_ext);

/**
 * 完整提取器
 * 包含所有段的提取操作
 */
typedef struct {
    segment_extractor_t **segments;  /* 每段的提取器 */
    size_t segment_count;
    size_t total_params;             /* 总参数数量 */
} full_extractor_t;

/* ==================== 参数提取 API ==================== */

/**
 * 执行单段提取
 */
int segment_extractor_execute(const segment_extractor_t *seg_ext,
                              const char *segment, size_t segment_len,
                              route_param_t *params, size_t param_capacity,
                              size_t *param_count);

/**
 * 执行完整提取（多段）
 */
int full_extractor_execute(const full_extractor_t *full_ext,
                           const char **segments, size_t segment_count,
                           route_param_t *params, size_t param_capacity,
                           size_t *out_count);

/**
 * 创建完整提取器
 */
full_extractor_t *full_extractor_create(segment_extractor_t **segment_extractors,
                                         size_t segment_count);

/**
 * 释放完整提取器
 */
void full_extractor_destroy(full_extractor_t *full_ext);

#endif /* EXTRACTOR_H */
