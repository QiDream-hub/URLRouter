#ifndef ROUTE_TREE_H
#define ROUTE_TREE_H

#include "pattern.h"
#include "router.h"
#include "stride/stride.h"

/* ============================================================
 * URLRouter 路由树
 *
 * 段匹配序列与段提取序列由 pattern.h 编译产出（Stride 提供序列与引擎）：
 * - 段匹配使用 stride_match_run()
 * - 段提取使用 stride_extract_run()
 *
 * 本文件只保留路由特有的部分：
 * - 相同匹配序列的合并（共享节点，前缀合并）
 * - 关键字（比对动作）越多越优先的子节点选择
 * ============================================================ */

struct route_node {
    /* 匹配序列（节点拥有）*/
    stride_seq_t *match;

    /* 子节点数组 */
    route_node_t **children;
    size_t child_count;
    size_t child_capacity;

    /* 叶子数据：提取序列数组*/
    stride_extractor_t **extractors; /* 节点拥有，数组长度 = 段数 */
    size_t segment_count;            /* 段数 */
    route_callback_t callback;
    void *userdata;
    int is_leaf;

    /* 命中后提取参数时按此分隔符切分查询路径 */
    char sep;
};

/**
 * 路由树（单 HTTP 方法）
 *
 * 匹配代价只与 URL 段数及同层子节点数相关，与注册的路由总数无关。
 */
typedef struct {
    route_node_t *root;
    size_t route_count; /* 注册的路由数量 */
} route_tree_t;

void route_tree_init(route_tree_t *tree);
void route_tree_destroy(route_tree_t *tree);

/**
 * 注册路由到树中
 *
 * 所有权：无论成功与否，match_seqs[] 与 extract_seqs[] 中所有指针
 * 都交给本函数处理（成功时节点/完整提取器接管，失败时释放）。
 *
 * @return 0 成功，-1 失败（冲突或内存不足）
 */
int route_tree_register(route_tree_t *tree,
                        stride_seq_t **match_seqs,
                        size_t segment_count,
                        stride_extractor_t **extract_seqs,
                        size_t extractor_count,
                        route_callback_t callback,
                        void *userdata,
                        char sep);

route_node_t *route_tree_match(route_tree_t *tree,
                               const char **segments,
                               size_t *seg_lens,
                               size_t segment_count);

/**
 * 比较两个段匹配序列是否相同（合并匹配序列的依据）
 */
int match_sequences_equal(const stride_seq_t *a, const stride_seq_t *b);

#endif /* ROUTE_TREE_H */
