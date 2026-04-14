#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "router.h"

/* ==================== 测试辅助宏 ==================== */
#define TEST_PASS() printf("  [PASS] %s\n", __func__)
#define TEST_FAIL(msg) do { printf("  [FAIL] %s: %s\n", __func__, msg); return 1; } while(0)
#define ASSERT_EQ(a, b) if ((a) != (b)) TEST_FAIL("assertion failed")
#define ASSERT_STREQ(a, b, n) if (strncmp((a), (b), (n)) != 0) TEST_FAIL("string mismatch")
#define ASSERT_TRUE(cond) if (!(cond)) TEST_FAIL("assertion failed")
#define ASSERT_FALSE(cond) if (cond) TEST_FAIL("assertion failed")

/* ==================== 测试回调 ==================== */
static int test_callback(void *request, void *response) {
    (void)request;
    (void)response;
    return 0;
}

/* ==================== 测试函数 ==================== */

/* 测试 1: 精确匹配 - 基础 */
static int test_exact_match_basic(void) {
    router_t *r = router_create();
    ASSERT_TRUE(r != NULL);
    
    // 注册简单精确匹配路由
    ASSERT_EQ(router_register(r, HTTP_GET, "/$'user'/$'profile'", test_callback, NULL), 0);
    
    // 匹配成功
    route_node_t *node = router_match(r, HTTP_GET, "/user/profile");
    ASSERT_TRUE(node != NULL);
    ASSERT_EQ(router_get_callback(node), test_callback);
    
    // 匹配失败 - 路径不匹配
    node = router_match(r, HTTP_GET, "/user/settings");
    ASSERT_TRUE(node == NULL);
    
    // 匹配失败 - 段数不匹配
    node = router_match(r, HTTP_GET, "/user/profile/extra");
    ASSERT_TRUE(node == NULL);
    
    router_destroy(r);
    TEST_PASS();
    return 0;
}

/* 测试 2: 精确匹配 - 特殊字符 */
static int test_exact_match_special_chars(void) {
    router_t *r = router_create();
    ASSERT_TRUE(r != NULL);
    
    // 包含 ? 和 & 的匹配
    ASSERT_EQ(router_register(r, HTTP_GET, "/$'search'$'?'", test_callback, NULL), 0);
    ASSERT_EQ(router_register(r, HTTP_GET, "/$'api'/$'v2.0'", test_callback, NULL), 0);
    
    route_node_t *node = router_match(r, HTTP_GET, "/search?");
    ASSERT_TRUE(node != NULL);
    
    node = router_match(r, HTTP_GET, "/api/v2.0");
    ASSERT_TRUE(node != NULL);
    
    router_destroy(r);
    TEST_PASS();
    return 0;
}

/* 测试 3: 定长捕获 */
static int test_fixed_length_capture(void) {
    router_t *r = router_create();
    ASSERT_TRUE(r != NULL);
    
    // 捕获 4 个字符
    ASSERT_EQ(router_register(r, HTTP_GET, "/$'user'/${4}", test_callback, NULL), 0);
    
    route_node_t *node = router_match(r, HTTP_GET, "/user/abcd");
    ASSERT_TRUE(node != NULL);
    
    route_param_t params[4];
    size_t count = 0;
    ASSERT_EQ(router_extract(node, "/user/abcd", params, 4, &count), 0);
    ASSERT_EQ(count, 1);
    ASSERT_EQ(params[0].len, 4);
    ASSERT_STREQ(params[0].ptr, "abcd", 4);
    
    // 长度不足应失败
    node = router_match(r, HTTP_GET, "/user/abc");
    ASSERT_TRUE(node == NULL);
    
    router_destroy(r);
    TEST_PASS();
    return 0;
}

