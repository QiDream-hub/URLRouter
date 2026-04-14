#include "pattern_compiler.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#define INITIAL_CAPACITY 16

/* ============================================================
 * URLRouter 模式编译器 - 实现文件
 * 
 * 根据设计文档 2.2 版本实现
 * - 词法分析：10 种操作符
 * - 特征序列编译：状态机驱动（IDLE/HOLD 两状态）
 * - 提取序列编译：常量移动合并优化
 * ============================================================ */

/* ==================== 工具函数 ==================== */

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static size_t parse_number(const char *str, size_t *num) {
    size_t result = 0;
    size_t i = 0;

    while (is_digit(str[i])) {
        result = result * 10 + (str[i] - '0');
        i++;
    }

    *num = result;
    return i;
}

static void feature_array_grow(feature_tuple_t **features,
                               size_t *capacity) {
    size_t new_cap = *capacity * 2;
    feature_tuple_t *new_arr = realloc(*features,
                                        new_cap * sizeof(feature_tuple_t));
    if (new_arr) {
        *features = new_arr;
        *capacity = new_cap;
    }
}

static void extractor_array_grow(extractor_op_t **extractors,
                                  size_t *capacity) {
    size_t new_cap = *capacity * 2;
    extractor_op_t *new_arr = realloc(*extractors,
                                       new_cap * sizeof(extractor_op_t));
    if (new_arr) {
        *extractors = new_arr;
        *capacity = new_cap;
    }
}

/* ==================== 词法分析 ==================== */

