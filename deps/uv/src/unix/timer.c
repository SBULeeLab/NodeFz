/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "uv.h"
#include "internal.h"
#include "heap-inl.h"

#include "list.h"
#include "scheduler.h"

#include <assert.h>
#include <limits.h>
#include <stdlib.h> /* qsort */

/* Keeping pointers to timers in an array makes it easy to shuffle them. */
struct heap_timer_ready_aux
{
  uv_timer_t **arr;
  unsigned size;
  unsigned max_size;
};

/* Wrapper around scheduler_thread_yield(SCHEDULE_POINT_TIMER_RUN, ...) for use with heap_walk.
 * Identifies ready timers according to the scheduler's whims (i.e. time dilation).
 * AUX is a heap_timer_ready_aux. 
 */
static void uv__heap_timer_ready (struct heap_node *heap_node, void *aux)
{
  uv_timer_t *handle = NULL;
  struct heap_timer_ready_aux *htra = (struct heap_timer_ready_aux *) aux;
  spd_timer_ready_t spd_timer_ready;

  assert(heap_node);
  assert(aux);

  handle = container_of(heap_node, uv_timer_t, heap_node);

  /* Ask the scheduler whether this timer is ready. */
  spd_timer_ready_init(&spd_timer_ready);
  spd_timer_ready.timer = handle;
  spd_timer_ready.now = handle->loop->time;
  spd_timer_ready.ready = -1;
  scheduler_thread_yield(SCHEDULE_POINT_TIMER_READY, &spd_timer_ready);
  assert(spd_timer_ready.ready == 0 || spd_timer_ready.ready == 1);

  if (spd_timer_ready.ready)
  {
    assert(htra->size < htra->max_size);
    htra->arr[htra->size] = handle;
    htra->size++;
  }
  
  return;
}

/* a and b are uv_timer_t **'s. "two arguments that point to the objects being compared."
 * Returns -1 if a times out before b, 1 if b times out before a, 0 in the event of a tie (is this possible?).
 */
static int qsort_timer_cmp (const void *a, const void *b)
{
  const uv_timer_t *a_timer = *(const uv_timer_t **) a;
  const uv_timer_t *b_timer = *(const uv_timer_t **) b;

  if (a_timer->timeout < b_timer->timeout)
    return -1;
  if (b_timer->timeout < a_timer->timeout)
    return 1;

  /* Compare start_id when both have the same timeout. start_id is
   * allocated with loop->timer_counter in uv_timer_start().
   */
  if (a_timer->start_id < b_timer->start_id)
    return -1;
  if (b_timer->start_id < a_timer->start_id)
    return 1;

  return 0;
}

static int heap_timer_less_than (const struct heap_node* ha, const struct heap_node* hb)
{
  const uv_timer_t *a = NULL, *b = NULL;

  a = container_of(ha, const uv_timer_t, heap_node);
  b = container_of(hb, const uv_timer_t, heap_node);

  if (a->timeout < b->timeout)
    return 1;
  if (b->timeout < a->timeout)
    return 0;

  /* Compare start_id when both have the same timeout. start_id is
   * allocated with loop->timer_counter in uv_timer_start().
   */
  if (a->start_id < b->start_id)
    return 1;
  if (b->start_id < a->start_id)
    return 0;

  return 0;
}


/* Returns the ready timers. Caller is responsible for uv__free'ing htra.arr. 
 * The ready timers are placed in the htra.arr sorted by timeout, so
 * if un-shuffled they'll be executed in the "natural" order.
 */
static struct heap_timer_ready_aux uv__ready_timers (uv_loop_t* loop)
{
  struct heap *timer_heap = (struct heap *) &loop->timer_heap;
  uv_timer_t **timerP_arr = NULL; 
  struct heap_timer_ready_aux htra;

  timerP_arr = (uv_timer_t **) uv__malloc(sizeof(uv_timer_t *) * timer_heap->nelts);
  assert(timerP_arr != NULL);

  htra.arr = timerP_arr;
  htra.size = 0;
  htra.max_size = timer_heap->nelts;

  /* Walk the whole heap, since uv__heap_timer_ready might dilate time for some and not others, in effect letting some timers "jump ahead". */
  heap_walk((struct heap*) &loop->timer_heap, uv__heap_timer_ready, &htra);

  /* Sort timers in increasing {timeout, start_id} order.
   * (heap_walk doesn't traverse in strict order of {timeout, start_id}, so htra.arr is not ordered). 
   * No need for qsort_r because we're on the (sole) looper thread. */
  qsort(htra.arr, htra.size, sizeof(uv_timer_t *), qsort_timer_cmp);

  return htra;
}

int uv_timer_init(uv_loop_t* loop, uv_timer_t* handle) {
  uv__handle_init(loop, (uv_handle_t*)handle, UV_TIMER);
  handle->timer_cb = NULL;
  handle->repeat = 0;
  return 0;
}

