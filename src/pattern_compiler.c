#define _GNU_SOURCE

#include "pattern_compiler.h"
#include "lexer.h"
#include "feature_compiler.h"
#include "extractor_compiler.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * URLRouter 模式编译器 - 编译入口
 *
 * 根据设计文档 2.2 版本实现
 * 协调词法分析、特征序列编译、提取序列编译
 * ============================================================ */

/* ==================== 完整编译接口 ==================== */

compile_result_t pattern_compile(const char *pattern) {
    compile_result_t result;
    memset(&result, 0, sizeof(result));

    if (!pattern || *pattern == '\0') {
        result.status = E_EMPTY_SEGMENT;
        result.error_msg = "Empty pattern";
        return result;
    }

    /* 第一阶段：词法分析 */
    op_t *ops = NULL;
    size_t op_count = 0;
    size_t op_capacity = 0;

    if (pattern_lex(pattern, &ops, &op_count, &op_capacity) != 0) {
        result.status = E_INVALID_PATTERN;
        result.error_msg = "Syntax error in pattern";
        return result;
    }

    /* 第二阶段：生成特征序列 */
    feature_tuple_t *features = NULL;
    size_t feature_count = 0;
    size_t feature_capacity = 0;

    if (pattern_generate_features(ops, op_count,
                                   &features, &feature_count,
                                   &feature_capacity) != 0) {
        op_array_free(ops);
        result.status = E_INVALID_PATTERN;
        result.error_msg = "Failed to generate features";
        return result;
    }

    /* 第三阶段：生成提取序列 */
    extractor_op_t *extractors = NULL;
    size_t extractor_count = 0;
    size_t param_count = 0;

    if (pattern_generate_extractors(ops, op_count,
                                     &extractors, &extractor_count,
                                     &param_count) != 0) {
        op_array_free(ops);
        feature_array_free(features, feature_count);
        result.status = E_INVALID_PATTERN;
        result.error_msg = "Failed to generate extractors";
        return result;
    }

    /* 释放操作符数组 */
    op_array_free(ops);

    /* 填充结果 */
    result.status = COMPILE_OK;
    result.features = features;
    result.feature_count = feature_count;
    result.extractors = extractors;
    result.extractor_count = extractor_count;
    result.param_count = param_count;

    return result;
}

void pattern_compile_free(compile_result_t *result) {
    if (!result) {
        return;
    }
    if (result->features) {
        /* 释放每个特征元组中的关键字 */
        for (size_t i = 0; i < result->feature_count; i++) {
            if (result->features[i].keyword) {
                free((char *)result->features[i].keyword);
            }
        }
        free(result->features);
    }
    if (result->extractors) {
        free(result->extractors);
    }
    memset(result, 0, sizeof(compile_result_t));
}

/* ==================== 提取器 API ==================== */

extractor_t *extractor_create(const extractor_op_t *ops, size_t op_count) {
    if (!ops || op_count == 0) {
        return NULL;
    }

    extractor_t *ext = calloc(1, sizeof(extractor_t));
    if (!ext) {
        return NULL;
    }

    ext->ops = calloc(op_count, sizeof(extractor_op_t));
    if (!ext->ops) {
        free(ext);
        return NULL;
    }

    ext->op_count = op_count;
    memcpy(ext->ops, ops, op_count * sizeof(extractor_op_t));

    /* 计算参数数量 */
    ext->param_count = 0;
    for (size_t i = 0; i < op_count; i++) {
        if (ops[i].type == EX_CAPTURE_LEN ||
            ops[i].type == EX_CAPTURE_CHR ||
            ops[i].type == EX_CAPTURE_END) {
            ext->param_count++;
        }
    }

    return ext;
}

void extractor_destroy(extractor_t *extractor) {
    if (!extractor) {
        return;
    }
    if (extractor->ops) {
        free(extractor->ops);
    }
    free(extractor);
}
