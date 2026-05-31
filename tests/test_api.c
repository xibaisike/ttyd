/*
 * BDD-style unit tests for src/api.c and related HTTP helpers.
 *
 * Style conventions:
 *   - Each `describe_*` block groups tests for one feature/unit.
 *   - Each test function is named `it_should_<expected behavior>_when_<context>`.
 *   - Each test body uses GIVEN / WHEN / THEN comments (Arrange-Act-Assert).
 *   - Suites are run via `cmocka_run_group_tests_name` so output is grouped.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <stdlib.h>
#include <cmocka.h>

#include <libwebsockets.h>
#include "server.h"
#include "api.h"

/* ==================================================================
 * Stubs for external symbols required by api.c
 * ================================================================== */

struct server *server = NULL;
struct endpoints endpoints = {NULL, NULL, NULL, ""};
session_t *g_sessions = NULL;
size_t g_scrollback_size = 4096;
volatile bool force_exit = false;
struct lws_context *context = NULL;

session_t *session_find(const char *name) {
  (void)name;
  return (session_t *)mock();
}

session_t *session_create(const char *name, uint16_t columns, uint16_t rows) {
  (void)name; (void)columns; (void)rows;
  return (session_t *)mock();
}

void session_close(session_t *session) {
  (void)session;
}

/* ==================================================================
 * Mock lws functions
 *
 * The libwebsockets stubs live in tests/libwebsocket_sub.c and are
 * linked into this test binary. lws_hdr_copy uses cmocka mock() —
 * see libwebsocket_sub.c for the will_return convention.
 * ================================================================== */

/* ==================================================================
 * Subject helpers (mirror logic of api.c / http.c so the BDD tests
 * stay self-contained and side-effect free)
 * ================================================================== */

struct parsed_query_params {
  int page_size;
  int page_number;
  char name_filter[128];
  bool has_name_filter;
};

static void parse_query_params(const char *full_path, struct parsed_query_params *out) {
  out->page_size = 20;
  out->page_number = 1;
  out->name_filter[0] = '\0';
  out->has_name_filter = false;

  char *query = strchr(full_path, '?');
  if (query == NULL) return;
  query++;

  char params[512];
  strncpy(params, query, sizeof(params) - 1);
  params[sizeof(params) - 1] = '\0';

  char *pair = strtok(params, "&");
  while (pair != NULL) {
    if (strncmp(pair, "pageSize=", 9) == 0) {
      out->page_size = atoi(pair + 9);
      if (out->page_size <= 0) out->page_size = 20;
    } else if (strncmp(pair, "pageNumber=", 11) == 0) {
      out->page_number = atoi(pair + 11);
      if (out->page_number <= 0) out->page_number = 1;
    } else if (strncmp(pair, "name=", 5) == 0) {
      strncpy(out->name_filter, pair + 5, sizeof(out->name_filter) - 1);
      out->name_filter[sizeof(out->name_filter) - 1] = '\0';
      out->has_name_filter = true;
    }
    pair = strtok(NULL, "&");
  }
}

enum api_route {
  ROUTE_LIST_SESSIONS,
  ROUTE_CREATE_SESSION,
  ROUTE_DELETE_SESSION,
  ROUTE_NOT_FOUND,
};

struct route_result {
  enum api_route route;
  char session_name[64];
};

static struct route_result route_request(const char *path, const char *method, const char *parent_path) {
  struct route_result result = {ROUTE_NOT_FOUND, ""};

  char clean_path[128];
  strncpy(clean_path, path, sizeof(clean_path) - 1);
  clean_path[sizeof(clean_path) - 1] = '\0';
  char *q = strchr(clean_path, '?');
  if (q != NULL) *q = '\0';

  const char *api_path = clean_path;
  size_t parent_len = strlen(parent_path);
  if (parent_len > 0 && strncmp(clean_path, parent_path, parent_len) == 0) {
    api_path = clean_path + parent_len;
  }

  if (strcmp(method, "GET") == 0 && strncmp(api_path, "/api/sessions", 13) == 0 &&
      (api_path[13] == '\0' || api_path[13] == '?')) {
    result.route = ROUTE_LIST_SESSIONS;
    return result;
  }

  if (strcmp(method, "POST") == 0 && strcmp(api_path, "/api/sessions") == 0) {
    result.route = ROUTE_CREATE_SESSION;
    return result;
  }

  if (strcmp(method, "DELETE") == 0 && strncmp(api_path, "/api/sessions/", 14) == 0) {
    const char *name = api_path + 14;
    if (strlen(name) > 0) {
      result.route = ROUTE_DELETE_SESSION;
      strncpy(result.session_name, name, sizeof(result.session_name) - 1);
      result.session_name[sizeof(result.session_name) - 1] = '\0';
      return result;
    }
  }

