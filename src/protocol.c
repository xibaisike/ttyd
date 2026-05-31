#include <errno.h>
#include <json.h>
#include <libwebsockets.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "logger.h"
#include "osc133.h"
#include "pty.h"
#include "profile.h"
#include "server.h"
#include "utils.h"
#include "compat.h"

// initial message list
static char initial_cmds[] = {SET_WINDOW_TITLE, SET_PREFERENCES};

static int send_initial_message(struct lws *wsi, int index) {
  unsigned char message[LWS_PRE + 1 + 4096];
  unsigned char *p = &message[LWS_PRE];
  char buffer[128];
  int n = 0;

  char cmd = initial_cmds[index];
  switch (cmd) {
    case SET_WINDOW_TITLE:
      gethostname(buffer, sizeof(buffer) - 1);
      n = snprintf((char *)p, 1 + 4096, "%c%s (%s)", cmd, server->command, buffer);
      break;
    case SET_PREFERENCES:
      n = snprintf((char *)p, 1 + 4096, "%c%s", cmd, server->prefs_json);
      break;
    default:
      break;
  }

  return lws_write(wsi, p, (size_t)n, LWS_WRITE_BINARY);
}

static json_object *parse_window_size(const char *buf, size_t len, uint16_t *cols, uint16_t *rows) {
  json_tokener *tok = json_tokener_new();
  json_object *obj = json_tokener_parse_ex(tok, buf, len);
  struct json_object *o = NULL;

  if (json_object_object_get_ex(obj, "columns", &o)) *cols = (uint16_t)json_object_get_int(o);
  if (json_object_object_get_ex(obj, "rows", &o)) *rows = (uint16_t)json_object_get_int(o);

  json_tokener_free(tok);
  return obj;
}

static bool check_host_origin(struct lws *wsi) {
  char buf[256];
  memset(buf, 0, sizeof(buf));
  int len = lws_hdr_copy(wsi, buf, (int)sizeof(buf), WSI_TOKEN_ORIGIN);
  if (len <= 0) return false;

  const char *prot, *address, *path;
  int port;
  if (lws_parse_uri(buf, &prot, &address, &port, &path)) return false;
  if (port == 80 || port == 443) {
    snprintf(buf, sizeof(buf), "%s", address);
  } else {
    snprintf(buf, sizeof(buf), "%s:%d", address, port);
  }

  char host_buf[256];
  memset(host_buf, 0, sizeof(host_buf));
  len = lws_hdr_copy(wsi, host_buf, (int)sizeof(host_buf), WSI_TOKEN_HOST);

  return len > 0 && strcasecmp(buf, host_buf) == 0;
}

static void osc133_handler(osc133_event_t *event, void *ctx) {
  session_t *session = (session_t *)ctx;
  switch (event->type) {
    case OSC133_A:
      session->shell_phase = SHELL_PROMPT;
      log_debug("[osc133_handler]", "event=A, session=%s, phase=PROMPT", session->name);
      break;
    case OSC133_B:
      session->shell_phase = SHELL_INPUT;
      log_debug("[osc133_handler]", "event=B, session=%s, phase=INPUT", session->name);
      break;
    case OSC133_C:
      session->shell_phase = SHELL_OUTPUT;
      session->output_start_total = session->scrollback.total_written;
      log_debug("[osc133_handler]", "event=C, session=%s, phase=OUTPUT, output_start_total=%zu",
                session->name, session->output_start_total);
      break;
    case OSC133_D:
      session->shell_phase = SHELL_IDLE;
      if (event->exit_code >= 0) session->last_exit_code = event->exit_code;
      log_debug("[osc133_handler]", "event=D, session=%s, phase=IDLE, exit_code=%d, last_exit_code=%d",
                session->name, event->exit_code, session->last_exit_code);
      break;
    case OSC133_R: {
      size_t written_since_c = session->scrollback.total_written - session->output_start_total;
      log_debug("[osc133_handler]",
                "event=R, session=%s, written_since_c=%zu, scrollback_len=%zu, total_written=%zu",
                session->name, written_since_c, session->scrollback.len,
                session->scrollback.total_written);
      if (written_since_c > 0 && written_since_c <= session->scrollback.len) {
        size_t target_len = session->scrollback.len - written_since_c;
        ring_buf_truncate(&session->scrollback, target_len);
        log_debug("[osc133_handler]", "event=R, session=%s, truncated to target_len=%zu",
                  session->name, target_len);
        session_conn_t *c = session->connections;
        while (c != NULL) {
          if (c->replay_offset > target_len) {
            log_debug("[osc133_handler]",
                      "event=R, session=%s, clamp replay_offset: old=%zu, new=%zu",
                      session->name, c->replay_offset, target_len);
            c->replay_offset = target_len;
          }
          c = c->next;
        }
      } else {
        log_debug("[osc133_handler]", "event=R, session=%s, skip truncate (out of range)",
                  session->name);
      }
      break;
    }
  }
}

