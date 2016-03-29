#include "mylog.h"

#include <pthread.h>

#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

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
  char buf1[2048];
  char buf2[2048];

  if (get_verbosity(logClass) < verbosity)
    return;

  assert(log_initialized());

  pid_t my_pid;
  pthread_t my_tid;
  va_list args;
  time_t now;
  char *now_s;

  /* Boilerplate into buf1. */
  now = time (NULL);
  now_s = ctime (&now);
  now_s[strlen (now_s) - 1] = '\0'; /* Remove the trailing newline. */
  my_pid = getpid();
  my_tid = pthread_self();
  sprintf(buf1, "%-10s %-3i %-30s %-7i %-20li ", log_class_strings[logClass], verbosity, now_s, my_pid, (long) my_tid);

  /* printf into buf2. */
  va_start(args, format);
  vsprintf(buf2, format, args);
  va_end(args);

  strcat(buf1, buf2);

  pthread_mutex_lock(&log_lock);
  printf("%s", buf1);
  pthread_mutex_unlock(&log_lock);

  fflush(NULL);
}
