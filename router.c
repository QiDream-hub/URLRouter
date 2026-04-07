#include "router.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 路由节点结构体
typedef struct route_node {
  char *segment;   // 段内容（关键字），通配符时为NULL
  int is_wildcard; // 是否是通配符${}
  int param_index; // 通配符捕获的参数索引

  struct route_node *next;        // 下一个兄弟节点（链表）
  struct route_node *first_child; // 第一个子节点

  route_callback_t callback; // 回调函数（只有终点节点才有）
} route_node_t;

// 路由器结构体
struct router {
  route_node_t *roots[HTTP_METHOD_COUNT];
};

// 创建节点
static route_node_t *create_node(void) {
  route_node_t *node = (route_node_t *)calloc(1, sizeof(route_node_t));
  if (!node)
    return NULL;
  node->param_index = -1;
  return node;
}

// 释放节点及其子树
static void free_node(route_node_t *node) {
  if (!node)
    return;

  // 递归释放子节点
  route_node_t *child = node->first_child;
  while (child) {
    route_node_t *next = child->next;
    free_node(child);
    child = next;
  }

  if (node->segment)
    free(node->segment);
  free(node);
}

// 解析pattern，返回段数组
static char **parse_pattern(const char *pattern, int *segment_count) {
  if (!pattern || pattern[0] != '/') {
    return NULL;
  }

  // 先统计段数
  int count = 0;
  const char *p = pattern;
  while (*p) {
    if (*p == '/') {
      count++;
      // 跳过连续的/
      while (*(p + 1) == '/')
        p++;
    }
    p++;
  }

  if (count == 0)
    return NULL;

  // 分配数组
  char **segments = (char **)calloc(count + 1, sizeof(char *));
  if (!segments)
    return NULL;

  // 解析每个段
  int idx = 0;
  p = pattern + 1; // 跳过开头的/

  while (*p && idx < count) {
    const char *start = p;

    // 找到段结束位置
    while (*p && *p != '/') {
      p++;
    }

    // 提取段内容
    size_t len = p - start;
    char *segment = (char *)malloc(len + 1);
    if (!segment) {
      // 清理已分配的内存
      for (int i = 0; i < idx; i++) {
        free(segments[i]);
      }
      free(segments);
      return NULL;
    }

    strncpy(segment, start, len);
    segment[len] = '\0';
    segments[idx++] = segment;

    // 跳过/
    if (*p == '/')
      p++;
  }

  segments[idx] = NULL;
  *segment_count = idx;
  return segments;
}

// 判断是否是关键字格式 $'xxx'
static int is_keyword(const char *segment) {
  if (!segment || strlen(segment) < 4)
    return 0;

  // 检查格式: $'xxx'
  if (segment[0] == '$' && segment[1] == '\'') {
    // 检查最后一个字符是否是单引号
    size_t len = strlen(segment);
    if (segment[len - 1] == '\'') {
      return 1;
    }
  }
  return 0;
}

// 提取关键字内容 $'keyword' -> keyword
static char *extract_keyword(const char *segment) {
  if (!is_keyword(segment))
    return NULL;

  size_t len = strlen(segment);
  // $'keyword' 格式，去掉 $' 和最后的 '
  size_t keyword_len = len - 3; // 减去 $' 和 '
  char *keyword = (char *)malloc(keyword_len + 1);
  if (!keyword)
    return NULL;

  strncpy(keyword, segment + 2, keyword_len);
  keyword[keyword_len] = '\0';
  return keyword;
}

// 判断是否是通配符 ${}
static int is_wildcard(const char *segment) {
  if (!segment)
    return 0;
  return strcmp(segment, "${}") == 0;
}

// 查找子节点
static route_node_t *find_child(route_node_t *node, const char *segment,
                                int prefer_keyword) {
  if (!node || !segment)
    return NULL;

  route_node_t *wildcard_child = NULL;

  for (route_node_t *child = node->first_child; child; child = child->next) {
    if (child->is_wildcard) {
      wildcard_child = child;
      continue;
    }

    // 关键字匹配
    if (child->segment && strcmp(child->segment, segment) == 0) {
      return child;
    }
  }

  // 没有精确匹配，返回通配符节点（如果存在）
  if (wildcard_child) {
    return wildcard_child;
  }

  return NULL;
}

// 注册路由的内部实现
static int register_route(router_t *router, http_method_t method,
                          char **segments, int segment_count,
                          route_callback_t callback) {
  if (method >= HTTP_METHOD_COUNT)
    return -1;

  // 获取根节点
  route_node_t *root = router->roots[method];
  if (!root) {
    root = create_node();
    if (!root)
      return -1;
    router->roots[method] = root;
  }

  route_node_t *current = root;
  int param_index = 0;

  // 遍历每个段，创建节点
  for (int i = 0; i < segment_count; i++) {
    char *segment = segments[i];
    int is_wild = is_wildcard(segment);
    int is_key = is_keyword(segment);

    route_node_t *child = NULL;

    if (is_wild) {
      // 查找通配符节点
      for (route_node_t *c = current->first_child; c; c = c->next) {
        if (c->is_wildcard) {
          child = c;
          break;
        }
      }

      if (!child) {
        // 创建通配符节点
        child = create_node();
        if (!child)
          return -1;
        child->is_wildcard = 1;
        child->segment = NULL;
        child->param_index = param_index++;

        // 添加到父节点
        child->next = current->first_child;
        current->first_child = child;
      } else if (child->param_index == -1) {
        // 如果已存在但没有参数索引，设置它
        child->param_index = param_index++;
      }
    } else if (is_key) {
      // 提取关键字
      char *keyword = extract_keyword(segment);
      if (!keyword)
        return -1;

      // 查找关键字节点
      for (route_node_t *c = current->first_child; c; c = c->next) {
        if (!c->is_wildcard && c->segment && strcmp(c->segment, keyword) == 0) {
          child = c;
          break;
        }
      }

      if (!child) {
        // 创建关键字节点
        child = create_node();
        if (!child) {
          free(keyword);
          return -1;
        }
        child->is_wildcard = 0;
        child->segment = keyword; // 直接使用已分配的内存
        child->param_index = -1;

        // 添加到父节点
        child->next = current->first_child;
        current->first_child = child;
      } else {
        // 节点已存在，释放临时分配的keyword
        free(keyword);
      }
    } else {
      // 无效的段格式
      fprintf(stderr, "Invalid segment format: %s\n", segment);
      return -1;
    }

    current = child;
  }

  // 设置回调
  if (current->callback) {
    fprintf(stderr, "Warning: route already exists, overwriting\n");
  }
  current->callback = callback;

  return 0;
}

