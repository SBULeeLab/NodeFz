#include "scheduler.h"

/* Include the various schedule implementations. */
#if defined(ENABLE_SCHEDULER_FUZZING_TIME)
  #include "scheduler_Fuzzing_Timer.h"
#endif /* ENABLE_SCHEDULER_FUZZING_TIME */

#if defined(ENABLE_SCHEDULER_CBTREE)
  #include "scheduler_CBTree.h"
#endif /* ENABLE_SCHEDULER_CBTREE */

#if defined(ENABLE_SCHEDULER_FUZZING_THREAD_ORDER)
  #include "scheduler_Fuzzing_ThreadOrder.h"
#endif /* ENABLE_SCHEDULER_FUZZING_THREAD_ORDER */

#include "list.h"
#include "map.h"
#include "mylog.h"
#include "timespec_funcs.h"
#include "synchronization.h"

#include "unix/internal.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>

/* Functions for scheduler typedefs. */

char *scheduler_type_strings[SCHEDULER_TYPE_MAX - SCHEDULER_TYPE_MIN + 1] = 
  {
    "CBTREE",
    "FUZZING_TIME",
    "FUZZING_THREAD_ORDER"
  };

const char * scheduler_type_to_string (scheduler_type_t type)
{
  char *str = NULL;
  assert(SCHEDULER_TYPE_MIN <= type && type <= SCHEDULER_TYPE_MAX);
  str = scheduler_type_strings[type - SCHEDULER_TYPE_MIN];
  return str;
}

char *scheduler_mode_strings[SCHEDULER_MODE_MAX - SCHEDULER_MODE_MIN + 1] = 
  {
    "RECORD",
    "REPLAY"
  };
  
const char * scheduler_mode_to_string (scheduler_mode_t mode)
{
  char *str = NULL;
  assert(SCHEDULER_MODE_MIN <= mode && mode <= SCHEDULER_MODE_MAX);
  str = scheduler_mode_strings[mode - SCHEDULER_MODE_MIN];
  return str;
}

char *thread_type_strings[THREAD_TYPE_MAX - THREAD_TYPE_MIN + 1] = 
  {
    "LOOPER",
    "THREADPOOL"
  };
  
const char * thread_type_to_string (thread_type_t type)
{
  char *str = NULL;
  assert(THREAD_TYPE_MIN <= type && type <= THREAD_TYPE_MAX);
  str = thread_type_strings[type - THREAD_TYPE_MIN];
  return str;
}

char *schedule_point_strings[SCHEDULE_POINT_MAX - SCHEDULE_POINT_MIN + 1] = 
  {
    "BEFORE_EXEC_CB",
    "AFTER_EXEC_CB",

    "TP_GOT_WORK",

    "TP_BEFORE_PUT_DONE",
    "TP_AFTER_PUT_DONE",
  };

const char * schedule_point_to_string (schedule_point_t point)
{
  char *str = NULL;
  assert(SCHEDULE_POINT_MIN <= point && point <= SCHEDULE_POINT_MAX);
  str = schedule_point_strings[point - SCHEDULE_POINT_MIN];
  return str;
}

/* Schedule Point Details (SPD) functions. */
static int SPD_BEFORE_EXEC_CB_MAGIC = 11929224;
static int SPD_AFTER_EXEC_CB_MAGIC = 11929224;
static int SPD_GOT_WORK_MAGIC = 46548678;
static int SPD_BEFORE_PUT_DONE_MAGIC = 59175099;
static int SPD_AFTER_PUT_DONE_MAGIC = 99281732;

void spd_before_exec_cb_init (spd_before_exec_cb_t *spd_before_exec_cb)
{
  assert(spd_before_exec_cb != NULL);
  spd_before_exec_cb->magic = SPD_BEFORE_EXEC_CB_MAGIC;
}

