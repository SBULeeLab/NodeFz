#include "scheduler_TP_Freedom.h"
#include "scheduler.h"

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
  int delay_range; /* max_delay - min_delay */
} tpFreedom_implDetails;

/***********************
 * Private API declarations
 ***********************/

/* Returns non-zero if the scheduler_tp_freedom looks valid (e.g. is initialized properly). */
int scheduler_tp_freedom__looks_valid (void);

/***********************
 * Public API definitions
 ***********************/

void
scheduler_tp_freedom_init (scheduler_mode_t mode, void *args, schedulerImpl_t *schedulerImpl)
{
  const char *tpSize = NULL;

  assert(args != NULL);
  assert(schedulerImpl != NULL);
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
  /* SPDs whose inputs/outputs we may need to examine. */
  spd_getting_work_t *spd_getting_work = NULL;

  QUEUE *q = NULL;
  int queue_len = 0;
  int q_ix = 0;

  assert(scheduler_tp_freedom__looks_valid());
  /* Ensure {point, pointDetails} are consistent. Afterwards we know the inputs are correct. */
  assert(schedule_point_looks_valid(point, pointDetails));

  /* For SCHEDULE_POINT_TP_GETTING_WORK, choose the queue index. */
  switch (point)
  {
    case SCHEDULE_POINT_TP_GETTING_WORK:
      assert(scheduler__get_thread_type() == THREAD_TYPE_THREADPOOL);
      spd_getting_work = (spd_getting_work_t *) pointDetails;
      QUEUE_LEN(queue_len, q, spd_getting_work->wq);
      assert(0 < queue_len);
      q_ix = rand() % MIN(queue_len, tpFreedom_implDetails.args.degrees_of_freedom);
      spd_getting_work->index = q_ix;
      mylog(LOG_SCHEDULER, 1, "uv__work_done: %s: index %i\n", schedule_point_to_string(point), q_ix);
      break;
    default:
      /* Nothing to do. */
      break;
  }

  /* Ensure mutex during CB execution. */
  switch (point)
  {
    case SCHEDULE_POINT_BEFORE_EXEC_CB:
      scheduler__lock();
      break;
    case SCHEDULE_POINT_AFTER_EXEC_CB:
      scheduler__unlock();
      break;
    default:
      /* Nothing to do. */
      break;
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
