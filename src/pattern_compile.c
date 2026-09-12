#include "pattern.h"

#include <string.h>

/* ============================================================
 * URLRouter 段模式 - 编译器
 *
 * 把 url_op_t 操作符序列翻译成 Stride 的序列构建函数调用：
 *   - 匹配序列：偏移 + STRIDE_ACT_COMPARE
 *   - 提取序列：偏移 + 捕获动作（$'文本' 优化为跳过其长度）
 *
 * 合并（连续的常量偏移相加、字面量绑定到当前偏移）由 Stride 的
 * 尾节点合并完成，本层不做状态机。
 * ============================================================ */

static int literal_aligned(const stride_blob_t *b) {
    return (b->bit_len % URL_PATTERN_STRIDE) == 0;
}

url_compile_result_t url_compile(const void *pattern, size_t pattern_len) {
    url_compile_result_t r;
    memset(&r, 0, sizeof(r));

    if (!pattern || (pattern_len == 0 && *(const char *)pattern == '\0')) {
        r.status = STRIDE_E_EMPTY_SEGMENT;
        r.error_msg = "empty pattern";
        return r;
    }

    url_op_t *ops = NULL;
    size_t op_count = 0, op_capacity = 0;
    if (url_lex(pattern, pattern_len, &ops, &op_count, &op_capacity) != 0) {
        r.status = STRIDE_E_INVALID_PATTERN;
        r.error_msg = "syntax error in pattern";
        return r;
    }
    if (op_count == 0) {
        url_ops_free(ops, op_count);
        r.status = STRIDE_E_EMPTY_SEGMENT;
        r.error_msg = "empty pattern";
        return r;
    }

    stride_seq_t *m = stride_seq_new();
    stride_seq_t *e = stride_seq_new();
    if (!m || !e) {
        stride_seq_free(m);
        stride_seq_free(e);
        url_ops_free(ops, op_count);
        r.status = STRIDE_E_NOMEM;
        r.error_msg = "out of memory";
        return r;
    }

    stride_status_t err = STRIDE_OK;

    for (size_t i = 0; i < op_count; i++) {
        const url_op_t *op = &ops[i];
        const stride_blob_t *lit = &op->data.literal;
        int rc = 0;

        switch (op->type) {
            case URL_OP_MATCH:
                /* 匹配：在该处比对；提取：跳过其长度（匹配阶段已验证） */
                if (!literal_aligned(lit)) {
                    err = STRIDE_E_ALIGN;
                    break;
                }
                rc |= stride_seq_compare(m, lit);
                rc |= stride_seq_skip_bits(e, lit->bit_len);
                break;

            case URL_OP_CAPTURE_STEPS:
                /* 匹配阶段：捕获 = 前进；提取阶段：捕获 n 步 */
                rc |= stride_seq_step_fwd(m, op->data.steps);
                rc |= stride_seq_capture_steps(e, op->data.steps);
                break;

            case URL_OP_CAPTURE_UNTIL:
                if (!literal_aligned(lit)) {
                    err = STRIDE_E_ALIGN;
                    break;
                }
                rc |= stride_seq_find_fwd(m, lit);
                rc |= stride_seq_capture_until(e, lit);
                break;

            case URL_OP_CAPTURE_END:
                rc |= stride_seq_abs_end(m, 0);
                rc |= stride_seq_capture_end(e);
                break;

            case URL_OP_JUMP_ABS:
                rc |= stride_seq_abs_head(m, op->data.steps);
                rc |= stride_seq_abs_head(e, op->data.steps);
                break;

            case URL_OP_JUMP_END:
                rc |= stride_seq_abs_end(m, op->data.jump_end.back_steps);
                rc |= stride_seq_abs_end(e, op->data.jump_end.back_steps);
                break;

            case URL_OP_JUMP_FWD:
                rc |= stride_seq_step_fwd(m, op->data.steps);
                rc |= stride_seq_step_fwd(e, op->data.steps);
                break;

            case URL_OP_JUMP_BACK:
                rc |= stride_seq_step_back(m, op->data.steps);
                rc |= stride_seq_step_back(e, op->data.steps);
                break;

            case URL_OP_FIND_FWD:
                if (!literal_aligned(lit)) {
                    err = STRIDE_E_ALIGN;
                    break;
                }
                rc |= stride_seq_find_fwd(m, lit);
                rc |= stride_seq_find_fwd(e, lit);
                break;

            case URL_OP_FIND_REV:
                if (!literal_aligned(lit)) {
                    err = STRIDE_E_ALIGN;
                    break;
                }
                rc |= stride_seq_find_rev(m, lit);
                rc |= stride_seq_find_rev(e, lit);
                break;

            default:
                err = STRIDE_E_INVALID_PATTERN;
                break;
        }

        if (rc != 0) {
            err = STRIDE_E_NOMEM;
        }
        if (err != STRIDE_OK) {
            break;
        }
    }

    url_ops_free(ops, op_count);

    if (err != STRIDE_OK) {
        stride_seq_free(m);
        stride_seq_free(e);
        r.status = err;
        r.error_msg = stride_status_str(err);
        return r;
    }

    r.status = STRIDE_OK;
    r.match = m;
    r.extract = e;
    r.param_count = stride_seq_param_count(e);
    return r;
}

void url_compile_free(url_compile_result_t *result) {
    if (!result) {
        return;
    }
    stride_seq_free(result->match);
    stride_seq_free(result->extract);
    memset(result, 0, sizeof(*result));
}