static pty_ctx_t *pty_ctx_init(session_t *session) {
  pty_ctx_t *ctx = xmalloc(sizeof(pty_ctx_t));
  ctx->session = session;
  ctx->ws_closed = false;
  return ctx;
}

static void pty_ctx_free(pty_ctx_t *ctx) { free(ctx); }

static void process_read_cb(pty_process *process, pty_buf_t *buf, bool eof) {
  pty_ctx_t *ctx = (pty_ctx_t *)process->ctx;
  if (ctx == NULL) {
    pty_buf_free(buf);
    return;
  }
  session_t *session = ctx->session;

  if (eof && !process_running(process)) {
    int status = process->exit_code == 0 ? 1000 : 1006;
    session_conn_t *c = session->connections;
    while (c != NULL) {
      c->pss->lws_close_status = status;
      lws_callback_on_writable(c->pss->wsi);
      c = c->next;
    }
    pty_buf_free(buf);
    return;
  }

  ring_buf_write(&session->scrollback, buf->base, buf->len);
  osc133_scan(buf->base, buf->len, osc133_handler, session);

  session_conn_t *c = session->connections;
  while (c != NULL) {
    if (c->replay_done) {
      pty_buf_t *copy = xmalloc(sizeof(pty_buf_t));
      copy->base = xmalloc(buf->len);
      memcpy(copy->base, buf->base, buf->len);
      copy->len = buf->len;
      c->pss->pty_buf = copy;
      lws_callback_on_writable(c->pss->wsi);
    }
    c = c->next;
  }

  pty_buf_free(buf);
  pty_pause(process);
}

static void process_exit_cb(pty_process *process) {
  pty_ctx_t *ctx = (pty_ctx_t *)process->ctx;
  if (ctx == NULL) return;
  session_t *session = ctx->session;

  lwsl_notice("process exited with code %d, pid: %d\n", process->exit_code, process->pid);

  int status = process->exit_code == 0 ? 1000 : 1006;
  session_conn_t *c = session->connections;
  while (c != NULL) {
    c->pss->lws_close_status = status;
    c->pss->process = NULL;
    lws_callback_on_writable(c->pss->wsi);
    c = c->next;
  }
  session->process = NULL;

  pty_ctx_free(ctx);

  if (force_exit) exit(0);
}

static char **build_args(struct pss_tty *pss) {
  int i, n = 0;
  char **argv = xmalloc((server->argc + pss->argc + 1) * sizeof(char *));

  for (i = 0; i < server->argc; i++) {
    argv[n++] = server->argv[i];
  }

  for (i = 0; i < pss->argc; i++) {
    argv[n++] = pss->args[i];
  }

  argv[n] = NULL;

  return argv;
}

static char **build_env(struct pss_tty *pss) {
  int i = 0, n = 2;
  char **envp = xmalloc(n * sizeof(char *));

  // TERM
  envp[i] = xmalloc(36);
  snprintf(envp[i], 36, "TERM=%s", server->terminal_type);
  i++;

  // TTYD_USER
  if (strlen(pss->user) > 0) {
    envp = xrealloc(envp, (++n) * sizeof(char *));
    envp[i] = xmalloc(40);
    snprintf(envp[i], 40, "TTYD_USER=%s", pss->user);
    i++;
  }

  envp[i] = NULL;

  return envp;
}

