#include "extractor.h"
#include <stdlib.h>
#include <string.h>

/* ==================== 提取器执行 ==================== */

int extractor_execute(const extractor_t *extractor,
                      const char *segment, size_t segment_len,
                      route_param_t *params, size_t param_capacity,
                      size_t *param_count) {
    if (!extractor || !segment || !params || !param_count) {
        return -1;
    }
    
    size_t cursor = 0;  /* 当前指针位置 */
    size_t out_idx = *param_count;  /* 输出参数索引 */
    
    for (size_t i = 0; i < extractor->op_count; i++) {
        const extractor_op_t *op = &extractor->ops[i];
        
        switch (op->type) {
            case EX_CAPTURE_LEN: {
                /* 定长捕获 */
                if (out_idx >= param_capacity) {
                    return -1;  /* 参数数组容量不足 */
                }
                
                size_t remaining = segment_len - cursor;
                if (op->data.length > remaining) {
                    return -1;  /* 长度不足 */
                }
                
                params[out_idx].ptr = segment + cursor;
                params[out_idx].len = op->data.length;
                out_idx++;
                
                cursor += op->data.length;
                break;
            }
            
            case EX_CAPTURE_CHR: {
                /* 捕获到字符 */
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
                    end_pos = segment_len;  /* 未找到则捕获到段尾 */
                }
                
                params[out_idx].ptr = segment + cursor;
                params[out_idx].len = end_pos - cursor;
                out_idx++;
                
                cursor = end_pos;
                break;
            }
            
            case EX_CAPTURE_END: {
                /* 捕获到段尾 */
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
                /* 跳过固定长度（优化后的验证操作） */
                if (cursor + op->data.length > segment_len) {
                    return -1;
                }
                cursor += op->data.length;
                break;
            }
            
            case EX_JUMP_POS: {
                /* 绝对跳转 */
                if (op->data.pos == (size_t)-1) {
                    /* END - 跳转到段尾 */
                    cursor = segment_len;
                } else if (op->data.pos & ((size_t)1 << (sizeof(size_t) * 8 - 1))) {
                    /* END-n - 从段尾偏移（用最高位标记负数） */
                    int offset = (int)(~op->data.pos);  /* 还原为正值 */
                    if ((int)segment_len - offset < 0) {
                        return -1;
                    }
                    cursor = (size_t)((int)segment_len - offset);
                } else {
                    /* 绝对位置 */
                    if (op->data.pos > segment_len) {
                        return -1;
                    }
                    cursor = op->data.pos;
                }
                break;
            }
            
            case EX_JUMP_FWD: {
                /* 向前移动 */
                if (cursor + op->data.offset > segment_len) {
                    return -1;
                }
                cursor += op->data.offset;
                break;
            }
            
            case EX_JUMP_BACK: {
                /* 向后移动 */
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

/* ==================== 提取器合并 ==================== */

extractor_t *extractor_merge(extractor_t **extractors, size_t count) {
    if (!extractors || count == 0) {
        return NULL;
    }
    
    /* 计算总操作数和参数数 */
    size_t total_ops = 0;
    size_t total_params = 0;
    
    for (size_t i = 0; i < count; i++) {
        if (extractors[i]) {
            total_ops += extractors[i]->op_count;
            total_params += extractors[i]->param_count;
        }
    }
    
    if (total_ops == 0) {
        return NULL;
    }
    
    /* 创建合并的提取器 */
    extractor_t *merged = calloc(1, sizeof(extractor_t));
    if (!merged) {
        return NULL;
    }
    
    merged->ops = calloc(total_ops, sizeof(extractor_op_t));
    if (!merged->ops) {
        free(merged);
        return NULL;
    }
    
    merged->op_count = 0;
    merged->param_count = total_params;
    
    /* 复制所有操作 */
    for (size_t i = 0; i < count; i++) {
        if (extractors[i] && extractors[i]->op_count > 0) {
            memcpy(&merged->ops[merged->op_count],
                   extractors[i]->ops,
                   extractors[i]->op_count * sizeof(extractor_op_t));
            merged->op_count += extractors[i]->op_count;
        }
    }
    
    return merged;
}
