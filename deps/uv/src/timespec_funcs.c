#include "timespec_funcs.h"
#include <assert.h>

/* RES = STOP - START. 
   STOP must be after START. */
void timespec_sub (const struct timespec *stop, const struct timespec *start, struct timespec *res)
{
  assert(stop);
  assert(start);
  assert(res);

  /* START < STOP-> */
  assert(start->tv_sec < stop->tv_sec || start->tv_nsec <= stop->tv_nsec);

  if (stop->tv_nsec < start->tv_nsec)
  {
    /* Borrow. */
    res->tv_nsec = 1000000000 + stop->tv_nsec - start->tv_nsec; /* Inline to avoid overflow. */
    res->tv_sec = stop->tv_sec - start->tv_sec - 1;
  }
  else
  {
    res->tv_nsec = stop->tv_nsec - start->tv_nsec;
    res->tv_sec = stop->tv_sec - start->tv_sec;
  }

  return;
}

long timespec_us (const struct timespec *ts)
{
  assert(ts != NULL);
  /* Convert to ns, then to us. */
  return ((long) ts->tv_sec*1000000000 + (long) ts->tv_nsec)/1000;
}
