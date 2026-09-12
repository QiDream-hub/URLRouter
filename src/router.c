#define _GNU_SOURCE /* for strdup */

#include "router.h"
#include "route_tree.h"
#include "stride/stride.h"
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

  /* 编译每个段：模式 → 匹配序列 + 提取序列 */
  stride_seq_t **segment_match = calloc(segment_count, sizeof(stride_seq_t *));
  stride_extractor_t **segment_extract =
      calloc(segment_count, sizeof(stride_extractor_t *));

  if (!segment_match || !segment_extract) {
    free(segment_match);
    free(segment_extract);
    free_segments(segments, segment_count);
    return -1;
  }

  int compile_error = 0;

  for (size_t i = 0; i < segment_count; i++) {
    url_compile_result_t result = url_compile(segments[i], 0);

    if (result.status != STRIDE_OK) {
      compile_error = 1;
      for (size_t j = 0; j < i; j++) {
        stride_seq_free(segment_match[j]);
        stride_seq_free(segment_extract[j]);
      }
      break;
    }

    /* 转移所有权 */
    segment_match[i] = result.match;
    segment_extract[i] = result.extract;
    result.match = NULL;
    result.extract = NULL;
    url_compile_free(&result);
  }

  free_segments(segments, segment_count);

  if (compile_error) {
    free(segment_match);
    free(segment_extract);
    return -1;
  }

  /* 注册到路由树：两个序列数组的所有权都交给它（成功或失败）*/
  int ret = route_tree_register(&router->trees[method], segment_match,
                                segment_count, segment_extract, segment_count,
                                callback, userdata, router->sep);

  free(segment_match);
  free(segment_extract);
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

  stride_full_extractor_t *extractor = node->extractor;
  if (!extractor || extractor->segment_count == 0) {
    *out_count = 0;
    return 0;
  }

  /* 节点必须记录了分隔符才能切分查询路径 */
  if (node->sep == '\0') {
    return -1;
  }

  /* 解析 URL 为段指针和长度数组（零拷贝） */
  const char **segments = NULL;
  size_t *seg_lens = NULL;
  size_t segment_count = 0;

  if (parse_url_segments(url, node->sep, &segments, &seg_lens,
                         &segment_count) != 0) {
    return -1;
  }

  if (segment_count == 0 || segment_count != extractor->segment_count) {
    free_url_segments(segments, seg_lens);
    return -1;
  }

  /* Stride 以比特计量：把段长（字节）换算成比特 */
  size_t *seg_bit_lens = calloc(segment_count, sizeof(size_t));
  stride_param_t *tmp =
      calloc(param_capacity ? param_capacity : 1, sizeof(stride_param_t));
  if (!seg_bit_lens || !tmp) {
    free(seg_bit_lens);
    free(tmp);
    free_url_segments(segments, seg_lens);
    return -1;
  }
  for (size_t i = 0; i < segment_count; i++) {
    seg_bit_lens[i] = seg_lens[i] * 8;
  }

  /* 执行完整提取（多段，参数按段顺序连接，零拷贝）*/
  size_t n = 0;
  int ret = stride_full_extractor_run(extractor, URL_PATTERN_STRIDE,
                                      (const void *const *)segments,
                                      seg_bit_lens, segment_count, tmp,
                                      param_capacity, &n);
  if (ret == 0) {
    /* 比特长度 → 字节长度 */
    for (size_t i = 0; i < n; i++) {
      if (tmp[i].bit_len % 8 != 0) {
        ret = -1;
        break;
      }
      params[i].ptr = (const char *)tmp[i].ptr;
      params[i].len = tmp[i].bit_len / 8;
    }
  }
  if (ret == 0) {
    *out_count = n;
  }

  free(seg_bit_lens);
  free(tmp);
  free_url_segments(segments, seg_lens);
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

  return param.len; /* 返回实际长度 */
}

int router_param_is_empty(route_param_t param) {
  return param.len == 0 || param.ptr == NULL;
}
