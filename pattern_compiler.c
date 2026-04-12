#include "pattern_compiler.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#define INITIAL_CAPACITY 16

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

/* ==================== 词法分析 ==================== */

int pattern_lex(const char *pattern, operator_t **out_ops,
                size_t *out_count, size_t *out_capacity) {
    if (!pattern || !out_ops || !out_count || !out_capacity) {
        return -1;
    }
    
    *out_ops = NULL;
    *out_count = 0;
    *out_capacity = INITIAL_CAPACITY;
    
    *out_ops = calloc(*out_capacity, sizeof(operator_t));
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
        
        operator_t *op = &(*out_ops)[count];
        memset(op, 0, sizeof(operator_t));
        
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
                        op->data.ch = ch;
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
                    /* $[>偏移] - 向结尾移动 */
                    p++; /* 跳过 '>' */
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
                    
                } else if (*p == '<') {
                    /* $[<偏移] - 向开头移动 */
                    p++; /* 跳过 '<' */
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
                    
                } else if (strncmp(p, "END", 3) == 0) {
                    /* $[END] 或 $[END-n] - 绝对跳转到段尾或段尾偏移 */
                    p += 3; /* 跳过 'END' */
                    
                    op->type = OP_JUMP_POS;
                    
                    if (*p == '-') {
                        p++; /* 跳过 '-' */
                        size_t offset;
                        size_t consumed = parse_number(p, &offset);
                        p += consumed;
                        
                        op->data.jump_pos.pos_type = POS_END_OFF;
                        op->data.jump_pos.value = (int)offset;
                        
                    } else {
                        op->data.jump_pos.pos_type = POS_END;
                        op->data.jump_pos.value = 0;
                    }
                    
                    if (*p != ']') {
                        free(*out_ops);
                        *out_ops = NULL;
                        return -1;
                    }
                    p++; /* 跳过 ']' */
                    count++;
                    
                } else if (is_digit(*p)) {
                    /* $[位置] - 绝对跳转 */
                    size_t pos;
                    size_t consumed = parse_number(p, &pos);
                    p += consumed;
                    
                    if (*p != ']') {
                        free(*out_ops);
                        *out_ops = NULL;
                        return -1;
                    }
                    p++; /* 跳过 ']' */
                    
                    op->type = OP_JUMP_POS;
                    op->data.jump_pos.pos_type = POS_ABS;
                    op->data.jump_pos.value = (int)pos;
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
            /* 非 $ 开头的字符，视为无效语法 */
            free(*out_ops);
            *out_ops = NULL;
            return -1;
        }
    }
    
    *out_count = count;
    return 0;
}

/* ==================== 特征序列生成 ==================== */

int pattern_generate_features(const operator_t *ops, size_t op_count,
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
    
    size_t count = 0;
    int cumulative_offset = 0;  /* 累计偏移量 */
    int has_pending_offset = 0;  /* 是否有待输出的偏移 */
    
    for (size_t i = 0; i < op_count; i++) {
        const operator_t *op = &ops[i];
        
        /* 确保有足够空间 */
        if (count >= *out_capacity) {
            feature_array_grow(out_features, out_capacity);
        }
        
        switch (op->type) {
            case OP_CAPTURE_LEN:
            case OP_JUMP_FWD:
                /* 正向偏移：累加 */
                if (op->type == OP_CAPTURE_LEN) {
                    cumulative_offset += (int)op->data.length;
                } else {
                    cumulative_offset += (int)op->data.offset;
                }
                has_pending_offset = 1;
                break;
                
            case OP_JUMP_BACK:
                /* 负向偏移：累加（减法） */
                cumulative_offset -= (int)op->data.offset;
                has_pending_offset = 1;
                break;
                
            case OP_CAPTURE_CHR: {
                /* 字符查找：先输出累计偏移，再输出字符查找 */
                if (has_pending_offset && cumulative_offset != 0) {
                    feature_tuple_t *ft = &(*out_features)[count++];
                    if (cumulative_offset > 0) {
                        ft->type = FT_OFFSET_POS;
                        ft->value = cumulative_offset;
                    } else {
                        ft->type = FT_OFFSET_NEG;
                        ft->value = cumulative_offset;
                    }
                    ft->keyword = NULL;
                    ft->keyword_len = 0;
                }
                cumulative_offset = 0;
                has_pending_offset = 0;
                
                /* 输出字符查找元组 */
                if (count >= *out_capacity) {
                    feature_array_grow(out_features, out_capacity);
                }
                feature_tuple_t *ft = &(*out_features)[count++];
                ft->type = FT_CHAR_FIND;
                ft->value = (int)(unsigned char)op->data.ch;
                ft->keyword = NULL;
                ft->keyword_len = 0;
                break;
            }
            
            case OP_MATCH: {
                /* 关键字匹配：累计偏移 + 关键字 */
                feature_tuple_t *ft = &(*out_features)[count++];
                
                if (has_pending_offset && cumulative_offset != 0) {
                    if (cumulative_offset > 0) {
                        ft->type = FT_OFFSET_POS;
                        ft->value = cumulative_offset;
                    } else {
                        ft->type = FT_OFFSET_NEG;
                        ft->value = cumulative_offset;
                    }
                } else {
                    ft->type = FT_OFFSET_POS;
                    ft->value = 0;
                }
                
                /* 复制关键字字符串 */
                ft->keyword_len = op->data.match.len;
                ft->keyword = malloc(ft->keyword_len + 1);
                if (ft->keyword) {
                    memcpy((char *)ft->keyword, op->data.match.text, ft->keyword_len);
                    ((char *)ft->keyword)[ft->keyword_len] = '\0';
                }
                
                cumulative_offset = 0;
                has_pending_offset = 0;
                break;
            }
            
            case OP_CAPTURE_END:
                /* 捕获到结尾：偏移到 END */
                if (count >= *out_capacity) {
                    feature_array_grow(out_features, out_capacity);
                }
                {
                    feature_tuple_t *ft = &(*out_features)[count++];
                    ft->type = FT_END_OFFSET;
                    ft->value = 0;
                    ft->keyword = NULL;
                    ft->keyword_len = 0;
                }
                cumulative_offset = 0;
                has_pending_offset = 0;
                break;
                
            case OP_JUMP_POS: {
                /* 绝对跳转：输出 END 相关偏移 */
                if (count >= *out_capacity) {
                    feature_array_grow(out_features, out_capacity);
                }
                feature_tuple_t *ft = &(*out_features)[count++];
                
                if (op->data.jump_pos.pos_type == POS_END) {
                    ft->type = FT_END_OFFSET;
                    ft->value = 0;
                } else if (op->data.jump_pos.pos_type == POS_END_OFF) {
                    ft->type = FT_END_OFFSET;
                    ft->value = -op->data.jump_pos.value;
                } else {
                    /* POS_ABS: 转换为相对偏移 */
                    ft->type = FT_OFFSET_POS;
                    ft->value = op->data.jump_pos.value;
                }
                
                ft->keyword = NULL;
                ft->keyword_len = 0;
                cumulative_offset = 0;
                has_pending_offset = 0;
                break;
            }
        }
    }
    
    /* 处理剩余的累计偏移 */
    if (has_pending_offset && cumulative_offset != 0) {
        if (count >= *out_capacity) {
            feature_array_grow(out_features, out_capacity);
        }
        feature_tuple_t *ft = &(*out_features)[count++];
        if (cumulative_offset > 0) {
            ft->type = FT_OFFSET_POS;
            ft->value = cumulative_offset;
        } else {
            ft->type = FT_OFFSET_NEG;
            ft->value = cumulative_offset;
        }
        ft->keyword = NULL;
        ft->keyword_len = 0;
    }
    
    *out_count = count;
    return 0;
}