  return result;
}

enum auth_result { MY_AUTH_OK, MY_AUTH_FAIL };

static enum auth_result check_basic_auth(const char *header_value, const char *expected_credential) {
  if (expected_credential == NULL) return MY_AUTH_OK;
  if (header_value == NULL || strlen(header_value) < 7) return MY_AUTH_FAIL;
  if (strstr(header_value, "Basic ") == NULL) return MY_AUTH_FAIL;
  if (strcmp(header_value + 6, expected_credential) == 0) return MY_AUTH_OK;
  return MY_AUTH_FAIL;
}

static bool check_accept_gzip(const char *accept_encoding) {
  if (accept_encoding == NULL || strlen(accept_encoding) == 0) return false;
  return strstr(accept_encoding, "gzip") != NULL;
}

/* ==================================================================
 * describe("detect_method")
 *   — exercises the real detect_method() from src/api.c by
 *     controlling lws_hdr_copy via cmocka mocks.
 * ================================================================== */

static void it_should_default_to_GET_when_no_method_headers_are_present(void **state) {
  (void)state;
  /* GIVEN no POST_URI header is present */
  will_return(lws_hdr_copy, NULL);

  /* WHEN detect_method is invoked */
  char method[16];
  detect_method((struct lws *)0x1, method, sizeof(method));

  /* THEN it falls back to "GET" */
  assert_string_equal(method, "GET");
}

static void it_should_return_POST_when_post_uri_header_is_present(void **state) {
  (void)state;
  /* GIVEN a POST_URI header indicating a POST request */
  will_return(lws_hdr_copy, "/api/sessions");

  /* WHEN detect_method is invoked */
  char method[16];
  detect_method((struct lws *)0x1, method, sizeof(method));

  /* THEN the method is "POST" */
  assert_string_equal(method, "POST");
}

#if defined(LWS_ROLE_H2)
static void it_should_return_DELETE_when_h2_method_header_is_DELETE(void **state) {
  (void)state;
  /* GIVEN no POST_URI but an H2 :method=DELETE header */
  will_return(lws_hdr_copy, NULL);
  will_return(lws_hdr_copy, "DELETE");

  /* WHEN detect_method is invoked */
  char method[16];
  detect_method((struct lws *)0x1, method, sizeof(method));

  /* THEN the method is "DELETE" */
  assert_string_equal(method, "DELETE");
}

static void it_should_return_PUT_when_h2_method_header_is_PUT(void **state) {
  (void)state;
  /* GIVEN no POST_URI but an H2 :method=PUT header */
  will_return(lws_hdr_copy, NULL);
  will_return(lws_hdr_copy, "PUT");

  /* WHEN detect_method is invoked */
  char method[16];
  detect_method((struct lws *)0x1, method, sizeof(method));

  /* THEN the method is "PUT" */
  assert_string_equal(method, "PUT");
}

static void it_should_return_GET_when_h2_method_header_is_explicitly_GET(void **state) {
  (void)state;
  /* GIVEN no POST_URI but an explicit H2 :method=GET header */
  will_return(lws_hdr_copy, NULL);
  will_return(lws_hdr_copy, "GET");

  /* WHEN detect_method is invoked */
  char method[16];
  detect_method((struct lws *)0x1, method, sizeof(method));

  /* THEN the method is "GET" */
  assert_string_equal(method, "GET");
}

static void it_should_prefer_POST_over_h2_method_header(void **state) {
  (void)state;
  /* GIVEN both POST_URI and an H2 :method header could be present */
  /* AND POST_URI is checked first */
  will_return(lws_hdr_copy, "/api/sessions");

  /* WHEN detect_method is invoked */
  char method[16];
  detect_method((struct lws *)0x1, method, sizeof(method));

  /* THEN POST takes precedence and the H2 header is never consulted */
  assert_string_equal(method, "POST");
}
#endif

/* ==================================================================
 * describe("query parameter parsing")
 * ================================================================== */

static void it_should_use_defaults_when_no_query_string_is_provided(void **state) {
  (void)state;
  /* GIVEN a path without a query string */
  /* WHEN the query is parsed */
  struct parsed_query_params p;
  parse_query_params("/api/sessions", &p);

  /* THEN defaults are applied */
  assert_int_equal(p.page_size, 20);
  assert_int_equal(p.page_number, 1);
  assert_false(p.has_name_filter);
}

