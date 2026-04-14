#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/pattern_compiler.h"
#include "../include/lexer.h"
#include "../include/extractor_compiler.h"

/* ============================================================
 * 提取序列编译器单元测试
 * ============================================================ */

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT(cond, msg) do { \
    if (cond) { \
        printf("  ✓ %s\n", msg); \
        tests_passed++; \
    } else { \
        printf("  ✗ FAILED: %s\n", msg); \
        tests_failed++; \
    } \
} while(0)

/* 辅助函数：编译模式并获取提取序列 */
static extractor_op_t *compile_extractors(const char *pattern, size_t *out_count, size_t *out_params) {
    op_t *ops = NULL;
    size_t op_count = 0, op_capacity = 0;
    extractor_op_t *extractors = NULL;
    size_t extractor_count = 0, param_count = 0;
    
    if (pattern_lex(pattern, &ops, &op_count, &op_capacity) != 0) {
        return NULL;
    }
    
    if (pattern_generate_extractors(ops, op_count, &extractors, &extractor_count, &param_count) != 0) {
        op_array_free(ops);
        return NULL;
    }
    
    op_array_free(ops);
    *out_count = extractor_count;
    *out_params = param_count;
    return extractors;
}

/* 测试示例 1：基础捕获（含匹配优化）${4}$'a' */
static void test_basic_capture_with_optimization(void) {
    printf("Test: Basic capture with match optimization (${4}$'a')\n");
    
    size_t count = 0, params = 0;
    extractor_op_t *ops = compile_extractors("${4}$'a'", &count, &params);
    
    ASSERT(ops != NULL, "Compilation succeeded");
    ASSERT(count == 2, "Two extractor ops output");
    ASSERT(params == 1, "One parameter produced");
    ASSERT(ops[0].type == EX_CAPTURE_LEN, "First op is EX_CAPTURE_LEN");
    ASSERT(ops[0].data.capture_len.length == 4, "Capture length is 4");
    ASSERT(ops[1].type == EX_SKIP_LEN, "Second op is EX_SKIP_LEN (optimized from OP_MATCH)");
    ASSERT(ops[1].data.skip_len.length == 1, "Skip length is 1");
    
    free(ops);
}

/* 测试示例 2：捕获到字符（无优化）${'='}$'='${} */
static void test_capture_chr_no_optimization(void) {
    printf("Test: Capture char without optimization (${'='}$'='${})\n");
    
    size_t count = 0, params = 0;
    extractor_op_t *ops = compile_extractors("${'='}$'='${}", &count, &params);
    
    ASSERT(ops != NULL, "Compilation succeeded");
    ASSERT(count == 3, "Three extractor ops output");
    ASSERT(params == 2, "Two parameters produced");
    ASSERT(ops[0].type == EX_CAPTURE_CHR, "First op is EX_CAPTURE_CHR");
    ASSERT(ops[1].type == EX_SKIP_LEN, "Second op is EX_SKIP_LEN");
    ASSERT(ops[2].type == EX_CAPTURE_END, "Third op is EX_CAPTURE_END");
    
    free(ops);
}

/* 测试示例 3：常量移动合并 ${2}$[>3]$'abc' */
static void test_const_move_merge(void) {
    printf("Test: Constant move merge (${2}$[>3]$'abc')\n");

    size_t count = 0, params = 0;
    extractor_op_t *ops = compile_extractors("${2}$[>3]$'abc'", &count, &params);

    ASSERT(ops != NULL, "Compilation succeeded");
    /* EX_SKIP_LEN 不参与合并（被 EX_CAPTURE_LEN 打断），但 $[>3] 与 EX_SKIP_LEN 合并为 EX_SKIP_LEN */
    ASSERT(count == 2, "Two extractor ops output");
    ASSERT(params == 1, "One parameter produced");
    ASSERT(ops[0].type == EX_CAPTURE_LEN, "First op is EX_CAPTURE_LEN");
    /* $[>3] + EX_SKIP_LEN(3) = EX_SKIP_LEN(6) */
    ASSERT(ops[1].type == EX_SKIP_LEN, "Second op is EX_SKIP_LEN (merged)");
    ASSERT(ops[1].data.skip_len.length == 6, "Skip length is 6 (3+3)");

    free(ops);
}

