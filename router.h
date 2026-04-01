#ifndef ROUTER_H
#define ROUTER_H

#include <stddef.h>

// HTTP 方法枚举
typedef enum {
    HTTP_GET,
    HTTP_POST,
    HTTP_PUT,
    HTTP_DELETE,
    HTTP_PATCH,
    HTTP_HEAD,
    HTTP_OPTIONS,
    HTTP_METHOD_COUNT
} http_method_t;

// 参数结构体
typedef struct {
    char **params;      // 捕获的参数值数组
    int param_count;    // 参数数量
} route_params_t;

// 回调函数类型
typedef void (*route_callback_t)(route_params_t *params, void *cbdata);

// 路由器结构体（不透明指针）
typedef struct router router_t;

// 创建路由器
router_t* router_create(void);

// 销毁路由器
void router_destroy(router_t *router);

// 注册路由
// pattern格式: /$'keyword'/${}/$'profile' 等
// 返回值: 0成功, -1失败
int router_register(router_t *router, 
                    http_method_t method,
                    const char *pattern,
                    route_callback_t callback,
                    void *cbdata);

// 匹配路由
// url: 请求的URL路径
// out_params: 输出参数，使用后需要调用 route_params_free
// 返回值: 匹配到的回调函数，如果未匹配返回NULL
route_callback_t router_match(router_t *router,
                               http_method_t method,
                               const char *url,
                               route_params_t *out_params);

// 释放参数结构体
void route_params_free(route_params_t *params);

#endif // ROUTER_H