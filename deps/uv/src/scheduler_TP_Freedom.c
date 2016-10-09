#include "scheduler_TP_Freedom.h"

#include "unix/linux-syscalls.h" /* struct uv__epoll_event */
#include "scheduler.h"
#include "timespec_funcs.h"
#include "uv-random.h"

#include <unistd.h> /* usleep, unlink */
#include <string.h> /* memcpy */
#include <stdlib.h> /* srand, getenv */
#include <time.h>   /* time */
#include <assert.h>

#define MIN(x, y) ((x) < (y) ? (x) : (y))

static int SCHEDULER_TP_FREEDOM_MAGIC = 81929393;

/* implDetails for the fuzzing timer scheduler. */

static struct
{
  int magic;

  int mode;
  scheduler_tp_freedom_args_t args;

  int looper_in_epoll; /* Non-zero if looper thread is between SCHEDULE_POINT_LOOPER_BEFORE_EPOLL and AFTER_EPOLL. */
  struct timespec looper_epoll_start_time; /* If looper_in_epoll, this is when looper reached SCHEDULE_POINT_LOOPER_BEFORE_EPOLL. */
} tpFreedom_implDetails;

/***********************
 * Private API declarations
 ***********************/

/* Returns non-zero if the scheduler_tp_freedom looks valid (e.g. is initialized properly). */
static int scheduler_tp_freedom__looks_valid (void);

/* Returns the queue len of q. */
static int scheduler_tp_freedom__queue_len (QUEUE *q);

/* Shuffle array of nitems events, each of size item_size.
 * Break the array into chunks of size degrees_of_freedom and shuffle each chunk.
 * degrees_of_freedom == -1 means "shuffle everything together".
 */
static void scheduler_tp_freedom__shuffle_items (int degrees_of_freedom, void *events, int nitems, size_t item_size);

/***********************
 * Public API definitions
 ***********************/

void
scheduler_tp_freedom_init (scheduler_mode_t mode, void *args, schedulerImpl_t *schedulerImpl)
{
  const char *tpSize = NULL;

  assert(args != NULL);
  assert(schedulerImpl != NULL);

  /* The TP Freedom scheduler simulates a multi-thread TP using a single TP thread. */
  tpSize = getenv("UV_THREADPOOL_SIZE");
  assert(tpSize != NULL && atoi(tpSize) == 1);

  /* Populate schedulerImpl. */
  schedulerImpl->register_lcbn = scheduler_tp_freedom_register_lcbn;
  schedulerImpl->next_lcbn_type = scheduler_tp_freedom_next_lcbn_type;
  schedulerImpl->thread_yield = scheduler_tp_freedom_thread_yield;
  schedulerImpl->emit = scheduler_tp_freedom_emit;
  schedulerImpl->lcbns_remaining = scheduler_tp_freedom_lcbns_remaining;
  schedulerImpl->schedule_has_diverged = scheduler_tp_freedom_schedule_has_diverged;

  /* Set implDetails. */
  memset(&tpFreedom_implDetails, 0, sizeof tpFreedom_implDetails);
  tpFreedom_implDetails.magic = SCHEDULER_TP_FREEDOM_MAGIC;
  tpFreedom_implDetails.mode = mode;
  tpFreedom_implDetails.args = *(scheduler_tp_freedom_args_t *) args;

  assert(tpFreedom_implDetails.args.tp_degrees_of_freedom == -1 || 1 <= tpFreedom_implDetails.args.tp_degrees_of_freedom);
  assert(0 <= tpFreedom_implDetails.args.iopoll_defer_perc && tpFreedom_implDetails.args.iopoll_defer_perc <= 100);

  return;
}

void
scheduler_tp_freedom_register_lcbn (lcbn_t *lcbn)
{
  assert(scheduler_tp_freedom__looks_valid());
  assert(lcbn != NULL && lcbn_looks_valid(lcbn));

  return;
}

enum callback_type
scheduler_tp_freedom_next_lcbn_type (void)
{
  assert(scheduler_tp_freedom__looks_valid());
  return CALLBACK_TYPE_ANY;
}

