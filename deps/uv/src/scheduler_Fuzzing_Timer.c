#include "scheduler_Fuzzing_Timer.h"
#include "scheduler.h"

#include <unistd.h> /* usleep, unlink */
#include <string.h> /* memcpy */
#include <stdlib.h> /* srand, rand */
#include <time.h>   /* time */
#include <assert.h>

static int SCHEDULER_FUZZING_TIMER_MAGIC = 65468789;

/* implDetails for the fuzzing timer scheduler. */

static struct
{
  int magic;

  int mode;
  scheduler_fuzzing_timer_args_t args;
  int delay_range; /* max_delay - min_delay */
} fuzzingTimer_implDetails;

/***********************
 * Private API declarations
 ***********************/

int scheduler_fuzzing_timer_looks_valid (void);

/***********************
 * Public API definitions
 ***********************/

void
scheduler_fuzzing_timer_init (scheduler_mode_t mode, void *args, schedulerImpl_t *schedulerImpl)
{
  assert(args != NULL);
  assert(schedulerImpl != NULL);
  assert(schedulerImpl != NULL);

  srand(time(NULL));

  /* Populate schedulerImpl. */
  schedulerImpl->register_lcbn = scheduler_fuzzing_timer_register_lcbn;
  schedulerImpl->next_lcbn_type = scheduler_fuzzing_timer_next_lcbn_type;
  schedulerImpl->thread_yield = scheduler_fuzzing_timer_thread_yield;
  schedulerImpl->emit = scheduler_fuzzing_timer_emit;
  schedulerImpl->lcbns_remaining = scheduler_fuzzing_timer_lcbns_remaining;
  schedulerImpl->schedule_has_diverged = scheduler_fuzzing_timer_schedule_has_diverged;

  /* Set implDetails. */
  fuzzingTimer_implDetails.magic = SCHEDULER_FUZZING_TIMER_MAGIC;
  fuzzingTimer_implDetails.mode = mode;
  memcpy(&fuzzingTimer_implDetails.args, args, sizeof(fuzzingTimer_implDetails.args));

  assert(fuzzingTimer_implDetails.args.min_delay < fuzzingTimer_implDetails.args.max_delay);
  fuzzingTimer_implDetails.delay_range = fuzzingTimer_implDetails.args.max_delay - fuzzingTimer_implDetails.args.min_delay;

  return;
}

void
scheduler_fuzzing_timer_register_lcbn (lcbn_t *lcbn)
{
  assert(scheduler_fuzzing_timer_looks_valid());
  assert(lcbn != NULL && lcbn_looks_valid(lcbn));

  return;
}

enum callback_type
scheduler_fuzzing_timer_next_lcbn_type (void)
{
  assert(scheduler_fuzzing_timer_looks_valid());
  return CALLBACK_TYPE_ANY;
}

void
scheduler_fuzzing_timer_thread_yield (schedule_point_t point, void *pointDetails)
{
  spd_before_exec_cb_t *spd_before_exec_cb = NULL;
  spd_after_exec_cb_t *spd_after_exec_cb = NULL;
  spd_got_work_t *spd_got_work = NULL;
  spd_before_put_done_t *spd_before_put_done = NULL;
  spd_after_put_done_t *spd_after_put_done = NULL;

  useconds_t sleep_fuzz = fuzzingTimer_implDetails.args.min_delay + (rand() % fuzzingTimer_implDetails.delay_range);

  assert(scheduler_fuzzing_timer_looks_valid());
  assert(pointDetails != NULL);

  /* Ensure valid input. */
  switch (point)
  {
    case SCHEDULE_POINT_BEFORE_EXEC_CB:
      spd_before_exec_cb = (spd_before_exec_cb_t *) pointDetails;
      assert(spd_before_exec_cb_is_valid(spd_before_exec_cb));
      break;
    case SCHEDULE_POINT_AFTER_EXEC_CB:
      spd_after_exec_cb = (spd_after_exec_cb_t *) pointDetails;
      assert(spd_after_exec_cb_is_valid(spd_after_exec_cb));
      break;
    case SCHEDULE_POINT_TP_GOT_WORK:
      assert(scheduler__get_thread_type() == THREAD_TYPE_THREADPOOL);
      spd_got_work = (spd_got_work_t *) pointDetails;
      assert(spd_got_work_is_valid(spd_got_work));
      /* TODO TESTING - significantly delay the first item */
      if (spd_got_work->work_item_num == 0)
        usleep(200000);
      break;
    case SCHEDULE_POINT_TP_BEFORE_PUT_DONE:
      assert(scheduler__get_thread_type() == THREAD_TYPE_THREADPOOL);
      spd_before_put_done = (spd_before_put_done_t *) pointDetails;
      assert(spd_before_put_done_is_valid(spd_before_put_done));
      break;
    case SCHEDULE_POINT_TP_AFTER_PUT_DONE:
      assert(scheduler__get_thread_type() == THREAD_TYPE_THREADPOOL);
      spd_after_put_done = (spd_after_put_done_t *) pointDetails;
      assert(spd_after_put_done_is_valid(spd_after_put_done));
      break;
    default:
      assert(!"scheduler_fuzzing_timer_thread_yield: Error, unexpected point");
  }

  /* The fuzzing timer scheduler doesn't do anything fancy, it just perturbs the time. */
  mylog(LOG_SCHEDULER, 1, "scheduler_fuzzing_timer_thread_yield: Sleeping for %llu usec\n", sleep_fuzz);
  usleep(sleep_fuzz);
}

void
scheduler_fuzzing_timer_emit (char *output_file)
{
  assert(scheduler_fuzzing_timer_looks_valid());
  unlink(output_file);
  return;
}

int
scheduler_fuzzing_timer_lcbns_remaining (void)
{
  assert(scheduler_fuzzing_timer_looks_valid());
  return -1;
}

int
scheduler_fuzzing_timer_schedule_has_diverged (void)
{
  assert(scheduler_fuzzing_timer_looks_valid());
  return -1;
}

/***********************
 * Private API definitions.
 ***********************/

int scheduler_fuzzing_timer_looks_valid (void)
{
  return (fuzzingTimer_implDetails.magic == SCHEDULER_FUZZING_TIMER_MAGIC);
}
