#define _GNU_SOURCE  /* for strdup */

#include "router.h"
#include "pattern_compiler.h"
#include "route_tree.h"
#include "extractor.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ==================== 路由器内部结构 ==================== */

struct router {
    route_tree_t trees[HTTP_METHOD_COUNT];
};

/* ==================== 工具函数 ==================== */

/**
 * 解析 URL 为段数组
 * 返回的 segments 数组和段内容都需要被释放
 */
static int parse_url(const char *url, char ***out_segments, size_t *out_count) {
    if (!url || !out_segments || !out_count) {
        return -1;
    }
    
    *out_segments = NULL;
    *out_count = 0;
    
    /* 必须以/开头 */
    if (url[0] != '/') {
        return -1;
    }
    
    /* 处理特殊情况：只有 "/" */
    if (url[1] == '\0') {
        return 0;
    }
    
    /* 检查空段（连续斜杠） */
    const char *check = url;
    while (*check) {
        if (*check == '/' && *(check + 1) == '/') {
            return -1;  /* 空段 */
        }
        check++;
    }
    
    /* 统计段数 */
    size_t count = 0;
    const char *p = url + 1;
    while (*p) {
        if (*p == '/') {
            count++;
        }
        p++;
    }
    if (*(p - 1) != '/') {
        count++;
    }
    
    if (count == 0) {
        return 0;
    }
    
    /* 分配数组 */
    char **segments = calloc(count, sizeof(char *));
    if (!segments) {
        return -1;
    }
    
    /* 复制并分割 URL */
    char *url_copy = strdup(url + 1);  /* 跳过开头的/ */
    if (!url_copy) {
        free(segments);
        return -1;
    }
    
    /* 分割 */
    char *saveptr = NULL;
    char *token = strtok_r(url_copy, "/", &saveptr);
    size_t idx = 0;
    
    while (token && idx < count) {
        segments[idx++] = token;
        token = strtok_r(NULL, "/", &saveptr);
    }
    
    *out_segments = segments;
    *out_count = idx;
    
    /* 注意：url_copy 不能释放，因为 segments 中的指针指向它 */
    return 0;
}

/**
 * 释放段数组
 */
static void free_segments(char **segments, size_t count) {
    if (segments) {
        if (count > 0 && segments[0]) {
            free(segments[0]);  /* 释放 strdup 的副本 */
        }
        free(segments);
    }
}

/* ==================== 路由器 API ==================== */

router_t *router_create(void) {
    router_t *router = calloc(1, sizeof(router_t));
    if (!router) {
        return NULL;
    }
    
    for (int i = 0; i < HTTP_METHOD_COUNT; i++) {
        route_tree_init(&router->trees[i]);
    }
    
    return router;
}

void router_destroy(router_t *router) {
    if (!router) {
        return;
    }
    
    for (int i = 0; i < HTTP_METHOD_COUNT; i++) {
        route_tree_destroy(&router->trees[i]);
    }
    
    free(router);
}

