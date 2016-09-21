#ifndef UV_SRC_SCHEDULER_FUZZING_TIMER_H_
#define UV_SRC_SCHEDULER_FUZZING_TIMER_H_

#include "scheduler.h"
#include "logical-callback-node.h"

struct scheduler_fuzzing_timer_args_s
{
  int min_delay;
  int max_delay;
};
typedef struct scheduler_fuzzing_timer_args_s scheduler_fuzzing_timer_args_t;

void
scheduler_fuzzing_timer_init (scheduler_mode_t mode, void *args, schedulerImpl_t *schedulerImpl);

void
scheduler_fuzzing_timer_register_lcbn (lcbn_t *lcbn);

enum callback_type
scheduler_fuzzing_timer_next_lcbn_type (void);

void
scheduler_fuzzing_timer_thread_yield (schedule_point_t point, void *schedule_point_details);

void
scheduler_fuzzing_timer_emit (char *output_file);

int
scheduler_fuzzing_timer_lcbns_remaining (void);

int
scheduler_fuzzing_timer_schedule_has_diverged (void);

#endif  /* UV_SRC_SCHEDULER_FUZZING_TIMER_H_ */