void
scheduler_tp_freedom_thread_yield (schedule_point_t point, void *pointDetails)
{
  assert(scheduler_tp_freedom__looks_valid());
  /* Ensure {point, pointDetails} are consistent. Afterwards we know the inputs are correct. */
  assert(schedule_point_looks_valid(point, pointDetails));

  if (point == SCHEDULE_POINT_TP_WANTS_WORK)
  {
    /* For SCHEDULE_POINT_TP_WANTS_WORK, decide whether or not to let the worker get work. 
     * Rules:
     *    - If the wq holds at least tp_degrees_of_freedom items, we have nothing more to wait for
     *    - Once looper blocks, we know there won't be more items
     *    - Worker can't wait too long
     */

    spd_wants_work_t *spd_wants_work = (spd_wants_work_t *) pointDetails;
    int queue_len = scheduler_tp_freedom__queue_len(spd_wants_work->wq);
    struct timespec now, wait_diff, looper_epoll_diff;
    long wait_diff_us = 0, looper_epoll_diff_us = 0;

    assert(clock_gettime(CLOCK_MONOTONIC_RAW, &now) == 0);
  
    /* TODO Weirdly I'm seeing cases where now is before start_time. Not sure how this happens, but ignoring it for now. */
    if (timespec_cmp(&now, &spd_wants_work->start_time) == 1)
    {
      timespec_sub(&now, &spd_wants_work->start_time, &wait_diff);
      wait_diff_us = timespec_us(&wait_diff);
    }
    else
      wait_diff_us = 0;

    if (tpFreedom_implDetails.looper_in_epoll)
    {
      /* TODO Weirdly I'm seeing cases where now is before looper_epoll_start_time. Not sure how this happens, but ignoring it for now. */
      if (timespec_cmp(&now, &tpFreedom_implDetails.looper_epoll_start_time) == 1)
      {
        timespec_sub(&now, &tpFreedom_implDetails.looper_epoll_start_time, &looper_epoll_diff);
        looper_epoll_diff_us = timespec_us(&looper_epoll_diff);
      }
      else
        looper_epoll_diff_us = 0;
    }

    if (0 < tpFreedom_implDetails.args.tp_degrees_of_freedom && tpFreedom_implDetails.args.tp_degrees_of_freedom <= queue_len)
    {
      mylog(LOG_SCHEDULER, 1, "scheduler_tp_freedom_thread_yield: thread can get work (tp_degrees_of_freedom %i, queue_len %i) (%s)\n", tpFreedom_implDetails.args.tp_degrees_of_freedom, queue_len, schedule_point_to_string(point));
      spd_wants_work->should_get_work = 1;
    }
    else if (tpFreedom_implDetails.args.tp_max_delay_us <= wait_diff_us)
    {
      mylog(LOG_SCHEDULER, 1, "scheduler_tp_freedom_thread_yield: thread can get work (tp_max_delay_us %llu exceeded) (%s)\n", tpFreedom_implDetails.args.tp_max_delay_us, schedule_point_to_string(point));
      spd_wants_work->should_get_work = 1;
    }
    else if (tpFreedom_implDetails.looper_in_epoll && tpFreedom_implDetails.args.tp_epoll_threshold <= looper_epoll_diff_us)
    {
      mylog(LOG_SCHEDULER, 1, "scheduler_tp_freedom_thread_yield: thread can get work (looper blocked in epoll for more than %llu us, no more work coming) (%s)\n", tpFreedom_implDetails.args.tp_epoll_threshold, schedule_point_to_string(point));
      spd_wants_work->should_get_work = 1;
    }
    else
      mylog(LOG_SCHEDULER, 1, "scheduler_tp_freedom_thread_yield: thread can't get work yet (tp_degrees_of_freedom %i, queue_len %i; delay %llu tp_max_delay_us %llu; looper_in_epoll %i looper_epoll_diff_us %li tp_epoll_threshold %li) (%s)\n", tpFreedom_implDetails.args.tp_degrees_of_freedom, queue_len, wait_diff_us, tpFreedom_implDetails.args.tp_max_delay_us, tpFreedom_implDetails.looper_in_epoll, looper_epoll_diff_us, tpFreedom_implDetails.args.tp_epoll_threshold, schedule_point_to_string(point));
  }
  else if (point == SCHEDULE_POINT_TP_GETTING_WORK || point == SCHEDULE_POINT_LOOPER_GETTING_DONE)
  {
    /* For SCHEDULE_POINT_..._GETTING_{WORK,DONE}, choose the queue index. */

    QUEUE *wq = NULL;
    int *indexP = NULL; /* Points to "index" field of the pointDetails object. */
    int wq_len = 0;
    int wq_ix = 0;
    int deg_freedom = tpFreedom_implDetails.args.tp_degrees_of_freedom;

    if (point == SCHEDULE_POINT_TP_GETTING_WORK)
    {
      wq = ((spd_getting_work_t *) pointDetails)->wq;
      indexP = &((spd_getting_work_t *) pointDetails)->index;
    }
    else if (point == SCHEDULE_POINT_LOOPER_GETTING_DONE)
    {
      wq = ((spd_getting_done_t *) pointDetails)->wq;
      indexP = &((spd_getting_done_t *) pointDetails)->index;
    }
    else
      assert(!"scheduler_tp_freedom_thread_yield: Error, how did we get here?");
    assert(wq != NULL && indexP != NULL);

    wq_len = scheduler_tp_freedom__queue_len(wq);
    assert(0 < wq_len);

    /* -1 means "pick any item". */
    if (deg_freedom == -1)
      deg_freedom = wq_len;

    wq_ix = rand_int(MIN(wq_len, deg_freedom));
    mylog(LOG_SCHEDULER, 1, "scheduler_tp_freedom_thread_yield: Chose wq_ix %i (item %i/%i) (%s)\n", wq_ix, wq_ix+1, wq_len, schedule_point_to_string(point));

    *indexP = wq_ix;
  }
  else if (point == SCHEDULE_POINT_LOOPER_BEFORE_EPOLL)
  {
    assert(!tpFreedom_implDetails.looper_in_epoll);
    tpFreedom_implDetails.looper_in_epoll = 1;
    assert(clock_gettime(CLOCK_MONOTONIC_RAW, &tpFreedom_implDetails.looper_epoll_start_time) == 0);
  }
  else if (point == SCHEDULE_POINT_LOOPER_AFTER_EPOLL)
  {
    assert(tpFreedom_implDetails.looper_in_epoll);
    tpFreedom_implDetails.looper_in_epoll = 0;
  }
  else if (point == SCHEDULE_POINT_LOOPER_IOPOLL_BEFORE_HANDLING_EVENTS)
  {
    /* For SCHEDULE_POINT_LOOPER_IOPOLL_BEFORE_HANDLING_EVENTS, decide the order of events and whether or not to handle each one.
     * Rules:
     *    - If the wq holds at least tp_degrees_of_freedom items, we have nothing more to wait for
     *    - Once looper blocks, we know there won't be more items
     *    - Worker can't wait too long
     */

    spd_iopoll_before_handling_events_t *spd_iopoll_before_handling_events = (spd_iopoll_before_handling_events_t *) pointDetails;

    if (0 < spd_iopoll_before_handling_events->shuffleable_items.nitems)
    {
      unsigned i = 0, should_defer = 0;

      /* Shuffle events to permute input order. */
      mylog(LOG_SCHEDULER, 1, "scheduler_tp_freedom_thread_yield: shuffling %i events with %i degrees of freedom\n", spd_iopoll_before_handling_events->shuffleable_items.nitems, tpFreedom_implDetails.args.iopoll_degrees_of_freedom);
      scheduler_tp_freedom__shuffle_items(tpFreedom_implDetails.args.iopoll_degrees_of_freedom, spd_iopoll_before_handling_events->shuffleable_items.items, spd_iopoll_before_handling_events->shuffleable_items.nitems, spd_iopoll_before_handling_events->shuffleable_items.item_size);

      /* Defer events. */
      mylog(LOG_SCHEDULER, 1, "scheduler_tp_freedom_thread_yield: deferring %i%% of events\n", tpFreedom_implDetails.args.iopoll_defer_perc);
      for (i = 0; i < spd_iopoll_before_handling_events->shuffleable_items.nitems; i++)
      {
        should_defer = (rand_int(100) < tpFreedom_implDetails.args.iopoll_defer_perc);
        spd_iopoll_before_handling_events->shuffleable_items.thoughts[i] = !should_defer;
        mylog(LOG_SCHEDULER, 1, "scheduler_tp_freedom_thread_yield: event %i should_handle_event %i\n", i, spd_iopoll_before_handling_events->shuffleable_items.thoughts[i]);
      }
    }
  }
  else if (point == SCHEDULE_POINT_TIMER_READY)
  {
    spd_timer_ready_t *spd_timer_ready = (spd_timer_ready_t *) pointDetails;
    int is_ready = -1;
    spd_timer_ready->ready = -1;

    is_ready = (spd_timer_ready->timer->timeout < spd_timer_ready->now);

    if (is_ready)
      spd_timer_ready->ready = 1;
    else
    {
      /* If it's not ready yet, check if we should run it early. */
      int might_go_early_choice = rand_int(1000); /* tenths of a percent */
      mylog(LOG_SCHEDULER, 1, "scheduler_tp_freedom_thread_yield: %s might_go_early_choice %i early_exec_tperc %i\n", schedule_point_to_string(point), might_go_early_choice, tpFreedom_implDetails.args.timer_early_exec_tperc);
      if (might_go_early_choice < tpFreedom_implDetails.args.timer_early_exec_tperc)
      {
        /* We might run early. Check how early the timer would be. */
        int go_early = 0;
        mylog(LOG_SCHEDULER, 1, "scheduler_tp_freedom_thread_yield: %s Timer %p might go early\n", schedule_point_to_string(point), spd_timer_ready->timer);

        if (tpFreedom_implDetails.args.timer_max_early_multiple == -1)
        {
          go_early = 1; /* Don't care about how early it is. */
          mylog(LOG_SCHEDULER, 1, "scheduler_tp_freedom_thread_yield: %s Timer %p not ready, but going early (timer_max_early_multiple %i)\n", spd_timer_ready->timer, schedule_point_to_string(point), tpFreedom_implDetails.args.timer_max_early_multiple);
        }
        else
        {
          uint64_t time_since_registration = spd_timer_ready->now - spd_timer_ready->timer->start_time;
          uint64_t total_timer_time = spd_timer_ready->timer->timeout - spd_timer_ready->timer->start_time;
          if (total_timer_time < time_since_registration * tpFreedom_implDetails.args.timer_max_early_multiple)
            go_early = 1; /* Close enough. */
          mylog(LOG_SCHEDULER, 1, "scheduler_tp_freedom_thread_yield: %s Timer %p go_early %i (total_timer_time %llu time_since_registration %llu timer_max_early_multiple %i)\n", schedule_point_to_string(point), spd_timer_ready->timer, go_early, total_timer_time, time_since_registration, tpFreedom_implDetails.args.timer_max_early_multiple);
        }

        if (go_early)
          spd_timer_ready->ready = 1;
        else
          spd_timer_ready->ready = 0;
      }
      else
      {
        mylog(LOG_SCHEDULER, 1, "scheduler_tp_freedom_thread_yield: %s Timer %p won't go early\n", schedule_point_to_string(point), spd_timer_ready->timer);
        spd_timer_ready->ready = 0;
      }
    }

    assert(spd_timer_ready->ready == 0 || spd_timer_ready->ready == 1);
  }
  else if (point == SCHEDULE_POINT_TIMER_RUN)
  {
    spd_timer_run_t *spd_timer_run = (spd_timer_run_t *) pointDetails;
    unsigned i;

    /* Timers have been declared ready. Shuffle, then decide whether to delay any. 
     * We delay all timers after the first delayed one to avoid additional shuffling. */

    /* Shuffle. */
    if (0 < spd_timer_run->shuffleable_items.nitems)
    {
      mylog(LOG_SCHEDULER, 1, "scheduler_tp_freedom_thread_yield: %s Shuffling %i timers with %i degrees of freedom\n", schedule_point_to_string(point), spd_timer_run->shuffleable_items.nitems, tpFreedom_implDetails.args.timer_degrees_of_freedom);
      scheduler_tp_freedom__shuffle_items(tpFreedom_implDetails.args.timer_degrees_of_freedom, spd_timer_run->shuffleable_items.items, spd_timer_run->shuffleable_items.nitems, spd_timer_run->shuffleable_items.item_size);
    }

    /* Delay. */
    for (i = 0; i < spd_timer_run->shuffleable_items.nitems; i++)
    {
      uv_timer_t *timer = ((uv_timer_t **) spd_timer_run->shuffleable_items.items)[i];
      int run = 0, go_late_choice = 0;

      go_late_choice = rand_int(1000); /* tenths of a percent */
      if (go_late_choice < tpFreedom_implDetails.args.timer_late_exec_tperc)
      {
        mylog(LOG_SCHEDULER, 1, "scheduler_tp_freedom_thread_yield: %s Timer %p ready, but deferring (and all the rest, too)\n", schedule_point_to_string(point), timer);
        run = 0;
      }
      else
      {
        mylog(LOG_SCHEDULER, 1, "scheduler_tp_freedom_thread_yield: %s Timer %p ready (timeout %llu), going as normal\n", schedule_point_to_string(point), timer, timer->timeout);
        run = 1;
      }

      spd_timer_run->shuffleable_items.thoughts[i] = run;
      if (!run)
      {
        /* Delay the rest. */
        int n_remaining = spd_timer_run->shuffleable_items.nitems - i - 1;
        if (n_remaining)
        {
          mylog(LOG_SCHEDULER, 1, "scheduler_tp_freedom_thread_yield: %s Delaying the remaining %i timers\n", schedule_point_to_string(point), n_remaining);
          memset(spd_timer_run->shuffleable_items.thoughts + i + 1, 0, n_remaining);
          break;
        }
      }
    }
  }
  else if (point == SCHEDULE_POINT_TIMER_NEXT_TIMEOUT)
  {
    spd_timer_next_timeout_t *spd_timer_next_timeout = (spd_timer_next_timeout_t *) pointDetails;

    uint64_t time_since_registration = spd_timer_next_timeout->now - spd_timer_next_timeout->timer->start_time;
    uint64_t earliest_time_for_timeout = 0;

    if (tpFreedom_implDetails.args.timer_max_early_multiple == -1)
      /* If there's flexibility on how early to make it, just say it's already due and we'll let probability take care of it. */
      earliest_time_for_timeout = spd_timer_next_timeout->now - 1;
    else
      earliest_time_for_timeout = MIN(spd_timer_next_timeout->timer->timeout, spd_timer_next_timeout->timer->start_time + time_since_registration * tpFreedom_implDetails.args.timer_max_early_multiple);

    if (earliest_time_for_timeout < spd_timer_next_timeout->now)
      spd_timer_next_timeout->time_until_timer = 0; 
    else
      spd_timer_next_timeout->time_until_timer = earliest_time_for_timeout - spd_timer_next_timeout->now;
    mylog(LOG_SCHEDULER, 1, "scheduler_tp_freedom_thread_yield: %s time_until_timer %llu (requested timeout %llu time_since_registration %llu earliest_time_for_timeout %llu now %llu)\n", schedule_point_to_string(point), spd_timer_next_timeout->time_until_timer, spd_timer_next_timeout->timer->timeout, time_since_registration, earliest_time_for_timeout, spd_timer_next_timeout->now);
  }

}

