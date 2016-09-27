#include "scheduler_TP_Freedom.h"
#include "scheduler.h"
#include "timespec_funcs.h"

#include <unistd.h> /* usleep, unlink */
#include <string.h> /* memcpy */
#include <stdlib.h> /* srand, rand, getenv */
#include <time.h>   /* time */
#include <assert.h>

#define MIN(x, y) ((x) < (y) ? (x) : (y))

static int SCHEDULER_TP_FREEDOM_MAGIC = 81929393;

/* implDetails for the fuzzing timer scheduler. */

static struct
{
  int magic;

  int mode;
  scheduler_tp_freedom_args_t args;

  int looper_in_epoll; /* Non-zero if looper thread is between SCHEDULE_POINT_LOOPER_BEFORE_EPOLL and AFTER_EPOLL. */
  struct timespec looper_epoll_start_time; /* If looper_in_epoll, this is when looper reached SCHEDULE_POINT_LOOPER_BEFORE_EPOLL. */
} tpFreedom_implDetails;

/***********************
 * Private API declarations
 ***********************/

/* Returns non-zero if the scheduler_tp_freedom looks valid (e.g. is initialized properly). */
int scheduler_tp_freedom__looks_valid (void);

/* Returns the queue len of q. */
int scheduler_tp_freedom__queue_len (QUEUE *q);

/***********************
 * Public API definitions
 ***********************/

void
scheduler_tp_freedom_init (scheduler_mode_t mode, void *args, schedulerImpl_t *schedulerImpl)
{
  const char *tpSize = NULL;

  assert(args != NULL);
  assert(schedulerImpl != NULL);

  /* The TP Freedom scheduler simulates a multi-thread TP using a single TP thread. */
  tpSize = getenv("UV_THREADPOOL_SIZE");
  assert(tpSize != NULL && atoi(tpSize) == 1);

  srand(time(NULL));

  /* Populate schedulerImpl. */
  schedulerImpl->register_lcbn = scheduler_tp_freedom_register_lcbn;
  schedulerImpl->next_lcbn_type = scheduler_tp_freedom_next_lcbn_type;
  schedulerImpl->thread_yield = scheduler_tp_freedom_thread_yield;
  schedulerImpl->emit = scheduler_tp_freedom_emit;
  schedulerImpl->lcbns_remaining = scheduler_tp_freedom_lcbns_remaining;
  schedulerImpl->schedule_has_diverged = scheduler_tp_freedom_schedule_has_diverged;

  /* Set implDetails. */
  memset(&tpFreedom_implDetails, 0, sizeof tpFreedom_implDetails);
  tpFreedom_implDetails.magic = SCHEDULER_TP_FREEDOM_MAGIC;
  tpFreedom_implDetails.mode = mode;
  tpFreedom_implDetails.args = *(scheduler_tp_freedom_args_t *) args;

  return;
}

void
scheduler_tp_freedom_register_lcbn (lcbn_t *lcbn)
{
  assert(scheduler_tp_freedom__looks_valid());
  assert(lcbn != NULL && lcbn_looks_valid(lcbn));

  return;
}

enum callback_type
scheduler_tp_freedom_next_lcbn_type (void)
{
  assert(scheduler_tp_freedom__looks_valid());
  return CALLBACK_TYPE_ANY;
}

