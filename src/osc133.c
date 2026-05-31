#include "osc133.h"

#include <stdlib.h>
#include <string.h>

// Match ESC ] 133 ; at position i, return index after the semicolon or 0 if no match.
static size_t match_osc_prefix(const char *buf, size_t len, size_t i) {
  if (i + 5 > len) return 0;
  if (buf[i] == '\x1b' && buf[i + 1] == ']' &&
      buf[i + 2] == '1' && buf[i + 3] == '3' && buf[i + 4] == '3' &&
      (i + 5 < len && buf[i + 5] == ';'))
    return i + 6;
  return 0;
}

// Find string terminator (BEL or ESC \) starting at pos.
// Returns index after the terminator, or 0 if not found.
static size_t find_st(const char *buf, size_t len, size_t pos) {
  for (size_t i = pos; i < len; i++) {
    if (buf[i] == '\x07') return i + 1;
    if (buf[i] == '\x1b' && i + 1 < len && buf[i + 1] == '\\') return i + 2;
  }
  return 0;
}

void osc133_scan(const char *buf, size_t len, osc133_cb cb, void *ctx) {
  size_t i = 0;
  while (i < len) {
    if (buf[i] != '\x1b') { i++; continue; }

    size_t after_prefix = match_osc_prefix(buf, len, i);
    if (after_prefix == 0 || after_prefix >= len) { i++; continue; }

    char type_char = buf[after_prefix];
    size_t params_start = after_prefix + 1;

    size_t end = find_st(buf, len, params_start);
    if (end == 0) { i++; continue; }

    osc133_event_t event;
    event.exit_code = -1;

    switch (type_char) {
      case 'A': event.type = OSC133_A; break;
      case 'B': event.type = OSC133_B; break;
      case 'C': event.type = OSC133_C; break;
      case 'D':
        event.type = OSC133_D;
        if (params_start < end && buf[params_start] == ';') {
          char num_buf[16];
          size_t st_pos = end;
          if (buf[end - 1] == '\x07') st_pos = end - 1;
          else if (end >= 2 && buf[end - 2] == '\x1b') st_pos = end - 2;
          size_t num_len = st_pos - (params_start + 1);
          if (num_len > 0 && num_len < sizeof(num_buf)) {
            memcpy(num_buf, buf + params_start + 1, num_len);
            num_buf[num_len] = '\0';
            event.exit_code = atoi(num_buf);
          }
        }
        break;
      case 'R': event.type = OSC133_R; break;
      default: i = end; continue;
    }

    cb(&event, ctx);
    i = end;
  }
}
