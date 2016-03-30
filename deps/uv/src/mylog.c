#include "mylog.h"

#include <pthread.h>

#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>

#include "uv-common.h" /* uv__malloc/free */

char log_class_strings[LOG_CLASS_MAX][100] = {
  "MAIN",
  "LCBN",
  "SCHEDULER",
  "THREADPOOL"
};

int verbosity_levels[LOG_CLASS_MAX];

pthread_mutex_t log_lock;
int initialized = 0;

int get_verbosity (enum log_class logClass)
{
  assert(LOG_CLASS_MIN <= logClass && logClass < LOG_CLASS_MAX);
  return verbosity_levels[logClass];
}

void init_log (void)
{
  int i;

  if (initialized)
    return;
  initialized = 1;

  pthread_mutex_init(&log_lock, NULL);

  /* Default verbosity levels. */
  for (i = LOG_CLASS_MIN; i < LOG_CLASS_MAX; i++)
    verbosity_levels[i] = 3;
  /* Print log header. */
  printf("%-10s %-3s %-30s %-7s %-20s %-10s\n", "LOG CLASS", "VOL", "TIME", "PID", "TID", "MESSAGE");
  fflush(NULL);
}

static int log_initialized (void)
{
  return initialized;
}

void set_verbosity (enum log_class logClass, int verbosity)
{
  assert(LOG_CLASS_MIN <= logClass && logClass < LOG_CLASS_MAX);
  verbosity_levels[logClass] = verbosity;
}

void mylog (enum log_class logClass, int verbosity, const char *format, ...)
{
  char buf[2048];
  pid_t my_pid;
  pthread_t my_tid;
  va_list args;

  struct timespec now;
  char now_s[64];
  struct tm t;

  if (get_verbosity(logClass) < verbosity)
    return;

  assert(log_initialized());

  memset(buf, 0, sizeof buf);

  pthread_mutex_lock(&log_lock); /* Monotonically increasing log timestamps. */
  /* Prefix. */
  my_pid = getpid();
  my_tid = pthread_self();

  assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
  localtime_r(&now.tv_sec, &t);

  memset(now_s, 0, sizeof now_s);
  strftime(now_s, sizeof now_s, "%a %b %d %H:%M:%S", &t);
  sprintf(now_s + strlen(now_s), ".%09ld", now.tv_nsec);

  snprintf(buf, 2048, "%-10s %-3i %-32s %-7i %-20li ", log_class_strings[logClass], verbosity, now_s, my_pid, (long) my_tid);

  /* User's statement. */
  va_start(args, format);
  vsnprintf(buf + strlen(buf), 2048 - strlen(buf), format, args);
  va_end(args);

  printf("%s", buf);
  fflush(NULL);

  pthread_mutex_unlock(&log_lock);
}