/**
 * 词法分析器
 * 将模式字符串解析为操作符序列
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
                size_t *out_count, size_t *out_capacity) {
    if (!pattern || !out_ops || !out_count || !out_capacity) {
        return -1;
    }

    *out_ops = NULL;
    *out_count = 0;
    *out_capacity = INITIAL_CAPACITY;

    *out_ops = calloc(*out_capacity, sizeof(op_t));
    if (!*out_ops) {
        return -1;
    }

    const char *p = pattern;
    size_t count = 0;

    while (*p) {
        /* 确保有足够空间 */
        if (count >= *out_capacity) {
            feature_array_grow((feature_tuple_t **)out_ops, out_capacity);
        }

        op_t *op = &(*out_ops)[count];
        memset(op, 0, sizeof(op_t));

        if (*p == '$') {
            p++; /* 跳过 '$' */

            if (*p == '\'') {
                /* $'文本' - 精确匹配 */
                p++; /* 跳过开头的 '\'' */
                const char *start = p;

                while (*p && *p != '\'') {
                    p++;
                }

                if (*p != '\'') {
                    free(*out_ops);
                    *out_ops = NULL;
                    return -1; /* 未闭合的单引号 */
                }

                op->type = OP_MATCH;
                op->data.match.text = start;
                op->data.match.len = p - start;
                p++; /* 跳过闭合的 '\'' */
                count++;

            } else if (*p == '{') {
                p++; /* 跳过 '{' */

                if (*p == '}') {
                    /* ${} - 捕获到结尾 */
                    op->type = OP_CAPTURE_END;
                    p++; /* 跳过 '}' */
                    count++;

                } else {
                    /* ${数字} 或 ${'字符'} */
                    if (*p == '\'') {
                        /* ${'字符'} - 捕获到字符 */
                        p++; /* 跳过开头的 '\'' */

                        if (*p == '\0') {
                            free(*out_ops);
                            *out_ops = NULL;
                            return -1;
                        }

                        char ch = *p;
                        p++;

                        if (*p != '\'') {
                            free(*out_ops);
                            *out_ops = NULL;
                            return -1; /* 期望闭合的单引号 */
                        }
                        p++; /* 跳过闭合的 '\'' */

                        if (*p != '}') {
                            free(*out_ops);
                            *out_ops = NULL;
                            return -1; /* 期望 '}' */
                        }
                        p++; /* 跳过 '}' */

                        op->type = OP_CAPTURE_CHR;
                        op->data.find.ch = ch;
                        count++;

                    } else if (is_digit(*p)) {
                        /* ${数字} - 定长捕获 */
                        size_t len;
                        size_t consumed = parse_number(p, &len);
                        p += consumed;

                        if (*p != '}') {
                            free(*out_ops);
                            *out_ops = NULL;
                            return -1;
                        }
                        p++; /* 跳过 '}' */

                        if (len == 0) {
                            free(*out_ops);
                            *out_ops = NULL;
                            return -1; /* 长度必须为正整数 */
                        }

                        op->type = OP_CAPTURE_LEN;
                        op->data.length = len;
                        count++;

                    } else {
                        /* 无效的 ${} 内容 */
                        free(*out_ops);
                        *out_ops = NULL;
                        return -1;
                    }
                }

            } else if (*p == '[') {
                p++; /* 跳过 '[' */

                if (*p == '>') {
                    p++; /* 跳过 '>' */
                    
                    if (*p == '\'') {
                        /* $[>'字符'] - 向结尾查找字符 */
                        p++; /* 跳过开头的 '\'' */
                        
                        if (*p == '\0') {
                            free(*out_ops);
                            *out_ops = NULL;
                            return -1;
                        }
                        
                        char ch = *p;
                        p++;
                        
                        if (*p != '\'') {
                            free(*out_ops);
                            *out_ops = NULL;
                            return -1;
                        }
                        p++; /* 跳过闭合的 '\'' */
                        
                        if (*p != ']') {
                            free(*out_ops);
                            *out_ops = NULL;
                            return -1;
                        }
                        p++; /* 跳过 ']' */
                        
                        op->type = OP_FIND_FWD;
                        op->data.find.ch = ch;
                        count++;
                        
                    } else {
                        /* $[>偏移] - 向结尾移动 */
                        size_t offset;
                        size_t consumed = parse_number(p, &offset);
                        p += consumed;

                        if (*p != ']') {
                            free(*out_ops);
                            *out_ops = NULL;
                            return -1;
                        }
                        p++; /* 跳过 ']' */

                        op->type = OP_JUMP_FWD;
                        op->data.offset = offset;
                        count++;
                    }

                } else if (*p == '<') {
                    p++; /* 跳过 '<' */
                    
                    if (*p == '\'') {
                        /* $[<'字符'] - 向开头查找字符 */
                        p++; /* 跳过开头的 '\'' */
                        
                        if (*p == '\0') {
                            free(*out_ops);
                            *out_ops = NULL;
                            return -1;
                        }
                        
                        char ch = *p;
                        p++;
                        
                        if (*p != '\'') {
                            free(*out_ops);
                            *out_ops = NULL;
                            return -1;
                        }
                        p++; /* 跳过闭合的 '\'' */
                        
                        if (*p != ']') {
                            free(*out_ops);
                            *out_ops = NULL;
                            return -1;
                        }
                        p++; /* 跳过 ']' */
                        
                        op->type = OP_FIND_REV;
                        op->data.find.ch = ch;
                        count++;
                        
                    } else {
                        /* $[<偏移] - 向开头移动 */
                        size_t offset;
                        size_t consumed = parse_number(p, &offset);
                        p += consumed;

                        if (*p != ']') {
                            free(*out_ops);
                            *out_ops = NULL;
                            return -1;
                        }
                        p++; /* 跳过 ']' */

                        op->type = OP_JUMP_BACK;
                        op->data.offset = offset;
                        count++;
                    }

                } else if (strncmp(p, "END", 3) == 0) {
                    /* $[END] 或 $[END-n] - END 跳转 */
                    p += 3; /* 跳过 'END' */

                    op->type = OP_JUMP_END;
                    op->data.jump_end.is_end = 1;

                    if (*p == '-') {
                        p++; /* 跳过 '-' */
                        size_t offset;
                        size_t consumed = parse_number(p, &offset);
                        p += consumed;

                        op->data.jump_end.offset = (int)offset;

                    } else {
                        op->data.jump_end.offset = 0;
                    }

                    if (*p != ']') {
                        free(*out_ops);
                        *out_ops = NULL;
                        return -1;
                    }
                    p++; /* 跳过 ']' */
                    count++;

                } else if (is_digit(*p)) {
                    /* $[位置] - 绝对跳转（基于 HEAD）*/
                    size_t pos;
                    size_t consumed = parse_number(p, &pos);
                    p += consumed;

                    if (*p != ']') {
                        free(*out_ops);
                        *out_ops = NULL;
                        return -1;
                    }
                    p++; /* 跳过 ']' */

                    op->type = OP_JUMP_ABS;
                    op->data.pos = pos;
                    count++;

                } else {
                    free(*out_ops);
                    *out_ops = NULL;
                    return -1;
                }

            } else {
                free(*out_ops);
                *out_ops = NULL;
                return -1; /* 无效的 $ 操作符 */
            }

        } else {
            free(*out_ops);
            *out_ops = NULL;
            return -1; /* 非 $ 开头的字符，视为无效语法 */
        }
    }

    *out_count = count;
    return 0;
}