int router_register(router_t *router, http_method_t method,
                    const char *pattern, route_callback_t callback,
                    void *userdata) {
    if (!router || !pattern || !callback) {
        return -1;
    }
    
    if (method >= HTTP_METHOD_COUNT || method < 0) {
        return -1;
    }
    
    /* 解析 pattern 为段数组 */
    char **segments = NULL;
    size_t segment_count = 0;
    
    if (parse_url(pattern, &segments, &segment_count) != 0) {
        return -1;
    }
    
    if (segment_count == 0) {
        return -1;
    }
    
    /* 编译每个段 */
    feature_tuple_t **segment_features = calloc(segment_count, sizeof(feature_tuple_t *));
    size_t *segment_feature_counts = calloc(segment_count, sizeof(size_t));
    extractor_t **segment_extractors = calloc(segment_count, sizeof(extractor_t *));
    
    if (!segment_features || !segment_feature_counts || !segment_extractors) {
        free(segment_features);
        free(segment_feature_counts);
        free(segment_extractors);
        free_segments(segments, segment_count);
        return -1;
    }
    
    int compile_error = 0;
    
    for (size_t i = 0; i < segment_count; i++) {
        compile_result_t result = pattern_compile(segments[i]);
        
        if (result.status != COMPILE_OK) {
            compile_error = 1;
            
            for (size_t j = 0; j < i; j++) {
                if (segment_features[j]) free(segment_features[j]);
                if (segment_extractors[j]) extractor_destroy(segment_extractors[j]);
            }
            break;
        }
        
        segment_features[i] = result.features;
        segment_feature_counts[i] = result.feature_count;
        segment_extractors[i] = result.extractor;
        
        result.features = NULL;
        result.extractor = NULL;
        pattern_compile_free(&result);
    }
    
    free_segments(segments, segment_count);
    
    if (compile_error) {
        free(segment_features);
        free(segment_feature_counts);
        free(segment_extractors);
        return -1;
    }
    
    /* 注册到路由树 */
    int ret = route_tree_register(&router->trees[method],
                                   segment_features,
                                   segment_feature_counts,
                                   segment_count,
                                   segment_extractors,
                                   segment_count,
                                   callback,
                                   userdata);
    
    /* 清理 */
    for (size_t i = 0; i < segment_count; i++) {
        if (segment_features[i]) free(segment_features[i]);
        if (segment_extractors[i]) extractor_destroy(segment_extractors[i]);
    }
    free(segment_features);
    free(segment_feature_counts);
    free(segment_extractors);
    
    return ret;
}

route_node_t *router_match(router_t *router, http_method_t method,
                           const char *url) {
    if (!router || !url) {
        return NULL;
    }
    
    if (method >= HTTP_METHOD_COUNT || method < 0) {
        return NULL;
    }
    
    /* 解析 URL 为段数组 */
    char **segments = NULL;
    size_t segment_count = 0;
    
    if (parse_url(url, &segments, &segment_count) != 0) {
        return NULL;
    }
    
    if (segment_count == 0) {
        free_segments(segments, segment_count);
        return NULL;
    }
    
    /* 在路由树中匹配 */
    route_node_t *node = route_tree_match(&router->trees[method],
                                           (const char **)segments,
                                           segment_count);
    
    free_segments(segments, segment_count);
    return node;
}

int router_extract(route_node_t *node, const char *url,
                   route_param_t *params, size_t param_capacity,
                   size_t *out_count) {
    if (!node || !url || !params || !out_count) {
        return -1;
    }
    
    extractor_t *extractor = node->extractor;
    if (!extractor) {
        *out_count = 0;
        return 0;
    }
    
    if (extractor->op_count == 0) {
        *out_count = 0;
        return 0;
    }
    
    /* 解析 URL 为段数组 */
    char **segments = NULL;
    size_t segment_count = 0;
    
    if (parse_url(url, &segments, &segment_count) != 0) {
        return -1;
    }
    
    if (segment_count == 0) {
        *out_count = 0;
        return 0;
    }
    
    /* 简单实现：只使用最后一段的提取器，作用于最后一段 */
    size_t param_count = 0;
    const char *segment = segments[segment_count - 1];
    size_t seg_len = strlen(segment);
    
    int ret = extractor_execute(extractor, segment, seg_len,
                                params, param_capacity, &param_count);
    
    free_segments(segments, segment_count);
    
    if (ret == 0) {
        *out_count = param_count;
    }
    
    return ret;
}

route_callback_t router_get_callback(route_node_t *node) {
    if (!node) {
        return NULL;
    }
    return node->callback;
}

void *router_get_userdata(route_node_t *node) {
    if (!node) {
        return NULL;
    }
    return node->userdata;
}

/* ==================== 辅助函数 ==================== */

size_t router_param_to_string(route_param_t param, char *buf, size_t buf_size) {
    if (!buf || buf_size == 0) {
        return 0;
    }
    
    size_t copy_len = param.len < buf_size - 1 ? param.len : buf_size - 1;
    
    if (param.ptr && copy_len > 0) {
        memcpy(buf, param.ptr, copy_len);
    }
    buf[copy_len] = '\0';
    
    return copy_len;
}

int router_param_is_empty(route_param_t param) {
    return param.len == 0 || param.ptr == NULL;
}
