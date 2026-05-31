#include "api.h"

#include <json.h>
#include <libwebsockets.h>
#include <string.h>

#include "server.h"
#include "utils.h"

#ifndef HTTP_STATUS_CREATED
#define HTTP_STATUS_CREATED 201
#endif
#ifndef HTTP_STATUS_SERVICE_UNAVAILABLE
#define HTTP_STATUS_SERVICE_UNAVAILABLE 503
#endif

static int api_send_json(struct lws *wsi, int status, const char *json, size_t json_len) {
  unsigned char buf[LWS_PRE + 4096], *p, *end;
  p = buf + LWS_PRE;
  end = p + sizeof(buf) - LWS_PRE;

  if (lws_add_http_header_status(wsi, (unsigned int)status, &p, end) ||
      lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_CONTENT_TYPE,
                                   (unsigned char *)"application/json", 16, &p, end) ||
      lws_add_http_header_content_length(wsi, (unsigned long)json_len, &p, end) ||
      lws_finalize_http_header(wsi, &p, end) ||
      lws_write(wsi, buf + LWS_PRE, (size_t)(p - (buf + LWS_PRE)), LWS_WRITE_HTTP_HEADERS) < 0)
    return 1;

  if (json_len > 0) {
    if (lws_write(wsi, (unsigned char *)json, json_len, LWS_WRITE_HTTP) < (int)json_len)
      return 1;
  }

  if (lws_http_transaction_completed(wsi)) return -1;
  return 0;
}

static int api_list_sessions(struct lws *wsi, struct pss_http *pss) {
  int page_size = 20;
  int page_number = 1;
  char *name_filter = NULL;

  // parse query params from path
  char *query = strchr(pss->path, '?');
  if (query != NULL) {
    query++;
    char params[512];
    strncpy(params, query, sizeof(params) - 1);
    params[sizeof(params) - 1] = '\0';

    char *pair = strtok(params, "&,");
    while (pair != NULL) {
      if (strncmp(pair, "pageSize=", 9) == 0) {
        page_size = atoi(pair + 9);
        if (page_size <= 0) page_size = 20;
      } else if (strncmp(pair, "pageNumber=", 11) == 0) {
        page_number = atoi(pair + 11);
        if (page_number <= 0) page_number = 1;
      } else if (strncmp(pair, "name=", 5) == 0) {
        name_filter = pair + 5;
      }
      pair = strtok(NULL, "&,");
    }
  }

  json_object *arr = json_object_new_array();
  int total = 0;
  int skip = (page_number - 1) * page_size;

  session_t *s = g_sessions;
  while (s != NULL) {
    if (name_filter != NULL && strstr(s->name, name_filter) == NULL) {
      s = s->next;
      continue;
    }
    if (total >= skip && total < skip + page_size) {
      json_object *obj = json_object_new_object();
      json_object_object_add(obj, "name", json_object_new_string(s->name));
      json_object_object_add(obj, "state", json_object_new_string(
          s->state == SESSION_RUNNING ? "running" : "paused"));
      json_object_object_add(obj, "connections", json_object_new_int(s->connection_count));
      json_object_object_add(obj, "created_at", json_object_new_int64((int64_t)s->created_at));
      json_object_array_add(arr, obj);
    }
    total++;
    s = s->next;
  }

  json_object *result = json_object_new_object();
  json_object_object_add(result, "sessions", arr);
  json_object_object_add(result, "total", json_object_new_int(total));
  json_object_object_add(result, "pageSize", json_object_new_int(page_size));
  json_object_object_add(result, "pageNumber", json_object_new_int(page_number));

  const char *json_str = json_object_to_json_string(result);
  size_t json_len = strlen(json_str);
  int ret = api_send_json(wsi, HTTP_STATUS_OK, json_str, json_len);
  json_object_put(result);
  return ret;
}