int spd_before_exec_cb_is_valid (spd_before_exec_cb_t *spd_before_exec_cb)
{
  return (spd_before_exec_cb != NULL &&
          spd_before_exec_cb->magic == SPD_BEFORE_EXEC_CB_MAGIC);
}

void spd_after_exec_cb_init (spd_after_exec_cb_t *spd_after_exec_cb)
{
  assert(spd_after_exec_cb != NULL);
  spd_after_exec_cb->magic = SPD_AFTER_EXEC_CB_MAGIC;
}

int spd_after_exec_cb_is_valid (spd_after_exec_cb_t *spd_after_exec_cb)
{
  return (spd_after_exec_cb != NULL &&
          spd_after_exec_cb->magic == SPD_AFTER_EXEC_CB_MAGIC);
}

void spd_got_work_init (spd_got_work_t *spd_got_work)
{
  assert(spd_got_work != NULL);
  spd_got_work->magic = SPD_GOT_WORK_MAGIC;
}

int spd_got_work_is_valid (spd_got_work_t *spd_got_work)
{
  return (spd_got_work != NULL &&
          spd_got_work->magic == SPD_GOT_WORK_MAGIC);
}

void spd_before_put_done_init (spd_before_put_done_t *spd_before_put_done)
{
  assert(spd_before_put_done != NULL);
  spd_before_put_done->magic = SPD_BEFORE_PUT_DONE_MAGIC;
}

int spd_before_put_done_is_valid (spd_before_put_done_t *spd_before_put_done)
{
  return (spd_before_put_done != NULL &&
          spd_before_put_done->magic == SPD_BEFORE_PUT_DONE_MAGIC);
}

void spd_after_put_done_init (spd_after_put_done_t *spd_after_put_done)
{
  assert(spd_after_put_done != NULL);
  spd_after_put_done->magic = SPD_AFTER_PUT_DONE_MAGIC;
}

int spd_after_put_done_is_valid (spd_after_put_done_t *spd_after_put_done)
{
  return (spd_after_put_done != NULL &&
          spd_after_put_done->magic == SPD_AFTER_PUT_DONE_MAGIC);
}

/***********************
 * Scheduler variable declarations.
 ***********************/

static int SCHEDULER_MAGIC = 8675309; /* Jenny. */

int scheduler_initialized = 0;
struct
{
  int magic;

  /* Constants. */
  scheduler_type_t type;
  scheduler_mode_t mode;
  char schedule_file[1024];
  void *args;

  /* Things we can track ourselves. */
  int n_executed;
  struct map *tidToType;

  /* Synchronization. */
  reentrant_mutex_t *mutex;

  /* Implementation-dependent. */
  schedulerImpl_t impl;
} scheduler;

/***********************
 * Private scheduler API declarations.
 ***********************/

/* Returns non-zero if scheduler is initialized and magic is OK. */
int scheduler__looks_valid (void);

/***********************
 * Public scheduler API definitions.
 ***********************/

void scheduler_init (scheduler_type_t type, scheduler_mode_t mode, char *schedule_file, void *args)
{
  assert(!scheduler_initialized);

  /* Shared amongst all scheduler implementations. */
  scheduler.magic = SCHEDULER_MAGIC;
  scheduler.type = type;
  scheduler.mode = mode;
  strncpy(scheduler.schedule_file, schedule_file, sizeof scheduler.schedule_file);
  scheduler.args = args;

  scheduler.n_executed = 0;
  scheduler.tidToType = map_create();
  assert(scheduler.tidToType != NULL);

  scheduler.mutex = reentrant_mutex_create();
  assert(scheduler.mutex != NULL);

  /* Specifics based on the scheduler type. */
  switch (scheduler.type)
  {
    case SCHEDULER_TYPE_CBTREE:
#if defined(ENABLE_SCHEDULER_CBTREE)
      scheduler_cbTree_init(mode, args, &scheduler.impl);
      break;
#endif
    case SCHEDULER_TYPE_FUZZING_TIME:
#if defined(ENABLE_SCHEDULER_FUZZING_TIME)
      scheduler_fuzzing_timer_init(mode, args, &scheduler.impl);
      break;
#endif
    case SCHEDULER_TYPE_FUZZING_THREAD_ORDER:
#if defined(ENABLE_SCHEDULER_FUZZING_THREAD_ORDER)
      scheduler_fuzzer_threadOrder_init(mode, args, &scheduler.impl);
      break;
#endif
    default:
      assert(!"How did we get here?");
  }

  scheduler_initialized = 1;

  return;
}

