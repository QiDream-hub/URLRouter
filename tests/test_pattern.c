#include <stdio.h>
#include <string.h>
#include "pattern.h"

/* ============================================================
 * 段模式 词法 + 编译 单元测试（已从 Stride 迁入 URLRouter）
 * ============================================================ */

static int passed = 0, failed = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) { printf("  [PASS] %s\n", msg); passed++; }                    \
        else { printf("  [FAIL] %s\n", msg); failed++; }                         \
    } while (0)

static url_op_t *lex(const char *pat, size_t *n) {
    url_op_t *ops = NULL;
    size_t cap = 0;
    if (url_lex(pat, 0, &ops, n, &cap) != 0) {
        return NULL;
    }
    return ops;
}

static int blob_is(const stride_blob_t *b, const char *s) {
    size_t n = strlen(s);
    return b->bit_len == n * 8 && b->data && memcmp(b->data, s, n) == 0;
}

static void test_lex_ops(void) {
    printf("\n词法分析...\n");
    size_t n = 0;
    url_op_t *ops;

    ops = lex("$'user'", &n);
    CHECK(ops && n == 1 && ops[0].type == URL_OP_MATCH &&
              blob_is(&ops[0].data.literal, "user"),
          "$'user' → MATCH");
    url_ops_free(ops, n);

    ops = lex("${4}", &n);
    CHECK(ops && n == 1 && ops[0].type == URL_OP_CAPTURE_STEPS &&
              ops[0].data.steps == 4,
          "${4} → CAPTURE_STEPS(4)");
    url_ops_free(ops, n);

    ops = lex("${'.'}", &n);
    CHECK(ops && n == 1 && ops[0].type == URL_OP_CAPTURE_UNTIL &&
              blob_is(&ops[0].data.literal, "."),
          "${'.'} → CAPTURE_UNTIL('.')");
    url_ops_free(ops, n);

    ops = lex("${}", &n);
    CHECK(ops && n == 1 && ops[0].type == URL_OP_CAPTURE_END,
          "${} → CAPTURE_END");
    url_ops_free(ops, n);

    ops = lex("$[5]", &n);
    CHECK(ops && n == 1 && ops[0].type == URL_OP_JUMP_ABS &&
              ops[0].data.steps == 5,
          "$[5] → JUMP_ABS(5)");
    url_ops_free(ops, n);

    ops = lex("$[END]", &n);
    CHECK(ops && n == 1 && ops[0].type == URL_OP_JUMP_END &&
              ops[0].data.jump_end.is_end == 1 &&
              ops[0].data.jump_end.back_steps == 0,
          "$[END] → JUMP_END(0)");
    url_ops_free(ops, n);

    ops = lex("$[END-4]", &n);
    CHECK(ops && n == 1 && ops[0].data.jump_end.back_steps == 4,
          "$[END-4] → JUMP_END(4)");
    url_ops_free(ops, n);

    ops = lex("$[>3]", &n);
    CHECK(ops && n == 1 && ops[0].type == URL_OP_JUMP_FWD &&
              ops[0].data.steps == 3,
          "$[>3] → JUMP_FWD(3)");
    url_ops_free(ops, n);

    ops = lex("$[<2]", &n);
    CHECK(ops && n == 1 && ops[0].type == URL_OP_JUMP_BACK &&
              ops[0].data.steps == 2,
          "$[<2] → JUMP_BACK(2)");
    url_ops_free(ops, n);

    ops = lex("$[>'=']", &n);
    CHECK(ops && n == 1 && ops[0].type == URL_OP_FIND_FWD &&
              blob_is(&ops[0].data.literal, "="),
          "$[>'='] → FIND_FWD");
    url_ops_free(ops, n);

    ops = lex("$[<'=']", &n);
    CHECK(ops && n == 1 && ops[0].type == URL_OP_FIND_REV &&
              blob_is(&ops[0].data.literal, "="),
          "$[<'='] → FIND_REV");
    url_ops_free(ops, n);
}

static void test_lex_escapes(void) {
    printf("\n转义与多字节...\n");
    size_t n = 0;

    url_op_t *ops = lex("$'a\\x00b'", &n);
    CHECK(ops && n == 1 && ops[0].data.literal.bit_len == 3 * 8 &&
              ((const unsigned char *)ops[0].data.literal.data)[1] == 0,
          "$'a\\x00b' → 含 NUL 的 3 字节字面量");
    url_ops_free(ops, n);

    ops = lex("$'it\\'s'", &n);
    CHECK(ops && n == 1 && blob_is(&ops[0].data.literal, "it's"),
          "转义单引号");
    url_ops_free(ops, n);

    ops = lex("$'\\\\'", &n);
    CHECK(ops && n == 1 && blob_is(&ops[0].data.literal, "\\"),
          "转义反斜杠");
    url_ops_free(ops, n);

    ops = lex("$'\xE7\x94\xA8\xE6\x88\xB7'", &n);
    CHECK(ops && n == 1 && ops[0].data.literal.bit_len == 6 * 8,
          "多字节（UTF-8）字面量 = 6 字节");
    url_ops_free(ops, n);
}

