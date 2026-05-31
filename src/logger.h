#ifndef TTYD_LOGGER_H
#define TTYD_LOGGER_H

#include <stdbool.h>
#include <stdio.h>

typedef enum {
  LOG_DEBUG = 0,
  LOG_INFO,
  LOG_NOTICE,
  LOG_WARN,
  LOG_ERROR,
} log_level_t;

typedef enum {
  LOG_OUTPUT_STDOUT = 0,
  LOG_OUTPUT_FILE,
} log_output_t;

void logger_init(void);
void logger_destroy(void);

void logger_set_level(log_level_t level);
log_level_t logger_get_level(void);

void logger_set_output(log_output_t output, const char *path);
log_output_t logger_get_output(void);

bool logger_is_enabled(log_level_t level);

void logger_log(log_level_t level, const char *tag, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

#define log_debug(tag, fmt, ...) \
  logger_log(LOG_DEBUG, tag, fmt, ##__VA_ARGS__)
#define log_info(tag, fmt, ...) \
  logger_log(LOG_INFO, tag, fmt, ##__VA_ARGS__)
#define log_notice(tag, fmt, ...) \
  logger_log(LOG_NOTICE, tag, fmt, ##__VA_ARGS__)
#define log_warn(tag, fmt, ...) \
  logger_log(LOG_WARN, tag, fmt, ##__VA_ARGS__)
#define log_error(tag, fmt, ...) \
  logger_log(LOG_ERROR, tag, fmt, ##__VA_ARGS__)

#endif  // TTYD_LOGGER_H