static int api_create_session(struct lws *wsi, struct pss_http *pss) {
  if (pss->body_len == 0) {
    const char *err = "{\"error\":\"empty body\"}";
    return api_send_json(wsi, HTTP_STATUS_BAD_REQUEST, err, strlen(err));
  }

  json_tokener *tok = json_tokener_new();
  json_object *obj = json_tokener_parse_ex(tok, pss->body, pss->body_len);
  json_tokener_free(tok);

  if (obj == NULL) {
    const char *err = "{\"error\":\"invalid json\"}";
    return api_send_json(wsi, HTTP_STATUS_BAD_REQUEST, err, strlen(err));
  }

  struct json_object *name_obj = NULL;
  if (!json_object_object_get_ex(obj, "name", &name_obj)) {
    json_object_put(obj);
    const char *err = "{\"error\":\"missing name\"}";
    return api_send_json(wsi, HTTP_STATUS_BAD_REQUEST, err, strlen(err));
  }

  const char *name = json_object_get_string(name_obj);
  if (name == NULL || strlen(name) == 0) {
    json_object_put(obj);
    const char *err = "{\"error\":\"empty name\"}";
    return api_send_json(wsi, HTTP_STATUS_BAD_REQUEST, err, strlen(err));
  }

  if (session_find(name) != NULL) {
    json_object_put(obj);
    const char *err = "{\"error\":\"session already exists\"}";
    return api_send_json(wsi, HTTP_STATUS_CONFLICT, err, strlen(err));
  }

  uint16_t columns = 80, rows = 24;
  struct json_object *o = NULL;
  if (json_object_object_get_ex(obj, "columns", &o)) columns = (uint16_t)json_object_get_int(o);
  if (json_object_object_get_ex(obj, "rows", &o)) rows = (uint16_t)json_object_get_int(o);

  session_t *session = session_create(name, columns, rows);
  json_object_put(obj);

  if (session == NULL) {
    const char *err = "{\"error\":\"failed to create session\"}";
    return api_send_json(wsi, HTTP_STATUS_SERVICE_UNAVAILABLE, err, strlen(err));
  }

  json_object *resp = json_object_new_object();
  json_object_object_add(resp, "name", json_object_new_string(session->name));
  json_object_object_add(resp, "state", json_object_new_string("running"));
  json_object_object_add(resp, "created_at", json_object_new_int64((int64_t)session->created_at));

  const char *json_str = json_object_to_json_string(resp);
  size_t json_len = strlen(json_str);
  int ret = api_send_json(wsi, HTTP_STATUS_CREATED, json_str, json_len);
  json_object_put(resp);
  return ret;
}

static int api_delete_session(struct lws *wsi, const char *name) {
  session_t *session = session_find(name);
  if (session == NULL) {
    const char *err = "{\"error\":\"session not found\"}";
    return api_send_json(wsi, HTTP_STATUS_NOT_FOUND, err, strlen(err));
  }

  session_close(session);

  const char *ok = "{\"status\":\"ok\"}";
  return api_send_json(wsi, HTTP_STATUS_OK, ok, strlen(ok));
}

void detect_method(struct lws *wsi, char *method_out, size_t method_len) {
  if (method_out == NULL || method_len == 0) return;

  // Default method is GET
  strncpy(method_out, "GET", method_len - 1);
  method_out[method_len - 1] = '\0';

  if (lws_hdr_total_length(wsi, WSI_TOKEN_POST_URI) > 0) {
    strncpy(method_out, "POST", method_len - 1);
    method_out[method_len - 1] = '\0';
  } else if (lws_hdr_total_length(wsi, WSI_TOKEN_DELETE_URI) > 0) {
    strncpy(method_out, "DELETE", method_len - 1);
    method_out[method_len - 1] = '\0';
  } else if (lws_hdr_total_length(wsi, WSI_TOKEN_PUT_URI) > 0) {
    strncpy(method_out, "PUT", method_len - 1);
    method_out[method_len - 1] = '\0';
  } else if (lws_hdr_total_length(wsi, WSI_TOKEN_PATCH_URI) > 0) {
    strncpy(method_out, "PATCH", method_len - 1);
    method_out[method_len - 1] = '\0';
  } else {
    char colon_method[16] = "";
    if (lws_hdr_copy(wsi, colon_method, sizeof(colon_method), WSI_TOKEN_HTTP_COLON_METHOD) > 0) {
      strncpy(method_out, colon_method, method_len - 1);
      method_out[method_len - 1] = '\0';
    }
  }
}


int api_handle_request(struct lws *wsi, struct pss_http *pss) {
  // determine path without query string
  char path[128];
  strip_query(path, pss->path, sizeof(path));

  // determine HTTP method
  // lws stores the request URI in a token named for the method (GET_URI,
  // POST_URI, DELETE_URI, etc). detect_method inspects those tokens and
  // falls back to WSI_TOKEN_HTTP_COLON_METHOD for HTTP/2 requests.
  char method[16];
  detect_method(wsi, method, sizeof(method));

  // determine query params for logging
  const char *params = strchr(pss->path, '?');
  if (params != NULL) params++;

  lwsl_notice("[api_handle_request] path=%s, method=%s, params=%s\n",
              path, method, params ? params : "");

  // find base api path — strip base path prefix
  const char *api_path = path;
  size_t parent_len = strlen(endpoints.parent);
  if (parent_len > 0 && strncmp(path, endpoints.parent, parent_len) == 0) {
    api_path = path + parent_len;
  }

  // route: GET /api/sessions
  if (strcmp(method, "GET") == 0 && strncmp(api_path, "/api/sessions", 13) == 0 &&
      (api_path[13] == '\0' || api_path[13] == '?')) {
    return api_list_sessions(wsi, pss);
  }

  // route: POST /api/sessions
  if (strcmp(method, "POST") == 0 && strcmp(api_path, "/api/sessions") == 0) {
    return api_create_session(wsi, pss);
  }

  // route: DELETE /api/sessions/{name}
  if (strcmp(method, "DELETE") == 0 && strncmp(api_path, "/api/sessions/", 14) == 0) {
    const char *name = api_path + 14;
    if (strlen(name) > 0) {
      return api_delete_session(wsi, name);
    }
  }

  const char *err = "{\"error\":\"not found\"}";
  return api_send_json(wsi, HTTP_STATUS_NOT_FOUND, err, strlen(err));
}