static bool spawn_process(struct pss_tty *pss, session_t *session, uint16_t columns, uint16_t rows) {
  pty_process *process = process_init((void *)pty_ctx_init(session), server->loop, build_args(pss), build_env(pss));
  if (server->cwd != NULL) process->cwd = strdup(server->cwd);
  if (columns > 0) process->columns = columns;
  if (rows > 0) process->rows = rows;
  if (pty_spawn(process, process_read_cb, process_exit_cb) != 0) {
    lwsl_err("pty_spawn: %d (%s)\n", errno, strerror(errno));
    process_free(process);
    return false;
  }
  lwsl_notice("started process, pid: %d\n", process->pid);
  session->process = process;
  pss->process = process;

  return true;
}

static void wsi_output(struct lws *wsi, pty_buf_t *buf) {
  if (buf == NULL) return;
  char *message = xmalloc(LWS_PRE + 1 + buf->len);
  char *ptr = message + LWS_PRE;

  *ptr = OUTPUT;
  memcpy(ptr + 1, buf->base, buf->len);
  size_t n = buf->len + 1;

  if (lws_write(wsi, (unsigned char *)ptr, n, LWS_WRITE_BINARY) < (int)n) {
    lwsl_err("write OUTPUT to WS\n");
  }

  free(message);
}

static bool check_auth(struct lws *wsi, struct pss_tty *pss) {
  if (server->auth_header != NULL) {
    return lws_hdr_custom_copy(wsi, pss->user, sizeof(pss->user), server->auth_header, strlen(server->auth_header)) > 0;
  }

  if (server->credential != NULL) {
    char buf[256];
    size_t n = lws_hdr_copy(wsi, buf, sizeof(buf), WSI_TOKEN_HTTP_AUTHORIZATION);
    return n >= 7 && strstr(buf, "Basic ") && !strcmp(buf + 6, server->credential);
  }

  return true;
}

static char *get_query_param(struct lws *wsi, const char *key) {
  char buf[256];
  size_t key_len = strlen(key);
  int n = 0;
  while (lws_hdr_copy_fragment(wsi, buf, sizeof(buf), WSI_TOKEN_HTTP_URI_ARGS, n++) > 0) {
    if (strncmp(buf, key, key_len) == 0 && buf[key_len] == '=') {
      return strdup(buf + key_len + 1);
    }
  }
  return NULL;
}

#define REPLAY_CHUNK_SIZE 4096

