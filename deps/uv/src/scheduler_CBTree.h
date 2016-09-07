#ifndef UV_SRC_SCHEDULER_CBTREE_H_
#define UV_SRC_SCHEDULER_CBTREE_H_

/* Implementation of scheduler.h based on CallBack Tree (CBTree). 
 * TODO More details.
 */

struct scheduler_cbtree_args_s
{
  int min_n_executed_before_divergence_allowed;
};
typedef struct scheduler_cbtree_args_s scheduler_cbtree_args_t;

struct scheduler_cbtree_details_s
{
  int min_n_executed_before_divergence_allowed;
  struct list *registration_schedule; /* List of the registered sched_lcbn_t's, in registration order. */

  /* REPLAY mode. */
  lcbn_t *shadow_root; /* Root of the "shadow tree" -- the registration tree described in the input file. */
  struct map *name_to_lcbn; /* Used to map hash(name) to lcbn. Allows us to re-build the tree. */
  struct list *desired_schedule; /* A tree_as_list list (rooted at shadow_root) of lcbn_t's, expressing desired execution order, first to last. The front of the list is the next to execute; see execution_schedule. */
  struct list *execution_schedule; /* List of the executed sched_lcbn_t's, in order of execution. Nodes are shifted from desired_schedule, wrapped in a sched_lcbn_t, and pushed on execution_schedule as they are executed. This gives us a cheap way to implement scheduler__find_scheduled_sched_lcbn. */
  int n_executed;

  /* DIVERGENCE */
  /* Divergence prior to having executed this many CBs will result in a crash. 
     Controlled via env variable UV_SCHEDULE_MIN_N_EXECUTED_BEFORE_DIVERGENCE_ALLOWED, which is checked in unified_callback_init and fed into scheduler_init.
     -1 means that divergence at any point in the schedule is allowed. */
  int min_n_executed_before_divergence_allowed;
  int diverged; /* 1 if the REPLAY'd schedule has diverged. */

  /* For timeout-based divergence detection. */
  int replay_divergence_timeout; /* In seconds. */
  struct timespec divergence_timer; /* Use scheduler__divergence_timer_X to reset and check. */
  int just_scheduler_advanced; /* Flag. */

  uv_mutex_t lock;
};
typedef struct scheduler_cbtree_details_s scheduler_cbtree_details_t;

/* schedulerImpl_* functions required by scheduler.h for all scheduler implementations. */
void scheduler_cbtree_init (schedule_mode_t mode, void *args, void *details);
void scheduler_cbtree_register_lcbn (lcbn_t *lcbn, void *details);
void scheduler_cbtree_execute_lcbn (lcbn_t *lcbn, void *details);
void scheduler_cbtree_emit (void *details);
int scheduler_cbtree_schedule_has_diverged (void *details);
int scheduler_cbtree_lcbns_remaining (void *details);

#endif  /* UV_SRC_SCHEDULER_CBTREE_H_ */
