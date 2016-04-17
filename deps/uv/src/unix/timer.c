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

/* For walking the heap in heap_apply. */
struct heap_apply_info
{
  struct list *list;
  enum execution_context exec_context;
};

static int uv__timer_ready(uv_timer_t *handle)
{
  int ready = 0;

  mylog(LOG_TIMER, 9, "uv__timer_ready: begin: handle %p\n", handle);
  assert(handle);

  ready = (handle->timeout < handle->loop->time);
  mylog(LOG_TIMER, 9, "uv__timer_ready: returning ready %i (timeout %llu time %llu)\n", ready, handle->timeout, handle->loop->time);
  return ready;
}

/* Wrapper around uv__timer_ready for use with heap_walk.
   AUX is a heap_apply_info. 
   If the timer of HN is ready, create a sched_context for it and add to the list in AUX. */
static void uv__heap_timer_ready(struct heap_node *hn, void *aux)
{
  uv_timer_t* handle;
  struct heap_apply_info *hai;
  sched_context_t *sched_context;

  assert(hn);
  assert(aux);

  hai = (struct heap_apply_info *) aux;
  assert(hai->list);
  handle = container_of(hn, uv_timer_t, heap_node);
  if (uv__timer_ready(handle))
  {
    sched_context = sched_context_create(hai->exec_context, CALLBACK_CONTEXT_HANDLE, handle);
    list_push_back(hai->list, &sched_context->elem);
  }
}