/* 测试 4: 捕获到字符 */
static int test_capture_until_char(void) {
    router_t *r = router_create();
    ASSERT_TRUE(r != NULL);

    // 捕获到 '=' 之前
    ASSERT_EQ(router_register(r, HTTP_GET, "/$'api'/${'='}$'='${}", test_callback, NULL), 0);

    route_node_t *node = router_match(r, HTTP_GET, "/api/name=alice");
    ASSERT_TRUE(node != NULL);

    route_param_t params[4];
    size_t count = 0;
    ASSERT_EQ(router_extract(node, "/api/name=alice", params, 4, &count), 0);
    ASSERT_EQ(count, 2);
    ASSERT_EQ(params[0].len, 4);
    ASSERT_STREQ(params[0].ptr, "name", 4);
    ASSERT_EQ(params[1].len, 5);
    ASSERT_STREQ(params[1].ptr, "alice", 5);

    // 测试捕获到 '-'，使用两段模式
    ASSERT_EQ(router_register(r, HTTP_GET, "/$'search'/$'-'${}", test_callback, NULL), 0);
    node = router_match(r, HTTP_GET, "/search/-result");
    ASSERT_TRUE(node != NULL);
    count = 0;
    ASSERT_EQ(router_extract(node, "/search/-result", params, 4, &count), 0);
    ASSERT_EQ(count, 1);
    ASSERT_EQ(params[0].len, 6);
    ASSERT_STREQ(params[0].ptr, "result", 6);

    router_destroy(r);
    TEST_PASS();
    return 0;
}

/* 测试 5: 捕获到结尾 */
static int test_capture_to_end(void) {
    router_t *r = router_create();
    ASSERT_TRUE(r != NULL);

    // 捕获整个段
    ASSERT_EQ(router_register(r, HTTP_GET, "/$'files'/${}", test_callback, NULL), 0);

    route_node_t *node = router_match(r, HTTP_GET, "/files/document.pdf");
    ASSERT_TRUE(node != NULL);

    route_param_t params[4];
    size_t count = 0;
    ASSERT_EQ(router_extract(node, "/files/document.pdf", params, 4, &count), 0);
    ASSERT_EQ(count, 1);
    ASSERT_EQ(params[0].len, 12);
    ASSERT_STREQ(params[0].ptr, "document.pdf", 12);

    // 空段捕获
    node = router_match(r, HTTP_GET, "/files/empty");
    ASSERT_TRUE(node != NULL);
    count = 0;
    router_extract(node, "/files/empty", params, 4, &count);
    ASSERT_EQ(count, 1);
    ASSERT_EQ(params[0].len, 5);
    ASSERT_STREQ(params[0].ptr, "empty", 5);

    router_destroy(r);
    TEST_PASS();
    return 0;
}

/* 测试 6: 绝对跳转 */
static int test_absolute_jump(void) {
    router_t *r = router_create();
    ASSERT_TRUE(r != NULL);
    
    // 跳转到位置 0 然后捕获
    ASSERT_EQ(router_register(r, HTTP_GET, "/$'file'/${}$[0]${'.'}$'.'${}", test_callback, NULL), 0);
    
    route_node_t *node = router_match(r, HTTP_GET, "/file/document.pdf");
    ASSERT_TRUE(node != NULL);
    
    route_param_t params[8];
    size_t count = 0;
    ASSERT_EQ(router_extract(node, "/file/document.pdf", params, 8, &count), 0);
    ASSERT_EQ(count, 3);
    // 第一次捕获: 整个文件名
    ASSERT_EQ(params[0].len, 12);
    ASSERT_STREQ(params[0].ptr, "document.pdf", 12);
    // 第二次捕获: 文件名（不含扩展名）
    ASSERT_EQ(params[1].len, 8);
    ASSERT_STREQ(params[1].ptr, "document", 8);
    // 第三次捕获: 扩展名
    ASSERT_EQ(params[2].len, 3);
    ASSERT_STREQ(params[2].ptr, "pdf", 3);
    
    // 测试 END 语法
    router_register(r, HTTP_GET, "/$'api'/$[END-4]${4}", test_callback, NULL);
    node = router_match(r, HTTP_GET, "/api/abcdefgh");
    ASSERT_TRUE(node != NULL);
    router_extract(node, "/api/abcdefgh", params, 8, &count);
    ASSERT_EQ(count, 1);
    ASSERT_EQ(params[0].len, 4);
    ASSERT_STREQ(params[0].ptr, "efgh", 4);
    
    router_destroy(r);
    TEST_PASS();
    return 0;
}

/* 测试 7: 相对移动 - 向结尾 */
static int test_move_forward(void) {
    router_t *r = router_create();
    ASSERT_TRUE(r != NULL);

    // 跳过前缀
    ASSERT_EQ(router_register(r, HTTP_GET, "/$'log'/$[>11]${}", test_callback, NULL), 0);

    route_node_t *node = router_match(r, HTTP_GET, "/log/2024-03-15_message");
    ASSERT_TRUE(node != NULL);

    route_param_t params[4];
    size_t count = 0;
    ASSERT_EQ(router_extract(node, "/log/2024-03-15_message", params, 4, &count), 0);
    ASSERT_EQ(count, 1);
    ASSERT_EQ(params[0].len, 7);
    ASSERT_STREQ(params[0].ptr, "message", 7);

    router_destroy(r);
    TEST_PASS();
    return 0;
}

