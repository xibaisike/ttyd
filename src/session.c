#include "session.h"

#include <string.h>
#include <time.h>

#include "logger.h"
#include "server.h"
#include "utils.h"

session_t *g_sessions = NULL;
size_t g_scrollback_size = 1048576;

void session_manager_init(size_t scrollback_size) {
  g_scrollback_size = scrollback_size;
  g_sessions = NULL;
}

void session_manager_destroy(void) {
  session_t *s = g_sessions;
  while (s != NULL) {
    session_t *next = s->next;
    if (s->process != NULL && process_running(s->process)) {
      pty_kill(s->process, server->sig_code);
    }
    ring_buf_free(&s->scrollback);
    session_conn_t *c = s->connections;
    while (c != NULL) {
      session_conn_t *cn = c->next;
      free(c);
      c = cn;
    }
    free(s);
    s = next;
  }
  g_sessions = NULL;
}

session_t *session_create(const char *name, uint16_t columns, uint16_t rows) {
  if (session_find(name) != NULL) return NULL;

  session_t *s = xmalloc(sizeof(session_t));
  memset(s, 0, sizeof(session_t));
  strncpy(s->name, name, sizeof(s->name) - 1);
  s->state = SESSION_RUNNING;
  s->shell_phase = SHELL_IDLE;
  s->last_exit_code = -1;
  s->output_start_total = 0;
  s->created_at = time(NULL);
  ring_buf_init(&s->scrollback, g_scrollback_size);

  s->next = g_sessions;
  g_sessions = s;

  return s;
}

session_t *session_find(const char *name) {
  session_t *s = g_sessions;

  while (s != NULL) {
    log_debug("[session_find]", "compare: target=%s, current=%s", name, s->name);
    if (strcmp(s->name, name) == 0) {
      log_debug("[session_find]", "found: name=%s", s->name);
      return s;
    }
    s = s->next;
  }
  log_debug("[session_find]", "not found: target=%s", name);
  return NULL;
}

void session_close(session_t *session) {
  if (session->process != NULL) {
    if (session->process->ctx != NULL) {
      free(session->process->ctx);
      session->process->ctx = NULL;
    }
    if (process_running(session->process)) {
      pty_kill(session->process, server->sig_code);
    }
  }

  session_conn_t *c = session->connections;
  while (c != NULL) {
    c->pss->lws_close_status = 1000;
    lws_callback_on_writable(c->pss->wsi);
    c = c->next;
  }

  // remove from global list
  session_t **pp = &g_sessions;
  while (*pp != NULL) {
    if (*pp == session) {
      *pp = session->next;
      break;
    }
    pp = &(*pp)->next;
  }

  ring_buf_free(&session->scrollback);
  c = session->connections;
  while (c != NULL) {
    session_conn_t *cn = c->next;
    free(c);
    c = cn;
  }
  free(session);
}

session_conn_t *session_attach(session_t *session, struct pss_tty *pss) {
  session_conn_t *conn = xmalloc(sizeof(session_conn_t));
  conn->pss = pss;
  conn->next = session->connections;
  conn->replay_offset = 0;
  conn->replay_done = (session->scrollback.len == 0);
  session->connections = conn;
  session->connection_count++;

  if (session->state == SESSION_PAUSED && session->process != NULL) {
    pty_resume(session->process);
    session->state = SESSION_RUNNING;
  }

  return conn;
}

void session_detach(session_t *session, struct pss_tty *pss) {
  session_conn_t **pp = &session->connections;
  while (*pp != NULL) {
    if ((*pp)->pss == pss) {
      session_conn_t *c = *pp;
      *pp = c->next;
      free(c);
      session->connection_count--;
      break;
    }
    pp = &(*pp)->next;
  }

  if (session->connection_count == 0 && session->process != NULL && process_running(session->process)) {
    pty_pause(session->process);
    session->state = SESSION_PAUSED;
  }
}

// Ring buffer implementation

void ring_buf_init(ring_buf_t *rb, size_t capacity) {
  rb->data = xmalloc(capacity);
  rb->capacity = capacity;
  rb->head = 0;
  rb->len = 0;
  rb->total_written = 0;
}

void ring_buf_free(ring_buf_t *rb) {
  if (rb->data != NULL) {
    free(rb->data);
    rb->data = NULL;
  }
  rb->capacity = 0;
  rb->head = 0;
  rb->len = 0;
}

void ring_buf_write(ring_buf_t *rb, const char *data, size_t len) {
  if (len == 0 || rb->capacity == 0) return;

  if (len >= rb->capacity) {
    memcpy(rb->data, data + len - rb->capacity, rb->capacity);
    rb->head = 0;
    rb->len = rb->capacity;
    return;
  }

  size_t first = rb->capacity - rb->head;
  if (first >= len) {
    memcpy(rb->data + rb->head, data, len);
  } else {
    memcpy(rb->data + rb->head, data, first);
    memcpy(rb->data, data + first, len - first);
  }
  rb->head = (rb->head + len) % rb->capacity;
  rb->len += len;
  if (rb->len > rb->capacity) rb->len = rb->capacity;
  rb->total_written += len;
}

size_t ring_buf_read(ring_buf_t *rb, size_t offset, char *out, size_t max_len) {
  if (offset >= rb->len) return 0;

  size_t available = rb->len - offset;
  size_t to_read = available < max_len ? available : max_len;

  // start position: logical start of buffer is at (head - len) mod capacity
  size_t start = (rb->head + rb->capacity - rb->len + offset) % rb->capacity;

  size_t first = rb->capacity - start;
  if (first >= to_read) {
    memcpy(out, rb->data + start, to_read);
  } else {
    memcpy(out, rb->data + start, first);
    memcpy(out + first, rb->data, to_read - first);
  }

  return to_read;
}

void ring_buf_truncate(ring_buf_t *rb, size_t target_len) {
  if (target_len >= rb->len) return;
  size_t discard = rb->len - target_len;
  rb->head = (rb->head + rb->capacity - discard) % rb->capacity;
  rb->len = target_len;
  rb->total_written -= discard;
}
