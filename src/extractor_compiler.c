#define _GNU_SOURCE

#include "../include/extractor_compiler.h"
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 16

/* ============================================================
 * URLRouter 提取序列编译器 - 实现文件
 *
 * 根据设计文档 2.2 版本实现
 * 支持两项优化：
 * 1. 匹配操作转换为常量偏移跳过（OP_MATCH → EX_SKIP_LEN）
 * 2. 连续常量移动操作合并
 * 见设计文档 2.2 版本 5.4 节
 * ============================================================ */

/* ==================== 工具函数 ==================== */

static void extractor_array_grow(extractor_op_t **extractors, size_t *capacity) {
    size_t new_cap = *capacity * 2;
    extractor_op_t *new_arr = realloc(*extractors, new_cap * sizeof(extractor_op_t));
    if (new_arr) {
        *extractors = new_arr;
        *capacity = new_cap;
    }
}

/* ==================== 辅助判断函数 ==================== */

/**
 * 判断操作是否产生参数（提取器操作类型版本）
 */
static int extractor_op_produces_param(extractor_op_type_t type) {
    return (type == EX_CAPTURE_LEN ||
            type == EX_CAPTURE_CHR ||
            type == EX_CAPTURE_END);
}

/**
 * 判断是否是可合并的常量移动操作（提取器操作类型版本）
 * 
 * 根据设计文档 5.4.2 节，可合并的操作：
 * - EX_SKIP_LEN
 * - EX_JUMP_FWD
 * - EX_JUMP_BACK
 */
static int extractor_is_const_move(extractor_op_type_t type) {
    return (type == EX_SKIP_LEN ||
            type == EX_JUMP_FWD ||
            type == EX_JUMP_BACK);
}

/* ==================== 提取序列编译主函数 ==================== */

/**
 * 从操作符序列生成提取序列（含编译时优化）
 * 
 * 两阶段处理：
 * 1. 基础转换：将操作符序列按设计文档 5.3 规则转换为提取操作序列
 * 2. 优化合并：遍历提取操作序列，合并连续常量移动
 */