/* 测试 8: 相对移动 - 向开头 */
static int test_move_backward(void) {
    router_t *r = router_create();
    ASSERT_TRUE(r != NULL);
    
    // 从段尾回退
    ASSERT_EQ(router_register(r, HTTP_GET, "/$'file'/$[<4]${4}", test_callback, NULL), 0);
    
    route_node_t *node = router_match(r, HTTP_GET, "/file/document.pdf");
    ASSERT_TRUE(node != NULL);
    
    route_param_t params[4];
    size_t count = 0;
    ASSERT_EQ(router_extract(node, "/file/document.pdf", params, 4, &count), 0);
    ASSERT_EQ(count, 1);
    ASSERT_EQ(params[0].len, 4);
    ASSERT_STREQ(params[0].ptr, ".pdf", 4);
    
    router_destroy(r);
    TEST_PASS();
    return 0;
}

/* 测试 9: 向结尾查找字符 */
static int test_find_forward(void) {
    router_t *r = router_create();
    ASSERT_TRUE(r != NULL);
    
    // 查找 '=' 然后跳过并捕获
    ASSERT_EQ(router_register(r, HTTP_GET, "/$'search'$'?'$[>'=']$[>1]${}", test_callback, NULL), 0);
    
    route_node_t *node = router_match(r, HTTP_GET, "/search?q=router");
    ASSERT_TRUE(node != NULL);
    
    route_param_t params[4];
    size_t count = 0;
    ASSERT_EQ(router_extract(node, "/search?q=router", params, 4, &count), 0);
    ASSERT_EQ(count, 1);
    ASSERT_EQ(params[0].len, 6);
    ASSERT_STREQ(params[0].ptr, "router", 6);
    
    // 查找不存在的字符应失败
    node = router_match(r, HTTP_GET, "/search?qrouter");
    ASSERT_TRUE(node == NULL);
    
    router_destroy(r);
    TEST_PASS();
    return 0;
}

/* 测试 10: 向开头查找字符 */
static int test_find_backward(void) {
    router_t *r = router_create();
    ASSERT_TRUE(r != NULL);
    
    // 从段尾向前查找 '.'
    ASSERT_EQ(router_register(r, HTTP_GET, "/$'file'/$[<'.']${}", test_callback, NULL), 0);
    
    route_node_t *node = router_match(r, HTTP_GET, "/file/document.pdf");
    ASSERT_TRUE(node != NULL);
    
    route_param_t params[4];
    size_t count = 0;
    ASSERT_EQ(router_extract(node, "/file/document.pdf", params, 4, &count), 0);
    ASSERT_EQ(count, 1);
    ASSERT_EQ(params[0].len, 4);
    ASSERT_STREQ(params[0].ptr, ".pdf", 4);
    
    router_destroy(r);
    TEST_PASS();
    return 0;
}

/* 测试 11: 多参数查询 */
static int test_multi_param_query(void) {
    router_t *r = router_create();
    ASSERT_TRUE(r != NULL);
    
    // 解析 ?q=router&page=2
    ASSERT_EQ(router_register(r, HTTP_GET, 
        "/$'search'$'?'${'='}$'='${'&'}$[>1]${'='}$'='${}", 
        test_callback, NULL), 0);
    
    route_node_t *node = router_match(r, HTTP_GET, "/search?q=router&page=2");
    ASSERT_TRUE(node != NULL);
    
    route_param_t params[8];
    size_t count = 0;
    ASSERT_EQ(router_extract(node, "/search?q=router&page=2", params, 8, &count), 0);
    ASSERT_EQ(count, 4);
    
    // q
    ASSERT_EQ(params[0].len, 1);
    ASSERT_STREQ(params[0].ptr, "q", 1);
    // router
    ASSERT_EQ(params[1].len, 6);
    ASSERT_STREQ(params[1].ptr, "router", 6);
    // page
    ASSERT_EQ(params[2].len, 4);
    ASSERT_STREQ(params[2].ptr, "page", 4);
    // 2
    ASSERT_EQ(params[3].len, 1);
    ASSERT_STREQ(params[3].ptr, "2", 1);
    
    router_destroy(r);
    TEST_PASS();
    return 0;
}

