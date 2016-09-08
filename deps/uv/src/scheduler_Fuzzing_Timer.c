#include "scheduler_Fuzzing_Timer.h"
#include "scheduler.h"

#include <unistd.h> /* usleep, unlink */

static int SCHEDULER_FUZZING_TIMER_MAGIC 65468789;

/* implDetails for the fuzzing timer scheduler. */

static struct
{
  int magic;

  int mode;
  scheduler_fuzzing_timer_args_t args;
} fuzzingTimer_implDetails;

/***********************
 * Private API declarations
 ***********************/

int scheduler_fuzzing_timer_looks_valid (void);

/***********************
 * Public API definitions
 ***********************/

void
scheduler_fuzzing_timer_init (scheduler_mode_t mode, void *args, schedulerImpl_t *schedulerImpl, void **implDetails)
{
  scheduler_fuzzing_timer_args_t *sft_args = (scheduler_fuzzing_timer_args_t *) args;

  assert(args != NULL);
  assert(schedulerImpl != NULL);
  assert(schedulerImpl != NULL);
  assert(implDetails != NULL);
  assert(*implDetails != NULL);

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
  *implDetails = &fuzzingTimer_implDetails;

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
  assert(scheduler_fuzzing_timer_looks_valid());
  /* TODO Pull from (min_delay, max_delay) */
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
