/* Jamie Davis <davisjam@vt.edu>
   Simple test program to try out fork-wait */

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


int main ()
{
  pid_t child = fork ();
  if (child == 0)
  {
    int exit_status = 0;
    printf ("Child exiting %i\n", exit_status);
    exit (exit_status);
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

  printf ("Parent returning\n");
  return 0;
}
