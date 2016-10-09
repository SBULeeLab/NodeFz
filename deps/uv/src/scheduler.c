#include "scheduler.h"

/* Include the various schedule implementations. */

#if defined(ENABLE_SCHEDULER_VANILLA)
  #include "scheduler_Vanilla.h"
#endif /* ENABLE_SCHEDULER_VANILLA */

#if defined(ENABLE_SCHEDULER_FUZZING_TIME)
  #include "scheduler_Fuzzing_Timer.h"
#endif /* ENABLE_SCHEDULER_FUZZING_TIME */

#if defined(ENABLE_SCHEDULER_TP_FREEDOM)
  #include "scheduler_TP_Freedom.h"
#endif /* ENABLE_SCHEDULER_TP_FREEDOM */

#if defined(ENABLE_SCHEDULER_CBTREE)
  #include "scheduler_CBTree.h"
#endif /* ENABLE_SCHEDULER_CBTREE */

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
    "VANILLA",
    "CBTREE",
    "FUZZING_TIME",
    "TP_FREEDOM"
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
    /* Shared */
    "BEFORE_EXEC_CB",
    "AFTER_EXEC_CB",

    /* Looper */
    "LOOPER_BEFORE_EPOLL",
    "LOOPER_AFTER_EPOLL",

    "LOOPER_IOPOLL_BEFORE_HANDLING_EVENTS",

    "LOOPER_GETTING_DONE",

    /* TP */
    "TP_WANTS_WORK",

    "TP_GETTING_WORK",
    "TP_GOT_WORK",

    "TP_BEFORE_PUT_DONE",
    "TP_AFTER_PUT_DONE",

    /* Timer. */ 
    "TIMER_READY",
    "TIMER_RUN",
    "TIMER_NEXT_TIMEOUT"
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
static int SPD_AFTER_EXEC_CB_MAGIC = 19283382;
static int SPD_LOOPER_BEFORE_EPOLL_MAGIC = 83911652;
static int SPD_LOOPER_AFTER_EPOLL_MAGIC = 88474402;
static int SPD_LOOPER_IOPOLL_BEFORE_HANDLING_EVENTS_MAGIC = 66459788;
static int SPD_TP_WANTS_WORK_MAGIC = 81892192;
static int SPD_TP_GETTING_WORK_MAGIC = 91827365;
static int SPD_TP_GOT_WORK_MAGIC = 46548678;
static int SPD_TP_BEFORE_PUT_DONE_MAGIC = 59175099;
static int SPD_TP_AFTER_PUT_DONE_MAGIC = 99281732;
static int SPD_LOOPER_GETTING_DONE_MAGIC = 10229334;
static int SPD_TIMER_READY_MAGIC = 64315287;
static int SPD_TIMER_RUN_MAGIC = 87874545;
static int SPD_TIMER_NEXT_TIMEOUT_MAGIC = 85563324;

void spd_before_exec_cb_init (spd_before_exec_cb_t *spd_before_exec_cb)
{
  assert(spd_before_exec_cb != NULL);
  memset(spd_before_exec_cb, 0, sizeof *spd_before_exec_cb);
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
  memset(spd_after_exec_cb, 0, sizeof *spd_after_exec_cb);
  spd_after_exec_cb->magic = SPD_AFTER_EXEC_CB_MAGIC;
}

int spd_after_exec_cb_is_valid (spd_after_exec_cb_t *spd_after_exec_cb)
{
  return (spd_after_exec_cb != NULL &&
          spd_after_exec_cb->magic == SPD_AFTER_EXEC_CB_MAGIC);
}

void spd_before_epoll_init (spd_before_epoll_t *spd_before_epoll)
{
  assert(spd_before_epoll != NULL);
  memset(spd_before_epoll, 0, sizeof *spd_before_epoll);
  spd_before_epoll->magic = SPD_LOOPER_BEFORE_EPOLL_MAGIC;
}

