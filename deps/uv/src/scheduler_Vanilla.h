#ifndef UV_SRC_SCHEDULER_VANILLA_H_
#define UV_SRC_SCHEDULER_VANILLA_H_

#include "scheduler.h"
#include "logical-callback-node.h"

struct scheduler_vanilla_args_s
{
  int filler;
};
typedef struct scheduler_vanilla_args_s scheduler_vanilla_args_t;

void
scheduler_vanilla_init (scheduler_mode_t mode, void *args, schedulerImpl_t *schedulerImpl);

void
scheduler_vanilla_register_lcbn (lcbn_t *lcbn);

enum callback_type
scheduler_vanilla_next_lcbn_type (void);

void
scheduler_vanilla_thread_yield (schedule_point_t point, void *schedule_point_details);

void
scheduler_vanilla_emit (char *output_file);

int
scheduler_vanilla_lcbns_remaining (void);

int
scheduler_vanilla_schedule_has_diverged (void);

#endif  /* UV_SRC_SCHEDULER_VANILLA_H_ */