/* ==================== 提取器创建 ==================== */

extractor_t *extractor_create(const operator_t *ops, size_t op_count) {
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
    
    ext->op_count = 0;
    ext->param_count = 0;
    
    for (size_t i = 0; i < op_count; i++) {
        const operator_t *op = &ops[i];
        extractor_op_t *ex_op = &ext->ops[ext->op_count];
        
        switch (op->type) {
            case OP_CAPTURE_LEN:
                ex_op->type = EX_CAPTURE_LEN;
                ex_op->data.length = op->data.length;
                ext->op_count++;
                ext->param_count++;
                break;
                
            case OP_CAPTURE_CHR:
                ex_op->type = EX_CAPTURE_CHR;
                ex_op->data.ch = op->data.ch;
                ext->op_count++;
                ext->param_count++;
                break;
                
            case OP_CAPTURE_END:
                ex_op->type = EX_CAPTURE_END;
                ext->op_count++;
                ext->param_count++;
                break;
                
            case OP_MATCH:
                /* 优化：验证转为跳过 */
                ex_op->type = EX_SKIP_LEN;
                ex_op->data.length = op->data.match.len;
                ext->op_count++;
                break;
                
            case OP_JUMP_POS:
                ex_op->type = EX_JUMP_POS;
                if (op->data.jump_pos.pos_type == POS_END) {
                    /* END 在运行时计算，这里用特殊值标记 */
                    ex_op->data.pos = (size_t)-1;
                } else if (op->data.jump_pos.pos_type == POS_END_OFF) {
                    /* END-n 用负数标记 */
                    ex_op->data.pos = (size_t)-(op->data.jump_pos.value + 1);
                } else {
                    ex_op->data.pos = (size_t)op->data.jump_pos.value;
                }
                ext->op_count++;
                break;
                
            case OP_JUMP_FWD:
                ex_op->type = EX_JUMP_FWD;
                ex_op->data.offset = op->data.offset;
                ext->op_count++;
                break;
                
            case OP_JUMP_BACK:
                ex_op->type = EX_JUMP_BACK;
                ex_op->data.offset = op->data.offset;
                ext->op_count++;
                break;
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

/* ==================== 完整编译接口 ==================== */

compile_result_t pattern_compile(const char *pattern) {
    compile_result_t result;
    memset(&result, 0, sizeof(result));
    
    if (!pattern || *pattern == '\0') {
        result.status = COMPILE_ERR_EMPTY;
        result.error_msg = "Empty pattern";
        return result;
    }
    
    /* 词法分析 */
    operator_t *ops = NULL;
    size_t op_count = 0;
    size_t op_capacity = 0;
    
    if (pattern_lex(pattern, &ops, &op_count, &op_capacity) != 0) {
        result.status = COMPILE_ERR_SYNTAX;
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
        result.status = COMPILE_ERR_SYNTAX;
        result.error_msg = "Failed to generate features";
        return result;
    }
    
    /* 创建提取器 */
    extractor_t *extractor = extractor_create(ops, op_count);
    if (!extractor && op_count > 0) {
        free(ops);
        free(features);
        result.status = COMPILE_ERR_SYNTAX;
        result.error_msg = "Failed to create extractor";
        return result;
    }
    
    /* 填充结果 */
    result.status = COMPILE_OK;
    result.features = features;
    result.feature_count = feature_count;
    result.feature_capacity = feature_capacity;
    result.extractor = extractor;
    
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
    if (result->extractor) {
        extractor_destroy(result->extractor);
    }
    memset(result, 0, sizeof(compile_result_t));
}
