#ifndef TTYD_PROFILE_H
#define TTYD_PROFILE_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

// Phases measured inside LWS_CALLBACK_SERVER_WRITEABLE
typedef enum {
  PROFILE_PHASE_INIT = 0,   // sending initial messages (window title, prefs)
  PROFILE_PHASE_REPLAY,     // replaying scrollback history to client
  PROFILE_PHASE_OUTPUT,     // forwarding live PTY output to client
  PROFILE_PHASE_COUNT,
} profile_phase_t;

// Gaps between consecutive phases (indexed by the earlier phase)
typedef enum {
  PROFILE_GAP_INIT_TO_REPLAY = 0,   // time from last INIT end to first REPLAY begin
  PROFILE_GAP_REPLAY_TO_OUTPUT,     // time from last REPLAY end to first OUTPUT begin
  PROFILE_GAP_COUNT,
} profile_gap_t;

typedef struct {
  struct timespec start;    // wall-clock start of the most recent invocation
  uint64_t total_us;        // cumulative duration in microseconds
  uint64_t count;           // number of completed invocations
  uint64_t max_us;          // maximum single-invocation duration
} profile_phase_stat_t;

typedef struct {
  struct timespec end_ts;   // timestamp when the earlier phase last ended
  uint64_t gap_us;          // elapsed time until the later phase first began
  bool measured;            // gap has been recorded
} profile_gap_stat_t;

typedef struct {
  profile_phase_stat_t phases[PROFILE_PHASE_COUNT];
  profile_gap_stat_t   gaps[PROFILE_GAP_COUNT];
} profile_t;

// Initialise all counters to zero.
void profile_init(profile_t *p);

// Mark the start of a phase invocation (call at the top of the phase block).
void profile_begin(profile_t *p, profile_phase_t phase);

// Mark the end of a phase invocation and accumulate the duration.
void profile_end(profile_t *p, profile_phase_t phase);

// Emit a summary of all phases and gaps via the custom logger (LOG_INFO).
// tag is used as the logger tag (e.g. the client address).
void profile_log(const profile_t *p, const char *tag);

// Reset all counters.
void profile_reset(profile_t *p);

#endif  // TTYD_PROFILE_H
