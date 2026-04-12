#ifndef EXTRACTOR_H
#define EXTRACTOR_H

#include "pattern_compiler.h"
#include "router.h"

/* ==================== 参数提取 API ==================== */

/**
 * 执行参数提取
 * @param extractor 提取器
 * @param segment 段内容
 * @param segment_len 段长度
 * @param params 参数数组（调用者提供）
 * @param param_capacity 参数数组容量
 * @param param_count 输入/输出：实际参数数量
 * @return 0 成功，-1 失败
 * 
 * 注意：返回的参数是指向 segment 的指针（零拷贝）
 */
int extractor_execute(const extractor_t *extractor,
                      const char *segment, size_t segment_len,
                      route_param_t *params, size_t param_capacity,
                      size_t *param_count);

/**
 * 合并多个提取器为一个
 * @param extractors 提取器数组
 * @param count 提取器数量
 * @return 合并后的提取器
 */
extractor_t *extractor_merge(extractor_t **extractors, size_t count);

#endif /* EXTRACTOR_H */
