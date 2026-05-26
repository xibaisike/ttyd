#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <stdlib.h>
#include <cmocka.h>

#include "session.h"
#include "logger.h"

/* Stubs for dependencies */

void logger_log(log_level_t level, const char *tag, const char *fmt, ...) {
  (void)level;
  (void)tag;
  (void)fmt;
}

bool process_running(pty_process *process) {
  (void)process;
  return false;
}

bool pty_kill(pty_process *process, int sig) {
  (void)process;
  (void)sig;
  return true;
}

void pty_pause(pty_process *process) { (void)process; }
void pty_resume(pty_process *process) { (void)process; }

struct server *server = NULL;

/* ==================================================================
 * TESTS: ring_buf
 * ================================================================== */

static void test_ring_buf_init(void **state) {
  (void)state;
  ring_buf_t rb;
  ring_buf_init(&rb, 64);
  assert_non_null(rb.data);
  assert_int_equal(rb.capacity, 64);
  assert_int_equal(rb.head, 0);
  assert_int_equal(rb.len, 0);
  assert_int_equal(rb.total_written, 0);
  ring_buf_free(&rb);
}

static void test_ring_buf_write_read_basic(void **state) {
  (void)state;
  ring_buf_t rb;
  ring_buf_init(&rb, 64);

  ring_buf_write(&rb, "hello", 5);
  assert_int_equal(rb.len, 5);
  assert_int_equal(rb.total_written, 5);

  char out[64];
  size_t n = ring_buf_read(&rb, 0, out, sizeof(out));
  assert_int_equal(n, 5);
  assert_memory_equal(out, "hello", 5);

  ring_buf_free(&rb);
}

static void test_ring_buf_write_empty(void **state) {
  (void)state;
  ring_buf_t rb;
  ring_buf_init(&rb, 64);

  ring_buf_write(&rb, "data", 0);
  assert_int_equal(rb.len, 0);

  ring_buf_free(&rb);
}

static void test_ring_buf_write_multiple(void **state) {
  (void)state;
  ring_buf_t rb;
  ring_buf_init(&rb, 64);

  ring_buf_write(&rb, "aaa", 3);
  ring_buf_write(&rb, "bbb", 3);
  assert_int_equal(rb.len, 6);
  assert_int_equal(rb.total_written, 6);

  char out[64];
  size_t n = ring_buf_read(&rb, 0, out, sizeof(out));
  assert_int_equal(n, 6);
  assert_memory_equal(out, "aaabbb", 6);

  ring_buf_free(&rb);
}

static void test_ring_buf_wrap_around(void **state) {
  (void)state;
  ring_buf_t rb;
  ring_buf_init(&rb, 8);

  ring_buf_write(&rb, "12345", 5);
  ring_buf_write(&rb, "6789", 4);
  // capacity=8, total written=9, only last 8 bytes kept: "23456789"
  assert_int_equal(rb.len, 8);

  char out[16];
  size_t n = ring_buf_read(&rb, 0, out, sizeof(out));
  assert_int_equal(n, 8);
  assert_memory_equal(out, "23456789", 8);

  ring_buf_free(&rb);
}

static void test_ring_buf_overflow_single_write(void **state) {
  (void)state;
  ring_buf_t rb;
  ring_buf_init(&rb, 4);

  ring_buf_write(&rb, "abcdefgh", 8);
  // write >= capacity: keeps last 4 bytes
  assert_int_equal(rb.len, 4);

  char out[8];
  size_t n = ring_buf_read(&rb, 0, out, sizeof(out));
  assert_int_equal(n, 4);
  assert_memory_equal(out, "efgh", 4);

  ring_buf_free(&rb);
}

static void test_ring_buf_read_with_offset(void **state) {
  (void)state;
  ring_buf_t rb;
  ring_buf_init(&rb, 64);

  ring_buf_write(&rb, "hello world", 11);

  char out[64];
  size_t n = ring_buf_read(&rb, 6, out, sizeof(out));
  assert_int_equal(n, 5);
  assert_memory_equal(out, "world", 5);

  ring_buf_free(&rb);
}

static void test_ring_buf_read_offset_beyond_len(void **state) {
  (void)state;
  ring_buf_t rb;
  ring_buf_init(&rb, 64);

  ring_buf_write(&rb, "hi", 2);

  char out[8];
  size_t n = ring_buf_read(&rb, 10, out, sizeof(out));
  assert_int_equal(n, 0);

  ring_buf_free(&rb);
}

static void test_ring_buf_read_partial(void **state) {
  (void)state;
  ring_buf_t rb;
  ring_buf_init(&rb, 64);

  ring_buf_write(&rb, "abcdef", 6);

  char out[3];
  size_t n = ring_buf_read(&rb, 0, out, 3);
  assert_int_equal(n, 3);
  assert_memory_equal(out, "abc", 3);

  ring_buf_free(&rb);
}

static void test_ring_buf_truncate(void **state) {
  (void)state;
  ring_buf_t rb;
  ring_buf_init(&rb, 64);

  ring_buf_write(&rb, "abcdef", 6);
  ring_buf_truncate(&rb, 3);
  assert_int_equal(rb.len, 3);

  char out[8];
  size_t n = ring_buf_read(&rb, 0, out, sizeof(out));
  assert_int_equal(n, 3);
  assert_memory_equal(out, "abc", 3);

  ring_buf_free(&rb);
}

