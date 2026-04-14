#define _GNU_SOURCE

#include "../include/feature_compiler.h"
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 16

/* ============================================================
 * URLRouter 特征序列编译器 - 实现文件
 *
 * 根据设计文档 2.2 版本实现
 * 状态机驱动（IDLE/HOLD 两状态）
 * 见设计文档 2.2 版本 4.1 节
 * ============================================================ */

/* ==================== 工具函数 ==================== */

static void feature_array_grow(feature_tuple_t **features, size_t *capacity) {
    size_t new_cap = *capacity * 2;
    feature_tuple_t *new_arr = realloc(*features, new_cap * sizeof(feature_tuple_t));
    if (new_arr) {
        *features = new_arr;
        *capacity = new_cap;
    }
}

void feature_array_free(feature_tuple_t *features, size_t count) {
    if (!features) return;
    for (size_t i = 0; i < count; i++) {
        if (features[i].keyword) {
            free((char *)features[i].keyword);
        }
    }
    free(features);
}

/* ==================== 状态机内部类型 ==================== */

/**
 * 操作分类（用于状态机）
 * 根据设计文档 4.1 节，捕获操作应该归类为对应的常量/动态操作
 */
typedef enum {
    OP_CLASS_CONST_POS,      /* 正向常量移动 */
    OP_CLASS_CONST_NEG,      /* 负向常量移动 */
    OP_CLASS_CONST_ABS_HEAD, /* 绝对常量移动（基于 HEAD）*/
    OP_CLASS_CONST_ABS_END,  /* 绝对常量移动（基于 END）*/
    OP_CLASS_DYNAMIC_FWD,    /* 动态正向查找 */
    OP_CLASS_DYNAMIC_REV,    /* 动态反向查找 */
    OP_CLASS_KEYWORD         /* 关键字匹配 */
} op_class_t;

/**
 * 持有元组（状态机内部使用）
 */
typedef struct {
    int has_value;        /* 是否有值 */
    int is_dynamic;       /* 是否是动态操作 */
    int is_end_based;     /* 是否基于 END */
    int is_head_abs;      /* 是否是绝对位置（基于 HEAD）*/
    int value;            /* 偏移量或字符 ASCII */
    char ch;              /* 查找字符（动态操作）*/
    int is_reverse;       /* 是否是反向查找 */
} hold_tuple_t;

/* ==================== 操作分类 ==================== */

/**
 * 获取操作符的分类和基础元组值
 * 
 * 根据设计文档 4.1 节：
 * - OP_CAPTURE_LEN → CONST_POS（正向常量移动）
 * - OP_CAPTURE_CHR → DYNAMIC_FIND_FWD（动态正向查找）
 * - OP_CAPTURE_END → CONST_ABS_END（绝对位置基于 END）
 */
