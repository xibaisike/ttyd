#ifndef TTYD_SESSION_H
#define TTYD_SESSION_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "pty.h"

enum session_state { SESSION_RUNNING, SESSION_PAUSED };

typedef struct {
  char *data;
  size_t capacity;
  size_t head;
  size_t len;
  size_t total_written;
} ring_buf_t;

enum shell_phase { SHELL_IDLE, SHELL_PROMPT, SHELL_INPUT, SHELL_OUTPUT };

struct pss_tty;

typedef struct session_conn_ {
  struct pss_tty *pss;
  struct session_conn_ *next;
  size_t replay_offset;
  bool replay_done;
} session_conn_t;

typedef struct session_ {
  char name[64];
  pty_process *process;
  enum session_state state;
  ring_buf_t scrollback;
  session_conn_t *connections;
  int connection_count;
  time_t created_at;
  struct session_ *next;
  enum shell_phase shell_phase;
  int last_exit_code;
  size_t output_start_total;
} session_t;

extern session_t *g_sessions;
extern size_t g_scrollback_size;

void session_manager_init(size_t scrollback_size);
void session_manager_destroy(void);

session_t *session_create(const char *name, uint16_t columns, uint16_t rows);
session_t *session_find(const char *name);
void session_close(session_t *session);
session_conn_t *session_attach(session_t *session, struct pss_tty *pss);
void session_detach(session_t *session, struct pss_tty *pss);

void ring_buf_init(ring_buf_t *rb, size_t capacity);
void ring_buf_free(ring_buf_t *rb);
void ring_buf_write(ring_buf_t *rb, const char *data, size_t len);
size_t ring_buf_read(ring_buf_t *rb, size_t offset, char *out, size_t max_len);
void ring_buf_truncate(ring_buf_t *rb, size_t target_len);

#endif  // TTYD_SESSION_H