/* ==================== 特征序列编译（状态机驱动） ==================== */

/**
 * 操作分类（用于状态机）
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
    int has_value;       /* 是否有值 */
    int is_dynamic;      /* 是否是动态操作 */
    int is_end_based;    /* 是否基于 END */
    int value;           /* 偏移量或字符 ASCII */
    char ch;             /* 查找字符（动态操作）*/
    int is_reverse;      /* 是否是反向查找 */
} hold_tuple_t;

/**
 * 获取操作符的分类和基础元组值
 */
static int get_op_class(const op_t *op, op_class_t *out_class, 
                        hold_tuple_t *out_tuple) {
    memset(out_tuple, 0, sizeof(hold_tuple_t));
    out_tuple->has_value = 1;

    switch (op->type) {
        case OP_CAPTURE_LEN:
            *out_class = OP_CLASS_CONST_POS;
            out_tuple->value = (int)op->data.length;
            return 0;

        case OP_JUMP_FWD:
            *out_class = OP_CLASS_CONST_POS;
            out_tuple->value = (int)op->data.offset;
            return 0;

        case OP_JUMP_BACK:
            *out_class = OP_CLASS_CONST_NEG;
            out_tuple->value = -(int)op->data.offset;
            return 0;

        case OP_JUMP_ABS:
            *out_class = OP_CLASS_CONST_ABS_HEAD;
            out_tuple->value = (int)op->data.pos;
            return 0;

        case OP_JUMP_END:
            *out_class = OP_CLASS_CONST_ABS_END;
            out_tuple->is_end_based = 1;
            out_tuple->value = -op->data.jump_end.offset; /* END-n 表示为负偏移 */
            return 0;

        case OP_CAPTURE_CHR:
            *out_class = OP_CLASS_DYNAMIC_FWD;
            out_tuple->is_dynamic = 1;
            out_tuple->ch = op->data.find.ch;
            out_tuple->value = (int)(unsigned char)op->data.find.ch;
            return 0;

        case OP_FIND_FWD:
            *out_class = OP_CLASS_DYNAMIC_FWD;
            out_tuple->is_dynamic = 1;
            out_tuple->ch = op->data.find.ch;
            out_tuple->value = (int)(unsigned char)op->data.find.ch;
            return 0;

        case OP_FIND_REV:
            *out_class = OP_CLASS_DYNAMIC_REV;
            out_tuple->is_dynamic = 1;
            out_tuple->is_reverse = 1;
            out_tuple->ch = op->data.find.ch;
            out_tuple->value = (int)(unsigned char)op->data.find.ch;
            return 0;

        case OP_MATCH:
            *out_class = OP_CLASS_KEYWORD;
            return 0;

        case OP_CAPTURE_END:
            *out_class = OP_CLASS_CONST_ABS_END;
            out_tuple->is_end_based = 1;
            out_tuple->value = 0; /* 纯 END */
            return 0;

        default:
            return -1;
    }
}

/**
 * 尝试将两个常量值相加
 * @return 0 成功，-1 不可相加
 */
