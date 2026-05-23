#include <stdio.h>
#include <string.h>
#include "router.h"
#include "extractor.h"

static int test_callback(void *request, void *response) {
    (void)request;
    (void)response;
    return 0;
}

int main(void) {
    printf("=== 段数匹配测试 ===\n\n");

    router_t *r = router_create();

    // 注册两段模式：/$'a'/${}
    printf("注册路由：/$'a'/${} (2 段)\n");
    if (router_register(r, HTTP_GET, "/$'a'/${}", test_callback, NULL) != 0) {
        printf("  注册失败\n");
        return 1;
    }

    // 注册三段模式：/$'a'/${}/${}
    printf("注册路由：/$'a'/${}/${} (3 段)\n");
    if (router_register(r, HTTP_GET, "/$'a'/${}/${}", test_callback, NULL) != 0) {
        printf("  注册失败\n");
        return 1;
    }

    printf("\n--- 测试匹配 ---\n");

    // 测试 /a/b/c (3 段)
    printf("\n测试 URL: /a/b/c (3 段)\n");
    route_node_t *node = router_match(r, HTTP_GET, "/a/b/c");
    if (node == NULL) {
        printf("  匹配结果：失败\n");
    } else {
        printf("  匹配结果：成功\n");
        route_param_t params[8];
        size_t count = 0;
        if (router_extract(node, "/a/b/c", params, 8, &count) == 0) {
            printf("  参数数量：%zu\n", count);
            for (size_t i = 0; i < count; i++) {
                char buf[64];
                router_param_to_string(params[i], buf, sizeof(buf));
                printf("    [%zu] = \"%s\"\n", i, buf);
            }
        }
    }

    // 测试 /a/b (2 段)
    printf("\n测试 URL: /a/b (2 段)\n");
    node = router_match(r, HTTP_GET, "/a/b");
    if (node == NULL) {
        printf("  匹配结果：失败\n");
    } else {
        printf("  匹配结果：成功\n");
        route_param_t params[8];
        size_t count = 0;
        if (router_extract(node, "/a/b", params, 8, &count) == 0) {
            printf("  参数数量：%zu\n", count);
            for (size_t i = 0; i < count; i++) {
                char buf[64];
                router_param_to_string(params[i], buf, sizeof(buf));
                printf("    [%zu] = \"%s\"\n", i, buf);
            }
        }
    }

    // 测试 /a/b/c/d (4 段)
    printf("\n测试 URL: /a/b/c/d (4 段)\n");
    node = router_match(r, HTTP_GET, "/a/b/c/d");
    if (node == NULL) {
        printf("  匹配结果：失败（预期，因为没有 4 段模式）\n");
    } else {
        printf("  匹配结果：成功\n");
    }

    router_destroy(r);

    printf("\n=== 测试完成 ===\n");
    return 0;
}
