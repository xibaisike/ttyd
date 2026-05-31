#include "profile.h"

#include <string.h>

#include "logger.h"

static const char *phase_names[PROFILE_PHASE_COUNT] = {
    "init",
    "replay",
    "output",
};

static const char *gap_names[PROFILE_GAP_COUNT] = {
    "init->replay",
    "replay->output",
};

// Returns elapsed microseconds between two timespecs.
static uint64_t elapsed_us(const struct timespec *start, const struct timespec *end) {
  int64_t sec  = (int64_t)end->tv_sec  - (int64_t)start->tv_sec;
  int64_t nsec = (int64_t)end->tv_nsec - (int64_t)start->tv_nsec;
  int64_t us   = sec * 1000000LL + nsec / 1000LL;
  return us > 0 ? (uint64_t)us : 0;
}

void profile_init(profile_t *p) {
  memset(p, 0, sizeof(*p));
}

void profile_begin(profile_t *p, profile_phase_t phase) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  // Record gap from the previous phase if this is the first invocation.
  if (phase == PROFILE_PHASE_REPLAY && p->phases[PROFILE_PHASE_REPLAY].count == 0) {
    profile_gap_stat_t *g = &p->gaps[PROFILE_GAP_INIT_TO_REPLAY];
    if (!g->measured && p->phases[PROFILE_PHASE_INIT].count > 0) {
      g->gap_us = elapsed_us(&g->end_ts, &now);
      g->measured = true;
    }
  } else if (phase == PROFILE_PHASE_OUTPUT && p->phases[PROFILE_PHASE_OUTPUT].count == 0) {
    profile_gap_stat_t *g = &p->gaps[PROFILE_GAP_REPLAY_TO_OUTPUT];
    if (!g->measured && p->phases[PROFILE_PHASE_REPLAY].count > 0) {
      g->gap_us = elapsed_us(&g->end_ts, &now);
      g->measured = true;
    }
  }

  p->phases[phase].start = now;
}

void profile_end(profile_t *p, profile_phase_t phase) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  profile_phase_stat_t *s = &p->phases[phase];
  uint64_t us = elapsed_us(&s->start, &now);

  s->total_us += us;
  s->count++;
  if (us > s->max_us) s->max_us = us;

  // Save the end timestamp for gap measurement to the next phase.
  if (phase == PROFILE_PHASE_INIT) {
    p->gaps[PROFILE_GAP_INIT_TO_REPLAY].end_ts = now;
  } else if (phase == PROFILE_PHASE_REPLAY) {
    p->gaps[PROFILE_GAP_REPLAY_TO_OUTPUT].end_ts = now;
  }
}

void profile_log(const profile_t *p, const char *tag) {
  for (int i = 0; i < PROFILE_PHASE_COUNT; i++) {
    const profile_phase_stat_t *s = &p->phases[i];
    if (s->count == 0) continue;
    uint64_t avg_us = s->total_us / s->count;
    log_info(tag, "phase=%-16s count=%-6llu total=%lluus avg=%lluus max=%lluus",
              phase_names[i],
              (unsigned long long)s->count,
              (unsigned long long)s->total_us,
              (unsigned long long)avg_us,
              (unsigned long long)s->max_us);
  }
  for (int i = 0; i < PROFILE_GAP_COUNT; i++) {
    const profile_gap_stat_t *g = &p->gaps[i];
    if (!g->measured) continue;
    log_info(tag, "gap=%-18s wait=%lluus (%.1fms)",
              gap_names[i],
              (unsigned long long)g->gap_us,
              (double)g->gap_us / 1000.0);
  }
}

void profile_reset(profile_t *p) {
  memset(p, 0, sizeof(*p));
}
