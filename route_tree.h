#ifndef ROUTE_TREE_H
#define ROUTE_TREE_H

#include "pattern_compiler.h"
#include "router.h"
#include "extractor.h"

/* ==================== 路由节点 ==================== */

/**
 * 路由节点结构
 * 每个节点代表一个段，包含特征序列和子节点
 */
struct route_node {
    /* 特征序列 - 用于匹配 */
    feature_tuple_t *features;
    size_t feature_count;
    
    /* 子节点数组 */
    route_node_t **children;
    size_t child_count;
    size_t child_capacity;
    
    /* 叶子节点数据 */
    extractor_t *extractor;     /* 参数提取器 */
    route_callback_t callback;  /* 处理函数 */
    void *userdata;             /* 用户数据 */
    
    /* 节点类型标记 */
    int is_leaf;                /* 是否是叶子节点 */
};

/* ==================== 路由树 ==================== */

/**
 * 路由树（单 HTTP 方法）
 */
typedef struct {
    route_node_t *root;
    size_t route_count;         /* 注册的路由数量 */
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
 * @param segments 段数组（已编译的特征序列）
 * @param segment_count 段数
 * @param segment_feature_counts 每段的特征数量
 * @param extractors 每段的提取器数组
 * @param extractor_count 提取器数量
 * @param callback 回调函数
 * @param userdata 用户数据
 * @return 0 成功，-1 失败（冲突等）
 */
int route_tree_register(route_tree_t *tree,
                        feature_tuple_t **segments,
                        size_t *segment_feature_counts,
                        size_t segment_count,
                        extractor_t **extractors,
                        size_t extractor_count,
                        route_callback_t callback,
                        void *userdata);

/**
 * 匹配 URL 到树中的节点
 * @param tree 路由树
 * @param segments URL 段数组
 * @param segment_count 段数
 * @return 匹配的节点，未匹配返回 NULL
 */
route_node_t *route_tree_match(route_tree_t *tree,
                               const char **segments,
                               size_t segment_count);

/**
 * 匹配 URL 到树中的节点（带长度参数）
 * @param tree 路由树
 * @param segments URL 段数组
 * @param seg_lens 每段长度数组
 * @param segment_count 段数
 * @return 匹配的节点，未匹配返回 NULL
 */
route_node_t *route_tree_match_seg_len(route_tree_t *tree,
                                        const char **segments,
                                        size_t *seg_lens,
                                        size_t segment_count);

/* ==================== 段匹配辅助 ==================== */

/**
 * 执行特征序列匹配段内容
 * @param features 特征序列
 * @param feature_count 特征数量
 * @param segment 段内容
 * @param segment_len 段长度
 * @return 0 匹配成功，-1 匹配失败
 */
int feature_execute(const feature_tuple_t *features, size_t feature_count,
                    const char *segment, size_t segment_len);

/**
 * 比较两个特征序列是否相同
 */
int feature_sequences_equal(const feature_tuple_t *a, size_t a_count,
                            const feature_tuple_t *b, size_t b_count);

#endif /* ROUTE_TREE_H */
