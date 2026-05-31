#ifndef TTYD_API_H
#define TTYD_API_H

#include <libwebsockets.h>

struct pss_http;

int api_handle_request(struct lws *wsi, struct pss_http *pss);
void detect_method(struct lws *wsi, char *method_out, size_t method_len);

#endif  // TTYD_API_H