static int timer_less_than(const struct heap_node* ha,
                           const struct heap_node* hb) {
  const uv_timer_t* a;
  const uv_timer_t* b;

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

/* Returns a list of sched_context_t's describing the ready timers.
   Callers are responsible for cleaning up the list, perhaps like this: 
     list_destroy_full(ready_timers, sched_context_destroy_func, NULL) */
static struct list * uv__ready_timers(uv_loop_t* loop, enum execution_context exec_context) {
  struct list *ready_timers = list_create();
  struct heap_apply_info hai;

  hai.list = ready_timers;
  hai.exec_context = exec_context;

  heap_walk((struct heap*) &loop->timer_heap, uv__heap_timer_ready, &hai);
  return ready_timers;
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

  if (cb == NULL)
    return -EINVAL;

  if (uv__is_active(handle))
    uv_timer_stop(handle);

  clamped_timeout = handle->loop->time + timeout;
  if (clamped_timeout < timeout)
    clamped_timeout = (uint64_t) -1;

  handle->timer_cb = cb;
  handle->timeout = clamped_timeout;
  handle->repeat = repeat;

  /* start_id is the second index to be compared in uv__timer_cmp() */
  handle->start_id = handle->loop->timer_counter++;

#ifdef UNIFIED_CALLBACK
  uv__register_callback(handle, (any_func) cb, UV_TIMER_CB);
#endif

  heap_insert((struct heap*) &handle->loop->timer_heap,
              (struct heap_node*) &handle->heap_node,
              timer_less_than);
  uv__handle_start(handle);

  return 0;
}


int uv_timer_stop(uv_timer_t* handle) {
  if (!uv__is_active(handle))
    return 0;

  heap_remove((struct heap*) &handle->loop->timer_heap,
              (struct heap_node*) &handle->heap_node,
              timer_less_than);

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
  uint64_t diff;

  heap_node = heap_min((const struct heap*) &loop->timer_heap);
  if (heap_node == NULL)
    return -1; /* block indefinitely */

  handle = container_of(heap_node, const uv_timer_t, heap_node);
  if (handle->timeout <= loop->time)
    return 0;

  diff = handle->timeout - loop->time;
  if (diff > INT_MAX)
  {
    /* JD: TODO. If scheduler delays execution of a timer,
      handle->timeout will be less than the current loop time.
      This means that we may wait "forever" instead of going back 
      to uv__run_timers. */
    assert(!"uv__next_timeout: you should change this");
    diff = INT_MAX;
  }

  return diff;
}


void uv__run_timers(uv_loop_t* loop) {
  struct list *ready_timers = NULL;
  sched_context_t *next_timer_context = NULL;
  sched_lcbn_t *next_timer_lcbn = NULL;
  uv_timer_t* next_timer_handle = NULL;

  mylog(LOG_TIMER, 9, "uv__run_timers: begin: loop %p\n", loop);

  if (heap_empty((struct heap *) &loop->timer_heap))
  {
    mylog(LOG_TIMER, 7, "uv__run_timers: No timers\n");
    goto DONE;
  }

  ready_timers = uv__ready_timers(loop, EXEC_CONTEXT_UV__RUN_TIMERS);
  /* Timers are time-sensitive, so new ones may become viable after we run old ones.
     Loop until we've no longer got a timer to run. 
     At the top of the loop, ready_timers is up to date. */
  for (;;)
  {
    mylog(LOG_TIMER, 7, "uv__run_timers: %u timers ready\n", list_size(ready_timers));
    /* Extract the next timer to run. */
    next_timer_context = scheduler_next_context(ready_timers);
    if (list_empty(ready_timers) || !next_timer_context)
      break;

    /* Run the next timer. */
    next_timer_lcbn = scheduler_next_lcbn(next_timer_context);
    next_timer_handle = (uv_timer_t *) next_timer_context->wrapper;

    assert(next_timer_lcbn->lcbn == lcbn_get(next_timer_handle->cb_type_to_lcbn, UV_TIMER_CB));
    assert(uv__timer_ready(next_timer_handle));

    uv_timer_stop(next_timer_handle);
    uv_timer_again(next_timer_handle);
#if UNIFIED_CALLBACK
    invoke_callback_wrap((any_func) next_timer_handle->timer_cb, UV_TIMER_CB, (long) next_timer_handle);
#else
    handle->timer_cb(next_timer_handle);
#endif

    /* Refresh the list. */
    list_destroy_full(ready_timers, sched_context_list_destroy_func, NULL);
    ready_timers = uv__ready_timers(loop, EXEC_CONTEXT_UV__RUN_TIMERS);
  }

  DONE:
    if (ready_timers)
      list_destroy_full(ready_timers, sched_context_list_destroy_func, NULL);
    mylog(LOG_TIMER, 9, "uv__run_timers: returning\n");
}

/* Returns a list of sched_lcbn_t's describing the ready LCBNs associated with HANDLE.
   Callers are responsible for cleaning up the list, perhaps like this: 
     list_destroy_full(ready_lcbns, sched_lcbn_destroy_func, NULL) */
struct list * uv__ready_timer_lcbns(void *h, enum execution_context exec_context) {
  uv_timer_t *handle = (uv_timer_t *) h;
  lcbn_t *lcbn = NULL;
  struct list *ready_timer_lcbns = list_create();
  
  assert(handle);
  assert(handle->type == UV_TIMER);

  switch (exec_context)
  {
    case EXEC_CONTEXT_UV__RUN_TIMERS:
      lcbn = lcbn_get(handle->cb_type_to_lcbn, UV_TIMER_CB);
      assert(lcbn && lcbn->cb == (any_func) handle->timer_cb);
      assert(lcbn->cb);
      list_push_back(ready_timer_lcbns, &sched_lcbn_create(lcbn)->elem);
      break;
    case EXEC_CONTEXT_UV__RUN_CLOSING_HANDLES:
      lcbn = lcbn_get(handle->cb_type_to_lcbn, UV_CLOSE_CB);
      assert(lcbn && lcbn->cb == (any_func) handle->close_cb);
      if (lcbn->cb)
        list_push_back(ready_timer_lcbns, &sched_lcbn_create(lcbn)->elem);
      break;
    default:
      assert(!"uv__ready_timer_lcbns: Error, unexpected context");
  }

  return ready_timer_lcbns;
}

void uv__timer_close(uv_timer_t* handle) {
  uv_timer_stop(handle);
}
