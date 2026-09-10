#include "route_tree.h"

#include <stdlib.h>
#include <string.h>

/* ============================================================
 * URLRouter 路由树 - 实现
 *
 * 段匹配与参数提取委托给 Stride：
 * - stride_feature_match()          特征序列匹配
 * - stride_full_extractor_*()       提取序列执行
 *
 * 路由特有逻辑保留在本文件：
 * - 合并相同特征序列（共享节点，避免为相同前缀重复建树）
 * - 子节点优先级（关键字越多越优先）
 * ============================================================ */

#define INITIAL_CHILD_CAPACITY 4

/* ==================== 节点操作 ==================== */

static route_node_t *create_node(void) {
    route_node_t *node = calloc(1, sizeof(route_node_t));
    if (!node) {
        return NULL;
    }

    node->child_capacity = INITIAL_CHILD_CAPACITY;
    node->children = calloc(node->child_capacity, sizeof(route_node_t *));
    if (!node->children) {
        free(node);
        return NULL;
    }

    node->is_leaf = 0;
    return node;
}

static void destroy_node(route_node_t *node) {
    if (!node) {
        return;
    }

    for (size_t i = 0; i < node->child_count; i++) {
        destroy_node(node->children[i]);
    }

    /* 特征序列由其拥有者接口释放（含关键字副本）*/
    stride_feature_free(node->features, node->feature_count);
    free(node->children);
    stride_full_extractor_destroy(node->extractor);

    free(node);
}

static int node_add_child(route_node_t *node, route_node_t *child) {
    if (node->child_count >= node->child_capacity) {
        size_t new_cap = node->child_capacity * 2;
        route_node_t **new_children =
            realloc(node->children, new_cap * sizeof(route_node_t *));
        if (!new_children) {
            return -1;
        }
        node->children = new_children;
        node->child_capacity = new_cap;
    }

    node->children[node->child_count++] = child;
    return 0;
}

static void node_set_leaf(route_node_t *node, stride_full_extractor_t *extractor,
                          route_callback_t callback, void *userdata, char sep) {
    node->is_leaf = 1;
    node->extractor = extractor;
    node->callback = callback;
    node->userdata = userdata;
    node->sep = sep;
}

/* ==================== 特征序列工具 ==================== */

/**
 * 深拷贝特征序列（含关键字副本），供节点独立持有
 * @return 新数组（调用者用 stride_feature_free 释放），失败返回 NULL
 */
static stride_feature_t *features_dup(const stride_feature_t *src,
                                      size_t count) {
    if (!src || count == 0) {
        return NULL;
    }

    stride_feature_t *dst = calloc(count, sizeof(stride_feature_t));
    if (!dst) {
        return NULL;
    }

    for (size_t i = 0; i < count; i++) {
        dst[i].type = src[i].type;
        dst[i].value = src[i].value;
        dst[i].keyword_len = src[i].keyword_len;

        if (src[i].keyword) {
            char *kw = malloc(src[i].keyword_len + 1);
            if (!kw) {
                stride_feature_free(dst, i);
                return NULL;
            }
            memcpy(kw, src[i].keyword, src[i].keyword_len);
            kw[src[i].keyword_len] = '\0';
            dst[i].keyword = kw;
        }
    }

    return dst;
}

/**
 * 比较两个特征序列是否相同（合并特征序列的依据）
 */
int feature_sequences_equal(const stride_feature_t *a, size_t a_count,
                            const stride_feature_t *b, size_t b_count) {
    if (a_count != b_count) {
        return 0;
    }

    for (size_t i = 0; i < a_count; i++) {
        if (a[i].type != b[i].type) {
            return 0;
        }
        if (a[i].value != b[i].value) {
            return 0;
        }

        if ((a[i].keyword == NULL) != (b[i].keyword == NULL)) {
            return 0;
        }

        if (a[i].keyword && b[i].keyword) {
            if (a[i].keyword_len != b[i].keyword_len) {
                return 0;
            }
            if (memcmp(a[i].keyword, b[i].keyword, a[i].keyword_len) != 0) {
                return 0;
            }
        }
    }

    return 1;
}

/* ==================== 路由树操作 ==================== */

void route_tree_init(route_tree_t *tree) {
    if (!tree) {
        return;
    }
    memset(tree, 0, sizeof(route_tree_t));
    tree->root = create_node();
}

void route_tree_destroy(route_tree_t *tree) {
    if (!tree) {
        return;
    }
    if (tree->root) {
        destroy_node(tree->root);
    }
    memset(tree, 0, sizeof(route_tree_t));
}

/* ==================== 路由注册 ==================== */