int spd_before_epoll_is_valid (spd_before_epoll_t *spd_before_epoll)
{
  return (spd_before_epoll != NULL &&
          spd_before_epoll->magic == SPD_LOOPER_BEFORE_EPOLL_MAGIC);
}

void spd_after_epoll_init (spd_after_epoll_t *spd_after_epoll)
{
  assert(spd_after_epoll != NULL);
  memset(spd_after_epoll, 0, sizeof *spd_after_epoll);
  spd_after_epoll->magic = SPD_LOOPER_AFTER_EPOLL_MAGIC;
}

int spd_after_epoll_is_valid (spd_after_epoll_t *spd_after_epoll)
{
  return (spd_after_epoll != NULL &&
          spd_after_epoll->magic == SPD_LOOPER_AFTER_EPOLL_MAGIC);
}

void spd_iopoll_before_handling_events_init (spd_iopoll_before_handling_events_t *spd_iopoll_before_handling_events)
{
  assert(spd_iopoll_before_handling_events != NULL);
  memset(spd_iopoll_before_handling_events, 0, sizeof *spd_iopoll_before_handling_events);
  spd_iopoll_before_handling_events->magic = SPD_LOOPER_IOPOLL_BEFORE_HANDLING_EVENTS_MAGIC;
}

int spd_iopoll_before_handling_events_is_valid (spd_iopoll_before_handling_events_t *spd_iopoll_before_handling_events)
{
  return (spd_iopoll_before_handling_events != NULL &&
          spd_iopoll_before_handling_events->magic == SPD_LOOPER_IOPOLL_BEFORE_HANDLING_EVENTS_MAGIC &&
          spd_iopoll_before_handling_events->shuffleable_items.items != NULL &&
          spd_iopoll_before_handling_events->shuffleable_items.thoughts != NULL);
}

void spd_wants_work_init (spd_wants_work_t *spd_wants_work)
{
  assert(spd_wants_work != NULL);
  memset(spd_wants_work, 0, sizeof *spd_wants_work);
  spd_wants_work->magic = SPD_TP_WANTS_WORK_MAGIC;
  spd_wants_work->wq = NULL;
  spd_wants_work->should_get_work = 0;
}

int spd_wants_work_is_valid (spd_wants_work_t *spd_wants_work)
{
  return (spd_wants_work != NULL &&
          spd_wants_work->magic == SPD_TP_WANTS_WORK_MAGIC);
}

void spd_getting_work_init (spd_getting_work_t *spd_getting_work)
{
  assert(spd_getting_work != NULL);
  memset(spd_getting_work, 0, sizeof *spd_getting_work);
  spd_getting_work->magic = SPD_TP_GETTING_WORK_MAGIC;
  spd_getting_work->wq = NULL;
  spd_getting_work->index = -1;
}

int spd_getting_work_is_valid (spd_getting_work_t *spd_getting_work)
{
  return (spd_getting_work != NULL &&
          spd_getting_work->magic == SPD_TP_GETTING_WORK_MAGIC);
}

void spd_got_work_init (spd_got_work_t *spd_got_work)
{
  assert(spd_got_work != NULL);
  memset(spd_got_work, 0, sizeof *spd_got_work);
  spd_got_work->magic = SPD_TP_GOT_WORK_MAGIC;
}

int spd_got_work_is_valid (spd_got_work_t *spd_got_work)
{
  return (spd_got_work != NULL &&
          spd_got_work->magic == SPD_TP_GOT_WORK_MAGIC);
}

void spd_before_put_done_init (spd_before_put_done_t *spd_before_put_done)
{
  assert(spd_before_put_done != NULL);
  memset(spd_before_put_done, 0, sizeof *spd_before_put_done);
  spd_before_put_done->magic = SPD_TP_BEFORE_PUT_DONE_MAGIC;
}

int spd_before_put_done_is_valid (spd_before_put_done_t *spd_before_put_done)
{
  return (spd_before_put_done != NULL &&
          spd_before_put_done->magic == SPD_TP_BEFORE_PUT_DONE_MAGIC);
}

