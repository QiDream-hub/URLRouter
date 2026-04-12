#include "router.h"
#include <stdio.h>
#include <string.h>

/* ==================== 测试处理函数 ==================== */

static int user_handler(const route_params_t *params, void *userdata) {
    (void)userdata;
    printf(">>> User Handler <<<\n");
    
    if (params && params->count > 0) {
        char buf[256];
        router_param_to_string(params->params[0], buf, sizeof(buf));
        printf("  User ID: %s\n", buf);
    }
    return 0;
}

static int user_posts_handler(const route_params_t *params, void *userdata) {
    (void)userdata;
    printf(">>> User Posts Handler <<<\n");
    
    if (params && params->count >= 2) {
        char user_id[64], post_id[64];
        router_param_to_string(params->params[0], user_id, sizeof(user_id));
        router_param_to_string(params->params[1], post_id, sizeof(post_id));
        printf("  User ID: %s\n", user_id);
        printf("  Post ID: %s\n", post_id);
    }
    return 0;
}

static int settings_handler(const route_params_t *params, void *userdata) {
    (void)userdata;
    (void)params;
    printf(">>> Settings Handler <<<\n");
    printf("  Settings page accessed\n");
    return 0;
}

static int search_handler(const route_params_t *params, void *userdata) {
    (void)userdata;
    printf(">>> Search Handler <<<\n");
    
    if (params && params->count >= 2) {
        char key[64], value[64];
        router_param_to_string(params->params[0], key, sizeof(key));
        router_param_to_string(params->params[1], value, sizeof(value));
        printf("  Query: %s = %s\n", key, value);
    }
    return 0;
}

static int api_version_handler(const route_params_t *params, void *userdata) {
    (void)userdata;
    printf(">>> API Version Handler <<<\n");
    
    if (params && params->count >= 2) {
        char major[16], minor[16];
        router_param_to_string(params->params[0], major, sizeof(major));
        router_param_to_string(params->params[1], minor, sizeof(minor));
        printf("  Version: %s.%s\n", major, minor);
    }
    return 0;
}

static int file_handler(const route_params_t *params, void *userdata) {
    (void)userdata;
    printf(">>> File Handler <<<\n");
    
    if (params && params->count >= 2) {
        char name[128], ext[16];
        router_param_to_string(params->params[0], name, sizeof(name));
        router_param_to_string(params->params[1], ext, sizeof(ext));
        printf("  Filename: %s\n", name);
        printf("  Extension: %s\n", ext);
    }
    return 0;
}

static int date_log_handler(const route_params_t *params, void *userdata) {
    (void)userdata;
    printf(">>> Date Log Handler <<<\n");
    
    if (params && params->count >= 3) {
        char year[8], month[8], day[8];
        router_param_to_string(params->params[0], year, sizeof(year));
        router_param_to_string(params->params[1], month, sizeof(month));
        router_param_to_string(params->params[2], day, sizeof(day));
        printf("  Date: %s-%s-%s\n", year, month, day);
    }
    return 0;
}

/* ==================== 测试辅助函数 ==================== */

static void test_route(router_t *router, http_method_t method,
                       const char *url, const char *description) {
    printf("\n[TEST] %s\n", description);
    printf("  URL: %s (method=%d)\n", url, method);

    route_node_t *node = router_match(router, method, url);

    if (node) {
        printf("  Match: SUCCESS\n");

        /* 提取参数 */
        route_param_t params[16];
        size_t param_count = 0;

        if (router_extract(node, url, params, 16, &param_count) == 0) {
            printf("  Parameters: %zu\n", param_count);
            for (size_t i = 0; i < param_count; i++) {
                char buf[128];
                router_param_to_string(params[i], buf, sizeof(buf));
                printf("    [%zu] = \"%s\"\n", i, buf);
            }
        }

        /* 调用处理函数 */
        route_callback_t callback = router_get_callback(node);
        void *userdata = router_get_userdata(node);

        if (callback) {
            route_params_t rp = {params, param_count};
            callback(&rp, userdata);
        }
    } else {
        printf("  Match: FAILED\n");
    }
}

/* ==================== 主测试函数 ==================== */

int main(void) {
    printf("=== URLRouter 新版本测试 ===\n\n");
    
    /* 创建路由器 */
    router_t *router = router_create();
    if (!router) {
        fprintf(stderr, "Failed to create router\n");
        return 1;
    }
    
    printf("=== 注册路由 ===\n");
    
    /* 注册基础路由 */
    router_register(router, HTTP_GET, "/$'user'/${}",
                    user_handler, NULL);

    router_register(router, HTTP_GET, "/$'user'/${}/$'posts'/${}",
                    user_posts_handler, NULL);

    router_register(router, HTTP_GET, "/$'settings'",
                    settings_handler, NULL);
    
    /* 注册搜索路由（查询参数）*/
    router_register(router, HTTP_GET, "/$'search'/$'?'${'='}$'='${}",
                    search_handler, NULL);

    /* 注册版本化 API */
    router_register(router, HTTP_GET, "/$'api'/$'v'${'.'}$'.'${}/$'users'",
                    api_version_handler, NULL);

    /* 注册文件路由 */
    router_register(router, HTTP_GET, "/$'files'/${'.'}$'.'${}",
                    file_handler, NULL);
    
    /* 注册日期日志路由 */
    router_register(router, HTTP_GET, "/$'logs'/${4}$'-'${2}$'-'${2}",
                    date_log_handler, NULL);
    
    printf("\n=== 测试路由匹配 ===\n");
    
    /* 测试 1: 基础用户路由 */
    test_route(router, HTTP_GET, "/user/alice", "基础用户路由");
    
    /* 测试 2: 用户帖子路由 */
    test_route(router, HTTP_GET, "/user/bob/posts/42", "用户帖子路由");
    
    /* 测试 3: 设置页面 */
    test_route(router, HTTP_GET, "/settings", "设置页面");
    
    /* 测试 4: 搜索查询 */
    test_route(router, HTTP_GET, "/search?q=router", "搜索查询");
    
    /* 测试 5: 版本化 API */
    test_route(router, HTTP_GET, "/api/v2.0/users", "版本化 API");
    
    /* 测试 6: 文件路由 */
    test_route(router, HTTP_GET, "/files/document.pdf", "文件路由");
    
    /* 测试 7: 日期日志 */
    test_route(router, HTTP_GET, "/logs/2024-03-15", "日期日志");
    
    /* 测试 8: 不匹配的路由 */
    test_route(router, HTTP_GET, "/nonexistent", "不存在的路由");
    
    /* 测试 9: HTTP 方法不匹配 */
    test_route(router, HTTP_POST, "/user/alice", "HTTP 方法不匹配");
    
    /* 测试 10: 定长捕获 - 长度不匹配 */
    test_route(router, HTTP_GET, "/user/toolong", "定长捕获测试 (应失败)");
    
    printf("\n=== 清理 ===\n");
    router_destroy(router);
    printf("路由器已销毁\n");
    
    return 0;
}