static int try_add_constants(hold_tuple_t *hold, op_class_t new_class, 
                             int new_value) {
    /* 动态操作不能与任何操作相加 */
    if (hold->is_dynamic) {
        return -1;
    }

    /* 检查基准兼容性 */
    if (hold->is_end_based) {
        /* END 基准：只能与负数（CONST_NEG）或 CONST_ABS_END 相加 */
        if (new_class == OP_CLASS_CONST_NEG) {
            /* END + 负数 = END - n */
            hold->value += new_value; /* new_value 是负数 */
            return 0;
        } else if (new_class == OP_CLASS_CONST_ABS_END) {
            /* END + END 偏移 */
            hold->value += new_value;
            return 0;
        } else {
            return -1; /* END 不能与正数或 HEAD 基准相加 */
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
    } else if (hold->is_end_based) {
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
                /* 不可相加：先输出持有，再持有新元组 */
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

/* ==================== 提取序列编译（含常量合并优化） ==================== */

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
 */
static int extractor_is_const_move(extractor_op_type_t type) {
    return (type == EX_SKIP_LEN ||
            type == EX_JUMP_FWD ||
            type == EX_JUMP_BACK);
}

/**
 * 从操作符序列生成提取序列（含编译时优化）
 * 
 * 优化规则：
 * 1. OP_MATCH -> EX_SKIP_LEN（常量偏移跳过）
 * 2. 连续常量移动操作合并为一个
 * 
 * 见设计文档 2.2 版本 5.4 节
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
                /* 优化：OP_MATCH -> EX_SKIP_LEN */
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
                
                if (merge_value > 0) {
                    merge_op->type = EX_JUMP_FWD;
                    merge_op->data.jump_fwd.offset = (size_t)merge_value;
                } else if (merge_value < 0) {
                    merge_op->type = EX_JUMP_BACK;
                    merge_op->data.jump_back.offset = (size_t)(-merge_value);
                } else {
                    /* 合并值为 0，不需要输出 */
                }
                
                if (merge_value != 0) {
                    (*out_count)++;
                }
                merge_start = -1;
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
        
        if (merge_value > 0) {
            merge_op->type = EX_JUMP_FWD;
            merge_op->data.jump_fwd.offset = (size_t)merge_value;
        } else if (merge_value < 0) {
            merge_op->type = EX_JUMP_BACK;
            merge_op->data.jump_back.offset = (size_t)(-merge_value);
        }
        
        if (merge_value != 0) {
            (*out_count)++;
        }
    }

    free(temp_ops);
    return 0;
}

/* ==================== 完整编译接口 ==================== */

compile_result_t pattern_compile(const char *pattern) {
    compile_result_t result;
    memset(&result, 0, sizeof(result));

    if (!pattern || *pattern == '\0') {
        result.status = E_EMPTY_SEGMENT;
        result.error_msg = "Empty pattern";
        return result;
    }

    /* 词法分析 */
    op_t *ops = NULL;
    size_t op_count = 0;
    size_t op_capacity = 0;

    if (pattern_lex(pattern, &ops, &op_count, &op_capacity) != 0) {
        result.status = E_INVALID_PATTERN;
        result.error_msg = "Syntax error in pattern";
        return result;
    }

    /* 生成特征序列 */
    feature_tuple_t *features = NULL;
    size_t feature_count = 0;
    size_t feature_capacity = 0;

    if (pattern_generate_features(ops, op_count,
                                   &features, &feature_count,
                                   &feature_capacity) != 0) {
        free(ops);
        result.status = E_INVALID_PATTERN;
        result.error_msg = "Failed to generate features";
        return result;
    }

    /* 生成提取序列 */
    extractor_op_t *extractors = NULL;
    size_t extractor_count = 0;
    size_t param_count = 0;

    if (pattern_generate_extractors(ops, op_count,
                                     &extractors, &extractor_count,
                                     &param_count) != 0) {
        free(ops);
        free(features);
        result.status = E_INVALID_PATTERN;
        result.error_msg = "Failed to generate extractors";
        return result;
    }

    /* 填充结果 */
    result.status = COMPILE_OK;
    result.features = features;
    result.feature_count = feature_count;
    result.extractors = extractors;
    result.extractor_count = extractor_count;
    result.param_count = param_count;

    free(ops);
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