/* 测试示例 4：动态操作打断合并 ${2}$[>'=']$[>3]$'abc' */
static void test_dynamic_interrupt_merge(void) {
    printf("Test: Dynamic interrupt merge (${2}$[>'=']$[>3]$'abc')\n");

    size_t count = 0, params = 0;
    extractor_op_t *ops = compile_extractors("${2}$[>'=']$[>3]$'abc'", &count, &params);

    ASSERT(ops != NULL, "Compilation succeeded");
    ASSERT(count == 3, "Three extractor ops output");
    ASSERT(params == 1, "One parameter produced");
    ASSERT(ops[0].type == EX_CAPTURE_LEN, "First op is EX_CAPTURE_LEN");
    ASSERT(ops[1].type == EX_FIND_FWD, "Second op is EX_FIND_FWD");
    /* $[>3] + EX_SKIP_LEN(3) = EX_SKIP_LEN(6) */
    ASSERT(ops[2].type == EX_SKIP_LEN, "Third op is EX_SKIP_LEN (merged)");
    ASSERT(ops[2].data.skip_len.length == 6, "Skip length is 6 (3+3)");

    free(ops);
}

/* 测试示例 5：正向与负向抵消 $[>5]$[<3]$'key' */
static void test_forward_backward_cancel(void) {
    printf("Test: Forward/backward cancel ($[>5]$[<3]$'key')\n");

    size_t count = 0, params = 0;
    extractor_op_t *ops = compile_extractors("$[>5]$[<3]$'key'", &count, &params);

    ASSERT(ops != NULL, "Compilation succeeded");
    /* $[>5] + $[<3] = EX_JUMP_FWD(5) + EX_JUMP_BACK(3) = EX_JUMP_FWD(2) */
    /* 然后与 EX_SKIP_LEN(3) 合并为 EX_SKIP_LEN(5) */
    ASSERT(count == 1, "One extractor op output (merged)");
    ASSERT(params == 0, "No parameters produced");
    ASSERT(ops[0].type == EX_SKIP_LEN, "Op is EX_SKIP_LEN (merged with match)");
    ASSERT(ops[0].data.skip_len.length == 5, "Skip length is 5 (5-3+3=5)");

    free(ops);
}

/* 测试：仅匹配操作 $'hello' */
static void test_match_only(void) {
    printf("Test: Match only ($'hello')\n");
    
    size_t count = 0, params = 0;
    extractor_op_t *ops = compile_extractors("$'hello'", &count, &params);
    
    ASSERT(ops != NULL, "Compilation succeeded");
    ASSERT(count == 1, "One extractor op output");
    ASSERT(params == 0, "No parameters produced");
    ASSERT(ops[0].type == EX_SKIP_LEN, "Op is EX_SKIP_LEN");
    ASSERT(ops[0].data.skip_len.length == 5, "Skip length is 5");
    
    free(ops);
}

/* 测试：仅捕获操作 ${10} */
static void test_capture_only(void) {
    printf("Test: Capture only (${10})\n");
    
    size_t count = 0, params = 0;
    extractor_op_t *ops = compile_extractors("${10}", &count, &params);
    
    ASSERT(ops != NULL, "Compilation succeeded");
    ASSERT(count == 1, "One extractor op output");
    ASSERT(params == 1, "One parameter produced");
    ASSERT(ops[0].type == EX_CAPTURE_LEN, "Op is EX_CAPTURE_LEN");
    ASSERT(ops[0].data.capture_len.length == 10, "Capture length is 10");
    
    free(ops);
}

/* 测试：多个连续常量移动合并 $[>2]$[>3]$[>4] */
static void test_multiple_const_moves(void) {
    printf("Test: Multiple constant moves ($[>2]$[>3]$[>4])\n");
    
    size_t count = 0, params = 0;
    extractor_op_t *ops = compile_extractors("$[>2]$[>3]$[>4]", &count, &params);
    
    ASSERT(ops != NULL, "Compilation succeeded");
    ASSERT(count == 1, "One extractor op output (all merged)");
    ASSERT(params == 0, "No parameters produced");
    ASSERT(ops[0].type == EX_JUMP_FWD, "Op is EX_JUMP_FWD");
    ASSERT(ops[0].data.jump_fwd.offset == 9, "Jump offset is 9 (2+3+4)");
    
    free(ops);
}

/* 测试：正向负向完全抵消 $[>5]$[<5] */
static void test_complete_cancel(void) {
    printf("Test: Complete cancel ($[>5]$[<5])\n");
    
    size_t count = 0, params = 0;
    extractor_op_t *ops = compile_extractors("$[>5]$[<5]", &count, &params);
    
    ASSERT(ops != NULL, "Compilation succeeded");
    ASSERT(count == 0, "Zero extractor ops output (completely cancelled)");
    ASSERT(params == 0, "No parameters produced");
    
    free(ops);
}

int main(void) {
    printf("=== Extractor Compiler Unit Tests ===\n\n");
    
    test_basic_capture_with_optimization();
    test_capture_chr_no_optimization();
    test_const_move_merge();
    test_dynamic_interrupt_merge();
    test_forward_backward_cancel();
    test_match_only();
    test_capture_only();
    test_multiple_const_moves();
    test_complete_cancel();
    
    printf("\n=== Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    
    return tests_failed > 0 ? 1 : 0;
}
