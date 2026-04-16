#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pattern_compiler.h"
#include "lexer.h"

/* ============================================================
 * 词法分析器单元测试
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

/* 测试 $'text' - OP_MATCH */
static void test_op_match(void) {
    printf("Test: OP_MATCH ($'text')\n");
    
    op_t *ops = NULL;
    size_t count = 0, capacity = 0;
    
    int ret = pattern_lex("'hello'", &ops, &count, &capacity);
    ASSERT(ret == -1, "Reject pattern without $ prefix");
    
    ret = pattern_lex("$'hello'", &ops, &count, &capacity);
    ASSERT(ret == 0, "Parse $'hello' successfully");
    ASSERT(count == 1, "One operator parsed");
    ASSERT(ops[0].type == OP_MATCH, "Operator type is OP_MATCH");
    ASSERT(ops[0].data.match.len == 5, "Match length is 5");
    ASSERT(strncmp(ops[0].data.match.text, "hello", 5) == 0, "Match text is 'hello'");
    
    op_array_free(ops);
}

/* 测试 ${n} - OP_CAPTURE_LEN */
static void test_op_capture_len(void) {
    printf("Test: OP_CAPTURE_LEN (${n})\n");
    
    op_t *ops = NULL;
    size_t count = 0, capacity = 0;
    
    int ret = pattern_lex("${4}", &ops, &count, &capacity);
    ASSERT(ret == 0, "Parse ${4} successfully");
    ASSERT(count == 1, "One operator parsed");
    ASSERT(ops[0].type == OP_CAPTURE_LEN, "Operator type is OP_CAPTURE_LEN");
    ASSERT(ops[0].data.length == 4, "Capture length is 4");
    
    op_array_free(ops);
}

/* 测试 ${'c'} - OP_CAPTURE_CHR */
static void test_op_capture_chr(void) {
    printf("Test: OP_CAPTURE_CHR (${'c'})\n");
    
    op_t *ops = NULL;
    size_t count = 0, capacity = 0;
    
    int ret = pattern_lex("${'.'}", &ops, &count, &capacity);
    ASSERT(ret == 0, "Parse ${'.'} successfully");
    ASSERT(count == 1, "One operator parsed");
    ASSERT(ops[0].type == OP_CAPTURE_CHR, "Operator type is OP_CAPTURE_CHR");
    ASSERT(ops[0].data.find.ch == '.', "Capture char is '.'");
    
    op_array_free(ops);
}

/* 测试 ${} - OP_CAPTURE_END */
static void test_op_capture_end(void) {
    printf("Test: OP_CAPTURE_END (${})\n");
    
    op_t *ops = NULL;
    size_t count = 0, capacity = 0;
    
    int ret = pattern_lex("${}", &ops, &count, &capacity);
    ASSERT(ret == 0, "Parse ${} successfully");
    ASSERT(count == 1, "One operator parsed");
    ASSERT(ops[0].type == OP_CAPTURE_END, "Operator type is OP_CAPTURE_END");
    
    op_array_free(ops);
}

/* 测试 $[n] - OP_JUMP_ABS */
static void test_op_jump_abs(void) {
    printf("Test: OP_JUMP_ABS ($[n])\n");
    
    op_t *ops = NULL;
    size_t count = 0, capacity = 0;
    
    int ret = pattern_lex("$[5]", &ops, &count, &capacity);
    ASSERT(ret == 0, "Parse $[5] successfully");
    ASSERT(count == 1, "One operator parsed");
    ASSERT(ops[0].type == OP_JUMP_ABS, "Operator type is OP_JUMP_ABS");
    ASSERT(ops[0].data.pos == 5, "Jump position is 5");
    
    op_array_free(ops);
}

/* 测试 $[END] / $[END-n] - OP_JUMP_END */
static void test_op_jump_end(void) {
    printf("Test: OP_JUMP_END ($[END] / $[END-n])\n");
    
    op_t *ops = NULL;
    size_t count = 0, capacity = 0;
    
    int ret = pattern_lex("$[END]", &ops, &count, &capacity);
    ASSERT(ret == 0, "Parse $[END] successfully");
    ASSERT(count == 1, "One operator parsed");
    ASSERT(ops[0].type == OP_JUMP_END, "Operator type is OP_JUMP_END");
    ASSERT(ops[0].data.jump_end.is_end == 1, "is_end flag is set");
    ASSERT(ops[0].data.jump_end.offset == 0, "END offset is 0");
    
    op_array_free(ops);
    
    ret = pattern_lex("$[END-4]", &ops, &count, &capacity);
    ASSERT(ret == 0, "Parse $[END-4] successfully");
    ASSERT(ops[0].data.jump_end.offset == 4, "END-4 offset is 4");
    
    op_array_free(ops);
}

/* 测试 $[>n] - OP_JUMP_FWD */
static void test_op_jump_fwd(void) {
    printf("Test: OP_JUMP_FWD ($[>n])\n");
    
    op_t *ops = NULL;
    size_t count = 0, capacity = 0;
    
    int ret = pattern_lex("$[>3]", &ops, &count, &capacity);
    ASSERT(ret == 0, "Parse $[>3] successfully");
    ASSERT(count == 1, "One operator parsed");
    ASSERT(ops[0].type == OP_JUMP_FWD, "Operator type is OP_JUMP_FWD");
    ASSERT(ops[0].data.offset == 3, "Jump forward offset is 3");
    
    op_array_free(ops);
}