static void test_ring_buf_truncate_noop(void **state) {
  (void)state;
  ring_buf_t rb;
  ring_buf_init(&rb, 64);

  ring_buf_write(&rb, "abc", 3);
  ring_buf_truncate(&rb, 10);
  assert_int_equal(rb.len, 3);

  ring_buf_free(&rb);
}

static void test_ring_buf_free_clears(void **state) {
  (void)state;
  ring_buf_t rb;
  ring_buf_init(&rb, 32);
  ring_buf_write(&rb, "data", 4);
  ring_buf_free(&rb);

  assert_null(rb.data);
  assert_int_equal(rb.capacity, 0);
  assert_int_equal(rb.len, 0);
}

/* ==================================================================
 * TESTS: session_manager / session_create / session_find
 * ================================================================== */

static int session_setup(void **state) {
  (void)state;
  session_manager_init(1024);
  return 0;
}

static int session_teardown(void **state) {
  (void)state;
  // manually free sessions (can't use session_manager_destroy since it
  // calls pty_kill with server->sig_code and server is NULL)
  session_t *s = g_sessions;
  while (s != NULL) {
    session_t *next = s->next;
    ring_buf_free(&s->scrollback);
    free(s);
    s = next;
  }
  g_sessions = NULL;
  return 0;
}

static void test_session_create_basic(void **state) {
  (void)state;
  session_t *s = session_create("test-1", 80, 24);
  assert_non_null(s);
  assert_string_equal(s->name, "test-1");
  assert_int_equal(s->state, SESSION_RUNNING);
  assert_int_equal(s->shell_phase, SHELL_IDLE);
  assert_int_equal(s->last_exit_code, -1);
  assert_int_equal(s->connection_count, 0);
  assert_null(s->process);
  assert_null(s->connections);
  assert_true(s->created_at > 0);
}

static void test_session_create_duplicate(void **state) {
  (void)state;
  session_t *s1 = session_create("dup", 80, 24);
  assert_non_null(s1);
  session_t *s2 = session_create("dup", 80, 24);
  assert_null(s2);
}

static void test_session_find_existing(void **state) {
  (void)state;
  session_create("alpha", 80, 24);
  session_create("beta", 80, 24);

  session_t *found = session_find("alpha");
  assert_non_null(found);
  assert_string_equal(found->name, "alpha");

  found = session_find("beta");
  assert_non_null(found);
  assert_string_equal(found->name, "beta");
}

static void test_session_find_not_found(void **state) {
  (void)state;
  session_create("exists", 80, 24);
  session_t *found = session_find("nope");
  assert_null(found);
}

static void test_session_create_multiple(void **state) {
  (void)state;
  session_t *s1 = session_create("s1", 80, 24);
  session_t *s2 = session_create("s2", 80, 24);
  session_t *s3 = session_create("s3", 80, 24);
  assert_non_null(s1);
  assert_non_null(s2);
  assert_non_null(s3);

  assert_non_null(session_find("s1"));
  assert_non_null(session_find("s2"));
  assert_non_null(session_find("s3"));
}

static void test_session_scrollback_initialized(void **state) {
  (void)state;
  session_t *s = session_create("scroll", 80, 24);
  assert_non_null(s);
  assert_int_equal(s->scrollback.capacity, 1024);
  assert_int_equal(s->scrollback.len, 0);
}

/* ==================================================================
 * Main
 * ================================================================== */

int main(void) {
  const struct CMUnitTest ring_buf_tests[] = {
    cmocka_unit_test(test_ring_buf_init),
    cmocka_unit_test(test_ring_buf_write_read_basic),
    cmocka_unit_test(test_ring_buf_write_empty),
    cmocka_unit_test(test_ring_buf_write_multiple),
    cmocka_unit_test(test_ring_buf_wrap_around),
    cmocka_unit_test(test_ring_buf_overflow_single_write),
    cmocka_unit_test(test_ring_buf_read_with_offset),
    cmocka_unit_test(test_ring_buf_read_offset_beyond_len),
    cmocka_unit_test(test_ring_buf_read_partial),
    cmocka_unit_test(test_ring_buf_truncate),
    cmocka_unit_test(test_ring_buf_truncate_noop),
    cmocka_unit_test(test_ring_buf_free_clears),
  };

  const struct CMUnitTest session_tests[] = {
    cmocka_unit_test_setup_teardown(test_session_create_basic, session_setup, session_teardown),
    cmocka_unit_test_setup_teardown(test_session_create_duplicate, session_setup, session_teardown),
    cmocka_unit_test_setup_teardown(test_session_find_existing, session_setup, session_teardown),
    cmocka_unit_test_setup_teardown(test_session_find_not_found, session_setup, session_teardown),
    cmocka_unit_test_setup_teardown(test_session_create_multiple, session_setup, session_teardown),
    cmocka_unit_test_setup_teardown(test_session_scrollback_initialized, session_setup, session_teardown),
  };

  int ret = 0;
  ret += cmocka_run_group_tests_name("ring_buf", ring_buf_tests, NULL, NULL);
  ret += cmocka_run_group_tests_name("session", session_tests, NULL, NULL);
  return ret;
}
