#include "logger.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static log_level_t g_log_level = LOG_NOTICE;
static log_output_t g_log_output = LOG_OUTPUT_STDOUT;
static FILE *g_log_file = NULL;
static char g_log_file_path[256] = "";

static const char *level_names[] = {"DEBUG", "INFO", "NOTICE", "WARN", "ERROR"};

static const char *level_name(log_level_t level) {
  if (level < 0 || level > LOG_ERROR) return "UNKNOWN";
  return level_names[level];
}

void logger_init(void) {
  const char *env_level = getenv("log_level");
  if (env_level != NULL) {
    if (strcmp(env_level, "debug") == 0)
      g_log_level = LOG_DEBUG;
    else if (strcmp(env_level, "info") == 0)
      g_log_level = LOG_INFO;
    else if (strcmp(env_level, "notice") == 0)
      g_log_level = LOG_NOTICE;
    else if (strcmp(env_level, "warn") == 0 || strcmp(env_level, "warning") == 0)
      g_log_level = LOG_WARN;
    else if (strcmp(env_level, "error") == 0 || strcmp(env_level, "err") == 0)
      g_log_level = LOG_ERROR;
  }

  const char *env_output = getenv("log_output");
  const char *env_file = getenv("log_file");
  if (env_output != NULL && strcmp(env_output, "file") == 0 && env_file != NULL) {
    logger_set_output(LOG_OUTPUT_FILE, env_file);
  } else {
    logger_set_output(LOG_OUTPUT_STDOUT, NULL);
  }
}

void logger_destroy(void) {
  if (g_log_file != NULL && g_log_file != stdout) {
    fclose(g_log_file);
    g_log_file = NULL;
  }
}

void logger_set_level(log_level_t level) {
  g_log_level = level;
}

log_level_t logger_get_level(void) {
  return g_log_level;
}

void logger_set_output(log_output_t output, const char *path) {
  if (g_log_file != NULL && g_log_file != stdout) {
    fclose(g_log_file);
    g_log_file = NULL;
  }

  g_log_output = output;
  if (output == LOG_OUTPUT_FILE && path != NULL) {
    strncpy(g_log_file_path, path, sizeof(g_log_file_path) - 1);
    g_log_file_path[sizeof(g_log_file_path) - 1] = '\0';
    g_log_file = fopen(path, "a");
    if (g_log_file == NULL) {
      fprintf(stderr, "logger: failed to open log file: %s\n", path);
      g_log_output = LOG_OUTPUT_STDOUT;
      g_log_file = stdout;
    }
  } else {
    g_log_file = stdout;
    g_log_file_path[0] = '\0';
  }
}

log_output_t logger_get_output(void) {
  return g_log_output;
}

bool logger_is_enabled(log_level_t level) {
  return level >= g_log_level;
}

void logger_log(log_level_t level, const char *tag, const char *fmt, ...) {
  if (!logger_is_enabled(level)) return;

  FILE *out = g_log_file != NULL ? g_log_file : stdout;

  time_t now = time(NULL);
  struct tm *tm_info = localtime(&now);
  char time_buf[32];
  strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

  fprintf(out, "%s [%s] %s: ", time_buf, level_name(level), tag);

  va_list args;
  va_start(args, fmt);
  vfprintf(out, fmt, args);
  va_end(args);

  fprintf(out, "\n");
  fflush(out);
}