/* 测试 $[<n] - OP_JUMP_BACK */
static void test_op_jump_back(void) {
    printf("Test: OP_JUMP_BACK ($[<n])\n");
    
    op_t *ops = NULL;
    size_t count = 0, capacity = 0;
    
    int ret = pattern_lex("$[<2]", &ops, &count, &capacity);
    ASSERT(ret == 0, "Parse $[<2] successfully");
    ASSERT(count == 1, "One operator parsed");
    ASSERT(ops[0].type == OP_JUMP_BACK, "Operator type is OP_JUMP_BACK");
    ASSERT(ops[0].data.offset == 2, "Jump back offset is 2");
    
    op_array_free(ops);
}

/* 测试 $[>'c'] - OP_FIND_FWD */
static void test_op_find_fwd(void) {
    printf("Test: OP_FIND_FWD ($[>'c'])\n");
    
    op_t *ops = NULL;
    size_t count = 0, capacity = 0;
    
    int ret = pattern_lex("$[>'=']", &ops, &count, &capacity);
    ASSERT(ret == 0, "Parse $[>'='] successfully");
    ASSERT(count == 1, "One operator parsed");
    ASSERT(ops[0].type == OP_FIND_FWD, "Operator type is OP_FIND_FWD");
    ASSERT(ops[0].data.find.ch == '=', "Find char is '='");
    
    op_array_free(ops);
}

/* 测试 $[<'c'] - OP_FIND_REV */
static void test_op_find_rev(void) {
    printf("Test: OP_FIND_REV ($[<'c'])\n");
    
    op_t *ops = NULL;
    size_t count = 0, capacity = 0;
    
    int ret = pattern_lex("$[<'=']", &ops, &count, &capacity);
    ASSERT(ret == 0, "Parse $[<'='] successfully");
    ASSERT(count == 1, "One operator parsed");
    ASSERT(ops[0].type == OP_FIND_REV, "Operator type is OP_FIND_REV");
    ASSERT(ops[0].data.find.ch == '=', "Find char is '='");
    
    op_array_free(ops);
}

/* 测试复合模式（单个段内的多个操作符）*/
static void test_complex_pattern(void) {
    printf("Test: Complex pattern (multiple ops in single segment)\n");
    
    op_t *ops = NULL;
    size_t count = 0, capacity = 0;
    
    /* 注意：词法分析器只处理单个段的内容，不包含 / 分隔符 */
    /* 测试：${1}${'a'}${2}$'key' - 多个操作符在同一段内 */
    int ret = pattern_lex("${1}${'a'}${2}$'key'", &ops, &count, &capacity);
    ASSERT(ret == 0, "Parse complex pattern successfully");
    ASSERT(count == 4, "Four operators parsed");
    if (count >= 1) {
        ASSERT(ops[0].type == OP_CAPTURE_LEN, "First op is OP_CAPTURE_LEN");
    }
    if (count >= 2) {
        ASSERT(ops[1].type == OP_CAPTURE_CHR, "Second op is OP_CAPTURE_CHR");
    }
    if (count >= 3) {
        ASSERT(ops[2].type == OP_CAPTURE_LEN, "Third op is OP_CAPTURE_LEN");
    }
    if (count >= 4) {
        ASSERT(ops[3].type == OP_MATCH, "Fourth op is OP_MATCH");
    }
    
    op_array_free(ops);
}

/* 测试错误处理 - 未闭合引号 */
static void test_error_unclosed_quote(void) {
    printf("Test: Error - unclosed quote\n");
    
    op_t *ops = NULL;
    size_t count = 0, capacity = 0;
    
    int ret = pattern_lex("$'hello", &ops, &count, &capacity);
    ASSERT(ret == -1, "Reject unclosed quote");
    
    ret = pattern_lex("${'.", &ops, &count, &capacity);
    ASSERT(ret == -1, "Reject unclosed quote in capture");
}

/* 测试错误处理 - 无效数字 */
static void test_error_invalid_number(void) {
    printf("Test: Error - invalid number\n");
    
    op_t *ops = NULL;
    size_t count = 0, capacity = 0;
    
    int ret = pattern_lex("${abc}", &ops, &count, &capacity);
    ASSERT(ret == -1, "Reject invalid number in capture");
}

/* 测试错误处理 - 零长度捕获 */
static void test_error_zero_length(void) {
    printf("Test: Error - zero length capture\n");
    
    op_t *ops = NULL;
    size_t count = 0, capacity = 0;
    
    int ret = pattern_lex("${0}", &ops, &count, &capacity);
    ASSERT(ret == -1, "Reject zero length capture");
}

int main(void) {
    printf("=== Lexer Unit Tests ===\n\n");
    
    test_op_match();
    test_op_capture_len();
    test_op_capture_chr();
    test_op_capture_end();
    test_op_jump_abs();
    test_op_jump_end();
    test_op_jump_fwd();
    test_op_jump_back();
    test_op_find_fwd();
    test_op_find_rev();
    test_complex_pattern();
    test_error_unclosed_quote();
    test_error_invalid_number();
    test_error_zero_length();
    
    printf("\n=== Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    
    return tests_failed > 0 ? 1 : 0;
}