static int get_op_class(const op_t *op, op_class_t *out_class, hold_tuple_t *out_tuple) {
    memset(out_tuple, 0, sizeof(hold_tuple_t));
    out_tuple->has_value = 1;

    switch (op->type) {
        case OP_CAPTURE_LEN:
            /* ${n} - 定长捕获：正向常量移动 */
            *out_class = OP_CLASS_CONST_POS;
            out_tuple->value = (int)op->data.length;
            return 0;

        case OP_CAPTURE_CHR:
            /* ${'c'} - 捕获到字符：动态正向查找 */
            *out_class = OP_CLASS_DYNAMIC_FWD;
            out_tuple->is_dynamic = 1;
            out_tuple->ch = op->data.find.ch;
            out_tuple->value = (int)(unsigned char)op->data.find.ch;
            return 0;

        case OP_CAPTURE_END:
            /* ${} - 捕获到结尾：绝对位置基于 END */
            *out_class = OP_CLASS_CONST_ABS_END;
            out_tuple->is_end_based = 1;
            out_tuple->value = 0;  /* 纯 END */
            return 0;

        case OP_JUMP_FWD:
            /* $[>n] - 向结尾移动：正向常量移动 */
            *out_class = OP_CLASS_CONST_POS;
            out_tuple->value = (int)op->data.offset;
            return 0;

        case OP_JUMP_BACK:
            /* $[<n] - 向开头移动：负向常量移动（从当前位置回退）*/
            *out_class = OP_CLASS_CONST_NEG;
            out_tuple->value = -(int)op->data.offset;
            return 0;

        case OP_JUMP_ABS:
            /* $[n] - 绝对跳转：绝对位置基于 HEAD */
            *out_class = OP_CLASS_CONST_ABS_HEAD;
            out_tuple->is_head_abs = 1;
            out_tuple->value = (int)op->data.pos;
            return 0;

        case OP_JUMP_END:
            /* $[END] / $[END-n] - END 跳转：绝对位置基于 END */
            *out_class = OP_CLASS_CONST_ABS_END;
            out_tuple->is_end_based = 1;
            out_tuple->value = -op->data.jump_end.offset; /* END-n 表示为负偏移 */
            return 0;

        case OP_FIND_FWD:
            /* $[>'c'] - 向结尾查找：动态正向查找 */
            *out_class = OP_CLASS_DYNAMIC_FWD;
            out_tuple->is_dynamic = 1;
            out_tuple->ch = op->data.find.ch;
            out_tuple->value = (int)(unsigned char)op->data.find.ch;
            return 0;

        case OP_FIND_REV:
            /* $[<'c'] - 向开头查找：动态反向查找 */
            *out_class = OP_CLASS_DYNAMIC_REV;
            out_tuple->is_dynamic = 1;
            out_tuple->is_reverse = 1;
            out_tuple->ch = op->data.find.ch;
            out_tuple->value = (int)(unsigned char)op->data.find.ch;
            return 0;

        case OP_MATCH:
            /* $'text' - 关键字匹配 */
            *out_class = OP_CLASS_KEYWORD;
            return 0;

        default:
            return -1;
    }
}

/* ==================== 常量相加规则 ==================== */

/**
 * 尝试将两个常量值相加
 * @return 0 成功，-1 不可相加
 * 
 * 根据设计文档 4.1 节 常量相加规则：
 * - 动态操作不能与任何操作相加
 * - END 基准只能与负数或 CONST_ABS_END 相加
 * - HEAD 基准可以与 CONST_POS/CONST_NEG/CONST_ABS_HEAD 相加
 */
static int try_add_constants(hold_tuple_t *hold, op_class_t new_class, int new_value) {
    /* 动态操作不能与任何操作相加 */
    if (hold->is_dynamic) {
        return -1;
    }

    /* 检查基准兼容性 */
    if (hold->is_end_based) {
        /* END 基准：只能与负数（CONST_NEG）或 CONST_ABS_END 相加 */
        if (new_class == OP_CLASS_CONST_POS) {
            /* END 不能加正数 */
            return -1;
        } else if (new_class == OP_CLASS_CONST_NEG || new_class == OP_CLASS_CONST_ABS_END) {
            /* END + 负数 = END - n */
            hold->value += new_value; /* new_value 是负数 */
            return 0;
        } else {
            return -1;
        }
    } else {
        /* HEAD 基准或纯偏移：可以与 CONST_POS/CONST_NEG/CONST_ABS_HEAD 相加 */
        if (new_class == OP_CLASS_CONST_POS) {
            hold->value += new_value;
            return 0;
        } else if (new_class == OP_CLASS_CONST_NEG) {
            hold->value += new_value; /* new_value 是负数 */
            return 0;
        } else if (new_class == OP_CLASS_CONST_ABS_HEAD) {
            /* HEAD 基准 + 绝对位置：转换为 HEAD + n */
            hold->value = new_value;
            return 0;
        } else {
            return -1;
        }
    }
}

/* ==================== 输出持有元组 ==================== */

/**
 * 输出持有元组到特征数组
 */