void spd_after_put_done_init (spd_after_put_done_t *spd_after_put_done)
{
  assert(spd_after_put_done != NULL);
  memset(spd_after_put_done, 0, sizeof *spd_after_put_done);
  spd_after_put_done->magic = SPD_TP_AFTER_PUT_DONE_MAGIC;
}

int spd_after_put_done_is_valid (spd_after_put_done_t *spd_after_put_done)
{
  return (spd_after_put_done != NULL &&
          spd_after_put_done->magic == SPD_TP_AFTER_PUT_DONE_MAGIC);
}

void spd_getting_done_init (spd_getting_done_t *spd_getting_done)
{
  assert(spd_getting_done != NULL);
  memset(spd_getting_done, 0, sizeof *spd_getting_done);
  spd_getting_done->magic = SPD_LOOPER_GETTING_DONE_MAGIC;
  spd_getting_done->index = -1;
}

int spd_getting_done_is_valid (spd_getting_done_t *spd_getting_done)
{
  return (spd_getting_done != NULL &&
          spd_getting_done->magic == SPD_LOOPER_GETTING_DONE_MAGIC);
}

void spd_timer_ready_init (spd_timer_ready_t *spd_timer_ready)
{
  assert(spd_timer_ready != NULL);
  memset(spd_timer_ready, 0, sizeof *spd_timer_ready);
  spd_timer_ready->magic = SPD_TIMER_READY_MAGIC;
  spd_timer_ready->timer = NULL;
  spd_timer_ready->ready = 0;
}

int spd_timer_ready_is_valid (spd_timer_ready_t *spd_timer_ready)
{
  return (spd_timer_ready != NULL &&
          spd_timer_ready->magic == SPD_TIMER_READY_MAGIC &&
          spd_timer_ready->timer != NULL);
}

void spd_timer_run_init (spd_timer_run_t *spd_timer_run)
{
  assert(spd_timer_run != NULL);
  memset(spd_timer_run, 0, sizeof *spd_timer_run);
  spd_timer_run->magic = SPD_TIMER_RUN_MAGIC;
}

int spd_timer_run_is_valid (spd_timer_run_t *spd_timer_run)
{
  return (spd_timer_run != NULL &&
          spd_timer_run->magic == SPD_TIMER_RUN_MAGIC &&
          spd_timer_run->shuffleable_items.items != NULL &&
          spd_timer_run->shuffleable_items.thoughts != NULL);
}

void spd_timer_next_timeout_init (spd_timer_next_timeout_t *spd_timer_next_timeout)
{
  assert(spd_timer_next_timeout != NULL);
  memset(spd_timer_next_timeout, 0, sizeof *spd_timer_next_timeout);
  spd_timer_next_timeout->magic = SPD_TIMER_NEXT_TIMEOUT_MAGIC;
  spd_timer_next_timeout->timer = NULL;
  spd_timer_next_timeout->now = 0;
  spd_timer_next_timeout->time_until_timer = 0;
}

int spd_timer_next_timeout_is_valid (spd_timer_next_timeout_t *spd_timer_next_timeout)
{
  return (spd_timer_next_timeout != NULL &&
          spd_timer_next_timeout->magic == SPD_TIMER_NEXT_TIMEOUT_MAGIC &&
          spd_timer_next_timeout->timer != NULL &&
          0 < spd_timer_next_timeout->now);
}