int uv_timer_start(uv_timer_t* handle,
                   uv_timer_cb cb,
                   uint64_t timeout,
                   uint64_t repeat) {
  uint64_t clamped_timeout;
  int rc = 0;

  ENTRY_EXIT_LOG((LOG_TIMER, 9, "uv_timer_start: begin: handle %p timeout %llu repeat %llu\n", handle, timeout, repeat));

  if (cb == NULL)
  {
    rc = -EINVAL;
    goto DONE;
  }

  if (uv__is_active(handle))
    uv_timer_stop(handle);

  clamped_timeout = handle->loop->time + timeout;
  if (clamped_timeout < timeout) /* Overflow? */
    clamped_timeout = (uint64_t) -1;
  mylog(LOG_TIMER, 7, "uv_timer_start: loop->time %llu handle %p timeout after %llu timeout loop time %llu\n", handle->loop->time, handle, timeout, clamped_timeout);

  handle->timer_cb = cb;
  handle->timeout = clamped_timeout;
  handle->start_time = handle->loop->time;
  handle->repeat = repeat;

  /* start_id is the second index to be compared in uv__timer_cmp() */
  handle->start_id = handle->loop->timer_counter++;

#ifdef UNIFIED_CALLBACK
  uv__register_callback(handle, (any_func) cb, UV_TIMER_CB);
#endif

  heap_insert((struct heap*) &handle->loop->timer_heap,
              (struct heap_node*) &handle->heap_node,
              heap_timer_less_than);
  uv__handle_start(handle);

  rc = 0;
  DONE:
    ENTRY_EXIT_LOG((LOG_TIMER, 9, "uv_timer_start: returning rc %i\n", rc));
    return rc;
}


int uv_timer_stop(uv_timer_t* handle) {
  if (!uv__is_active(handle))
    return 0;

  mylog(LOG_TIMER, 7, "uv_timer_stop: Stopping timer (handle %p)\n", handle);

  heap_remove((struct heap*) &handle->loop->timer_heap,
              (struct heap_node*) &handle->heap_node,
              heap_timer_less_than);

  uv__handle_stop(handle);

  return 0;
}


int uv_timer_again(uv_timer_t* handle) {
  if (handle->timer_cb == NULL)
    return -EINVAL;

  if (handle->repeat) {
    uv_timer_stop(handle);
    uv_timer_start(handle, handle->timer_cb, handle->repeat, handle->repeat);
  }

  return 0;
}


void uv_timer_set_repeat(uv_timer_t* handle, uint64_t repeat) {
  handle->repeat = repeat;
}


uint64_t uv_timer_get_repeat(const uv_timer_t* handle) {
  return handle->repeat;
}


int uv__next_timeout(const uv_loop_t* loop) {
  const struct heap_node* heap_node;
  const uv_timer_t* handle;
  spd_timer_next_timeout_t spd_timer_next_timeout;

  heap_node = heap_min((const struct heap*) &loop->timer_heap);
  if (heap_node == NULL)
    return -1; /* block indefinitely */

  handle = container_of(heap_node, const uv_timer_t, heap_node);

  spd_timer_next_timeout_init(&spd_timer_next_timeout);
  spd_timer_next_timeout.timer = (uv_timer_t * /* I promise not to modify it */) handle;
  spd_timer_next_timeout.now = loop->time;
  scheduler_thread_yield(SCHEDULE_POINT_TIMER_NEXT_TIMEOUT, &spd_timer_next_timeout);

  /* We have to return an int, so cap if needed. */
  if (INT_MAX < spd_timer_next_timeout.time_until_timer)
    spd_timer_next_timeout.time_until_timer = INT_MAX;

  mylog(LOG_TIMER, 7, "uv__next_timeout: time_until_timer %llu\n", spd_timer_next_timeout.time_until_timer);
  return spd_timer_next_timeout.time_until_timer;
}

void uv__run_timers(uv_loop_t* loop) {
  unsigned i;
  spd_timer_run_t spd_timer_run;
  int *should_run = NULL;
  struct heap_timer_ready_aux htra;

  /* Calculating ready timers here means that any timers registered by ready timers
   *  won't be candidates for execution until the next time we call uv__run_timers.
   * This is appropriate since it matches the Node.js docs (timers have a minimum timeout of 1ms in the future).
   */
  htra = uv__ready_timers(loop);
  should_run = (int *) uv__malloc(htra.size * sizeof(int));
  assert(should_run != NULL);
  memset(should_run, 0, sizeof(int)*htra.size);

  spd_timer_run_init(&spd_timer_run);
  /* Ask scheduler which timers to handle in what order. 
   * Scheduler will defer every timer after the first deferred one,
   * so that there is no shuffling due to deferral. */
  spd_timer_run_init(&spd_timer_run);
  spd_timer_run.shuffleable_items.item_size = sizeof(uv_timer_t *);
  spd_timer_run.shuffleable_items.nitems = htra.size;
  spd_timer_run.shuffleable_items.items = htra.arr;
  spd_timer_run.shuffleable_items.thoughts = should_run;
  scheduler_thread_yield(SCHEDULE_POINT_TIMER_RUN, &spd_timer_run);

  for (i = 0; i < spd_timer_run.shuffleable_items.nitems; i++)
  {
    uv_timer_t *timer = ((uv_timer_t **) spd_timer_run.shuffleable_items.items)[i];
    if (spd_timer_run.shuffleable_items.thoughts[i] == 1)
    {
      mylog(LOG_TIMER, 7, "uv__run_timers: running timer %p, time is %llu, timer has timeout %llu\n", timer, timer->loop->time, timer->timeout);
      uv_timer_stop(timer);
      uv_timer_again(timer);

     #if UNIFIED_CALLBACK
      invoke_callback_wrap((any_func) timer->timer_cb, UV_TIMER_CB, (long) timer);
     #else
      timer->timer_cb(timer);
     #endif
    }
    else
      /* If performance is a problem, we could break out of the loop here. */
      mylog(LOG_TIMER, 7, "uv__run_timers: deferring ready timer %p\n", timer);
  }

  uv__free(htra.arr);
  uv__free(should_run);

  return;
}

void uv__timer_close(uv_timer_t* handle) {
  uv_timer_stop(handle);
}