static int output_hold_tuple(feature_tuple_t **features, size_t *capacity,
                             size_t *count, hold_tuple_t *hold,
                             const char *keyword, size_t keyword_len) {
    if (*count >= *capacity) {
        feature_array_grow(features, capacity);
    }

    feature_tuple_t *ft = &(*features)[*count];
    memset(ft, 0, sizeof(feature_tuple_t));

    if (hold->is_dynamic) {
        if (hold->is_reverse) {
            ft->type = FT_DYNAMIC_FIND_REV;
        } else {
            ft->type = FT_DYNAMIC_FIND_FWD;
        }
        ft->value = hold->value;
    } else if (hold->is_head_abs) {
        /* 绝对位置（基于 HEAD）*/
        ft->type = FT_CONST_ABS_HEAD;
        ft->value = hold->value;
    } else if (hold->is_end_based) {
        /* 绝对位置（基于 END）*/
        ft->type = FT_CONST_ABS_END;
        ft->value = hold->value; /* 负值表示 END-n */
    } else {
        /* HEAD 基准或纯偏移 */
        if (hold->value >= 0) {
            ft->type = FT_CONST_REL_FWD;
        } else {
            ft->type = FT_CONST_REL_BACK;
        }
        ft->value = hold->value;
    }

    if (keyword) {
        ft->keyword_len = keyword_len;
        ft->keyword = malloc(keyword_len + 1);
        if (ft->keyword) {
            memcpy((char *)ft->keyword, keyword, keyword_len);
            ((char *)ft->keyword)[keyword_len] = '\0';
        }
    } else {
        ft->keyword = NULL;
        ft->keyword_len = 0;
    }

    (*count)++;
    return 0;
}

/* ==================== 特征序列编译主函数 ==================== */

/**
 * 从操作符序列生成特征序列（状态机驱动）
 * 
 * 状态机：
 * - IDLE: 空闲，无持有操作
 * - HOLD: 持有一个移动元组，等待合并或输出
 * 
 * 状态转换规则见设计文档 2.2 版本 4.1 节
 */
int pattern_generate_features(const op_t *ops, size_t op_count,
                              feature_tuple_t **out_features,
                              size_t *out_count, size_t *out_capacity) {
    if (!ops || !out_features || !out_count || !out_capacity) {
        return -1;
    }

    *out_features = NULL;
    *out_count = 0;
    *out_capacity = INITIAL_CAPACITY;

    *out_features = calloc(*out_capacity, sizeof(feature_tuple_t));
    if (!*out_features) {
        return -1;
    }

    hold_tuple_t hold;
    memset(&hold, 0, sizeof(hold));
    int state = 0; /* 0 = IDLE, 1 = HOLD */
    size_t count = 0;

    for (size_t i = 0; i < op_count; i++) {
        const op_t *op = &ops[i];
        op_class_t op_class;
        hold_tuple_t tuple;

        if (get_op_class(op, &op_class, &tuple) != 0) {
            continue; /* 跳过无效操作 */
        }

        if (state == 0) { /* IDLE */
            if (op_class == OP_CLASS_KEYWORD) {
                /* IDLE + 关键字：直接输出 (0, kw) */
                output_hold_tuple(out_features, out_capacity, &count,
                                  &hold, op->data.match.text, op->data.match.len);
                /* 保持在 IDLE */
            } else {
                /* IDLE + 其他：持有元组 */
                hold = tuple;
                state = 1; /* HOLD */
            }
        } else { /* HOLD */
            if (op_class == OP_CLASS_KEYWORD) {
                /* HOLD + 关键字：合并输出 (持有值，kw) */
                output_hold_tuple(out_features, out_capacity, &count,
                                  &hold, op->data.match.text, op->data.match.len);
                memset(&hold, 0, sizeof(hold));
                state = 0; /* IDLE */
            } else if (try_add_constants(&hold, op_class, tuple.value) == 0) {
                /* 常量可相加：合并到持有元组 */
                /* 保持在 HOLD */
            } else {
                /* 不可相加（动态操作或基准不兼容）：先输出持有，再持有新元组 */
                output_hold_tuple(out_features, out_capacity, &count, &hold, NULL, 0);
                hold = tuple;
                state = 1; /* HOLD */
            }
        }
    }

    /* 扫描结束：若在 HOLD，输出持有 */
    if (state == 1) {
        output_hold_tuple(out_features, out_capacity, &count, &hold, NULL, 0);
    }

    *out_count = count;
    return 0;
}