/* 测试 12: 版本号解析 */
static int test_version_parsing(void) {
    router_t *r = router_create();
    ASSERT_TRUE(r != NULL);
    
    // 解析 v2.0.1
    ASSERT_EQ(router_register(r, HTTP_GET, 
        "/$'api'/$'v'${'.'}$'.'${'.'}$'.'${}", 
        test_callback, NULL), 0);
    
    route_node_t *node = router_match(r, HTTP_GET, "/api/v2.0.1");
    ASSERT_TRUE(node != NULL);
    
    route_param_t params[8];
    size_t count = 0;
    ASSERT_EQ(router_extract(node, "/api/v2.0.1", params, 8, &count), 0);
    ASSERT_EQ(count, 3);
    
    ASSERT_EQ(params[0].len, 1);
    ASSERT_STREQ(params[0].ptr, "2", 1);
    ASSERT_EQ(params[1].len, 1);
    ASSERT_STREQ(params[1].ptr, "0", 1);
    ASSERT_EQ(params[2].len, 1);
    ASSERT_STREQ(params[2].ptr, "1", 1);
    
    router_destroy(r);
    TEST_PASS();
    return 0;
}

/* 测试 13: 日期解析 */
static int test_date_parsing(void) {
    router_t *r = router_create();
    ASSERT_TRUE(r != NULL);
    
    // 解析 YYYY-MM-DD
    ASSERT_EQ(router_register(r, HTTP_GET, 
        "/$'logs'/${4}$'-'${2}$'-'${2}", 
        test_callback, NULL), 0);
    
    route_node_t *node = router_match(r, HTTP_GET, "/logs/2024-03-15");
    ASSERT_TRUE(node != NULL);
    
    route_param_t params[8];
    size_t count = 0;
    ASSERT_EQ(router_extract(node, "/logs/2024-03-15", params, 8, &count), 0);
    ASSERT_EQ(count, 3);
    
    ASSERT_EQ(params[0].len, 4);
    ASSERT_STREQ(params[0].ptr, "2024", 4);
    ASSERT_EQ(params[1].len, 2);
    ASSERT_STREQ(params[1].ptr, "03", 2);
    ASSERT_EQ(params[2].len, 2);
    ASSERT_STREQ(params[2].ptr, "15", 2);
    
    // 长度不足应失败
    node = router_match(r, HTTP_GET, "/logs/2024-3-5");
    ASSERT_TRUE(node == NULL);
    
    router_destroy(r);
    TEST_PASS();
    return 0;
}

/* 测试 14: 复杂日志格式 */
static int test_complex_log_format(void) {
    router_t *r = router_create();
    ASSERT_TRUE(r != NULL);
    
    // 解析 2024-03-15_ERROR_message
    ASSERT_EQ(router_register(r, HTTP_GET, 
        "/$'log'/$[>'_']$[>1]${8}$[<'_']${}", 
        test_callback, NULL), 0);
    
    route_node_t *node = router_match(r, HTTP_GET, "/log/2024-03-15_ERROR_message");
    ASSERT_TRUE(node != NULL);
    
    route_param_t params[8];
    size_t count = 0;
    ASSERT_EQ(router_extract(node, "/log/2024-03-15_ERROR_message", params, 8, &count), 0);
    ASSERT_EQ(count, 2);
    
    // 第一个捕获: ERROR_me (8 个字符)
    ASSERT_EQ(params[0].len, 8);
    ASSERT_STREQ(params[0].ptr, "ERROR_me", 8);
    // 第二个捕获: message
    ASSERT_EQ(params[1].len, 8);
    ASSERT_STREQ(params[1].ptr, "_message", 8);
    
    router_destroy(r);
    TEST_PASS();
    return 0;
}

/* 测试 15: 边界情况 - 段尾对齐 */
static int test_segment_end_alignment(void) {
    router_t *r = router_create();
    ASSERT_TRUE(r != NULL);
    
    // 模式消费整个段
    ASSERT_EQ(router_register(r, HTTP_GET, "/$'user'/${2}", test_callback, NULL), 0);
    
    // 精确匹配 - 段长度等于捕获长度
    route_node_t *node = router_match(r, HTTP_GET, "/user/ab");
    ASSERT_TRUE(node != NULL);
    
    // 段长度大于捕获长度 - 应失败
    node = router_match(r, HTTP_GET, "/user/abc");
    ASSERT_TRUE(node == NULL);
    
    // 添加尾随匹配
    router_register(r, HTTP_GET, "/$'user'/${2}$'c'", test_callback, NULL);
    node = router_match(r, HTTP_GET, "/user/abc");
    ASSERT_TRUE(node != NULL);
    
    router_destroy(r);
    TEST_PASS();
    return 0;
}