/**
 * 查找或创建具有相同特征序列的子节点
 *
 * 这就是“合并特征序列”优化：不同路由在某一层若编译出完全相同的
 * 特征序列，则共享同一个节点，从而让树规模只与不同前缀的数量相关。
 */
static route_node_t *find_or_create_child(route_node_t *parent,
                                          const stride_feature_t *features,
                                          size_t feature_count) {
    for (size_t i = 0; i < parent->child_count; i++) {
        route_node_t *child = parent->children[i];
        if (feature_sequences_equal(child->features, child->feature_count,
                                    features, feature_count)) {
            return child;
        }
    }

    route_node_t *node = create_node();
    if (!node) {
        return NULL;
    }

    node->features = features_dup(features, feature_count);
    if (feature_count > 0 && !node->features) {
        free(node->children);
        free(node);
        return NULL;
    }
    node->feature_count = feature_count;

    if (node_add_child(parent, node) != 0) {
        stride_feature_free(node->features, node->feature_count);
        free(node->children);
        free(node);
        return NULL;
    }

    return node;
}

static int check_conflict(route_node_t *node, size_t segment_index,
                          stride_feature_t **segments,
                          size_t *segment_feature_counts,
                          size_t segment_count) {
    if (segment_index >= segment_count) {
        return node->is_leaf ? -1 : 0;
    }

    stride_feature_t *current_features = segments[segment_index];
    size_t current_count = segment_feature_counts[segment_index];

    for (size_t i = 0; i < node->child_count; i++) {
        route_node_t *child = node->children[i];

        if (feature_sequences_equal(child->features, child->feature_count,
                                    current_features, current_count)) {
            return check_conflict(child, segment_index + 1, segments,
                                  segment_feature_counts, segment_count);
        }
    }

    return 0;
}

int route_tree_register(route_tree_t *tree, stride_feature_t **segments,
                        size_t *segment_feature_counts, size_t segment_count,
                        stride_extractor_t **extractors,
                        size_t extractor_count, route_callback_t callback,
                        void *userdata, char sep) {
    if (!tree || !tree->root || !segments || segment_count == 0) {
        return -1;
    }

    if (check_conflict(tree->root, 0, segments, segment_feature_counts,
                       segment_count) != 0) {
        return -1;
    }

    route_node_t *current = tree->root;

    for (size_t i = 0; i < segment_count; i++) {
        route_node_t *child = find_or_create_child(
            current, segments[i], segment_feature_counts[i]);
        if (!child) {
            return -1;
        }
        current = child;
    }

    /* 从各段提取器组装完整提取器（接管 extractors 中指针的所有权）*/
    stride_extractor_t **seg_extractors =
        calloc(segment_count, sizeof(stride_extractor_t *));
    if (!seg_extractors) {
        return -1;
    }

    for (size_t i = 0; i < extractor_count; i++) {
        seg_extractors[i] = extractors[i];
    }

    stride_full_extractor_t *full =
        stride_full_extractor_create(seg_extractors, segment_count);
    free(seg_extractors);

    if (!full) {
        return -1;
    }

    node_set_leaf(current, full, callback, userdata, sep);
    tree->route_count++;

    return 0;
}

/* ==================== 路由匹配 ==================== */

/**
 * 计算节点的优先级分数：分数越高越具体，应优先匹配
 * - 带关键字的元组权重高
 * - 纯通配符（如 ${}）权重低
 */
static int get_node_priority(route_node_t *node) {
    if (!node || node->feature_count == 0) {
        return 0;
    }

    int priority = 0;
    for (size_t i = 0; i < node->feature_count; i++) {
        priority += (node->features[i].keyword != NULL) ? 100 : 1;
    }
    return priority;
}

route_node_t *route_tree_match(route_tree_t *tree, const char **segments,
                               size_t *seg_lens, size_t segment_count) {
    if (!tree || !tree->root || !segments || segment_count == 0) {
        return NULL;
    }

    route_node_t *current = tree->root;

    for (size_t i = 0; i < segment_count; i++) {
        const char *segment = segments[i];
        size_t seg_len = seg_lens ? seg_lens[i] : strlen(segment);

        route_node_t *matched = NULL;
        int best_priority = -1;

        /* 在所有命中的子节点中选择优先级最高者 */
        for (size_t j = 0; j < current->child_count; j++) {
            route_node_t *child = current->children[j];

            if (stride_feature_match(child->features, child->feature_count,
                                     segment, seg_len) == 0) {
                int priority = get_node_priority(child);
                if (priority > best_priority) {
                    best_priority = priority;
                    matched = child;
                }
            }
        }

        if (!matched) {
            return NULL;
        }

        current = matched;
    }

    if (current->is_leaf && current->callback) {
        return current;
    }

    return NULL;
}