static void it_should_apply_pageSize_from_query_string(void **state) {
  (void)state;
  struct parsed_query_params p;
  parse_query_params("/api/sessions?pageSize=50", &p);

  assert_int_equal(p.page_size, 50);
  assert_int_equal(p.page_number, 1);
  assert_false(p.has_name_filter);
}

static void it_should_apply_pageNumber_from_query_string(void **state) {
  (void)state;
  struct parsed_query_params p;
  parse_query_params("/api/sessions?pageNumber=3", &p);

  assert_int_equal(p.page_size, 20);
  assert_int_equal(p.page_number, 3);
}

static void it_should_apply_all_supported_params_together(void **state) {
  (void)state;
  struct parsed_query_params p;
  parse_query_params("/api/sessions?pageSize=10&pageNumber=2&name=test", &p);

  assert_int_equal(p.page_size, 10);
  assert_int_equal(p.page_number, 2);
  assert_true(p.has_name_filter);
  assert_string_equal(p.name_filter, "test");
}

static void it_should_fallback_to_default_when_pageSize_is_zero(void **state) {
  (void)state;
  struct parsed_query_params p;
  parse_query_params("/api/sessions?pageSize=0", &p);
  assert_int_equal(p.page_size, 20);
}

static void it_should_fallback_to_default_when_pageSize_is_negative(void **state) {
  (void)state;
  struct parsed_query_params p;
  parse_query_params("/api/sessions?pageSize=-5", &p);
  assert_int_equal(p.page_size, 20);
}

static void it_should_fallback_to_default_when_pageNumber_is_zero(void **state) {
  (void)state;
  struct parsed_query_params p;
  parse_query_params("/api/sessions?pageNumber=0", &p);
  assert_int_equal(p.page_number, 1);
}

static void it_should_fallback_to_default_when_pageNumber_is_negative(void **state) {
  (void)state;
  struct parsed_query_params p;
  parse_query_params("/api/sessions?pageNumber=-1", &p);
  assert_int_equal(p.page_number, 1);
}

static void it_should_fallback_to_default_when_pageSize_is_non_numeric(void **state) {
  (void)state;
  struct parsed_query_params p;
  parse_query_params("/api/sessions?pageSize=abc", &p);
  assert_int_equal(p.page_size, 20);
}

static void it_should_accept_an_empty_name_filter_as_present(void **state) {
  (void)state;
  struct parsed_query_params p;
  parse_query_params("/api/sessions?name=", &p);

  assert_true(p.has_name_filter);
  assert_string_equal(p.name_filter, "");
}

static void it_should_preserve_special_characters_in_name_filter(void **state) {
  (void)state;
  struct parsed_query_params p;
  parse_query_params("/api/sessions?name=my-session_01", &p);

  assert_true(p.has_name_filter);
  assert_string_equal(p.name_filter, "my-session_01");
}

static void it_should_ignore_unknown_query_params(void **state) {
  (void)state;
  struct parsed_query_params p;
  parse_query_params("/api/sessions?foo=bar&pageSize=5&unknown=x", &p);

  assert_int_equal(p.page_size, 5);
  assert_int_equal(p.page_number, 1);
  assert_false(p.has_name_filter);
}

static void it_should_let_last_duplicate_param_win(void **state) {
  (void)state;
  struct parsed_query_params p;
  parse_query_params("/api/sessions?pageSize=5&pageSize=30", &p);
  assert_int_equal(p.page_size, 30);
}

/* ==================================================================
 * describe("route resolution")
 * ================================================================== */

static void it_should_route_GET_api_sessions_to_list(void **state) {
  (void)state;
  struct route_result r = route_request("/api/sessions", "GET", "");
  assert_int_equal(r.route, ROUTE_LIST_SESSIONS);
}

static void it_should_route_GET_api_sessions_with_query_to_list(void **state) {
  (void)state;
  struct route_result r = route_request("/api/sessions?pageSize=10", "GET", "");
  assert_int_equal(r.route, ROUTE_LIST_SESSIONS);
}

static void it_should_route_POST_api_sessions_to_create(void **state) {
  (void)state;
  struct route_result r = route_request("/api/sessions", "POST", "");
  assert_int_equal(r.route, ROUTE_CREATE_SESSION);
}

static void it_should_route_DELETE_api_sessions_named_to_delete(void **state) {
  (void)state;
  struct route_result r = route_request("/api/sessions/mysess", "DELETE", "");

  assert_int_equal(r.route, ROUTE_DELETE_SESSION);
  assert_string_equal(r.session_name, "mysess");
}