// 公共API实现

router_t *router_create(void) {
  router_t *router = (router_t *)calloc(1, sizeof(router_t));
  if (!router)
    return NULL;

  for (int i = 0; i < HTTP_METHOD_COUNT; i++) {
    router->roots[i] = NULL;
  }

  return router;
}

void router_destroy(router_t *router) {
  if (!router)
    return;

  for (int i = 0; i < HTTP_METHOD_COUNT; i++) {
    if (router->roots[i]) {
      free_node(router->roots[i]);
    }
  }

  free(router);
}

int router_register(router_t *router, http_method_t method, const char *pattern,
                    route_callback_t callback) {
  if (!router || !pattern || !callback)
    return -1;
  if (method >= HTTP_METHOD_COUNT)
    return -1;

  printf("Registering route: %s\n", pattern);

  int segment_count = 0;
  char **segments = parse_pattern(pattern, &segment_count);
  if (!segments) {
    fprintf(stderr, "Failed to parse pattern: %s\n", pattern);
    return -1;
  }

  printf("  Segments (%d): ", segment_count);
  for (int i = 0; i < segment_count; i++) {
    printf("[%s] ", segments[i]);
  }
  printf("\n");

  int result =
      register_route(router, method, segments, segment_count, callback);

  // 清理临时segments（注意：segment的内存已经被复制到节点中或释放了）
  for (int i = 0; i < segment_count; i++) {
    free(segments[i]);
  }
  free(segments);

  return result;
}

route_callback_t router_match(router_t *router, http_method_t method,
                              const char *url, route_params_t *out_params) {
  if (!router || !url || !out_params)
    return NULL;
  if (method >= HTTP_METHOD_COUNT)
    return NULL;

  printf("\nMatching URL: %s\n", url);

  // 初始化输出参数
  out_params->params = NULL;
  out_params->param_count = 0;

  // 解析URL为段
  int segment_count = 0;
  char **segments = parse_pattern(url, &segment_count);
  if (!segments) {
    printf("  Failed to parse URL\n");
    return NULL;
  }

  printf("  URL segments (%d): ", segment_count);
  for (int i = 0; i < segment_count; i++) {
    printf("[%s] ", segments[i]);
  }
  printf("\n");

  // 获取根节点
  route_node_t *root = router->roots[method];
  if (!root) {
    printf("  No routes for this method\n");
    for (int i = 0; i < segment_count; i++)
      free(segments[i]);
    free(segments);
    return NULL;
  }

  // 临时存储参数（最多支持32个）
  char *params_tmp[32] = {NULL};
  int param_count = 0;

  route_node_t *current = root;
  int matched = 1;

  // 遍历匹配
  for (int i = 0; i < segment_count; i++) {
    route_node_t *next = find_child(current, segments[i], 1);
    if (!next) {
      printf("  No match at segment %d: %s\n", i, segments[i]);
      matched = 0;
      break;
    }

    printf("  Matched segment %d: %s -> ", i, segments[i]);
    if (next->is_wildcard) {
      printf("wildcard (param_index=%d)\n", next->param_index);
      // 如果是通配符节点，捕获参数
      if (next->param_index >= 0 && next->param_index < 32) {
        params_tmp[next->param_index] = strdup(segments[i]);
        if (next->param_index + 1 > param_count) {
          param_count = next->param_index + 1;
        }
      }
    } else {
      printf("keyword '%s'\n", next->segment);
    }

    current = next;
  }

  // 清理segments
  for (int i = 0; i < segment_count; i++) {
    free(segments[i]);
  }
  free(segments);

  // 检查是否完全匹配（到达终点且有回调）
  if (!matched || !current->callback) {
    printf("  No callback at endpoint\n");
    // 清理临时参数
    for (int i = 0; i < param_count; i++) {
      if (params_tmp[i]) {
        free(params_tmp[i]);
        params_tmp[i] = NULL;
      }
    }
    return NULL;
  }

  printf("  Match successful! Callback found\n");

  // 构建输出参数
  if (param_count > 0) {
    out_params->params = (char **)calloc(param_count, sizeof(char *));
    if (out_params->params) {
      for (int i = 0; i < param_count; i++) {
        out_params->params[i] = params_tmp[i];
      }
      out_params->param_count = param_count;
    } else {
      // 分配失败，清理
      for (int i = 0; i < param_count; i++) {
        if (params_tmp[i])
          free(params_tmp[i]);
      }
      return NULL;
    }
  }

  return current->callback;
}

void route_params_free(route_params_t *params) {
  if (!params)
    return;

  if (params->params) {
    for (int i = 0; i < params->param_count; i++) {
      if (params->params[i]) {
        free(params->params[i]);
      }
    }
    free(params->params);
    params->params = NULL;
  }
  params->param_count = 0;
}