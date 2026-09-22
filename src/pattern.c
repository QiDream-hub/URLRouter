#include "pattern.h"

#include <stdlib.h>
#include <string.h>

/* ============================================================
 * URLRouter 段模式 - 词法分析
 *
 * 把 $'文本' / ${n} / ${'文本'} / ${} / $[n] / $[END] / $[END-n] /
 * $[>n] / $[<n] / $[>'文本'] / $[<'文本'] 切成 url_op_t 序列。
 *
 * 文本按字节读取，支持 \\、\'、\n、\t、\r、\0、\xNN 转义；
 * 转义后的字节串由操作符拥有。
 * ============================================================ */

#define URL_LEX_INITIAL_CAPACITY 16

typedef struct {
    const unsigned char *p;
    const unsigned char *end;
} cursor_t;

static int is_digit(unsigned char c) {
    return c >= '0' && c <= '9';
}

static int hex_val(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int at_end(const cursor_t *c) {
    return c->p >= c->end;
}

static int peek(const cursor_t *c) {
    return at_end(c) ? -1 : (int)*c->p;
}

/* 读取无符号整数 */
static int scan_uint(cursor_t *c, size_t *out) {
    if (at_end(c) || !is_digit(*c->p)) {
        return -1;
    }
    size_t v = 0;
    while (!at_end(c) && is_digit(*c->p)) {
        v = v * 10 + (size_t)(*c->p - '0');
        c->p++;
    }
    *out = v;
    return 0;
}

/**
 * 读取一段单引号包裹的文本（c->p 指向开引号），展开转义。
 * 成功后 out->data 为新分配的字节串（调用方负责释放）。
 */
static int scan_quoted(cursor_t *c, stride_blob_t *out) {
    if (at_end(c) || *c->p != '\'') {
        return -1;
    }
    c->p++;

    size_t cap = 16, n = 0;
    unsigned char *buf = (unsigned char *)malloc(cap);
    if (!buf) {
        return -1;
    }

    while (!at_end(c) && *c->p != '\'') {
        unsigned char ch = *c->p++;

        if (ch == '\\') {
            if (at_end(c)) {
                free(buf);
                return -1;
            }
            unsigned char e = *c->p++;
            switch (e) {
                case '\\': ch = '\\'; break;
                case '\'': ch = '\''; break;
                case 'n':  ch = '\n'; break;
                case 't':  ch = '\t'; break;
                case 'r':  ch = '\r'; break;
                case '0':  ch = '\0'; break;
                case 'x': {
                    if (c->end - c->p < 2) {
                        free(buf);
                        return -1;
                    }
                    int hi = hex_val(c->p[0]);
                    int lo = hex_val(c->p[1]);
                    if (hi < 0 || lo < 0) {
                        free(buf);
                        return -1;
                    }
                    ch = (unsigned char)(hi * 16 + lo);
                    c->p += 2;
                    break;
                }
                default:
                    free(buf);
                    return -1;
            }
        }

        if (n == cap) {
            unsigned char *nb = (unsigned char *)realloc(buf, cap * 2);
            if (!nb) {
                free(buf);
                return -1;
            }
            buf = nb;
            cap *= 2;
        }
        buf[n++] = ch;
    }

    if (at_end(c) || *c->p != '\'') { /* 未闭合 */
        free(buf);
        return -1;
    }
    c->p++; /* 跳过闭引号 */

    out->data = buf;
    out->len = n; /* URLRouter 以字节为单位 */
    return 0;
}

void url_ops_free(url_op_t *ops, size_t count) {
    if (!ops) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        if (ops[i].type == URL_OP_MATCH || ops[i].type == URL_OP_CAPTURE_UNTIL ||
            ops[i].type == URL_OP_FIND_FWD || ops[i].type == URL_OP_FIND_REV) {
            free((void *)ops[i].data.literal.data);
        }
    }
    free(ops);
}

/* ==================== 主扫描 ==================== */

