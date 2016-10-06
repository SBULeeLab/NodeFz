#include "scheduler_Vanilla.h"
#include "scheduler.h"

#include <unistd.h> /* usleep, unlink */
#include <string.h> /* memcpy */
#include <stdlib.h> /* srand, rand */
#include <time.h>   /* time */
#include <assert.h>

static int SCHEDULER_VANILLA_MAGIC = 25798633;

/* implDetails for the fuzzing timer scheduler. */

static struct
{
  int magic;
} vanilla_implDetails;

/***********************
 * Private API declarations
 ***********************/

/* Returns non-zero if the scheduler_vanilla looks valid (e.g. is initialized properly). */
int scheduler_vanilla__looks_valid (void);

/***********************
 * Public API definitions
 ***********************/

void
scheduler_vanilla_init (scheduler_mode_t mode, void *args, schedulerImpl_t *schedulerImpl)
{
  assert(args != NULL);
  assert(schedulerImpl != NULL);
  assert(schedulerImpl != NULL);

  /* Populate schedulerImpl. */
  schedulerImpl->register_lcbn = scheduler_vanilla_register_lcbn;
  schedulerImpl->next_lcbn_type = scheduler_vanilla_next_lcbn_type;
  schedulerImpl->thread_yield = scheduler_vanilla_thread_yield;
  schedulerImpl->emit = scheduler_vanilla_emit;
  schedulerImpl->lcbns_remaining = scheduler_vanilla_lcbns_remaining;
  schedulerImpl->schedule_has_diverged = scheduler_vanilla_schedule_has_diverged;

  /* Set implDetails. */
  vanilla_implDetails.magic = SCHEDULER_VANILLA_MAGIC;

  return;
}

void
scheduler_vanilla_register_lcbn (lcbn_t *lcbn)
{
  assert(scheduler_vanilla__looks_valid());
  assert(lcbn != NULL && lcbn_looks_valid(lcbn));

  return;
}

enum callback_type
scheduler_vanilla_next_lcbn_type (void)
{
  assert(scheduler_vanilla__looks_valid());
  return CALLBACK_TYPE_ANY;
}

void
scheduler_vanilla_thread_yield (schedule_point_t point, void *pointDetails)
{
  /* SPDs with complex inputs/outputs to modify. */
  spd_iopoll_before_handling_events_t *spd_iopoll_before_handling_events = NULL;
  spd_timer_run_t *spd_timer_run = NULL;
  spd_timer_next_timeout_t *spd_timer_next_timeout = NULL;

  int i = 0;

  assert(scheduler_vanilla__looks_valid());
  /* Ensure {point, pointDetails} are consistent. Afterwards we know the inputs are correct. */
  assert(schedule_point_looks_valid(point, pointDetails));

  /* - Supply output for points that want it. */
  switch (point)
  {
    /* As a vanilla scheduler, we simply tell threads to "behave normally" (e.g. honor the FIFO queue). */
    case SCHEDULE_POINT_TP_WANTS_WORK:
      assert(scheduler__get_thread_type() == THREAD_TYPE_THREADPOOL);
      ((spd_wants_work_t *) pointDetails)->should_get_work = 1;
      break;
    case SCHEDULE_POINT_TP_GETTING_WORK:
      assert(scheduler__get_thread_type() == THREAD_TYPE_THREADPOOL);
      ((spd_getting_work_t *) pointDetails)->index = 0;
      break;
    case SCHEDULE_POINT_LOOPER_IOPOLL_BEFORE_HANDLING_EVENTS:
      assert(scheduler__get_thread_type() == THREAD_TYPE_LOOPER);
      spd_iopoll_before_handling_events = (spd_iopoll_before_handling_events_t *) pointDetails;
      /* Handle every event. */
      for (i = 0; i < spd_iopoll_before_handling_events->nevents; i++)
        spd_iopoll_before_handling_events->should_handle_event[i] = 1;
      break;
    case SCHEDULE_POINT_LOOPER_GETTING_DONE:
      assert(scheduler__get_thread_type() == THREAD_TYPE_LOOPER);
      ((spd_getting_done_t *) pointDetails)->index = 0;
      break;
    case SCHEDULE_POINT_TIMER_RUN:
      assert(scheduler__get_thread_type() == THREAD_TYPE_LOOPER);
      spd_timer_run = (spd_timer_run_t *) pointDetails;
      /* Run timers if they've expired. */
      if (spd_timer_run->timer->timeout < spd_timer_run->now)
        spd_timer_run->run = 1;
      else
        spd_timer_run->run = 0;
      break;
    case SCHEDULE_POINT_TIMER_NEXT_TIMEOUT:
      assert(scheduler__get_thread_type() == THREAD_TYPE_LOOPER);
      spd_timer_next_timeout = (spd_timer_next_timeout_t *) pointDetails;
      if (spd_timer_next_timeout->timer->timeout < spd_timer_run->now)
        /* Already expired. */
        spd_timer_next_timeout->time_until_timer = 0;
      else
        spd_timer_next_timeout->time_until_timer = spd_timer_next_timeout->timer->timeout - spd_timer_next_timeout->now;
      break;
    default:
      /* Nothing to do. */
      break;
  }

  return;
}

void
scheduler_vanilla_emit (char *output_file)
{
  assert(scheduler_vanilla__looks_valid());
  unlink(output_file);
  return;
}

int
scheduler_vanilla_lcbns_remaining (void)
{
  assert(scheduler_vanilla__looks_valid());
  return -1;
}

int
scheduler_vanilla_schedule_has_diverged (void)
{
  assert(scheduler_vanilla__looks_valid());
  return -1;
}

/***********************
 * Private API definitions.
 ***********************/

int
scheduler_vanilla__looks_valid (void)
{
  return (vanilla_implDetails.magic == SCHEDULER_VANILLA_MAGIC);
}