int pattern_generate_extractors(const op_t *ops, size_t op_count,
                                extractor_op_t **out_extractors,
                                size_t *out_count, size_t *out_param_count) {
    if (!ops || !out_extractors || !out_count || !out_param_count) {
        return -1;
    }

    *out_extractors = NULL;
    *out_count = 0;
    *out_param_count = 0;
    size_t capacity = INITIAL_CAPACITY;

    *out_extractors = calloc(capacity, sizeof(extractor_op_t));
    if (!*out_extractors) {
        return -1;
    }

    /* 第一阶段：基础转换，暂存到临时数组 */
    extractor_op_t *temp_ops = calloc(op_count * 2, sizeof(extractor_op_t));
    if (!temp_ops) {
        free(*out_extractors);
        *out_extractors = NULL;
        return -1;
    }
    size_t temp_count = 0;

    for (size_t i = 0; i < op_count; i++) {
        const op_t *op = &ops[i];
        extractor_op_t *ex_op = &temp_ops[temp_count++];
        memset(ex_op, 0, sizeof(extractor_op_t));

        switch (op->type) {
            case OP_CAPTURE_LEN:
                ex_op->type = EX_CAPTURE_LEN;
                ex_op->data.capture_len.length = op->data.length;
                (*out_param_count)++;
                break;

            case OP_CAPTURE_CHR:
                ex_op->type = EX_CAPTURE_CHR;
                ex_op->data.capture_chr.ch = op->data.find.ch;
                (*out_param_count)++;
                break;

            case OP_CAPTURE_END:
                ex_op->type = EX_CAPTURE_END;
                (*out_param_count)++;
                break;

            case OP_MATCH:
                /* 优化：OP_MATCH → EX_SKIP_LEN（常量偏移跳过）*/
                ex_op->type = EX_SKIP_LEN;
                ex_op->data.skip_len.length = op->data.match.len;
                break;

            case OP_JUMP_ABS:
                ex_op->type = EX_JUMP_ABS;
                ex_op->data.jump_abs.pos = op->data.pos;
                break;

            case OP_JUMP_END:
                ex_op->type = EX_JUMP_END;
                ex_op->data.jump_end.is_end = op->data.jump_end.is_end;
                ex_op->data.jump_end.offset = op->data.jump_end.offset;
                break;

            case OP_JUMP_FWD:
                ex_op->type = EX_JUMP_FWD;
                ex_op->data.jump_fwd.offset = op->data.offset;
                break;

            case OP_JUMP_BACK:
                ex_op->type = EX_JUMP_BACK;
                ex_op->data.jump_back.offset = op->data.offset;
                break;

            case OP_FIND_FWD:
                ex_op->type = EX_FIND_FWD;
                ex_op->data.find_fwd.ch = op->data.find.ch;
                break;

            case OP_FIND_REV:
                ex_op->type = EX_FIND_REV;
                ex_op->data.find_rev.ch = op->data.find.ch;
                break;

            default:
                temp_count--; /* 跳过无效操作 */
                break;
        }
    }

    /* 第二阶段：常量移动合并 */
    int merge_start = -1;   /* 合并段起始索引，-1 表示无合并中 */
    int merge_value = 0;    /* 合并值（正向为正，负向为负）*/
    int has_skip_len = 0;   /* 是否包含 EX_SKIP_LEN */

    for (size_t i = 0; i < temp_count; i++) {
        extractor_op_t *op = &temp_ops[i];

        /* 确保有足够空间 */
        if (*out_count >= capacity) {
            extractor_array_grow(out_extractors, &capacity);
        }

        /* 检查是否可以合并 */
        if (extractor_is_const_move(op->type) && !extractor_op_produces_param(op->type)) {
            /* 计算当前操作的偏移值 */
            int op_value = 0;
            switch (op->type) {
                case EX_SKIP_LEN:
                    op_value = (int)op->data.skip_len.length;
                    has_skip_len = 1;
                    break;
                case EX_JUMP_FWD:
                    op_value = (int)op->data.jump_fwd.offset;
                    break;
                case EX_JUMP_BACK:
                    op_value = -(int)op->data.jump_back.offset;
                    break;
                default:
                    op_value = 0;
                    break;
            }

            if (merge_start < 0) {
                /* 开始新的合并段 */
                merge_start = (int)i;
                merge_value = op_value;
                has_skip_len = (op->type == EX_SKIP_LEN);
            } else {
                /* 继续合并 */
                merge_value += op_value;
            }
        } else {
            /* 非合并操作或产生参数的操作：打断合并 */
            if (merge_start >= 0) {
                /* 输出之前的合并结果 */
                if (*out_count >= capacity) {
                    extractor_array_grow(out_extractors, &capacity);
                }
                extractor_op_t *merge_op = &(*out_extractors)[*out_count];

                /* 如果包含 EX_SKIP_LEN，输出为 EX_SKIP_LEN；否则输出为 EX_JUMP_FWD/EX_JUMP_BACK */
                if (has_skip_len && merge_value > 0) {
                    merge_op->type = EX_SKIP_LEN;
                    merge_op->data.skip_len.length = (size_t)merge_value;
                } else if (merge_value > 0) {
                    merge_op->type = EX_JUMP_FWD;
                    merge_op->data.jump_fwd.offset = (size_t)merge_value;
                } else if (merge_value < 0) {
                    merge_op->type = EX_JUMP_BACK;
                    merge_op->data.jump_back.offset = (size_t)(-merge_value);
                }
                /* 合并值为 0，不需要输出 */

                if (merge_value != 0 || has_skip_len) {
                    (*out_count)++;
                }
                merge_start = -1;
                has_skip_len = 0;
            }

            /* 输出当前操作（如果是产生参数的操作或不可合并的操作）*/
            if (!extractor_is_const_move(op->type) || extractor_op_produces_param(op->type)) {
                extractor_op_t *dst_op = &(*out_extractors)[*out_count];
                memcpy(dst_op, op, sizeof(extractor_op_t));
                (*out_count)++;
            }
        }
    }

    /* 处理剩余的合并段 */
    if (merge_start >= 0) {
        if (*out_count >= capacity) {
            extractor_array_grow(out_extractors, &capacity);
        }
        extractor_op_t *merge_op = &(*out_extractors)[*out_count];

        /* 如果包含 EX_SKIP_LEN，输出为 EX_SKIP_LEN；否则输出为 EX_JUMP_FWD/EX_JUMP_BACK */
        if (has_skip_len && merge_value > 0) {
            merge_op->type = EX_SKIP_LEN;
            merge_op->data.skip_len.length = (size_t)merge_value;
        } else if (merge_value > 0) {
            merge_op->type = EX_JUMP_FWD;
            merge_op->data.jump_fwd.offset = (size_t)merge_value;
        } else if (merge_value < 0) {
            merge_op->type = EX_JUMP_BACK;
            merge_op->data.jump_back.offset = (size_t)(-merge_value);
        }

        if (merge_value != 0 || has_skip_len) {
            (*out_count)++;
        }
    }

    free(temp_ops);
    return 0;
}
