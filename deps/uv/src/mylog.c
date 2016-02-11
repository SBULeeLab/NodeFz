#include "mylog.h"

#include <pthread.h>

#include <stdio.h>
#include <stdarg.h>

pthread_mutex_t log_lock;
void init_log (void)
{
  pthread_mutex_init(&log_lock, NULL);
}

void mylog (const char *format, ...)
{
  char buf1[2048];
  char buf2[2048];

  static int initialized = 0;
  if (!initialized)
  {
    /* Technically racy, I suppose... */
    init_log();
    initialized = 1;
  }

  pid_t my_pid;
  pthread_t my_tid;
  va_list args;
  time_t now;
  char *now_s;

  now = time (NULL);
  now_s = ctime (&now);
  now_s [strlen (now_s) - 1] = '\0'; /* Remove the trailing newline. */
#if 0
  generation = get_generation ();
  indents[0] = '\0';
  for (i = 0; i < generation; i++)
    strncat (indents, "  ", 512);

  my_pid = getpid ();
  printf("%s %s gen %i process %i: ", indents, now_s, generation, my_pid);
#else
  my_pid = getpid();
  my_tid = pthread_self();
  sprintf(buf1, "%s process %i thread %li: ", now_s, my_pid, (long) my_tid);
#endif

  va_start(args, format);
  vsprintf(buf2, format, args);
  va_end(args);

  strcat(buf1, buf2);

#if 0
  pthread_mutex_lock(&log_lock);
  printf(buf1);
  pthread_mutex_unlock(&log_lock);

  fflush(NULL);
#else
#endif
}