static void test_lex_errors(void) {
    printf("\n语法错误...\n");
    size_t n = 0;

    CHECK(lex("$'unclosed", &n) == NULL, "未闭合引号被拒绝");
    CHECK(lex("${0}", &n) == NULL, "零步捕获被拒绝");
    CHECK(lex("${abc}", &n) == NULL, "非法 ${} 内容被拒绝");
    CHECK(lex("$[abc]", &n) == NULL, "非法位置表达式被拒绝");
    CHECK(lex("user", &n) == NULL, "缺少 $ 前缀被拒绝");
    CHECK(lex("$", &n) == NULL, "孤立 $ 被拒绝");
}

static void test_compile(void) {
    printf("\n编译...\n");

    url_compile_result_t r = url_compile("$'v'${'.'}$'.'${}", 0);
    CHECK(r.status == STRIDE_OK, "$'v'${'.'}$'.'${} 编译成功");
    CHECK(r.match && stride_seq_count(r.match) == 3,
          "匹配序列 3 个节点（比对/查找合并/段尾）");
    CHECK(r.param_count == 2, "2 个捕获");
    CHECK(r.extract && stride_seq_count(r.extract) == 2,
          "提取序列 2 个节点（跳过与捕获绑定到同一节点）");
    CHECK(r.extract && r.extract->head &&
              r.extract->head->move == STRIDE_MOVE_SKIP_BITS &&
              r.extract->head->act == STRIDE_ACT_CAPTURE_UNTIL,
          "节点0 = 跳过字面量 + 捕获到定界串");

    /* 段尾定位与查找合并进同一节点 */
    const stride_step_t *n0 = r.match->head;
    const stride_step_t *n1 = n0 ? n0->next : NULL;
    const stride_step_t *n2 = n1 ? n1->next : NULL;
    CHECK(n0 && n0->act == STRIDE_ACT_COMPARE, "节点0 为比对");
    CHECK(n1 && n1->move == STRIDE_MOVE_FIND_FWD &&
              n1->act == STRIDE_ACT_COMPARE,
          "节点1 为查找 + 比对（字面量绑定到偏移）");
    CHECK(n2 && n2->move == STRIDE_MOVE_ABS_END, "节点2 为段尾定位");

    url_compile_free(&r);
    CHECK(r.match == NULL && r.extract == NULL, "url_compile_free 清零");

    /* 连续常量偏移在尾节点合并 */
    r = url_compile("${1}${1}${1}$'key'", 0);
    CHECK(r.status == STRIDE_OK && stride_seq_count(r.match) == 1,
          "${1}${1}${1}$'key' 匹配序列合并为 1 个节点");
    url_compile_free(&r);

    /* END 基准与后退合并 */
    r = url_compile("${}$[<4]$'dddd'", 0);
    CHECK(r.status == STRIDE_OK && stride_seq_count(r.match) == 1 &&
              r.match->head->move == STRIDE_MOVE_ABS_END &&
              r.match->head->move_value == 4,
          "${}$[<4]$'dddd' → ABS_END(4)");
    url_compile_free(&r);

    /* 空模式 */
    r = url_compile("", 0);
    CHECK(r.status == STRIDE_E_EMPTY_SEGMENT, "空模式 → E_EMPTY_SEGMENT");
    url_compile_free(&r);

    r = url_compile(NULL, 0);
    CHECK(r.status == STRIDE_E_EMPTY_SEGMENT, "NULL → E_EMPTY_SEGMENT");
    url_compile_free(&r);

    r = url_compile("$'x", 0);
    CHECK(r.status == STRIDE_E_INVALID_PATTERN, "语法错误 → E_INVALID_PATTERN");
    url_compile_free(&r);
}

int main(void) {
    printf("=== URLRouter 段模式（词法 + 编译）测试 ===\n");
    test_lex_ops();
    test_lex_escapes();
    test_lex_errors();
    test_compile();
    printf("\n=== 结果 ===\n通过 %d，失败 %d\n", passed, failed);
    return failed ? 1 : 0;
}