int schedule_point_looks_valid (schedule_point_t point, void *pointDetails)
{
  spd_before_exec_cb_t *spd_before_exec_cb = NULL;
  spd_after_exec_cb_t *spd_after_exec_cb = NULL;
  spd_before_epoll_t *spd_before_epoll = NULL;
  spd_after_epoll_t *spd_after_epoll = NULL;
  spd_iopoll_before_handling_events_t *spd_iopoll_before_handling_events = NULL;
  spd_wants_work_t *spd_wants_work = NULL;
  spd_getting_work_t *spd_getting_work = NULL;
  spd_got_work_t *spd_got_work = NULL;
  spd_before_put_done_t *spd_before_put_done = NULL;
  spd_after_put_done_t *spd_after_put_done = NULL;
  spd_getting_done_t *spd_getting_done = NULL;
  spd_timer_ready_t *spd_timer_ready = NULL;
  spd_timer_run_t *spd_timer_run = NULL;
  spd_timer_next_timeout_t *spd_timer_next_timeout = NULL;

  int is_valid = 0;

  assert(pointDetails != NULL);

  /* Ensure valid input. */
  switch (point)
  {
    case SCHEDULE_POINT_BEFORE_EXEC_CB:
      spd_before_exec_cb = (spd_before_exec_cb_t *) pointDetails;
      is_valid = spd_before_exec_cb_is_valid(spd_before_exec_cb);
      break;
    case SCHEDULE_POINT_AFTER_EXEC_CB:
      spd_after_exec_cb = (spd_after_exec_cb_t *) pointDetails;
      is_valid = spd_after_exec_cb_is_valid(spd_after_exec_cb);
      break;
    case SCHEDULE_POINT_LOOPER_BEFORE_EPOLL:
      spd_before_epoll = (spd_before_epoll_t *) pointDetails;
      is_valid = spd_before_epoll_is_valid(spd_before_epoll);
      break;
    case SCHEDULE_POINT_LOOPER_AFTER_EPOLL:
      spd_after_epoll = (spd_after_epoll_t *) pointDetails;
      is_valid = spd_after_epoll_is_valid(spd_after_epoll);
      break;
    case SCHEDULE_POINT_LOOPER_IOPOLL_BEFORE_HANDLING_EVENTS:
      spd_iopoll_before_handling_events = (spd_iopoll_before_handling_events_t *) pointDetails;
      is_valid = spd_iopoll_before_handling_events_is_valid(spd_iopoll_before_handling_events);
      break;
    case SCHEDULE_POINT_LOOPER_GETTING_DONE:
      spd_getting_done = (spd_getting_done_t *) pointDetails;
      is_valid = spd_getting_done_is_valid(spd_getting_done);
      break;
    case SCHEDULE_POINT_TP_WANTS_WORK:
      spd_wants_work = (spd_wants_work_t *) pointDetails;
      is_valid = spd_wants_work_is_valid(spd_wants_work);
      break;
    case SCHEDULE_POINT_TP_GETTING_WORK:
      spd_getting_work = (spd_getting_work_t *) pointDetails;
      is_valid = spd_getting_work_is_valid(spd_getting_work);
      break;
    case SCHEDULE_POINT_TP_GOT_WORK:
      spd_got_work = (spd_got_work_t *) pointDetails;
      is_valid = spd_got_work_is_valid(spd_got_work);
      break;
    case SCHEDULE_POINT_TP_BEFORE_PUT_DONE:
      spd_before_put_done = (spd_before_put_done_t *) pointDetails;
      is_valid = spd_before_put_done_is_valid(spd_before_put_done);
      break;
    case SCHEDULE_POINT_TP_AFTER_PUT_DONE:
      spd_after_put_done = (spd_after_put_done_t *) pointDetails;
      is_valid = spd_after_put_done_is_valid(spd_after_put_done);
      break;
    case SCHEDULE_POINT_TIMER_READY:
      spd_timer_ready = (spd_timer_ready_t *) pointDetails;
      is_valid = spd_timer_ready_is_valid(spd_timer_ready);
      break;
    case SCHEDULE_POINT_TIMER_RUN:
      spd_timer_run = (spd_timer_run_t *) pointDetails;
      is_valid = spd_timer_run_is_valid(spd_timer_run);
      break;
    case SCHEDULE_POINT_TIMER_NEXT_TIMEOUT:
      spd_timer_next_timeout = (spd_timer_next_timeout_t *) pointDetails;
      is_valid = spd_timer_next_timeout_is_valid(spd_timer_next_timeout);
      break;
    default:
      assert(!"schedule_point_looks_valid: Error, unexpected point");
  }

  return is_valid;
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

  /* Things we can track ourselves (not handled by a schedulerImpl_t). */
  int n_executed; /* Protected by mutex. */
  struct map *tidToType;
  uv_thread_t current_cb_thread;

  /* Synchronization. */
  reentrant_mutex_t *mutex; /* Control using scheduler__[un]lock. */

  /* Implementation-dependent. */
  schedulerImpl_t impl;
} scheduler;


