#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <stdlib.h>
#include <cmocka.h>

#include "osc133.h"

#define MAX_EVENTS 16

typedef struct {
  osc133_event_t events[MAX_EVENTS];
  int count;
} capture_t;

static void capture_cb(osc133_event_t *event, void *ctx) {
  capture_t *cap = ctx;
  if (cap->count < MAX_EVENTS) {
    cap->events[cap->count++] = *event;
  }
}

/* helper: OSC 133 with BEL terminator */
#define OSC_BEL(type) "\x1b]133;" type "\x07"
/* helper: OSC 133 with ESC\ terminator */
#define OSC_ST(type) "\x1b]133;" type "\x1b\\"

/* ==================================================================
 * TESTS: single events with BEL terminator
 * ================================================================== */

static void test_event_a_bel(void **state) {
  (void)state;
  capture_t cap = {0};
  const char *buf = OSC_BEL("A");
  osc133_scan(buf, strlen(buf), capture_cb, &cap);
  assert_int_equal(cap.count, 1);
  assert_int_equal(cap.events[0].type, OSC133_A);
  assert_int_equal(cap.events[0].exit_code, -1);
}

static void test_event_b_bel(void **state) {
  (void)state;
  capture_t cap = {0};
  const char *buf = OSC_BEL("B");
  osc133_scan(buf, strlen(buf), capture_cb, &cap);
  assert_int_equal(cap.count, 1);
  assert_int_equal(cap.events[0].type, OSC133_B);
}

static void test_event_c_bel(void **state) {
  (void)state;
  capture_t cap = {0};
  const char *buf = OSC_BEL("C");
  osc133_scan(buf, strlen(buf), capture_cb, &cap);
  assert_int_equal(cap.count, 1);
  assert_int_equal(cap.events[0].type, OSC133_C);
}

static void test_event_d_no_exit_code(void **state) {
  (void)state;
  capture_t cap = {0};
  const char *buf = OSC_BEL("D");
  osc133_scan(buf, strlen(buf), capture_cb, &cap);
  assert_int_equal(cap.count, 1);
  assert_int_equal(cap.events[0].type, OSC133_D);
  assert_int_equal(cap.events[0].exit_code, -1);
}

static void test_event_d_with_exit_code_zero(void **state) {
  (void)state;
  capture_t cap = {0};
  const char *buf = "\x1b]133;D;0\x07";
  osc133_scan(buf, strlen(buf), capture_cb, &cap);
  assert_int_equal(cap.count, 1);
  assert_int_equal(cap.events[0].type, OSC133_D);
  assert_int_equal(cap.events[0].exit_code, 0);
}

static void test_event_d_with_exit_code_nonzero(void **state) {
  (void)state;
  capture_t cap = {0};
  const char *buf = "\x1b]133;D;127\x07";
  osc133_scan(buf, strlen(buf), capture_cb, &cap);
  assert_int_equal(cap.count, 1);
  assert_int_equal(cap.events[0].type, OSC133_D);
  assert_int_equal(cap.events[0].exit_code, 127);
}

static void test_event_r_bel(void **state) {
  (void)state;
  capture_t cap = {0};
  const char *buf = OSC_BEL("R");
  osc133_scan(buf, strlen(buf), capture_cb, &cap);
  assert_int_equal(cap.count, 1);
  assert_int_equal(cap.events[0].type, OSC133_R);
}

/* ==================================================================
 * TESTS: ESC\ (ST) terminator
 * ================================================================== */

static void test_event_a_st(void **state) {
  (void)state;
  capture_t cap = {0};
  const char *buf = OSC_ST("A");
  osc133_scan(buf, strlen(buf), capture_cb, &cap);
  assert_int_equal(cap.count, 1);
  assert_int_equal(cap.events[0].type, OSC133_A);
}

static void test_event_d_exit_code_st(void **state) {
  (void)state;
  capture_t cap = {0};
  const char *buf = "\x1b]133;D;42\x1b\\";
  osc133_scan(buf, strlen(buf), capture_cb, &cap);
  assert_int_equal(cap.count, 1);
  assert_int_equal(cap.events[0].type, OSC133_D);
  assert_int_equal(cap.events[0].exit_code, 42);
}

/* ==================================================================
 * TESTS: multiple events
 * ================================================================== */