/* 测试 16: HTTP 方法隔离 */
static int test_http_method_isolation(void) {
    router_t *r = router_create();
    ASSERT_TRUE(r != NULL);
    
    // 为不同方法注册相同路径
    ASSERT_EQ(router_register(r, HTTP_GET, "/$'user'/${}", test_callback, (void*)1), 0);
    ASSERT_EQ(router_register(r, HTTP_POST, "/$'user'/${}", test_callback, (void*)2), 0);
    ASSERT_EQ(router_register(r, HTTP_PUT, "/$'user'/${}", test_callback, (void*)3), 0);
    
    // GET 匹配
    route_node_t *node = router_match(r, HTTP_GET, "/user/alice");
    ASSERT_TRUE(node != NULL);
    ASSERT_EQ(router_get_userdata(node), (void*)1);
    
    // POST 匹配
    node = router_match(r, HTTP_POST, "/user/alice");
    ASSERT_TRUE(node != NULL);
    ASSERT_EQ(router_get_userdata(node), (void*)2);
    
    // PUT 匹配
    node = router_match(r, HTTP_PUT, "/user/alice");
    ASSERT_TRUE(node != NULL);
    ASSERT_EQ(router_get_userdata(node), (void*)3);
    
    // DELETE 未注册
    node = router_match(r, HTTP_DELETE, "/user/alice");
    ASSERT_TRUE(node == NULL);
    
    router_destroy(r);
    TEST_PASS();
    return 0;
}

/* 测试 17: 参数缓冲区容量检查 */
static int test_param_buffer_capacity(void) {
    router_t *r = router_create();
    ASSERT_TRUE(r != NULL);
    
    // 注册产生 3 个参数的路由
    ASSERT_EQ(router_register(r, HTTP_GET, 
        "/$'api'/${4}/${2}/${2}", 
        test_callback, NULL), 0);
    
    route_node_t *node = router_match(r, HTTP_GET, "/api/2024/03/15");
    ASSERT_TRUE(node != NULL);
    
    route_param_t params[2];
    size_t count = 0;
    
    // 缓冲区容量不足
    ASSERT_EQ(router_extract(node, "/api/2024/03/15", params, 2, &count), -1);
    
    router_destroy(r);
    TEST_PASS();
    return 0;
}

/* 测试 18: 空参数处理 */
static int test_empty_param_handling(void) {
    router_t *r = router_create();
    ASSERT_TRUE(r != NULL);

    // 捕获到段尾（空段）
    ASSERT_EQ(router_register(r, HTTP_GET, "/$'api'/${}", test_callback, NULL), 0);

    route_node_t *node = router_match(r, HTTP_GET, "/api/empty");
    ASSERT_TRUE(node != NULL);

    route_param_t params[4];
    size_t count = 0;
    ASSERT_EQ(router_extract(node, "/api/empty", params, 4, &count), 0);
    ASSERT_EQ(count, 1);
    ASSERT_EQ(params[0].len, 5);
    ASSERT_STREQ(params[0].ptr, "empty", 5);

    router_destroy(r);
    TEST_PASS();
    return 0;
}

/* 测试 19: 数字作为捕获字符 */
static int test_digit_as_capture_char(void) {
    router_t *r = router_create();
    ASSERT_TRUE(r != NULL);

    // 捕获到数字 '4' 之前，然后捕获剩余部分
    ASSERT_EQ(router_register(r, HTTP_GET, "/$'code'/${'4'}${}", test_callback, NULL), 0);

    route_node_t *node = router_match(r, HTTP_GET, "/code/abc4def");
    ASSERT_TRUE(node != NULL);

    route_param_t params[4];
    size_t count = 0;
    ASSERT_EQ(router_extract(node, "/code/abc4def", params, 4, &count), 0);
    ASSERT_EQ(count, 2);
    ASSERT_EQ(params[0].len, 3);
    ASSERT_STREQ(params[0].ptr, "abc", 3);
    ASSERT_EQ(params[1].len, 4);
    ASSERT_STREQ(params[1].ptr, "4def", 4);

    router_destroy(r);
    TEST_PASS();
    return 0;
}