/***********************
 * Private scheduler API declarations.
 ***********************/

/* Returns non-zero if scheduler is initialized and magic is OK. */
int scheduler__looks_valid (void);

/* Returns the current holder of scheduler.mutex, or REENTRANT_MUTEX_NO_HOLDER if no holder. */
uv_thread_t scheduler__current_lock_holder (void);

/* Returns the depth of scheduler.mutex. */
int scheduler__lock_depth (void);

/***********************
 * Public scheduler API definitions.
 ***********************/

void scheduler_init (scheduler_type_t type, scheduler_mode_t mode, char *schedule_file, void *args)
{
  struct timespec now;
  long now_us = 0;

  assert(!scheduler_initialized);

  assert(clock_gettime(CLOCK_MONOTONIC_RAW, &now) == 0);
  now_us = timespec_us(&now);
  mylog(LOG_SCHEDULER, 1, "scheduler_init: seeding RNG with %lu\n", now_us);
  srand(now_us);

  /* Shared amongst all scheduler implementations. */
  scheduler.magic = SCHEDULER_MAGIC;
  scheduler.type = type;
  scheduler.mode = mode;
  strncpy(scheduler.schedule_file, schedule_file, sizeof scheduler.schedule_file);
  scheduler.args = args;

  scheduler.n_executed = 0;
  scheduler.tidToType = map_create();
  assert(scheduler.tidToType != NULL);
  scheduler.current_cb_thread = NO_CURRENT_CB_THREAD;

  scheduler.mutex = reentrant_mutex_create();
  assert(scheduler.mutex != NULL);

  /* Specifics based on the scheduler type. */
  switch (scheduler.type)
  {
    case SCHEDULER_TYPE_VANILLA:
#if defined(ENABLE_SCHEDULER_VANILLA)
      scheduler_vanilla_init(mode, args, &scheduler.impl);
      break;
#endif
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
    case SCHEDULER_TYPE_TP_FREEDOM:
#if defined(ENABLE_SCHEDULER_TP_FREEDOM)
      scheduler_tp_freedom_init(mode, args, &scheduler.impl);
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
    scheduler.n_executed++; /* We hold scheduler__lock. */

  scheduler.impl.thread_yield(point, schedule_point_details);

  /* Ensure mutex during CB execution. */
  switch (point)
  {
    case SCHEDULE_POINT_BEFORE_EXEC_CB:
      scheduler__lock();
      scheduler.current_cb_thread = uv_thread_self();
      break;
    case SCHEDULE_POINT_AFTER_EXEC_CB:
      assert(scheduler_current_cb_thread() == uv_thread_self());
      /* If we're executing the bottom-most CB in a stack, there's no current CB thread. */
      if (scheduler__lock_depth() == 1)
        scheduler.current_cb_thread = NO_CURRENT_CB_THREAD;
      scheduler__unlock();
      break;
    default:
      /* Nothing to do. */
      break;
  }

  return;
}

uv_thread_t scheduler_current_cb_thread (void)
{
  return scheduler.current_cb_thread;
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

uv_thread_t scheduler__current_lock_holder (void)
{
  assert(scheduler__looks_valid());
  return reentrant_mutex_holder(scheduler.mutex);
}

int scheduler__lock_depth (void)
{
  assert(scheduler__looks_valid());
  return reentrant_mutex_depth(scheduler.mutex);
}