static void test_multiple_events(void **state) {
  (void)state;
  capture_t cap = {0};
  const char *buf = OSC_BEL("A") "some text" OSC_BEL("B") "more" OSC_BEL("C");
  osc133_scan(buf, strlen(buf), capture_cb, &cap);
  assert_int_equal(cap.count, 3);
  assert_int_equal(cap.events[0].type, OSC133_A);
  assert_int_equal(cap.events[1].type, OSC133_B);
  assert_int_equal(cap.events[2].type, OSC133_C);
}

static void test_full_command_cycle(void **state) {
  (void)state;
  capture_t cap = {0};
  // A (prompt start) -> B (prompt end) -> C (output start) -> D;0 (command done)
  const char *buf = OSC_BEL("A") "$ ls" OSC_BEL("B") OSC_BEL("C") "file.txt\n" "\x1b]133;D;0\x07";
  osc133_scan(buf, strlen(buf), capture_cb, &cap);
  assert_int_equal(cap.count, 4);
  assert_int_equal(cap.events[0].type, OSC133_A);
  assert_int_equal(cap.events[1].type, OSC133_B);
  assert_int_equal(cap.events[2].type, OSC133_C);
  assert_int_equal(cap.events[3].type, OSC133_D);
  assert_int_equal(cap.events[3].exit_code, 0);
}

/* ==================================================================
 * TESTS: no events / malformed
 * ================================================================== */

static void test_plain_text_no_events(void **state) {
  (void)state;
  capture_t cap = {0};
  const char *buf = "hello world\n";
  osc133_scan(buf, strlen(buf), capture_cb, &cap);
  assert_int_equal(cap.count, 0);
}

static void test_empty_buffer(void **state) {
  (void)state;
  capture_t cap = {0};
  osc133_scan("", 0, capture_cb, &cap);
  assert_int_equal(cap.count, 0);
}

static void test_incomplete_sequence(void **state) {
  (void)state;
  capture_t cap = {0};
  // ESC ] 133 ; A but no terminator
  const char *buf = "\x1b]133;A";
  osc133_scan(buf, strlen(buf), capture_cb, &cap);
  assert_int_equal(cap.count, 0);
}

static void test_unknown_type_skipped(void **state) {
  (void)state;
  capture_t cap = {0};
  // Unknown type 'Z' should be skipped, valid 'A' should still be caught
  const char *buf = OSC_BEL("Z") OSC_BEL("A");
  osc133_scan(buf, strlen(buf), capture_cb, &cap);
  assert_int_equal(cap.count, 1);
  assert_int_equal(cap.events[0].type, OSC133_A);
}

static void test_truncated_osc_prefix(void **state) {
  (void)state;
  capture_t cap = {0};
  // ESC ] 13 (incomplete prefix)
  const char *buf = "\x1b]13";
  osc133_scan(buf, strlen(buf), capture_cb, &cap);
  assert_int_equal(cap.count, 0);
}

static void test_interleaved_with_other_escapes(void **state) {
  (void)state;
  capture_t cap = {0};
  // Regular CSI sequence followed by OSC 133
  const char *buf = "\x1b[32m" "green text" "\x1b[0m" OSC_BEL("B");
  osc133_scan(buf, strlen(buf), capture_cb, &cap);
  assert_int_equal(cap.count, 1);
  assert_int_equal(cap.events[0].type, OSC133_B);
}

/* ==================================================================
 * Main
 * ================================================================== */

int main(void) {
  const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_event_a_bel),
    cmocka_unit_test(test_event_b_bel),
    cmocka_unit_test(test_event_c_bel),
    cmocka_unit_test(test_event_d_no_exit_code),
    cmocka_unit_test(test_event_d_with_exit_code_zero),
    cmocka_unit_test(test_event_d_with_exit_code_nonzero),
    cmocka_unit_test(test_event_r_bel),
    cmocka_unit_test(test_event_a_st),
    cmocka_unit_test(test_event_d_exit_code_st),
    cmocka_unit_test(test_multiple_events),
    cmocka_unit_test(test_full_command_cycle),
    cmocka_unit_test(test_plain_text_no_events),
    cmocka_unit_test(test_empty_buffer),
    cmocka_unit_test(test_incomplete_sequence),
    cmocka_unit_test(test_unknown_type_skipped),
    cmocka_unit_test(test_truncated_osc_prefix),
    cmocka_unit_test(test_interleaved_with_other_escapes),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}
