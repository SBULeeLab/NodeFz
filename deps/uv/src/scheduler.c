#include "scheduler.h"

/* Include the various schedule implementations. */
#include "scheduler_CBTree.h"
#include "scheduler_Fuzzing_Timer.h"
#include "scheduler_Fuzzing_ThreadOrder.h"

#include "list.h"
#include "map.h"
#include "mylog.h"
#include "timespec_funcs.h"

#include "unix/internal.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>

#define SCHEDULER_MAGIC 8675309 /* Jenny. */
#define SCHED_LCBN_MAGIC 19283746
#define SCHED_CONTEXT_MAGIC 55443322

int initialized = 0;

struct
{
  int magic;

  /* Constants. */
  schedule_type_t type;
  schedule_mode_t mode;
  char schedule_file[1024];
  void *args;

  /* Dependent on implementation. */
  void *details;
  schedulerImpl_init init;
  schedulerImpl_register_lcbn register_lcbn;
  schedulerImpl_execute_lcbn execute_lcbn;
  schedulerImpl_emit emit;
  schedulerImpl_schedule_has_diverged schedule_has_diverged;
  schedulerImpl_lcbns_remaining lcbns_remaining;

  /* Things we can track ourself. */
  int n_executed;

  /* Mutex. */
  uv_mutex_t lock; /* TODO Do I want to define my own re-entrant wrapper around uv_mutex_t? Seems like a good idea. */
} scheduler;

static void scheduler__lock (void);
static void scheduler__unlock (void);

/***********************
 * Private API definitions
 ***********************/
static void scheduler__lock (void)
{
  uv_mutex_lock(&scheduler.lock);
}

static void scheduler__unlock (void)
{
}

/***********************
 * Public API definitions
 ***********************/

void scheduler_init (scheduler_type_t type, schedule_mode_t mode, char *schedule_file, void *args)
{
  assert(!initialized);
  /* Initial fill. */
  scheduler.magic = SCHEDULER_MAGIC;
  scheduler.type = type;
  scheduler.mode = mode;
  strncpy(scheduler.schedule_file, schedule_file, sizeof scheduler.schedule_file);
  scheduler.args = args;

  /* Assign function pointers. */
  switch (scheduler.type)
  {
    case SCHEDULER_TYPE_CBTREE:
      scheduler.init = scheduler_cbtree_init;
      scheduler.register_lcbn = scheduler_cbtree_register_lcbn;
      scheduler.execute_lcbn = scheduler_cbtree_execute_lcbn;
      scheduler.emit = scheduler_cbtree_emit;
      scheduler.schedule_has_diverged = scheduler_cbtree_schedule_has_diverged;
      scheduler.lcbns_remaining = scheduler_cbtree_lcbns_remaining;
      break;
    SCHEDULER_TYPE_FUZZER_TIMER:
      /* TODO */
      assert(!"Not supported");
      break;
    SCHEDULER_TYPE_FUZZER_THREAD_ORDER:
      /* TODO */
      assert(!"Not supported");
      break;
    default:
      assert(!"How did we get here?");
  }

  uv_mutex_init(&scheduler.lock);
}