static void it_should_not_route_DELETE_when_session_name_is_empty(void **state) {
  (void)state;
  struct route_result r = route_request("/api/sessions/", "DELETE", "");
  assert_int_equal(r.route, ROUTE_NOT_FOUND);
}

static void it_should_return_not_found_for_unknown_path(void **state) {
  (void)state;
  struct route_result r = route_request("/api/unknown", "GET", "");
  assert_int_equal(r.route, ROUTE_NOT_FOUND);
}

static void it_should_return_not_found_for_unsupported_method(void **state) {
  (void)state;
  struct route_result r = route_request("/api/sessions", "PUT", "");
  assert_int_equal(r.route, ROUTE_NOT_FOUND);
}

static void it_should_strip_base_path_before_routing(void **state) {
  (void)state;
  struct route_result r = route_request("/base/api/sessions", "GET", "/base");
  assert_int_equal(r.route, ROUTE_LIST_SESSIONS);
}

static void it_should_strip_base_path_for_DELETE_route(void **state) {
  (void)state;
  struct route_result r = route_request("/myapp/api/sessions/foo", "DELETE", "/myapp");

  assert_int_equal(r.route, ROUTE_DELETE_SESSION);
  assert_string_equal(r.session_name, "foo");
}

static void it_should_return_not_found_when_base_path_does_not_match(void **state) {
  (void)state;
  struct route_result r = route_request("/other/api/sessions", "GET", "/base");
  assert_int_equal(r.route, ROUTE_NOT_FOUND);
}

static void it_should_not_match_paths_that_only_share_a_prefix(void **state) {
  (void)state;
  struct route_result r = route_request("/api/sessions_extra", "GET", "");
  assert_int_equal(r.route, ROUTE_NOT_FOUND);
}

/* ==================================================================
 * describe("Basic auth header parsing")
 * ================================================================== */

static void it_should_pass_when_no_credential_is_required(void **state) {
  (void)state;
  assert_int_equal(check_basic_auth(NULL, NULL), MY_AUTH_OK);
}

static void it_should_pass_when_credentials_match(void **state) {
  (void)state;
  assert_int_equal(check_basic_auth("Basic dXNlcjpwYXNz", "dXNlcjpwYXNz"), MY_AUTH_OK);
}

static void it_should_fail_when_credentials_do_not_match(void **state) {
  (void)state;
  assert_int_equal(check_basic_auth("Basic wrong", "dXNlcjpwYXNz"), MY_AUTH_FAIL);
}

static void it_should_fail_when_authorization_header_is_missing(void **state) {
  (void)state;
  assert_int_equal(check_basic_auth(NULL, "dXNlcjpwYXNz"), MY_AUTH_FAIL);
}

static void it_should_fail_when_authorization_header_is_empty(void **state) {
  (void)state;
  assert_int_equal(check_basic_auth("", "dXNlcjpwYXNz"), MY_AUTH_FAIL);
}

static void it_should_fail_when_scheme_is_not_Basic(void **state) {
  (void)state;
  assert_int_equal(check_basic_auth("Bearer token123", "token123"), MY_AUTH_FAIL);
}

static void it_should_fail_when_header_is_too_short(void **state) {
  (void)state;
  assert_int_equal(check_basic_auth("Basic", "x"), MY_AUTH_FAIL);
}

/* ==================================================================
 * describe("Accept-Encoding gzip detection")
 * ================================================================== */

static void it_should_detect_gzip_in_a_multi_value_header(void **state) {
  (void)state;
  assert_true(check_accept_gzip("gzip, deflate, br"));
}

static void it_should_detect_gzip_when_it_is_the_only_value(void **state) {
  (void)state;
  assert_true(check_accept_gzip("gzip"));
}

static void it_should_not_detect_gzip_when_absent(void **state) {
  (void)state;
  assert_false(check_accept_gzip("deflate, br"));
}

static void it_should_not_detect_gzip_in_an_empty_header(void **state) {
  (void)state;
  assert_false(check_accept_gzip(""));
}

static void it_should_not_detect_gzip_when_header_is_null(void **state) {
  (void)state;
  assert_false(check_accept_gzip(NULL));
}

static void it_should_detect_gzip_in_the_middle_of_the_header(void **state) {
  (void)state;
  assert_true(check_accept_gzip("deflate, gzip, br"));
}

/* ==================================================================
 * Test runner — one cmocka group per `describe` block
 * ================================================================== */

