#include "router.h"
#include <stdio.h>

// 回调函数示例

int user_posts_handler(void *params, void *cbdata) {
  printf("\n>>> CALLBACK: User Posts Handler <<<\n");
  route_params_t *tmp = (route_params_t *)params;
  if (tmp && tmp->param_count >= 2) {
    printf("User ID: %s\n", tmp->params[0]);
    printf("Post ID: %s\n", tmp->params[1]);
  }
  printf("---\n\n");
  return 0;
}

int settings_handler(void *params, void *cbdata) {
  printf("\n>>> CALLBACK: Settings Handler <<<\n");
  printf("Settings page accessed\n");
  printf("---\n\n");
  return 0;
}

int main() {
  // 创建路由器
  router_t *router = router_create();
  if (!router) {
    fprintf(stderr, "Failed to create router\n");
    return 1;
  }

  printf("=== Registering Routes ===\n");

  // 注册路由
  router_register(router, HTTP_GET, "/$'user'/${}/$'posts'/${}",
                  user_posts_handler);

  router_register(router, HTTP_GET, "/$'settings'", settings_handler);

  printf("\n=== Testing Route Matching ===\n");

  // 测试1: 匹配用户profile
  route_params_t params;
  route_callback_t cb;

  // 测试2: 匹配用户posts
  cb = router_match(router, HTTP_GET, "/user/bob/posts/123", &params);
  if (cb) {
    cb(&params, NULL);
    route_params_free(&params);
  } else {
    printf("No match for /user/bob/posts/123\n");
  }

  // 测试3: 匹配settings
  cb = router_match(router, HTTP_GET, "/settings", &params);
  if (cb) {
    cb(&params, NULL);
    route_params_free(&params);
  } else {
    printf("No match for /settings\n");
  }

  // 测试4: 不存在的路由
  cb = router_match(router, HTTP_GET, "/nonexistent/path", &params);
  if (cb) {
    cb(&params, NULL);
    route_params_free(&params);
  } else {
    printf("No match for /nonexistent/path (expected)\n");
  }

  // 测试5: 不同的HTTP方法
  cb = router_match(router, HTTP_POST, "/user/alice/profile", &params);
  if (cb) {
    cb(&params, NULL);
    route_params_free(&params);
  } else {
    printf(
        "No match for POST /user/alice/profile (method mismatch, expected)\n");
  }

  // 销毁路由器
  router_destroy(router);

  return 0;
}