void
scheduler_tp_freedom_emit (char *output_file)
{
  assert(scheduler_tp_freedom__looks_valid());
  unlink(output_file);
  return;
}

int
scheduler_tp_freedom_lcbns_remaining (void)
{
  assert(scheduler_tp_freedom__looks_valid());
  return -1;
}

int
scheduler_tp_freedom_schedule_has_diverged (void)
{
  assert(scheduler_tp_freedom__looks_valid());
  return -1;
}

/***********************
 * Private API definitions.
 ***********************/

static int
scheduler_tp_freedom__looks_valid (void)
{
  return (tpFreedom_implDetails.magic == SCHEDULER_TP_FREEDOM_MAGIC);
}

static int
scheduler_tp_freedom__queue_len (QUEUE *q)
{
  QUEUE *qP = NULL;
  int queue_len = 0;

  assert(q != NULL);

  QUEUE_LEN(queue_len, qP, q);
  assert(0 <= queue_len);
  return queue_len;
}

static void
scheduler_tp_freedom__shuffle_items (int degrees_of_freedom, void *items, int nitems, size_t item_size)
{
  int chunk_len = 0, n_chunks = 0, last_chunk_len = 0, i = 0;
  char *itemsP = items;

  /* Nothing to shuffle. */
  if (nitems <= 1)
    return;
  /* Shuffling in chunks of 1 will have no effect. */
  if (degrees_of_freedom == 1)
    return;

  if (degrees_of_freedom == -1)
    chunk_len = nitems;
  else
    chunk_len = degrees_of_freedom;
  assert(0 < chunk_len);

  n_chunks = nitems / chunk_len;
  if (nitems % chunk_len != 0)
    n_chunks++;
  last_chunk_len = (nitems % chunk_len == 0) ? chunk_len : nitems % chunk_len;

  mylog(LOG_SCHEDULER, 1, "scheduler_tp_freedom__shuffle_items: nitems %i degrees_of_freedom %i chunk_len %i n_chunks %i\n", nitems, degrees_of_freedom, chunk_len, n_chunks);

  for (i = 0; i < n_chunks; i++)
  {
    int this_chunk_len = (i < n_chunks-1) ? chunk_len : last_chunk_len;
    mylog(LOG_SCHEDULER, 1, "scheduler_tp_freedom__shuffle_items: i %i n_chunks %i this_chunk_len %i\n", i, n_chunks, this_chunk_len);
    random_shuffle(itemsP, this_chunk_len, item_size);
    itemsP += this_chunk_len*item_size;
  }

  return;
}