void
scheduler_tp_freedom_thread_yield (schedule_point_t point, void *pointDetails)
{
  assert(scheduler_tp_freedom__looks_valid());
  /* Ensure {point, pointDetails} are consistent. Afterwards we know the inputs are correct. */
  assert(schedule_point_looks_valid(point, pointDetails));

  if (point == SCHEDULE_POINT_TP_WANTS_WORK)
  {
    /* For SCHEDULE_POINT_TP_WANTS_WORK, decide whether or not to let the worker get work. 
     * Rules:
     *    - If the wq holds at least degrees_of_freedom items, we have nothing more to wait for
     *    - Once looper blocks, we know there won't be more items
     *    - Worker can't wait too long
     */

    spd_wants_work_t *spd_wants_work = (spd_wants_work_t *) pointDetails;
    int queue_len = scheduler_tp_freedom__queue_len(spd_wants_work->wq);
    struct timespec now, wait_diff, looper_epoll_diff;
    long wait_diff_us = 0, looper_epoll_diff_us = 0;

    assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);

    timespec_sub(&now, &spd_wants_work->start_time, &wait_diff);
    wait_diff_us = timespec_us(&wait_diff);

    if (tpFreedom_implDetails.looper_in_epoll)
    {
      timespec_sub(&now, &tpFreedom_implDetails.looper_epoll_start_time, &looper_epoll_diff);
      looper_epoll_diff_us = timespec_us(&looper_epoll_diff);
    }

    if (tpFreedom_implDetails.args.degrees_of_freedom <= queue_len)
    {
      mylog(LOG_SCHEDULER, 1, "scheduler_tp_freedom_thread_yield: thread can get work (degrees_of_freedom %i, queue_len %i) (%s)\n", tpFreedom_implDetails.args.degrees_of_freedom, queue_len, schedule_point_to_string(point));
      spd_wants_work->should_get_work = 1;
    }
    else if (tpFreedom_implDetails.args.max_delay_us <= wait_diff_us)
    {
      mylog(LOG_SCHEDULER, 1, "scheduler_tp_freedom_thread_yield: thread can get work (max_delay_us %llu exceeded) (%s)\n", tpFreedom_implDetails.args.max_delay_us, schedule_point_to_string(point));
      spd_wants_work->should_get_work = 1;
    }
    else if (tpFreedom_implDetails.looper_in_epoll && tpFreedom_implDetails.args.epoll_threshold <= looper_epoll_diff_us)
    {
      mylog(LOG_SCHEDULER, 1, "scheduler_tp_freedom_thread_yield: thread can get work (looper blocked in epoll for more than %llu us, no more work coming) (%s)\n", tpFreedom_implDetails.args.epoll_threshold, schedule_point_to_string(point));
      spd_wants_work->should_get_work = 1;
    }
    else
      mylog(LOG_SCHEDULER, 1, "scheduler_tp_freedom_thread_yield: thread can't get work yet (degrees_of_freedom %i, queue_len %i; delay %llu max_delay_us %llu; looper_in_epoll %i looper_epoll_diff_us %li epoll_threshold %li) (%s)\n", tpFreedom_implDetails.args.degrees_of_freedom, queue_len, wait_diff_us, tpFreedom_implDetails.args.max_delay_us, tpFreedom_implDetails.looper_in_epoll, looper_epoll_diff_us, tpFreedom_implDetails.args.epoll_threshold, schedule_point_to_string(point));
  }
  else if (point == SCHEDULE_POINT_TP_GETTING_WORK || point == SCHEDULE_POINT_LOOPER_GETTING_DONE)
  {
    /* For SCHEDULE_POINT_..._GETTING_{WORK,DONE}, choose the queue index. */

    QUEUE *wq = NULL;
    int *indexP = NULL; /* Points to "index" field of the pointDetails object. */
    int wq_len = 0;
    int wq_ix = 0;

    if (point == SCHEDULE_POINT_TP_GETTING_WORK)
    {
      wq = ((spd_getting_work_t *) pointDetails)->wq;
      indexP = &((spd_getting_work_t *) pointDetails)->index;
    }
    else if (point == SCHEDULE_POINT_LOOPER_GETTING_DONE)
    {
      wq = ((spd_getting_done_t *) pointDetails)->wq;
      indexP = &((spd_getting_done_t *) pointDetails)->index;
    }
    else
      assert(!"scheduler_tp_freedom_thread_yield: Error, how did we get here?");
    assert(wq != NULL && indexP != NULL);

    wq_len = scheduler_tp_freedom__queue_len(wq);
    wq_ix = rand() % MIN(wq_len, tpFreedom_implDetails.args.degrees_of_freedom);
    mylog(LOG_SCHEDULER, 1, "scheduler_tp_freedom_thread_yield: Chose wq_ix %i (item %i/%i) (%s)\n", wq_ix, wq_ix+1, wq_len, schedule_point_to_string(point));

    *indexP = wq_ix;
  }
  else if (point == SCHEDULE_POINT_LOOPER_BEFORE_EPOLL)
  {
    assert(!tpFreedom_implDetails.looper_in_epoll);
    tpFreedom_implDetails.looper_in_epoll = 1;
    assert(clock_gettime(CLOCK_MONOTONIC, &tpFreedom_implDetails.looper_epoll_start_time) == 0);
  }
  else if (point == SCHEDULE_POINT_LOOPER_AFTER_EPOLL)
  {
    assert(tpFreedom_implDetails.looper_in_epoll);
    tpFreedom_implDetails.looper_in_epoll = 0;
  }

}

void
scheduler_tp_freedom_emit (char *output_file)
{
  assert(scheduler_tp_freedom__looks_valid());
  unlink(output_file);
  return;
}

int
scheduler_tp_freedom_lcbns_remaining (void)
{
  assert(scheduler_tp_freedom__looks_valid());
  return -1;
}

int
scheduler_tp_freedom_schedule_has_diverged (void)
{
  assert(scheduler_tp_freedom__looks_valid());
  return -1;
}

/***********************
 * Private API definitions.
 ***********************/

int
scheduler_tp_freedom__looks_valid (void)
{
  return (tpFreedom_implDetails.magic == SCHEDULER_TP_FREEDOM_MAGIC);
}

int scheduler_tp_freedom__queue_len (QUEUE *q)
{
  QUEUE *qP = NULL;
  int queue_len = 0;

  assert(q != NULL);

  QUEUE_LEN(queue_len, qP, q);
  assert(0 <= queue_len);
  return queue_len;
}