/* 测试 20: 语法错误处理 */
static int test_syntax_error_handling(void) {
    router_t *r = router_create();
    ASSERT_TRUE(r != NULL);
    
    // 空模式
    ASSERT_EQ(router_register(r, HTTP_GET, "", test_callback, NULL), -1);
    
    // 不以 / 开头
    ASSERT_EQ(router_register(r, HTTP_GET, "user/profile", test_callback, NULL), -1);
    
    // 无效的捕获语法
    ASSERT_EQ(router_register(r, HTTP_GET, "/${abc}", test_callback, NULL), -1);
    
    // 缺少闭合引号
    ASSERT_EQ(router_register(r, HTTP_GET, "/${'abc}", test_callback, NULL), -1);
    
    router_destroy(r);
    TEST_PASS();
    return 0;
}

/* 测试 21: 路由冲突检测 */
static int test_route_conflict_detection(void) {
    router_t *r = router_create();
    ASSERT_TRUE(r != NULL);
    
    // 注册第一个路由
    ASSERT_EQ(router_register(r, HTTP_GET, "/$'user'/${}", test_callback, (void*)1), 0);
    
    // 尝试注册冲突路由（相同模式）
    ASSERT_EQ(router_register(r, HTTP_GET, "/$'user'/${}", test_callback, (void*)2), -1);
    
    // 不同方法可以注册相同模式
    ASSERT_EQ(router_register(r, HTTP_POST, "/$'user'/${}", test_callback, (void*)2), 0);
    
    router_destroy(r);
    TEST_PASS();
    return 0;
}

/* 测试 22: 参数转字符串 */
static int test_param_to_string(void) {
    router_t *r = router_create();
    ASSERT_TRUE(r != NULL);
    
    ASSERT_EQ(router_register(r, HTTP_GET, "/$'user'/${}", test_callback, NULL), 0);
    
    route_node_t *node = router_match(r, HTTP_GET, "/user/alice");
    ASSERT_TRUE(node != NULL);
    
    route_param_t params[4];
    size_t count = 0;
    ASSERT_EQ(router_extract(node, "/user/alice", params, 4, &count), 0);
    
    char buf[64];
    size_t len = router_param_to_string(params[0], buf, sizeof(buf));
    ASSERT_EQ(len, 5);
    ASSERT_STREQ(buf, "alice", 5);
    
    // 缓冲区不足
    char small_buf[4];
    len = router_param_to_string(params[0], small_buf, sizeof(small_buf));
    ASSERT_EQ(len, 5);
    ASSERT_STREQ(small_buf, "ali", 3);
    ASSERT_EQ(small_buf[3], '\0');
    
    router_destroy(r);
    TEST_PASS();
    return 0;
}

/* ==================== 主函数 ==================== */
int main(void) {
    printf("\n=== URLRouter 语法支持测试 ===\n\n");
    
    int failed = 0;
    
    printf("测试精确匹配...\n");
    failed |= test_exact_match_basic();
    failed |= test_exact_match_special_chars();
    
    printf("\n测试捕获操作...\n");
    failed |= test_fixed_length_capture();
    failed |= test_capture_until_char();
    failed |= test_capture_to_end();
    
    printf("\n测试移动操作...\n");
    failed |= test_absolute_jump();
    failed |= test_move_forward();
    failed |= test_move_backward();
    failed |= test_find_forward();
    failed |= test_find_backward();
    
    printf("\n测试复杂模式...\n");
    failed |= test_multi_param_query();
    failed |= test_version_parsing();
    failed |= test_date_parsing();
    failed |= test_complex_log_format();
    
    printf("\n测试边界情况...\n");
    failed |= test_segment_end_alignment();
    failed |= test_http_method_isolation();
    failed |= test_param_buffer_capacity();
    failed |= test_empty_param_handling();
    failed |= test_digit_as_capture_char();
    
    printf("\n测试错误处理...\n");
    failed |= test_syntax_error_handling();
    failed |= test_route_conflict_detection();
    
    printf("\n测试辅助函数...\n");
    failed |= test_param_to_string();
    
    printf("\n=== 测试结果 ===\n");
    if (failed) {
        printf("\n[FAIL] 部分测试失败\n");
        return 1;
    } else {
        printf("\n[PASS] 所有测试通过\n");
        return 0;
    }
}