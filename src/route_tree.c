#include "route_tree.h"

#include <stdlib.h>
#include <string.h>

/* ============================================================
 * URLRouter 路由树 - 实现
 *
 * 段匹配 / 参数提取委托给 Stride：
 * - stride_match_run()              匹配序列执行
 * - stride_full_extractor_run()     提取序列执行
 *
 * 路由特有逻辑：
 * - 合并相同匹配序列（不同路由在某一层若编译出完全相同的匹配序列，
 *   共享同一节点，使树规模只与不同前缀数量相关）
 * - 子节点优先级（比对动作越多越优先）
 * ============================================================ */

#define INITIAL_CHILD_CAPACITY 4

/* ==================== 节点操作 ==================== */

static route_node_t *create_node(void) {
    route_node_t *node = (route_node_t *)calloc(1, sizeof(route_node_t));
    if (!node) {
        return NULL;
    }
    node->child_capacity = INITIAL_CHILD_CAPACITY;
    node->children =
        (route_node_t **)calloc(node->child_capacity, sizeof(route_node_t *));
    if (!node->children) {
        free(node);
        return NULL;
    }
    return node;
}

static void destroy_node(route_node_t *node) {
    if (!node) {
        return;
    }
    for (size_t i = 0; i < node->child_count; i++) {
        destroy_node(node->children[i]);
    }
    stride_seq_free(node->match);
    free(node->children);
    /* 释放提取序列数组 */
    if (node->extractors) {
        for (size_t i = 0; i < node->segment_count; i++) {
            stride_seq_free(node->extractors[i]);
        }
        free(node->extractors);
    }
    free(node);
}

static int node_add_child(route_node_t *node, route_node_t *child) {
    if (node->child_count >= node->child_capacity) {
        size_t new_cap = node->child_capacity * 2;
        route_node_t **na = (route_node_t **)realloc(
            node->children, new_cap * sizeof(route_node_t *));
        if (!na) {
            return -1;
        }
        node->children = na;
        node->child_capacity = new_cap;
    }
    node->children[node->child_count++] = child;
    return 0;
}

/* ==================== 树生命周期 ==================== */

void route_tree_init(route_tree_t *tree) {
    if (!tree) {
        return;
    }
    memset(tree, 0, sizeof(*tree));
    tree->root = create_node();
}

void route_tree_destroy(route_tree_t *tree) {
    if (!tree) {
        return;
    }
    if (tree->root) {
        destroy_node(tree->root);
    }
    memset(tree, 0, sizeof(*tree));
}

/* ==================== 匹配序列比较 ==================== */

static int blob_equal(const stride_blob_t *a, const stride_blob_t *b) {
    if (a->len != b->len) {
        return 0;
    }
    if (a->len == 0) {
        return 1;
    }
    return memcmp(a->data, b->data, a->len) == 0;
}

int match_sequences_equal(const stride_seq_t *a, const stride_seq_t *b) {
    if (a == b) {
        return 1;
    }
    if (!a || !b || a->count != b->count) {
        return 0;
    }

    const stride_step_t *x = a->head;
    const stride_step_t *y = b->head;
    for (; x && y; x = x->next, y = y->next) {
        if (x->move != y->move || x->move_value != y->move_value) {
            return 0;
        }
        if (x->act != y->act || x->act_value != y->act_value) {
            return 0;
        }
        if (!blob_equal(&x->move_target, &y->move_target) ||
            !blob_equal(&x->act_target, &y->act_target)) {
            return 0;
        }
    }
    return x == NULL && y == NULL;
}

/**
 * 查找或创建具有相同匹配序列的子节点
 *
 * 无论成功与否都接管 match_seq 的所有权：
 * - 命中已有节点 → 释放传入序列（合并）
 * - 新建节点     → 序列挂到节点上
 * - 失败         → 释放传入序列并返回 NULL
 */
static route_node_t *find_or_create_child(route_node_t *parent,
                                          stride_seq_t *match_seq) {
    for (size_t i = 0; i < parent->child_count; i++) {
        route_node_t *child = parent->children[i];
        if (match_sequences_equal(child->match, match_seq)) {
            stride_seq_free(match_seq);
            return child;
        }
    }

    route_node_t *node = create_node();
    if (!node) {
        stride_seq_free(match_seq);
        return NULL;
    }
    node->match = match_seq;

    if (node_add_child(parent, node) != 0) {
        node->match = NULL; /* 由 destroy_node 释放 children，这里手动收尾 */
        free(node->children);
        free(node);
        stride_seq_free(match_seq);
        return NULL;
    }
    return node;
}

static int check_conflict(route_node_t *node, size_t segment_index,
                          stride_seq_t **match_seqs, size_t segment_count) {
    if (segment_index >= segment_count) {
        return node->is_leaf ? -1 : 0;
    }
    for (size_t i = 0; i < node->child_count; i++) {
        route_node_t *child = node->children[i];
        if (match_sequences_equal(child->match, match_seqs[segment_index])) {
            return check_conflict(child, segment_index + 1, match_seqs,
                                  segment_count);
        }
    }
    return 0;
}

/* ==================== 注册 ==================== */

static void free_seqs(stride_seq_t **seqs, size_t from, size_t to) {
    for (size_t i = from; i < to; i++) {
        stride_seq_free(seqs[i]);
    }
}

int route_tree_register(route_tree_t *tree, stride_seq_t **match_seqs,
                        size_t segment_count, stride_extractor_t **extract_seqs,
                        size_t extractor_count, route_callback_t callback,
                        void *userdata, char sep) {
    if (!tree || !tree->root || !match_seqs || segment_count == 0) {
        return -1;
    }

    if (check_conflict(tree->root, 0, match_seqs, segment_count) != 0) {
        free_seqs(match_seqs, 0, segment_count);
        free_seqs(extract_seqs, 0, extractor_count);
        return -1;
    }

    route_node_t *current = tree->root;
    size_t consumed = 0;
    for (size_t i = 0; i < segment_count; i++) {
        route_node_t *child = find_or_create_child(current, match_seqs[i]);
        consumed = i + 1; /* find_or_create_child 始终接管所有权 */
        if (!child) {
            free_seqs(match_seqs, consumed, segment_count);
            free_seqs(extract_seqs, 0, extractor_count);
            return -1;
        }
        current = child;
    }

    /* 组装提取序列数组（接管 extract_seqs 中指针的所有权）*/
    stride_extractor_t **segs =
        (stride_extractor_t **)calloc(segment_count, sizeof(stride_extractor_t *));
    if (!segs) {
        free_seqs(extract_seqs, 0, extractor_count);
        return -1;
    }
    for (size_t i = 0; i < extractor_count; i++) {
        segs[i] = extract_seqs[i];
    }

    /* segs 数组的所有权交给 find_or_create_child 返回的节点 */

    current->is_leaf = 1;
    current->extractors = segs;
    current->segment_count = segment_count;
    current->callback = callback;
    current->userdata = userdata;
    current->sep = sep;

    tree->route_count++;
    return 0;
}

/**
 * 优先级：比对动作越多越具体，应优先匹配
 */
static int get_node_priority(const route_node_t *node) {
    if (!node || !node->match) {
        return 0;
    }
    int priority = 0;
    for (const stride_step_t *n = node->match->head; n; n = n->next) {
        priority += (n->act == STRIDE_ACT_COMPARE) ? 100 : 1;
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

        for (size_t j = 0; j < current->child_count; j++) {
            route_node_t *child = current->children[j];
            if (stride_match_run(child->match, segment, seg_len) == 0) {
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