int main(void) {
  /* describe: detect_method */
  const struct CMUnitTest detect_method_suite[] = {
    cmocka_unit_test(it_should_default_to_GET_when_no_method_headers_are_present),
    cmocka_unit_test(it_should_return_POST_when_post_uri_header_is_present),
#if defined(LWS_ROLE_H2)
    cmocka_unit_test(it_should_return_DELETE_when_h2_method_header_is_DELETE),
    cmocka_unit_test(it_should_return_PUT_when_h2_method_header_is_PUT),
    cmocka_unit_test(it_should_return_GET_when_h2_method_header_is_explicitly_GET),
    cmocka_unit_test(it_should_prefer_POST_over_h2_method_header),
#endif
  };

  /* describe: query parameter parsing */
  const struct CMUnitTest query_params_suite[] = {
    cmocka_unit_test(it_should_use_defaults_when_no_query_string_is_provided),
    cmocka_unit_test(it_should_apply_pageSize_from_query_string),
    cmocka_unit_test(it_should_apply_pageNumber_from_query_string),
    cmocka_unit_test(it_should_apply_all_supported_params_together),
    cmocka_unit_test(it_should_fallback_to_default_when_pageSize_is_zero),
    cmocka_unit_test(it_should_fallback_to_default_when_pageSize_is_negative),
    cmocka_unit_test(it_should_fallback_to_default_when_pageNumber_is_zero),
    cmocka_unit_test(it_should_fallback_to_default_when_pageNumber_is_negative),
    cmocka_unit_test(it_should_fallback_to_default_when_pageSize_is_non_numeric),
    cmocka_unit_test(it_should_accept_an_empty_name_filter_as_present),
    cmocka_unit_test(it_should_preserve_special_characters_in_name_filter),
    cmocka_unit_test(it_should_ignore_unknown_query_params),
    cmocka_unit_test(it_should_let_last_duplicate_param_win),
  };

  /* describe: route resolution */
  const struct CMUnitTest routing_suite[] = {
    cmocka_unit_test(it_should_route_GET_api_sessions_to_list),
    cmocka_unit_test(it_should_route_GET_api_sessions_with_query_to_list),
    cmocka_unit_test(it_should_route_POST_api_sessions_to_create),
    cmocka_unit_test(it_should_route_DELETE_api_sessions_named_to_delete),
    cmocka_unit_test(it_should_not_route_DELETE_when_session_name_is_empty),
    cmocka_unit_test(it_should_return_not_found_for_unknown_path),
    cmocka_unit_test(it_should_return_not_found_for_unsupported_method),
    cmocka_unit_test(it_should_strip_base_path_before_routing),
    cmocka_unit_test(it_should_strip_base_path_for_DELETE_route),
    cmocka_unit_test(it_should_return_not_found_when_base_path_does_not_match),
    cmocka_unit_test(it_should_not_match_paths_that_only_share_a_prefix),
  };

  /* describe: Basic auth */
  const struct CMUnitTest auth_suite[] = {
    cmocka_unit_test(it_should_pass_when_no_credential_is_required),
    cmocka_unit_test(it_should_pass_when_credentials_match),
    cmocka_unit_test(it_should_fail_when_credentials_do_not_match),
    cmocka_unit_test(it_should_fail_when_authorization_header_is_missing),
    cmocka_unit_test(it_should_fail_when_authorization_header_is_empty),
    cmocka_unit_test(it_should_fail_when_scheme_is_not_Basic),
    cmocka_unit_test(it_should_fail_when_header_is_too_short),
  };

  /* describe: Accept-Encoding */
  const struct CMUnitTest accept_encoding_suite[] = {
    cmocka_unit_test(it_should_detect_gzip_in_a_multi_value_header),
    cmocka_unit_test(it_should_detect_gzip_when_it_is_the_only_value),
    cmocka_unit_test(it_should_not_detect_gzip_when_absent),
    cmocka_unit_test(it_should_not_detect_gzip_in_an_empty_header),
    cmocka_unit_test(it_should_not_detect_gzip_when_header_is_null),
    cmocka_unit_test(it_should_detect_gzip_in_the_middle_of_the_header),
  };

  int failed = 0;
  failed += cmocka_run_group_tests_name("describe: detect_method",
                                        detect_method_suite, NULL, NULL);
  failed += cmocka_run_group_tests_name("describe: query parameter parsing",
                                        query_params_suite, NULL, NULL);
  failed += cmocka_run_group_tests_name("describe: route resolution",
                                        routing_suite, NULL, NULL);
  failed += cmocka_run_group_tests_name("describe: Basic auth header parsing",
                                        auth_suite, NULL, NULL);
  failed += cmocka_run_group_tests_name("describe: Accept-Encoding gzip detection",
                                        accept_encoding_suite, NULL, NULL);
  return failed;
}
