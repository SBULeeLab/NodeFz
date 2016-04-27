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

#include "uv-common.h" /* uv__malloc/uv__free */

/* Global vars. */
char *log_class_strings[] = {
  "MAIN",
  "LCBN",
  "SCHEDULER",
  "THREADPOOL",
  "TIMER",
  "LIST",
  "MAP",
  "TREE",
  "UV_STREAM",
  "UV_IO"
};

int verbosity_levels[LOG_CLASS_MAX] = {
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0
};

uv_mutex_t log_lock;
int initialized = 0;
FILE *output_stream = NULL;

/* Private functions. */

static int log_initialized (void)
{
  return initialized;
}

static int is_log_class_valid (enum log_class log_class)
{
  return (LOG_CLASS_MIN <= log_class && log_class < LOG_CLASS_MAX);
}

static int get_verbosity (enum log_class log_class)
{
  assert(is_log_class_valid(log_class));
  return verbosity_levels[log_class];
}

/* Store a mylog prefix in BUF with space for at least LEN chars.
   If you want monotonically increasing timestamps, hold log_lock before you call. 
   Returns BUF for your chaining convenience. */
pid_t my_pid = -1;
static char * mylog_gen_prefix (enum log_class log_class, int verbosity, char *buf, int len)
{
  long my_tid = (long) uv_thread_self();

  struct timespec now;
  char now_s[64];
  struct tm t;

  if (my_pid == -1)
    my_pid = getpid();

  assert(log_initialized());
  assert(buf);
  assert(is_log_class_valid(log_class));

  assert(clock_gettime(CLOCK_REALTIME, &now) == 0);
  localtime_r(&now.tv_sec, &t);

  memset(now_s, 0, sizeof now_s);
  strftime(now_s, sizeof now_s, "%a %b %d %H:%M:%S", &t);
  snprintf(now_s + strlen(now_s), sizeof(now_s) - strlen(now_s), ".%09ld", now.tv_nsec);

  snprintf(buf, len, "%-10s %-3i %-32s %-7i %-20li ", log_class_strings[log_class], verbosity, now_s, my_pid, (long) my_tid);
  return buf;
}

static void mylog_persistent_print (FILE *stream, char *str)
{
  int amt_printed = 0, amt_remaining = 0;
  char *str_to_print = NULL;

  /* When verbose logging is active, sometimes fprintf returns -1. 
     NB Sometimes it returns -1 while still printing everything but the trailing newline. Beats me why. 
        This can result in output like 'XX\n' where X is a str_to_print. */
  str_to_print = str;
  amt_printed = 0;
  amt_remaining = strlen(str);
  while (amt_remaining)
  {
    amt_printed = fprintf(stream, "%s", str_to_print);
    assert(amt_printed <= amt_remaining);
    if (0 <= amt_printed)
    {
      amt_remaining -= amt_printed;
      str_to_print += amt_printed;
    }
    else
    {
      fprintf(stream, "mylog: Warning, tried to print %u bytes but only managed %i. Trying again. This may result in duplicate output. Problem: %s\n", (unsigned) strlen(str_to_print), amt_printed, strerror(errno));
      fflush(stream); /* Something holding up the stream? */
    }
  }

}

/* Public functions. */

void mylog_init (void)
{
  int i = 0;

  if (initialized)
    return;
  initialized = 1;

  output_stream = stderr;

  uv_mutex_init(&log_lock);

  /* Default verbosity levels. */
  for (i = LOG_CLASS_MIN; i < LOG_CLASS_MAX; i++)
    verbosity_levels[i] = 3;

  /* Print log header. */
  fprintf(output_stream, "%-10s %-3s %-32s %-7s %-20s %-10s\n", "LOG CLASS", "VOL", "TIME", "PID", "TID", "MESSAGE");
  fflush(output_stream);
}

void mylog_set_verbosity (enum log_class log_class, int verbosity)
{
  assert(is_log_class_valid(log_class));
  verbosity_levels[log_class] = verbosity;
}

void mylog_set_all_verbosity (int verbosity)
{
  int i = 0;
  for (i = LOG_CLASS_MIN; i < LOG_CLASS_MAX; i++)
    verbosity_levels[i] = verbosity;
}

static char log_buf[2048]; /* Only access under log_lock. */
void mylog (enum log_class log_class, int verbosity, const char *format, ...)
{
  va_list args;

  assert(log_initialized());
  assert(is_log_class_valid(log_class));
  assert(format);

  if (get_verbosity(log_class) < verbosity)
    return;

  uv_mutex_lock(&log_lock); /* Thread safety; monotonically increasing log timestamps. */

  mylog_gen_prefix(log_class, verbosity, log_buf, sizeof log_buf);

  /* Prep user's message. */
  va_start(args, format);
  vsnprintf(log_buf + strlen(log_buf), sizeof(log_buf) - strlen(log_buf), format, args);
  va_end(args);
  assert(log_buf[strlen(log_buf)-1] == '\n');

  mylog_persistent_print(output_stream, log_buf);

  uv_mutex_unlock(&log_lock);
}

/* Print buf as LEN char's. */
void mylog_buf (enum log_class log_class, int verbosity, char *buf, int len)
{
  int i = 0;

  assert(log_initialized());
  assert(is_log_class_valid(log_class));

  if (get_verbosity(log_class) < verbosity)
    return;

  uv_mutex_lock(&log_lock); /* Thread safety; monotonically increasing log timestamps. */

  mylog_gen_prefix(log_class, verbosity, log_buf, sizeof log_buf);

  /* Print the prefix. */
  mylog_persistent_print(output_stream, log_buf);

  /* Print the buffer itself, one char at a time in case of null bytes. */
  snprintf(log_buf, sizeof log_buf, "mylog_buf: Buffer %p, len %i: <", buf, len);
  mylog_persistent_print(output_stream, log_buf);
  for (i = 0; i < len; i++)
  {
    snprintf(log_buf, sizeof log_buf, "%c", buf[i]);
    mylog_persistent_print(output_stream, log_buf);
  }
  snprintf(log_buf, sizeof log_buf, ">\n");
  mylog_persistent_print(output_stream, log_buf);

  uv_mutex_unlock(&log_lock);
}

/* Unit testing. */

/* For competing logger threads. */
static void mylog_UT_logger (void *arg)
{
  int i = 0;
  enum log_class class = LOG_MAIN;

  mylog(LOG_MAIN, 0, "HELLO WORLD -- mylog_UT: begin\n");

  for (i = 0; i < 100; i++)
  {
    uv_thread_yield();
    for (class = LOG_MAIN; class < LOG_CLASS_MAX; class++)
    {
      mylog(class, 0, "HELLO WORLD\n"); /* Should print. */
      mylog(class, 11, "HELLO WORLD\n"); /* Should not print. */
    }
  }

  mylog(LOG_MAIN, 0, "HELLO WORLD -- mylog_UT_logger: end\n");
}

void mylog_UT (void)
{
  int i, n_threads = 10;
  uv_thread_t threads[10];

  mylog(LOG_MAIN, 0, "mylog_UT: begin\n");

  for (i = 0; i < n_threads; i++)
    assert(!uv_thread_create(threads + i, mylog_UT_logger, NULL));
  for (i = 0; i < n_threads; i++)
    assert(!uv_thread_join(threads + i));

  mylog(LOG_MAIN, 0, "mylog_UT: passed\n");
}
