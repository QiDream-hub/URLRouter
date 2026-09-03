#define _GNU_SOURCE /* for strdup */

#include "router.h"
#include "extractor.h"
#include "pattern_compiler.h"
#include "route_tree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * URLRouter 路由器 - 实现文件
 *
 * 根据设计文档 2.2 版本实现
 * 支持零拷贝段解析和参数提取
 * ============================================================ */

/* ==================== 路由器内部结构 ==================== */

struct router {
  route_tree_t trees[HTTP_METHOD_COUNT];
  char sep; /* 路径分隔符*/
};

/* ==================== 工具函数 ==================== */

/**
 * 解析路径为段数组（用于注册时的模式解析）
 * @param path 路径字符串
 * @param sep 分隔符
 * 返回的 segments 数组和段内容都需要被释放
 */
static int parse_url(const char *path, char sep, char ***out_segments,
                     size_t *out_count) {
  if (!path || !out_segments || !out_count) {
    return -1;
  }
  if (sep == '\0') {
    return -1;
  }

  *out_segments = NULL;
  *out_count = 0;

  /* 必须以分隔符开头 */
  if (path[0] != sep) {
    return -1;
  }

  /* 处理特殊情况：只有分隔符本身（根路径） */
  if (path[1] == '\0') {
    return 0;
  }

  /* 检查空段（连续分隔符） */
  const char *check = path;
  while (*check) {
    if (*check == sep && *(check + 1) == sep) {
      return -1; /* 空段 */
    }
    check++;
  }

  /* 统计段数 */
  size_t count = 0;
  const char *p = path + 1;
  while (*p) {
    if (*p == sep) {
      count++;
    }
    p++;
  }
  if (*(p - 1) != sep) {
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

  /* 复制并分割路径 */
  char delim[2] = {sep, '\0'};
  char *path_copy = strdup(path + 1); /* 跳过开头的分隔符 */
  if (!path_copy) {
    free(segments);
    return -1;
  }

  /* 分割 */
  char *saveptr = NULL;
  char *token = strtok_r(path_copy, delim, &saveptr);
  size_t idx = 0;

  while (token && idx < count) {
    segments[idx++] = token;
    token = strtok_r(NULL, delim, &saveptr);
  }

  *out_segments = segments;
  *out_count = idx;

  /* 注意：path_copy 不能释放，因为 segments 中的指针指向它 */
  return 0;
}

/**
 * 释放段数组
 */
static void free_segments(char **segments, size_t count) {
  if (segments) {
    if (count > 0 && segments[0]) {
      free(segments[0]); /* 释放 strdup 的副本 */
    }
    free(segments);
  }
}

/**
 * 解析路径为段指针和长度数组（零拷贝，指向原始路径）
 * @param path 原始路径字符串
 * @param sep 分隔符
 * @param out_segments 输出段指针数组（调用者负责释放）
 * @param out_seg_lens 输出段长度数组（调用者负责释放）
 * @param out_count 输出段数量
 * @return 0 成功，-1 失败
 */
static int parse_url_segments(const char *path, char sep,
                              const char ***out_segments, size_t **out_seg_lens,
                              size_t *out_count) {
  if (!path || !out_segments || !out_seg_lens || !out_count) {
    return -1;
  }
  if (sep == '\0') {
    return -1;
  }

  *out_segments = NULL;
  *out_seg_lens = NULL;
  *out_count = 0;

  /* 必须以分隔符开头 */
  if (path[0] != sep) {
    return -1;
  }

  /* 处理特殊情况：只有分隔符本身（根路径） */
  if (path[1] == '\0') {
    return 0;
  }

  /* 检查空段（连续分隔符） */
  const char *check = path;
  while (*check) {
    if (*check == sep && *(check + 1) == sep) {
      return -1; /* 空段 */
    }
    check++;
  }

  /* 统计段数 */
  size_t count = 0;
  const char *p = path + 1;
  while (*p) {
    if (*p == sep) {
      count++;
    }
    p++;
  }
  if (*(p - 1) != sep) {
    count++;
  }

  if (count == 0) {
    return 0;
  }

  /* 分配数组 */
  const char **segments = calloc(count, sizeof(const char *));
  size_t *seg_lens = calloc(count, sizeof(size_t));
  if (!segments || !seg_lens) {
    free(segments);
    free(seg_lens);
    return -1;
  }

  /* 填充段指针（指向原始路径）和长度 */
  p = path + 1;
  size_t idx = 0;
  const char *seg_start = p;

  while (*p) {
    if (*p == sep) {
      if (idx < count) {
        segments[idx] = seg_start;
        seg_lens[idx] = p - seg_start;
        idx++;
      }
      p++;
      seg_start = p;
    } else {
      p++;
    }
  }

  /* 最后一段 */
  if (p > seg_start && idx < count) {
    segments[idx] = seg_start;
    seg_lens[idx] = p - seg_start;
  }

  *out_segments = segments;
  *out_seg_lens = seg_lens;
  *out_count = count;

  return 0;
}

/**
 * 释放段指针和长度数组
 */
static void free_url_segments(const char **segments, size_t *seg_lens) {
  free(segments);
  free(seg_lens);
}

/* ==================== 路由器 API ==================== */

router_t *router_create(char sep) {
  if (sep == '\0') {
    return NULL;
  }

  router_t *router = calloc(1, sizeof(router_t));
  if (!router) {
    return NULL;
  }

  router->sep = sep;

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

int router_register(router_t *router, http_method_t method, const char *pattern,
                    route_callback_t callback, void *userdata) {
  if (!router || !pattern || !callback) {
    return -1;
  }

  if (method >= HTTP_METHOD_COUNT || method < 0) {
    return -1;
  }

  /* 解析 pattern 为段数组 */
  char **segments = NULL;
  size_t segment_count = 0;

  if (parse_url(pattern, router->sep, &segments, &segment_count) != 0) {
    return -1;
  }

  if (segment_count == 0) {
    free_segments(segments, segment_count);
    return -1;
  }

  /* 编译每个段 */
  feature_tuple_t **segment_features =
      calloc(segment_count, sizeof(feature_tuple_t *));
  size_t *segment_feature_counts = calloc(segment_count, sizeof(size_t));
  extractor_t **segment_extractors =
      calloc(segment_count, sizeof(extractor_t *));

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
        if (segment_features[j])
          free(segment_features[j]);
        if (segment_extractors[j])
          extractor_destroy(segment_extractors[j]);
      }
      break;
    }

    segment_features[i] = result.features;
    segment_feature_counts[i] = result.feature_count;
    segment_extractors[i] =
        extractor_create(result.extractors, result.extractor_count);

    /* 防止 pattern_compile_free 释放已转移的所有权 */
    result.features = NULL;
    result.extractors = NULL;
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
  int ret = route_tree_register(&router->trees[method], segment_features,
                                segment_feature_counts, segment_count,
                                segment_extractors, segment_count, callback,
                                userdata, router->sep);

  /* 清理特征序列（提取器已转移到路由树，不再释放） */
  for (size_t i = 0; i < segment_count; i++) {
    if (segment_features[i])
      free(segment_features[i]);
    /* segment_extractors[i] 已转移到路由树，不释放 */
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

  /* 解析 URL 为段指针和长度数组（零拷贝） */
  const char **segments = NULL;
  size_t *seg_lens = NULL;
  size_t segment_count = 0;

  if (parse_url_segments(url, router->sep, &segments, &seg_lens,
                         &segment_count) != 0) {
    return NULL;
  }

  if (segment_count == 0) {
    return NULL;
  }

  /* 在路由树中匹配 */
  route_node_t *node = route_tree_match(&router->trees[method], segments,
                                        seg_lens, segment_count);

  free_url_segments(segments, seg_lens);
  return node;
}

int router_extract(route_node_t *node, const char *url, route_param_t *params,
                   size_t param_capacity, size_t *out_count) {
  if (!node || !url || !params || !out_count) {
    return -1;
  }

  full_extractor_t *extractor = node->extractor;
  if (!extractor || extractor->segment_count == 0) {
    *out_count = 0;
    return 0;
  }

  /* 解析 URL 为段指针和长度数组（零拷贝） */
  const char **segments = NULL;
  size_t *seg_lens = NULL;
  size_t segment_count = 0;

  // 检查分隔符
  if (node->sep) {
    return -1;
  }

  if (parse_url_segments(url, node->sep, &segments, &seg_lens,
                         &segment_count) != 0) {
    return -1;
  }

  if (segment_count == 0 || segment_count != extractor->segment_count) {
    free_url_segments(segments, seg_lens);
    return -1;
  }

  /* 执行完整提取（多段）*/
  size_t param_idx = 0;
  for (size_t i = 0; i < segment_count; i++) {
    size_t seg_param_count = 0;

    if (extractor->segments[i]) {
      int ret = segment_extractor_execute(
          extractor->segments[i], segments[i], seg_lens[i], &params[param_idx],
          param_capacity - param_idx, &seg_param_count);
      if (ret != 0) {
        free_url_segments(segments, seg_lens);
        return -1;
      }
    }

    param_idx += seg_param_count;
    /* 检查是否超过参数容量 */
    if (param_idx > param_capacity) {
      free_url_segments(segments, seg_lens);
      return -1; /* 参数容量不足 */
    }
  }

  /* 释放数组（不释放段内容，因为指向原始 URL） */
  free_url_segments(segments, seg_lens);

  *out_count = param_idx;
  return 0;
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

  return param.len; /* 返回实际长度 */
}

int router_param_is_empty(route_param_t param) {
  return param.len == 0 || param.ptr == NULL;
}