int callback_tty(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
  struct pss_tty *pss = (struct pss_tty *)user;
  char buf[256];
  size_t n = 0;

  switch (reason) {
    case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION:
      if (server->once && server->client_count > 0) {
        lwsl_warn("refuse to serve WS client due to the --once option.\n");
        return 1;
      }
      if (server->max_clients > 0 && server->client_count == server->max_clients) {
        lwsl_warn("refuse to serve WS client due to the --max-clients option.\n");
        return 1;
      }
      if (!check_auth(wsi, pss)) return 1;

      n = lws_hdr_copy(wsi, pss->path, sizeof(pss->path), WSI_TOKEN_GET_URI);
#if defined(LWS_ROLE_H2)
      if (n <= 0) n = lws_hdr_copy(wsi, pss->path, sizeof(pss->path), WSI_TOKEN_HTTP_COLON_PATH);
#endif
      if (strncmp(pss->path, endpoints.ws, n) != 0) {
        lwsl_warn("refuse to serve WS client for illegal ws path: %s\n", pss->path);
        return 1;
      }

      if (server->check_origin && !check_host_origin(wsi)) {
        lwsl_warn(
            "refuse to serve WS client from different origin due to the "
            "--check-origin option.\n");
        return 1;
      }
      break;

    case LWS_CALLBACK_ESTABLISHED:
      pss->initialized = false;
      pss->authenticated = false;
      pss->wsi = wsi;
      pss->lws_close_status = LWS_CLOSE_STATUS_NOSTATUS;
      pss->pid = get_query_param(wsi, "pid");
      pss->session = NULL;
      pss->session_conn = NULL;
      profile_init(&pss->profile);

      if (server->url_arg) {
        while (lws_hdr_copy_fragment(wsi, buf, sizeof(buf), WSI_TOKEN_HTTP_URI_ARGS, n++) > 0) {
          if (strncmp(buf, "arg=", 4) == 0) {
            pss->args = xrealloc(pss->args, (pss->argc + 1) * sizeof(char *));
            pss->args[pss->argc] = strdup(&buf[4]);
            pss->argc++;
          }
        }
      }

      server->client_count++;

      lws_get_peer_simple(lws_get_network_wsi(wsi), pss->address, sizeof(pss->address));
      lwsl_notice("WS   %s - %s, clients: %d\n", pss->path, pss->address, server->client_count);
      break;

    case LWS_CALLBACK_SERVER_WRITEABLE:
      if (!pss->initialized) {
        if (pss->initial_cmd_index == sizeof(initial_cmds)) {
          pss->initialized = true;
          if (pss->session != NULL && pss->session->process != NULL)
            pty_resume(pss->session->process);
          lws_callback_on_writable(wsi);
          break;
        }
        profile_begin(&pss->profile, PROFILE_PHASE_INIT);
        if (send_initial_message(wsi, pss->initial_cmd_index) < 0) {
          profile_end(&pss->profile, PROFILE_PHASE_INIT);
          lwsl_err("failed to send initial message, index: %d\n", pss->initial_cmd_index);
          lws_close_reason(wsi, LWS_CLOSE_STATUS_UNEXPECTED_CONDITION, NULL, 0);
          return -1;
        }
        profile_end(&pss->profile, PROFILE_PHASE_INIT);
        pss->initial_cmd_index++;
        lws_callback_on_writable(wsi);
        break;
      }

      // replay phase
      if (pss->session_conn != NULL && !pss->session_conn->replay_done) {
        profile_begin(&pss->profile, PROFILE_PHASE_REPLAY);
        char replay_buf[REPLAY_CHUNK_SIZE];
        size_t bytes = ring_buf_read(&pss->session->scrollback, pss->session_conn->replay_offset, replay_buf, REPLAY_CHUNK_SIZE);
        if (bytes == 0) {
          pss->session_conn->replay_done = true;
          profile_end(&pss->profile, PROFILE_PHASE_REPLAY);
        } else {
          char *message = xmalloc(LWS_PRE + 1 + bytes);
          char *ptr = message + LWS_PRE;
          *ptr = OUTPUT;
          memcpy(ptr + 1, replay_buf, bytes);
          if (lws_write(wsi, (unsigned char *)ptr, bytes + 1, LWS_WRITE_BINARY) < (int)(bytes + 1)) {
            lwsl_err("write replay to WS\n");
          }
          free(message);
          profile_end(&pss->profile, PROFILE_PHASE_REPLAY);
          pss->session_conn->replay_offset += bytes;
          lws_callback_on_writable(wsi);
        }
        break;
      }

      if (pss->lws_close_status > LWS_CLOSE_STATUS_NOSTATUS) {
        lws_close_reason(wsi, pss->lws_close_status, NULL, 0);
        return 1;
      }

      if (pss->pty_buf != NULL) {
        profile_begin(&pss->profile, PROFILE_PHASE_OUTPUT);
        wsi_output(wsi, pss->pty_buf);
        pty_buf_free(pss->pty_buf);
        pss->pty_buf = NULL;
        profile_end(&pss->profile, PROFILE_PHASE_OUTPUT);
        if (pss->session != NULL && pss->session->process != NULL)
          pty_resume(pss->session->process);
      }
      break;

    case LWS_CALLBACK_RECEIVE:
      if (pss->buffer == NULL) {
        pss->buffer = xmalloc(len);
        pss->len = len;
        memcpy(pss->buffer, in, len);
      } else {
        pss->buffer = xrealloc(pss->buffer, pss->len + len);
        memcpy(pss->buffer + pss->len, in, len);
        pss->len += len;
      }

      const char command = pss->buffer[0];

      // check auth
      if (server->credential != NULL && !pss->authenticated && command != JSON_DATA) {
        lwsl_warn("WS client not authenticated\n");
        return 1;
      }

      // check if there are more fragmented messages
      if (lws_remaining_packet_payload(wsi) > 0 || !lws_is_final_fragment(wsi)) {
        return 0;
      }

      switch (command) {
        case INPUT:
          if (!server->writable) break;
          if (pss->session == NULL || pss->session->process == NULL) break;
          int err = pty_write(pss->session->process, pty_buf_init(pss->buffer + 1, pss->len - 1));
          if (err) {
            lwsl_err("uv_write: %s (%s)\n", uv_err_name(err), uv_strerror(err));
            return -1;
          }
          break;
        case RESIZE_TERMINAL:
          if (pss->session == NULL || pss->session->process == NULL) break;
          json_object_put(
              parse_window_size(pss->buffer + 1, pss->len - 1, &pss->session->process->columns, &pss->session->process->rows));
          pty_resize(pss->session->process);
          break;
        case PAUSE:
          if (pss->session != NULL && pss->session->process != NULL)
            pty_pause(pss->session->process);
          break;
        case RESUME:
          if (pss->session != NULL && pss->session->process != NULL)
            pty_resume(pss->session->process);
          break;
        case JSON_DATA: {
          if (pss->session != NULL) break;
          uint16_t columns = 0;
          uint16_t rows = 0;
          json_object *obj = parse_window_size(pss->buffer, pss->len, &columns, &rows);
          if (server->credential != NULL) {
            struct json_object *o = NULL;
            if (json_object_object_get_ex(obj, "AuthToken", &o)) {
              const char *token = json_object_get_string(o);
              if (token != NULL && !strcmp(token, server->credential))
                pss->authenticated = true;
              else
                lwsl_warn("WS authentication failed with token: %s\n", token);
            }
            if (!pss->authenticated) {
              json_object_put(obj);
              lws_close_reason(wsi, LWS_CLOSE_STATUS_POLICY_VIOLATION, NULL, 0);
              return -1;
            }
          }
          json_object_put(obj);

          // look up or create session
          session_t *session = NULL;
          if (pss->pid != NULL) {
            session = session_find(pss->pid);
            if (session == NULL) {
              session = session_create(pss->pid, columns, rows);
              if (session == NULL) {
                lwsl_err("failed to create session: %s\n", pss->pid);
                return 1;
              }
              if (!spawn_process(pss, session, columns, rows)) {
                return 1;
              }
            } else {
              if (session->process == NULL) {
                if (!spawn_process(pss, session, columns, rows)) {
                  return 1;
                }
              }
              pss->process = session->process;
            }
          } else {
            // no pid: generate a name
            char auto_name[64];
            snprintf(auto_name, sizeof(auto_name), "session-%ld", (long)time(NULL));
            session = session_create(auto_name, columns, rows);
            if (session == NULL) return 1;
            if (!spawn_process(pss, session, columns, rows)) return 1;
          }

          pss->session = session;
          pss->session_conn = session_attach(session, pss);
          lws_callback_on_writable(wsi);
          break;
        }
        default:
          lwsl_warn("ignored unknown message type: %c\n", command);
          break;
      }

      if (pss->buffer != NULL) {
        free(pss->buffer);
        pss->buffer = NULL;
      }
      break;

    case LWS_CALLBACK_CLOSED:
      if (pss->wsi == NULL) break;

      profile_log(&pss->profile, pss->address);
      server->client_count--;
      lwsl_notice("WS closed from %s, clients: %d\n", pss->address, server->client_count);
      if (pss->pid != NULL) free(pss->pid);
      if (pss->buffer != NULL) free(pss->buffer);
      if (pss->pty_buf != NULL) pty_buf_free(pss->pty_buf);
      for (int i = 0; i < pss->argc; i++) {
        free(pss->args[i]);
      }

      if (pss->session != NULL) {
        session_detach(pss->session, pss);
      }

      if ((server->once || server->exit_no_conn) && server->client_count == 0) {
        lwsl_notice("exiting due to the --once/--exit-no-conn option.\n");
        lws_cancel_service(context);
        force_exit = true;
        exit(0);
      }
      break;

    default:
      break;
  }

  return 0;
}
