#ifndef ROUTER_H
#define ROUTER_H

#include <stddef.h>

/* ============================================================
 * URLRouter - 轻量级 URL 路由库
 * 
 * 核心设计：
 * 1. 匹配与提取分离：匹配阶段快速定位，提取阶段按需执行
 * 2. 特征序列驱动：(移动操作，关键字) 元组实现高效匹配
 * 3. 零拷贝参数：参数直接指向 URL 原始数据
 * 4. HTTP 方法隔离：每个方法独立的路由树
 * 
 * 线程安全说明：
 * - 路由树在注册完成后进入只读状态，匹配操作无需加锁
 * - 注册操作由调用者自行保证线程安全（通常无多线程注册需求）
 * - 如需动态注册，请在外部使用同步机制保护 router_register 调用
 * ============================================================ */

/* ==================== HTTP 方法 ==================== */
typedef enum {
  HTTP_GET = 0,
  HTTP_POST = 1,
  HTTP_PUT = 2,
  HTTP_DELETE = 3,
  HTTP_PATCH = 4,
  HTTP_HEAD = 5,
  HTTP_OPTIONS = 6,
  HTTP_METHOD_COUNT
} http_method_t;

/* ==================== 参数结构 ==================== */
/**
 * 路由参数（零拷贝）
 * ptr: 指向原始 URL 字符串的指针
 * len: 参数长度
 */
typedef struct {
  const char *ptr;
  size_t len;
} route_param_t;

/**
 * 参数列表
 * params: 参数数组
 * count: 参数数量
 */
typedef struct {
  route_param_t *params;
  size_t count;
} route_params_t;

/* ==================== 回调函数 ==================== */
/**
 * 路由处理回调
 * @param params 参数列表
 * @param userdata 用户数据（注册时传入）
 * @return 0 表示成功，非 0 表示失败
 */
typedef int (*route_callback_t)(void *request, void *response);

/* ==================== 路由器结构 ==================== */
typedef struct router router_t;
typedef struct route_node route_node_t;

/* ==================== 核心 API ==================== */

/**
 * 创建路由器
 * @return 路由器指针，失败返回 NULL
 */
router_t *router_create(void);

/**
 * 销毁路由器
 * @param router 路由器指针
 */
void router_destroy(router_t *router);

/**
 * 注册路由
 * @param router 路由器
 * @param method HTTP 方法
 * @param pattern 路由模式字符串
 *              格式示例：/$'user'/${}/$'posts'/${}
 *              操作符：
 *                $'文本'     - 精确匹配
 *                ${长度}     - 定长捕获
 *                ${'字符'}   - 捕获到指定字符前
 *                ${}         - 捕获到段尾
 *                $[位置]     - 绝对跳转 (位置可为整数或 END 或 END-n)
 *                $[>偏移]    - 向结尾移动
 *                $[<偏移]    - 向开头移动
 *                $[>'字符']  - 向结尾查找字符
 *                $[<'字符']  - 向开头查找字符
 * @param callback 处理函数
 * @param userdata 用户数据，传递给回调
 * @return 0 成功，-1 失败（模式语法错误、冲突等）
 */
int router_register(router_t *router, http_method_t method, const char *pattern,
                    route_callback_t callback, void *userdata);

/**
 * 匹配路由
 * @param router 路由器
 * @param method HTTP 方法
 * @param url 请求 URL 路径
 * @return 匹配的节点指针，未匹配返回 NULL
 *
 * 注意：匹配成功后需调用 router_extract 提取参数
 */
route_node_t *router_match(router_t *router, http_method_t method,
                           const char *url);

/**
 * 从匹配的节点提取参数
 * @param node 匹配返回的节点
 * @param url 原始 URL 字符串
 * @param params 参数数组（由调用者提供缓冲区）
 * @param param_capacity 参数数组容量
 * @param out_count 输出实际参数数量
 * @return 0 成功，-1 失败
 *
 * 注意：返回的参数是指向 URL 的指针，URL 在使用期间必须保持有效
 */
int router_extract(route_node_t *node, const char *url, route_param_t *params,
                   size_t param_capacity, size_t *out_count);

/**
 * 获取节点的回调函数
 * @param node 路由节点
 * @return 回调函数指针
 */
route_callback_t router_get_callback(route_node_t *node);

/**
 * 获取节点的用户数据
 * @param node 路由节点
 * @return 用户数据指针
 */
void *router_get_userdata(route_node_t *node);

/* ==================== 辅助函数 ==================== */

/**
 * 将参数转为 null 结尾的字符串（需调用者分配缓冲区）
 * @param param 参数
 * @param buf 缓冲区
 * @param buf_size 缓冲区大小
 * @return 写入的字符串长度
 */
size_t router_param_to_string(route_param_t param, char *buf, size_t buf_size);

/**
 * 检查参数是否为空
 * @param param 参数
 * @return 1 为空，0 非空
 */
int router_param_is_empty(route_param_t param);

#endif /* ROUTER_H */
