#define _GNU_SOURCE

#include "lexer.h"
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 16

/* ============================================================
 * URLRouter 词法分析器 - 实现文件
 *
 * 根据设计文档 2.2 版本实现
 * 支持 10 种操作符的词法分析
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

static void op_array_grow(op_t **ops, size_t *capacity) {
    size_t new_cap = *capacity * 2;
    op_t *new_arr = realloc(*ops, new_cap * sizeof(op_t));
    if (new_arr) {
        *ops = new_arr;
        *capacity = new_cap;
    }
}

void op_array_free(op_t *ops) {
    free(ops);
}

/* ==================== 词法分析器实现 ==================== */

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
            op_array_grow(out_ops, out_capacity);
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
