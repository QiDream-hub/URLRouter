#include <stdio.h>
#include "router.h"

// 回调函数示例
void user_profile_handler(route_params_t *params, void *cbdata) {
    printf("\n>>> CALLBACK: User Profile Handler <<<\n");
    if (cbdata) {
        printf("Callback data: %s\n", (char*)cbdata);
    }
    
    if (params && params->param_count > 0) {
        printf("Captured parameters:\n");
        for (int i = 0; i < params->param_count; i++) {
            printf("  param[%d] = %s\n", i, params->params[i] ? params->params[i] : "(null)");
        }
    } else {
        printf("No parameters captured\n");
    }
    printf("---\n\n");
}

void user_posts_handler(route_params_t *params, void *cbdata) {
    printf("\n>>> CALLBACK: User Posts Handler <<<\n");
    if (params && params->param_count >= 2) {
        printf("User ID: %s\n", params->params[0]);
        printf("Post ID: %s\n", params->params[1]);
    }
    printf("---\n\n");
}

void settings_handler(route_params_t *params, void *cbdata) {
    printf("\n>>> CALLBACK: Settings Handler <<<\n");
    printf("Settings page accessed\n");
    printf("---\n\n");
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
    router_register(router, HTTP_GET, "/$'user'/${}/$'profile'", 
                    user_profile_handler, (void*)"profile data");
    
    router_register(router, HTTP_GET, "/$'user'/${}/$'posts'/${}", 
                    user_posts_handler, NULL);
    
    router_register(router, HTTP_GET, "/$'settings'", 
                    settings_handler, NULL);
    
    printf("\n=== Testing Route Matching ===\n");
    
    // 测试1: 匹配用户profile
    route_params_t params;
    route_callback_t cb;
    
    cb = router_match(router, HTTP_GET, "/user/alice/profile", &params);
    if (cb) {
        cb(&params, NULL);
        route_params_free(&params);
    } else {
        printf("No match for /user/alice/profile\n");
    }
    
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
        printf("No match for POST /user/alice/profile (method mismatch, expected)\n");
    }
    
    // 销毁路由器
    router_destroy(router);
    
    return 0;
}