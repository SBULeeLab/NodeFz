#ifndef UV_SRC_SCHEDULER_TP_FREEDOM_H_
#define UV_SRC_SCHEDULER_TP_FREEDOM_H_

#include "scheduler.h"
#include "logical-callback-node.h"

struct scheduler_tp_freedom_args_s
{
  /* How far into the queue can we select work and done items? 
   * This corresponds to the number of simulated threads in the TP. */
  int degrees_of_freedom;

  /* Maximum time to wait for work/done queue to fill. */
  useconds_t max_delay_us;
  /* If looper has been in epoll for longer than epoll_threshold useconds, assume it's blocked and that work queue will not fill further. */
  useconds_t epoll_threshold;
};
typedef struct scheduler_tp_freedom_args_s scheduler_tp_freedom_args_t;

/* Env var UV_THREADPOOL_SIZE must be 1. */
void
scheduler_tp_freedom_init (scheduler_mode_t mode, void *args, schedulerImpl_t *schedulerImpl);

void
scheduler_tp_freedom_register_lcbn (lcbn_t *lcbn);

enum callback_type
scheduler_tp_freedom_next_lcbn_type (void);

void
scheduler_tp_freedom_thread_yield (schedule_point_t point, void *schedule_point_details);

void
scheduler_tp_freedom_emit (char *output_file);

int
scheduler_tp_freedom_lcbns_remaining (void);

int
scheduler_tp_freedom_schedule_has_diverged (void);

#endif  /* UV_SRC_SCHEDULER_TP_FREEDOM_H_ */
