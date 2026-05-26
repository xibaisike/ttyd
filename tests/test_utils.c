#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <stdlib.h>
#include <cmocka.h>

#include "utils.h"

/* ==================================================================
 * TESTS: uppercase / lowercase
 * ================================================================== */

static void test_uppercase_basic(void **state) {
  (void)state;
  char buf[] = "hello";
  uppercase(buf);
  assert_string_equal(buf, "HELLO");
}

static void test_uppercase_mixed(void **state) {
  (void)state;
  char buf[] = "HeLLo World";
  uppercase(buf);
  assert_string_equal(buf, "HELLO WORLD");
}

static void test_uppercase_empty(void **state) {
  (void)state;
  char buf[] = "";
  uppercase(buf);
  assert_string_equal(buf, "");
}

static void test_lowercase_basic(void **state) {
  (void)state;
  char buf[] = "HELLO";
  lowercase(buf);
  assert_string_equal(buf, "hello");
}

static void test_lowercase_mixed(void **state) {
  (void)state;
  char buf[] = "HeLLo World";
  lowercase(buf);
  assert_string_equal(buf, "hello world");
}

/* ==================================================================
 * TESTS: endswith
 * ================================================================== */

static void test_endswith_true(void **state) {
  (void)state;
  assert_true(endswith("hello.txt", ".txt"));
}

static void test_endswith_false(void **state) {
  (void)state;
  assert_false(endswith("hello.txt", ".csv"));
}

static void test_endswith_suffix_longer(void **state) {
  (void)state;
  assert_false(endswith("hi", "hello"));
}

static void test_endswith_equal_length(void **state) {
  (void)state;
  assert_false(endswith("txt", "txt"));
}

/* ==================================================================
 * TESTS: get_sig / get_sig_name
 * ================================================================== */

static void test_get_sig_hup(void **state) {
  (void)state;
  assert_int_equal(get_sig("HUP"), 1);
}

static void test_get_sig_with_prefix(void **state) {
  (void)state;
  assert_int_equal(get_sig("SIGHUP"), 1);
}

static void test_get_sig_case_insensitive(void **state) {
  (void)state;
  assert_int_equal(get_sig("hup"), 1);
  assert_int_equal(get_sig("SIGINT"), 2);
}

static void test_get_sig_numeric_fallback(void **state) {
  (void)state;
  assert_int_equal(get_sig("9"), 9);
}

static void test_get_sig_name_hup(void **state) {
  (void)state;
  char buf[32];
  get_sig_name(1, buf, sizeof(buf));
  assert_string_equal(buf, "SIGHUP");
}

static void test_get_sig_name_kill(void **state) {
  (void)state;
  char buf[32];
  get_sig_name(9, buf, sizeof(buf));
  assert_string_equal(buf, "SIGKILL");
}

/* ==================================================================
 * TESTS: xmalloc / xrealloc
 * ================================================================== */

static void test_xmalloc_zero(void **state) {
  (void)state;
  void *p = xmalloc(0);
  assert_null(p);
}

static void test_xmalloc_nonzero(void **state) {
  (void)state;
  void *p = xmalloc(64);
  assert_non_null(p);
  free(p);
}

static void test_xrealloc_both_zero(void **state) {
  (void)state;
  void *p = xrealloc(NULL, 0);
  assert_null(p);
}

static void test_xrealloc_grow(void **state) {
  (void)state;
  void *p = xmalloc(16);
  assert_non_null(p);
  p = xrealloc(p, 128);
  assert_non_null(p);
  free(p);
}

/* ==================================================================
 * Main
 * ================================================================== */

int main(void) {
  const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_uppercase_basic),
    cmocka_unit_test(test_uppercase_mixed),
    cmocka_unit_test(test_uppercase_empty),
    cmocka_unit_test(test_lowercase_basic),
    cmocka_unit_test(test_lowercase_mixed),

    cmocka_unit_test(test_endswith_true),
    cmocka_unit_test(test_endswith_false),
    cmocka_unit_test(test_endswith_suffix_longer),
    cmocka_unit_test(test_endswith_equal_length),

    cmocka_unit_test(test_get_sig_hup),
    cmocka_unit_test(test_get_sig_with_prefix),
    cmocka_unit_test(test_get_sig_case_insensitive),
    cmocka_unit_test(test_get_sig_numeric_fallback),
    cmocka_unit_test(test_get_sig_name_hup),
    cmocka_unit_test(test_get_sig_name_kill),

    cmocka_unit_test(test_xmalloc_zero),
    cmocka_unit_test(test_xmalloc_nonzero),
    cmocka_unit_test(test_xrealloc_both_zero),
    cmocka_unit_test(test_xrealloc_grow),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}