int url_lex(const char *pattern, size_t pattern_len, url_op_t **out_ops,
            size_t *out_count, size_t *out_capacity) {
    if (!pattern || !out_ops || !out_count || !out_capacity) {
        return -1;
    }
    if (pattern_len == 0) {
        pattern_len = strlen(pattern);
    }

    cursor_t c;
    c.p = (const unsigned char *)pattern;
    c.end = c.p + pattern_len;

    size_t capacity = URL_LEX_INITIAL_CAPACITY;
    url_op_t *ops = (url_op_t *)calloc(capacity, sizeof(url_op_t));
    if (!ops) {
        return -1;
    }
    size_t count = 0;

#define URL_LEX_GROW()                                                     \
    do {                                                                   \
        if (count >= capacity) {                                           \
            size_t nc = capacity * 2;                                      \
            url_op_t *na =                                                 \
                (url_op_t *)realloc(ops, nc * sizeof(url_op_t));           \
            if (!na) {                                                     \
                goto fail;                                                 \
            }                                                              \
            memset(na + capacity, 0, (nc - capacity) * sizeof(url_op_t));  \
            ops = na;                                                      \
            capacity = nc;                                                 \
        }                                                                  \
    } while (0)

    if (at_end(&c)) {
        /* 空模式 */
        *out_ops = ops;
        *out_count = 0;
        *out_capacity = capacity;
        return 0;
    }

    while (!at_end(&c)) {
        URL_LEX_GROW();
        url_op_t *op = &ops[count];
        memset(op, 0, sizeof(*op));

        if (*c.p != '$') {
            goto fail; /* 非 $ 起始视为非法 */
        }
        c.p++;

        int ch = peek(&c);
        if (ch < 0) {
            goto fail;
        }

        if (ch == '\'') {
            /* $'文本' */
            op->type = URL_OP_MATCH;
            if (scan_quoted(&c, &op->data.literal) != 0) {
                goto fail;
            }
            count++;
            continue;
        }

        if (ch == '{') {
            c.p++;
            int in = peek(&c);
            if (in < 0) {
                goto fail;
            }

            if (in == '}') {
                c.p++;
                op->type = URL_OP_CAPTURE_END;
                count++;
                continue;
            }

            if (in == '\'') {
                op->type = URL_OP_CAPTURE_UNTIL;
                if (scan_quoted(&c, &op->data.literal) != 0) {
                    goto fail;
                }
                if (peek(&c) != '}') {
                    goto fail;
                }
                c.p++;
                count++;
                continue;
            }

            if (is_digit((unsigned char)in)) {
                size_t n;
                if (scan_uint(&c, &n) != 0 || n == 0) {
                    goto fail;
                }
                if (peek(&c) != '}') {
                    goto fail;
                }
                c.p++;
                op->type = URL_OP_CAPTURE_BYTES;
                op->data.bytes = n;
                count++;
                continue;
            }

            goto fail;
        }

        if (ch == '[') {
            c.p++;
            int in = peek(&c);
            if (in < 0) {
                goto fail;
            }

            if (in == '>') {
                c.p++;
                if (peek(&c) == '\'') {
                    op->type = URL_OP_FIND_FWD;
                    if (scan_quoted(&c, &op->data.literal) != 0) {
                        goto fail;
                    }
                } else {
                    size_t n;
                    if (scan_uint(&c, &n) != 0) {
                        goto fail;
                    }
                    op->type = URL_OP_JUMP_FWD;
                    op->data.bytes = n;
                }
                if (peek(&c) != ']') {
                    goto fail;
                }
                c.p++;
                count++;
                continue;
            }

            if (in == '<') {
                c.p++;
                if (peek(&c) == '\'') {
                    op->type = URL_OP_FIND_REV;
                    if (scan_quoted(&c, &op->data.literal) != 0) {
                        goto fail;
                    }
                } else {
                    size_t n;
                    if (scan_uint(&c, &n) != 0) {
                        goto fail;
                    }
                    op->type = URL_OP_JUMP_BACK;
                    op->data.bytes = n;
                }
                if (peek(&c) != ']') {
                    goto fail;
                }
                c.p++;
                count++;
                continue;
            }

            if (c.end - c.p >= 3 && memcmp(c.p, "END", 3) == 0) {
                c.p += 3;
                op->type = URL_OP_JUMP_END;
                op->data.jump_end.is_end = 1;
                op->data.jump_end.back_bytes = 0;
                if (peek(&c) == '-') {
                    c.p++;
                    size_t n;
                    if (scan_uint(&c, &n) != 0) {
                        goto fail;
                    }
                    op->data.jump_end.back_bytes = n;
                }
                if (peek(&c) != ']') {
                    goto fail;
                }
                c.p++;
                count++;
                continue;
            }

            if (is_digit((unsigned char)in)) {
                size_t n;
                if (scan_uint(&c, &n) != 0) {
                    goto fail;
                }
                if (peek(&c) != ']') {
                    goto fail;
                }
                c.p++;
                op->type = URL_OP_JUMP_ABS;
                op->data.bytes = n;
                count++;
                continue;
            }

            goto fail;
        }

        goto fail;
    }

#undef URL_LEX_GROW

    *out_ops = ops;
    *out_count = count;
    *out_capacity = capacity;
    return 0;

fail:
    /* count 槽位可能已分配了文本但尚未计数，一并释放 */
    url_ops_free(ops, count + 1);
    *out_ops = NULL;
    *out_count = 0;
    *out_capacity = 0;
    return -1;
}
