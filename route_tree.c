#include "route_tree.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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
    
    if (node->features) {
        /* 释放每个特征元组中的关键字 */
        for (size_t i = 0; i < node->feature_count; i++) {
            if (node->features[i].keyword) {
                free((char *)node->features[i].keyword);
            }
        }
        free(node->features);
    }
    if (node->children) {
        free(node->children);
    }
    if (node->extractor) {
        extractor_destroy(node->extractor);
    }
    
    free(node);
}

static int node_add_child(route_node_t *node, route_node_t *child) {
    if (node->child_count >= node->child_capacity) {
        size_t new_cap = node->child_capacity * 2;
        route_node_t **new_children = realloc(node->children,
                                               new_cap * sizeof(route_node_t *));
        if (!new_children) {
            return -1;
        }
        node->children = new_children;
        node->child_capacity = new_cap;
    }
    
    node->children[node->child_count++] = child;
    return 0;
}

static void node_set_leaf(route_node_t *node, extractor_t *extractor,
                          route_callback_t callback, void *userdata) {
    node->is_leaf = 1;
    node->extractor = extractor;
    node->callback = callback;
    node->userdata = userdata;
}

/* ==================== 特征序列执行 ==================== */

int feature_execute(const feature_tuple_t *features, size_t feature_count,
                    const char *segment, size_t segment_len) {
    size_t cursor = 0;
    
    for (size_t i = 0; i < feature_count; i++) {
        const feature_tuple_t *ft = &features[i];
        
        switch (ft->type) {
            case FT_OFFSET_POS: {
                size_t new_pos = cursor + (size_t)ft->value;
                if (new_pos > segment_len) {
                    return -1;
                }
                cursor = new_pos;
                break;
            }
            
            case FT_OFFSET_NEG: {
                int new_pos = (int)cursor + ft->value;
                if (new_pos < 0) {
                    return -1;
                }
                cursor = (size_t)new_pos;
                break;
            }
            
            case FT_CHAR_FIND: {
                char target = (char)ft->value;
                const char *found = memchr(segment + cursor, target,
                                           segment_len - cursor);
                if (!found) {
                    return -1;
                }
                cursor = (size_t)(found - segment);
                break;
            }
            
            case FT_END_OFFSET: {
                if (ft->value == 0) {
                    cursor = segment_len;
                } else {
                    int new_pos = (int)segment_len + ft->value;
                    if (new_pos < 0) {
                        return -1;
                    }
                    cursor = (size_t)new_pos;
                }
                break;
            }
        }
        
        /* 检查关键字匹配 */
        if (ft->keyword) {
            size_t remaining = segment_len - cursor;
            if (remaining < ft->keyword_len) {
                return -1;
            }
            
            if (memcmp(segment + cursor, ft->keyword, ft->keyword_len) != 0) {
                return -1;
            }
            
            cursor += ft->keyword_len;
        }
    }
    
    /* 段尾对齐检查 */
    if (cursor != segment_len) {
        return -1;
    }
    
    return 0;
}

int feature_sequences_equal(const feature_tuple_t *a, size_t a_count,
                            const feature_tuple_t *b, size_t b_count) {
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

static route_node_t *find_or_create_child(route_node_t *parent,
                                          const feature_tuple_t *features,
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
    
    if (feature_count > 0) {
        node->features = malloc(feature_count * sizeof(feature_tuple_t));
        if (!node->features) {
            free(node);
            return NULL;
        }
        memcpy(node->features, features, feature_count * sizeof(feature_tuple_t));
        node->feature_count = feature_count;
    }
    
    if (node_add_child(parent, node) != 0) {
        if (node->features) {
            free(node->features);
        }
        free(node);
        return NULL;
    }
    
    return node;
}

static int check_conflict(route_node_t *node, size_t segment_index,
                          feature_tuple_t **segments,
                          size_t *segment_feature_counts,
                          size_t segment_count) {
    if (segment_index >= segment_count) {
        return node->is_leaf ? -1 : 0;
    }
    
    feature_tuple_t *current_features = segments[segment_index];
    size_t current_count = segment_feature_counts[segment_index];
    
    for (size_t i = 0; i < node->child_count; i++) {
        route_node_t *child = node->children[i];
        
        if (feature_sequences_equal(child->features, child->feature_count,
                                    current_features, current_count)) {
            return check_conflict(child, segment_index + 1,
                                  segments, segment_feature_counts,
                                  segment_count);
        }
    }
    
    return 0;
}

int route_tree_register(route_tree_t *tree,
                        feature_tuple_t **segments,
                        size_t *segment_feature_counts,
                        size_t segment_count,
                        extractor_t **extractors,
                        size_t extractor_count,
                        route_callback_t callback,
                        void *userdata) {
    if (!tree || !tree->root || !segments || segment_count == 0) {
        return -1;
    }
    
    if (check_conflict(tree->root, 0, segments, segment_feature_counts,
                       segment_count) != 0) {
        return -1;
    }
    
    route_node_t *current = tree->root;
    
    for (size_t i = 0; i < segment_count; i++) {
        feature_tuple_t *features = segments[i];
        size_t feature_count = segment_feature_counts[i];
        
        route_node_t *child = find_or_create_child(current, features, feature_count);
        if (!child) {
            return -1;
        }
        
        current = child;
    }
    
    /* 合并提取器：将所有段的提取器操作合并 */
    extractor_t *merged_extractor = extractor_merge(extractors, extractor_count);
    
    node_set_leaf(current, merged_extractor, callback, userdata);
    tree->route_count++;
    
    return 0;
}

/* ==================== 路由匹配 ==================== */

route_node_t *route_tree_match(route_tree_t *tree,
                               const char **segments,
                               size_t segment_count) {
    if (!tree || !tree->root || !segments || segment_count == 0) {
        return NULL;
    }
    
    route_node_t *current = tree->root;
    
    for (size_t i = 0; i < segment_count; i++) {
        const char *segment = segments[i];
        size_t seg_len = strlen(segment);
        
        route_node_t *matched = NULL;
        
        for (size_t j = 0; j < current->child_count; j++) {
            route_node_t *child = current->children[j];
            
            if (feature_execute(child->features, child->feature_count,
                                segment, seg_len) == 0) {
                matched = child;
                break;
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

route_node_t *route_tree_match_seg_len(route_tree_t *tree,
                                        const char **segments,
                                        size_t *seg_lens,
                                        size_t segment_count) {
    (void)tree;
    (void)segments;
    (void)seg_lens;
    (void)segment_count;
    return NULL;
}
