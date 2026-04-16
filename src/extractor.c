#include "extractor.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * URLRouter 参数提取器 - 实现文件
 * 
 * 根据设计文档 2.2 版本实现
 * 支持 10 种提取操作类型
 * ============================================================ */

/* ==================== 段提取器执行 ==================== */

/**
 * 执行段提取器
 * 
 * 支持的操作类型：
 * - EX_CAPTURE_LEN: 定长捕获
 * - EX_CAPTURE_CHR: 捕获到字符
 * - EX_CAPTURE_END: 捕获到结尾
 * - EX_SKIP_LEN: 跳过固定长度
 * - EX_JUMP_ABS: 绝对跳转（基于 HEAD）
 * - EX_JUMP_END: END 跳转
 * - EX_JUMP_FWD: 正向移动
 * - EX_JUMP_BACK: 负向移动
 * - EX_FIND_FWD: 正向查找
 * - EX_FIND_REV: 反向查找
 */
int segment_extractor_execute(const segment_extractor_t *seg_ext,
                              const char *segment, size_t segment_len,
                              route_param_t *params, size_t param_capacity,
                              size_t *param_count) {
    if (!seg_ext || !segment || !params || !param_count) {
        return -1;
    }

    size_t cursor = 0;
    size_t out_idx = *param_count;

    for (size_t i = 0; i < seg_ext->op_count; i++) {
        const extractor_op_t *op = &seg_ext->ops[i];

        switch (op->type) {
            case EX_CAPTURE_LEN: {
                if (out_idx >= param_capacity) {
                    return -1;
                }

                size_t remaining = segment_len - cursor;
                if (op->data.capture_len.length > remaining) {
                    return -1;
                }

                params[out_idx].ptr = segment + cursor;
                params[out_idx].len = op->data.capture_len.length;
                out_idx++;

                cursor += op->data.capture_len.length;
                break;
            }

            case EX_CAPTURE_CHR: {
                if (out_idx >= param_capacity) {
                    return -1;
                }

                char target = op->data.capture_chr.ch;
                const char *found = memchr(segment + cursor, target,
                                           segment_len - cursor);

                size_t end_pos;
                if (found) {
                    end_pos = (size_t)(found - segment);
                } else {
                    end_pos = segment_len;
                }

                params[out_idx].ptr = segment + cursor;
                params[out_idx].len = end_pos - cursor;
                out_idx++;

                cursor = end_pos;
                break;
            }

            case EX_CAPTURE_END: {
                if (out_idx >= param_capacity) {
                    return -1;
                }

                params[out_idx].ptr = segment + cursor;
                params[out_idx].len = segment_len - cursor;
                out_idx++;

                cursor = segment_len;
                break;
            }

            case EX_SKIP_LEN: {
                if (cursor + op->data.skip_len.length > segment_len) {
                    return -1;
                }
                cursor += op->data.skip_len.length;
                break;
            }

            case EX_JUMP_ABS: {
                /* 绝对跳转：基于 HEAD（位置 0）*/
                size_t new_pos = op->data.jump_abs.pos;
                if (new_pos > segment_len) {
                    return -1;
                }
                cursor = new_pos;
                break;
            }

            case EX_JUMP_END: {
                /* END 跳转 */
                if (op->data.jump_end.is_end) {
                    int offset = op->data.jump_end.offset;
                    if (offset == 0) {
                        /* 纯 END */
                        cursor = segment_len;
                    } else {
                        /* END-n：从段尾向前偏移 */
                        int new_pos = (int)segment_len - offset;
                        if (new_pos < 0 || (size_t)new_pos > segment_len) {
                            return -1;
                        }
                        cursor = (size_t)new_pos;
                    }
                }
                break;
            }

            case EX_JUMP_FWD: {
                /* $[>n] - 从当前位置前进 n（相对移动）*/
                if (cursor + op->data.jump_fwd.offset > segment_len) {
                    return -1;
                }
                cursor += op->data.jump_fwd.offset;
                break;
            }

            case EX_JUMP_BACK: {
                /* $[<n] - 从当前位置向开头回退 n */
                int new_pos = (int)cursor - (int)op->data.jump_back.offset;
                if (new_pos < 0) {
                    return -1;
                }
                cursor = (size_t)new_pos;
                break;
            }

            case EX_FIND_FWD: {
                char target = op->data.find_fwd.ch;
                const char *found = memchr(segment + cursor, target,
                                           segment_len - cursor);
                if (!found) {
                    return -1;
                }
                cursor = (size_t)(found - segment);
                break;
            }

            case EX_FIND_REV: {
                /* 从当前位置向前查找字符
                 * 如果 cursor 在开头 (0)，则从段尾开始查找
                 */
                char target = op->data.find_rev.ch;

                /* 如果 cursor 在开头，从段尾开始查找 */
                size_t start_pos = (cursor == 0) ? segment_len - 1 : cursor - 1;
                
                if (start_pos >= segment_len) {
                    return -1;  /* 段为空 */
                }

                size_t i = start_pos;
                while (1) {
                    if (segment[i] == target) {
                        cursor = i;
                        break;
                    }
                    if (i == 0) {
                        return -1;  /* 未找到 */
                    }
                    i--;
                }
                break;
            }
        }
    }

    *param_count = out_idx;
    return 0;
}

/* ==================== 完整提取器执行 ==================== */

int full_extractor_execute(const full_extractor_t *full_ext,
                           const char **segments, size_t *seg_lens,
                           size_t segment_count,
                           route_param_t *params, size_t param_capacity,
                           size_t *out_count) {
    if (!full_ext || !segments || !params || !out_count) {
        return -1;
    }

    if (full_ext->segment_count != segment_count) {
        return -1;
    }

    size_t param_idx = 0;

    /* 对每段执行对应的提取器 */
    for (size_t i = 0; i < segment_count; i++) {
        const char *segment = segments[i];
        size_t seg_len = seg_lens ? seg_lens[i] : strlen(segment);

        if (full_ext->segments[i]) {
            int ret = segment_extractor_execute(full_ext->segments[i],
                                                 segment, seg_len,
                                                 params, param_capacity,
                                                 &param_idx);
            if (ret != 0) {
                return -1;
            }
        }
    }

    *out_count = param_idx;
    return 0;
}

/* ==================== 完整提取器创建 ==================== */

full_extractor_t *full_extractor_create(segment_extractor_t **segment_extractors,
                                         size_t segment_count) {
    if (!segment_extractors || segment_count == 0) {
        return NULL;
    }

    full_extractor_t *full_ext = calloc(1, sizeof(full_extractor_t));
    if (!full_ext) {
        return NULL;
    }

    full_ext->segments = calloc(segment_count, sizeof(segment_extractor_t *));
    if (!full_ext->segments) {
        free(full_ext);
        return NULL;
    }

    full_ext->segment_count = segment_count;
    full_ext->total_params = 0;

    /* 复制指针数组（不接管所有权，调用者负责释放 segment_extractors 数组）*/
    for (size_t i = 0; i < segment_count; i++) {
        full_ext->segments[i] = segment_extractors[i];
        if (segment_extractors[i]) {
            full_ext->total_params += segment_extractors[i]->param_count;
        }
    }

    return full_ext;
}

void full_extractor_destroy(full_extractor_t *full_ext) {
    if (!full_ext) {
        return;
    }

    /* 释放每段的提取器 */
    for (size_t i = 0; i < full_ext->segment_count; i++) {
        if (full_ext->segments[i]) {
            segment_extractor_destroy(full_ext->segments[i]);
        }
    }

    if (full_ext->segments) {
        free(full_ext->segments);
    }
    free(full_ext);
}
