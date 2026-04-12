#include "extractor.h"
#include <stdlib.h>
#include <string.h>

/* ==================== 段提取器执行 ==================== */

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
                if (op->data.length > remaining) {
                    return -1;
                }
                
                params[out_idx].ptr = segment + cursor;
                params[out_idx].len = op->data.length;
                out_idx++;
                
                cursor += op->data.length;
                break;
            }
            
            case EX_CAPTURE_CHR: {
                if (out_idx >= param_capacity) {
                    return -1;
                }
                
                char target = op->data.ch;
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
                if (cursor + op->data.length > segment_len) {
                    return -1;
                }
                cursor += op->data.length;
                break;
            }
            
            case EX_JUMP_POS: {
                if (op->data.pos == (size_t)-1) {
                    cursor = segment_len;
                } else if (op->data.pos & ((size_t)1 << (sizeof(size_t) * 8 - 1))) {
                    int offset = (int)(~op->data.pos);
                    if ((int)segment_len - offset < 0) {
                        return -1;
                    }
                    cursor = (size_t)((int)segment_len - offset);
                } else {
                    if (op->data.pos > segment_len) {
                        return -1;
                    }
                    cursor = op->data.pos;
                }
                break;
            }
            
            case EX_JUMP_FWD: {
                if (cursor + op->data.offset > segment_len) {
                    return -1;
                }
                cursor += op->data.offset;
                break;
            }
            
            case EX_JUMP_BACK: {
                if (op->data.offset > cursor) {
                    return -1;
                }
                cursor -= op->data.offset;
                break;
            }
        }
    }
    
    *param_count = out_idx;
    return 0;
}

/* ==================== 完整提取器执行 ==================== */

int full_extractor_execute(const full_extractor_t *full_ext,
                           const char **segments, size_t segment_count,
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
        size_t seg_len = strlen(segment);
        size_t seg_param_count = 0;
        
        if (full_ext->segments[i]) {
            int ret = segment_extractor_execute(full_ext->segments[i],
                                                 segment, seg_len,
                                                 &params[param_idx],
                                                 param_capacity - param_idx,
                                                 &seg_param_count);
            if (ret != 0) {
                return -1;
            }
        }
        
        param_idx += seg_param_count;
    }
    
    *out_count = param_idx;
    return 0;
}

/* ==================== 段提取器创建 ==================== */

segment_extractor_t *segment_extractor_create(const operator_t *ops, size_t op_count) {
    if (!ops || op_count == 0) {
        return NULL;
    }

    segment_extractor_t *seg_ext = calloc(1, sizeof(segment_extractor_t));
    if (!seg_ext) {
        return NULL;
    }

    seg_ext->ops = calloc(op_count, sizeof(extractor_op_t));
    if (!seg_ext->ops) {
        free(seg_ext);
        return NULL;
    }

    seg_ext->op_count = 0;
    seg_ext->param_count = 0;

    for (size_t i = 0; i < op_count; i++) {
        const operator_t *op = &ops[i];
        extractor_op_t *ex_op = &seg_ext->ops[seg_ext->op_count];

        switch (op->type) {
            case OP_CAPTURE_LEN:
                ex_op->type = EX_CAPTURE_LEN;
                ex_op->data.length = op->data.length;
                seg_ext->op_count++;
                seg_ext->param_count++;
                break;

            case OP_CAPTURE_CHR:
                ex_op->type = EX_CAPTURE_CHR;
                ex_op->data.ch = op->data.ch;
                seg_ext->op_count++;
                seg_ext->param_count++;
                break;

            case OP_CAPTURE_END:
                ex_op->type = EX_CAPTURE_END;
                seg_ext->op_count++;
                seg_ext->param_count++;
                break;

            case OP_MATCH:
                ex_op->type = EX_SKIP_LEN;
                ex_op->data.length = op->data.match.len;
                seg_ext->op_count++;
                break;

            case OP_JUMP_POS:
                ex_op->type = EX_JUMP_POS;
                if (op->data.jump_pos.pos_type == POS_END) {
                    ex_op->data.pos = (size_t)-1;
                } else if (op->data.jump_pos.pos_type == POS_END_OFF) {
                    ex_op->data.pos = (size_t)-(op->data.jump_pos.value + 1);
                } else {
                    ex_op->data.pos = (size_t)op->data.jump_pos.value;
                }
                seg_ext->op_count++;
                break;

            case OP_JUMP_FWD:
                ex_op->type = EX_JUMP_FWD;
                ex_op->data.offset = op->data.offset;
                seg_ext->op_count++;
                break;

            case OP_JUMP_BACK:
                ex_op->type = EX_JUMP_BACK;
                ex_op->data.offset = op->data.offset;
                seg_ext->op_count++;
                break;
        }
    }

    return seg_ext;
}

void segment_extractor_destroy(segment_extractor_t *seg_ext) {
    if (!seg_ext) {
        return;
    }
    if (seg_ext->ops) {
        free(seg_ext->ops);
    }
    free(seg_ext);
}

/* ==================== extractor_t 别名函数 ==================== */

/* extractor_t 和 segment_extractor_t 是相同类型，提供别名函数 */
extractor_t *extractor_create(const operator_t *ops, size_t op_count) {
    return segment_extractor_create(ops, op_count);
}

void extractor_destroy(extractor_t *extractor) {
    segment_extractor_destroy(extractor);
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

/* ==================== 旧 API 兼容（已废弃） ==================== */

/* 这些函数保留用于向后兼容，但不再使用 */
int extractor_execute(const extractor_t *extractor,
                      const char *segment, size_t segment_len,
                      route_param_t *params, size_t param_capacity,
                      size_t *param_count) {
    /* 转换为段提取器执行 */
    segment_extractor_t seg_ext;
    seg_ext.ops = (extractor_op_t *)extractor->ops;
    seg_ext.op_count = extractor->op_count;
    seg_ext.param_count = 0;
    
    return segment_extractor_execute(&seg_ext, segment, segment_len,
                                      params, param_capacity, param_count);
}

extractor_t *extractor_merge(extractor_t **extractors, size_t count) {
    (void)extractors;
    (void)count;
    return NULL;  /* 不再使用 */
}
