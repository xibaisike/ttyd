#ifndef TTYD_OSC133_H
#define TTYD_OSC133_H

#include <stddef.h>

typedef enum { OSC133_A, OSC133_B, OSC133_C, OSC133_D, OSC133_R } osc133_type_t;

typedef struct {
  osc133_type_t type;
  int exit_code;
} osc133_event_t;

typedef void (*osc133_cb)(osc133_event_t *event, void *ctx);

void osc133_scan(const char *buf, size_t len, osc133_cb cb, void *ctx);

#endif
