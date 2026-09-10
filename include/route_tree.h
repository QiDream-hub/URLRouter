#ifndef ROUTE_TREE_H
#define ROUTE_TREE_H

#include "router.h"
#include "stride/extractor.h"
#include "stride/feature.h"

/* ============================================================
 * URLRouter 路由树
 *
 * 特征序列与提取序列由 Stride 编译产出（third_party/Stride）：
 * - 段匹配使用 stride_feature_match()
 * - 参数提取使用 stride_full_extractor_*()
 *
 * 本文件只保留路由特有的部分：
 * - 相同特征序列的合并（共享节点，前缀合并）
 * - 关键字越多优先级越高的子节点选择
 * ============================================================ */

/* ==================== 路由节点 ==================== */

/**
 * 路由节点结构
 * 每个节点代表一个段，包含特征序列和子节点
 */
struct route_node {
    /* 特征序列 - 用于匹配（节点拥有，深拷贝自注册时的序列）*/
    stride_feature_t *features;
    size_t feature_count;

    /* 子节点数组 */
    route_node_t **children;
    size_t child_count;
    size_t child_capacity;

    /* 叶子节点数据 */
    stride_full_extractor_t *extractor; /* 完整提取器（多段） */
    route_callback_t callback;          /* 处理函数 */
    void *userdata;                     /* 用户数据 */

    /* 节点类型标记 */
    int is_leaf; /* 是否是叶子节点 */

    /* 该节点所属路由树的路径分隔符 */
    char sep; /* 命中后提取参数时按此分隔符切分查询路径 */
};

/* ==================== 路由树 ==================== */

/**
 * 路由树（单 HTTP 方法）
 *
 * 匹配代价只与 URL 段数成正比，与注册的路由数量无关。
 */
typedef struct {
    route_node_t *root;
    size_t route_count; /* 注册的路由数量 */
} route_tree_t;

/* ==================== 树操作 API ==================== */

/**
 * 初始化路由树
 */
void route_tree_init(route_tree_t *tree);

/**
 * 销毁路由树
 */
void route_tree_destroy(route_tree_t *tree);

/**
 * 注册路由到树中
 * @param tree 路由树
 * @param segments 每段的特征序列（由 Stride 编译产出）
 * @param segment_feature_counts 每段的特征数量
 * @param segment_count 段数
 * @param extractors 每段的提取器数组（所有权转移给路由树）
 * @param extractor_count 提取器数量
 * @param callback 回调函数
 * @param userdata 用户数据
 * @param sep 路径分隔符
 * @return 0 成功，-1 失败（冲突等）
 */
int route_tree_register(route_tree_t *tree,
                        stride_feature_t **segments,
                        size_t *segment_feature_counts,
                        size_t segment_count,
                        stride_extractor_t **extractors,
                        size_t extractor_count,
                        route_callback_t callback,
                        void *userdata,
                        char sep);

/**
 * 匹配 URL 到树中的节点
 * @param tree 路由树
 * @param segments URL 段数组
 * @param seg_lens 每段长度数组
 * @param segment_count 段数
 * @return 匹配的节点，未匹配返回 NULL
 */
route_node_t *route_tree_match(route_tree_t *tree,
                               const char **segments,
                               size_t *seg_lens,
                               size_t segment_count);

/* ==================== 段匹配辅助 ==================== */

/**
 * 比较两个特征序列是否相同（用于合并相同特征序列的节点）
 */
int feature_sequences_equal(const stride_feature_t *a, size_t a_count,
                            const stride_feature_t *b, size_t b_count);

#endif /* ROUTE_TREE_H */
