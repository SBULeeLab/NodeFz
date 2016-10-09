#ifndef UV_SRC_SCHEDULER_TP_FREEDOM_H_
#define UV_SRC_SCHEDULER_TP_FREEDOM_H_

#include "scheduler.h"
#include "logical-callback-node.h"

struct scheduler_tp_freedom_args_s
{
  /* TP parameters. */

  /* How far into the work queue can we select work and done items? 
   * This corresponds to the number of simulated threads in the TP. */
  int tp_degrees_of_freedom;

  /* Maximum time to wait for work/done queue to fill. */
  useconds_t tp_max_delay_us;
  /* If looper has been in epoll for longer than epoll_threshold useconds, assume it's blocked and that work queue will not fill further. */
  useconds_t tp_epoll_threshold;

  /* Looper epoll (uv__io_poll) parameters. */

  /* In iopoll, how far can we swap ready events? Give -1 for "no limit". */
  int iopoll_degrees_of_freedom;
  /* In iopoll, what percentage of ready events to defer until the next loop? 
   * A value of 100 will result in no forward progress. */
  int iopoll_defer_perc;

  /* Timer parameters. */
  /* In uv__run_timers, how far can we swap ready timers? Give -1 for "no limit". 
   * Legal because of https://nodejs.org/api/timers.html#timers_settimeout_callback_delay_arg. */
  int timer_degrees_of_freedom;
  /* Probability of executing a timer early. Measured in tenths of a percent. */
  int timer_early_exec_tperc;
  /* Limit on how early we might execute a timer, to avoid absurd situations.
   * Ratio of the full time to how long since it was registered.
   * -1 means no limit.
   */
  int timer_max_early_multiple;
  /* Probability of executing a timer late. Measured in tenths of a percent. */
  int timer_late_exec_tperc;
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
