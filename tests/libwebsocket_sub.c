/*
 * Stubs for libwebsockets symbols used by src/*.c during unit tests.
 *
 * These stubs replace the real libwebsockets implementations so test
 * binaries (e.g. test_api) can link without pulling in the full
 * libwebsockets library. Where a behavior must be observable from a
 * test, the stub uses cmocka's mock() / will_return() machinery.
 *
 * Conventions:
 *   - lws_hdr_copy:
 *       will_return(lws_hdr_copy, "string") → returns positive length, fills dest
 *       will_return(lws_hdr_copy, NULL)     → returns -1 (header not present)
 *   - All other functions are no-op stubs returning success.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <stdlib.h>
#include <cmocka.h>

#include <libwebsockets.h>

int lws_hdr_copy(struct lws *wsi, char *dest, int len, enum lws_token_indexes h) {
  (void)wsi;
  (void)h;
  char *value = mock_ptr_type(char *);
  if (value == NULL) return -1;
  int slen = (int)strlen(value);
  if (slen >= len) slen = len - 1;
  memcpy(dest, value, slen);
  dest[slen] = '\0';
  return slen;
}

int lws_add_http_header_status(struct lws *wsi, unsigned int code,
                               unsigned char **p, unsigned char *end) {
  (void)wsi; (void)code; (void)p; (void)end;
  return 0;
}

int lws_add_http_header_by_token(struct lws *wsi, enum lws_token_indexes token,
                                 const unsigned char *value, int len,
                                 unsigned char **p, unsigned char *end) {
  (void)wsi; (void)token; (void)value; (void)len; (void)p; (void)end;
  return 0;
}

int lws_add_http_header_content_length(struct lws *wsi, lws_filepos_t len,
                                       unsigned char **p, unsigned char *end) {
  (void)wsi; (void)len; (void)p; (void)end;
  return 0;
}

int lws_finalize_http_header(struct lws *wsi, unsigned char **p, unsigned char *end) {
  (void)wsi; (void)p; (void)end;
  return 0;
}

int lws_write(struct lws *wsi, unsigned char *buf, size_t len, enum lws_write_protocol wp) {
  (void)wsi; (void)buf; (void)wp;
  return (int)len;
}

int lws_http_transaction_completed(struct lws *wsi) {
  (void)wsi;
  return 0;
}

void _lws_log(int filter, const char *format, ...) {
  (void)filter; (void)format;
}

int lws_hdr_custom_length(struct lws *wsi, const char *name, int nlen) {
  (void)wsi; (void)name; (void)nlen;
  return 0;
}

int
lws_hdr_total_length(struct lws *wsi, enum lws_token_indexes h) {
  return 0;
}

int lws_callback_on_writable(struct lws *wsi) {
  (void)wsi;
  return 0;
}