void scheduler_register_thread (thread_type_t type)
{
#if defined(JD_DEBUG)
  int found = 0;
  /* Not already present. */
  assert(map_lookup(scheduler.tidToType, (int) uv_thread_self(), &found) == NULL);
  assert(!found);
#endif

  mylog(LOG_SCHEDULER, 1, "scheduler_register_thread: registering %lli as %s\n", uv_thread_self(), thread_type_to_string(type));

  assert(scheduler__looks_valid());

  map_insert(scheduler.tidToType, (int) uv_thread_self(), (void *) type);
  return;
}

void scheduler_register_lcbn (lcbn_t *lcbn)
{
  assert(scheduler__looks_valid());
  scheduler.impl.register_lcbn(lcbn);
  return;
}

enum callback_type scheduler_next_lcbn_type (void)
{
  enum callback_type ret;
  assert(scheduler__looks_valid());
  
  ret = scheduler.impl.next_lcbn_type();
  return ret;
}

void scheduler_thread_yield (schedule_point_t point, void *schedule_point_details)
{
  assert(scheduler__looks_valid());

  if (point == SCHEDULE_POINT_AFTER_EXEC_CB)
  {
    scheduler__lock();
    scheduler.n_executed++;
    scheduler__unlock();
  }

  scheduler.impl.thread_yield(point, schedule_point_details);
  return;
}

void scheduler_emit (void)
{
  char output_file[1024];
  assert(scheduler__looks_valid());

  strcpy(output_file, scheduler.schedule_file);
  if (scheduler.mode == SCHEDULER_MODE_REPLAY)
    strcat(output_file, "-replay");

  scheduler.impl.emit(output_file);
  return;
}

int scheduler_lcbns_remaining (void)
{
  int n_remaining = 0;

  assert(scheduler__looks_valid());
  n_remaining = scheduler.impl.lcbns_remaining();
  return n_remaining;
}

int scheduler_schedule_has_diverged (void)
{
  int has_diverged = 0;
  assert(scheduler__looks_valid());
  has_diverged = scheduler.impl.schedule_has_diverged();
  return has_diverged;
}

int scheduler_n_executed (void)
{
  assert(scheduler__looks_valid());
  /* Not thread-safe, but monotonically increasing so NBD. */
  return scheduler.n_executed;
}

scheduler_mode_t scheduler_get_scheduler_mode (void)
{
  assert(scheduler__looks_valid());
  return scheduler.mode;
}

/***********************
 * "Protected" scheduler API definitions.
 ***********************/

void scheduler__lock (void)
{
  assert(scheduler__looks_valid());
  reentrant_mutex_lock(scheduler.mutex);
}

void scheduler__unlock (void)
{
  assert(scheduler__looks_valid());
  reentrant_mutex_unlock(scheduler.mutex);
}

thread_type_t scheduler__get_thread_type (void)
{
  int found = 0;
  thread_type_t type;

  assert(scheduler__looks_valid());
  
  type = (thread_type_t) map_lookup(scheduler.tidToType, (int) uv_thread_self(), &found);
  assert(found);

  return type;
}

/***********************
 * Private scheduler API definitions.
 ***********************/

int scheduler__looks_valid (void)
{
  return (scheduler_initialized &&
          scheduler.magic == SCHEDULER_MAGIC);
}
