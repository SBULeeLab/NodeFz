/* Jamie Davis <davisjam@vt.edu>
   Simple test program to:
    - first, demonstrate that after fork the child has only one thread
    - second, try out pthread_atfork and demonstrate its use in re-starting threads */

#include <stddef.h> /* NULL */
#include <stdlib.h>
#include <string.h> /* strerror */
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <limits.h> /* INT_MAX, PATH_MAX, IOV_MAX */
#include <sys/uio.h> /* writev */
#include <sys/resource.h> /* getrusage */
#include <pwd.h>
#include <stdlib.h> /* Extra? */
#include <stdio.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include "sys/param.h"
#include <string.h>
#include <pthread.h>

#define N_THREADS 10

int n_active_threads = 0;
int counter = 0;

/* Declarations. */
void * thread_routine (void *);
void child_after_fork (void);

int main ()
{
  int i;

  pthread_atfork (NULL, NULL, child_after_fork);

  /* Launch N_THREADS threads. They increment global var counter. */
  pthread_attr_t attr;
  pthread_attr_init (&attr);
  pthread_t tinfo[N_THREADS];
  for (i = 0; i < N_THREADS; i++)
  {
    pthread_create (&tinfo[i], &attr, thread_routine, NULL);
    /* Wait for this thread to start. */
    while (n_active_threads <= i)
      sched_yield ();
  }

  printf ("Launched %i threads\n", N_THREADS);

  printf ("before sleep: counter: %i\n", counter);
  usleep (1000);
  printf ("after sleep: counter: %i\n", counter);

  pid_t child = fork ();
  if (child == 0)
  {
    child = fork ();
    printf ("Child: before sleep: counter: %i\n", counter);
    usleep (5000);
    printf ("Child: after sleep: counter: %i\n", counter);
    exit (0);
  }
  else
  {
    for (;;)
    {
      int status;
      int rc = waitpid (child, &status, 0);
      if (rc == child)
      {
        if (WIFEXITED (status))
          printf ("Child exited normally with status %i\n", WEXITSTATUS (status));
        else
          printf ("Child exited with signal? %i (%i)\n", WIFSIGNALED (status), WTERMSIG (status));
        break;
      }
      else
      {
        printf ("Error, waitpid gave %i: %s\n", rc, strerror(errno));
      }

    }
  }

  printf ("Parent: before sleep: counter: %i\n", counter);
  usleep (5000);
  printf ("Parent: after sleep: counter: %i\n", counter);

  printf ("Parent returning\n");
  return 0;
}

/* Definitions. */

void * thread_routine (void *args)
{
  n_active_threads++;
  //printf ("thread_routine: Hello world from thread %lu (n_active_threads %i)\n", pthread_self (), n_active_threads);
  for (;;)
    counter++;
}

void child_after_fork (void)
{
  int i;

  printf ("child_after_fork: Recovering from fork ()\n");

  /* Launch N_THREADS threads. They increment global var counter. */
  pthread_attr_t attr;
  pthread_attr_init (&attr);
  pthread_t tinfo[N_THREADS];
  for (i = 0; i < N_THREADS; i++)
  {
    pthread_create (&tinfo[i], &attr, thread_routine, NULL);
    /* Wait for this thread to start. */
    while (n_active_threads <= i)
      sched_yield ();
  }

  printf ("child_after_fork: Child: Launched %i threads\n", N_THREADS);
}

