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

struct heap_apply_info
{
  struct list *list;
  enum execution_context exec_context;
};

static int uv__timer_ready(uv_timer_t *handle)
{
  assert(handle);
  return (handle->timeout < handle->loop->time);
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
  struct list *ready_timers;
  struct heap_apply_info hai;

  ready_timers = list_create();
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
  uv__register_callback(handle, cb, UV_TIMER_CB);
#endif

  heap_insert((struct heap*) &handle->loop->timer_heap,
              (struct heap_node*) &handle->heap_node,
              timer_less_than);
  uv__handle_start(handle);

#if UNIFIED_CALLBACK
  /* Might not be NULL; the parent of a repeating timer (itself) is set in invoke_callback. 
     TODO This assumes that once uv_timer_start'd, a timer is never stop'd and start'd by the
     owner of the handle. Need to differentiate between uv_timer_again and other uses. */
  if (handle->logical_parent == NULL)
  {
    handle->logical_parent = current_callback_node_get();
    handle->self_parent = 0;
  }
  assert(handle->logical_parent != NULL);
#endif

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
    diff = INT_MAX;

  return diff;
}


void uv__run_timers(uv_loop_t* loop) {
  struct list *ready_timers;
  sched_context_t *next_timer_context;
  sched_lcbn_t *next_timer_lcbn;
  uv_timer_t* next_timer_handle;

  ready_timers = NULL;
  for (;;) {
    if (ready_timers)
      list_destroy_full(ready_timers, sched_context_list_destroy_func, NULL);

    /* Find the ready timers. */
    ready_timers = uv__ready_timers(loop, EXEC_CONTEXT_UV__RUN_TIMERS);

    /* Extract the next timer to run. */
    next_timer_context = scheduler_next_context(ready_timers);
    if (list_empty(ready_timers) || !next_timer_context)
      break;

    /* Run the next timer. */
    next_timer_lcbn = scheduler_next_lcbn(next_timer_context);
    next_timer_handle = (uv_timer_t *) next_timer_context->handle_or_req;

    assert(next_timer_lcbn->lcbn == lcbn_get(next_timer_handle->cb_type_to_lcbn, UV_TIMER_CB));
    assert(uv__timer_ready(next_timer_handle));

    uv_timer_stop(next_timer_handle);
    uv_timer_again(next_timer_handle);
#if UNIFIED_CALLBACK
    INVOKE_CALLBACK_1(UV_TIMER_CB, next_timer_handle->timer_cb, next_timer_handle);
#else
    handle->timer_cb(next_timer_handle);
#endif
  }

  if (ready_timers)
    list_destroy_full(ready_timers, sched_context_list_destroy_func, NULL);
}

/* Returns a list of sched_lcbn_t's describing the ready LCBNs associated with HANDLE.
   Callers are responsible for cleaning up the list, perhaps like this: 
     list_destroy_full(ready_lcbns, sched_lcbn_destroy_func, NULL) */
struct list * uv__ready_timer_lcbns(void *h, enum execution_context exec_context) {
  uv_handle_t *handle;
  lcbn_t *lcbn;
  struct list *ready_timer_lcbns;
  
  assert(exec_context == EXEC_CONTEXT_UV__RUN_TIMERS);

  handle = (uv_handle_t *) h;
  assert(handle);
  lcbn = lcbn_get(handle->cb_type_to_lcbn, UV_TIMER_CB);
  assert(lcbn);

  /* Timers have only one pending CB. */
  ready_timer_lcbns = list_create();
  list_push_back(ready_timer_lcbns, &sched_lcbn_create(lcbn)->elem);
  return ready_timer_lcbns;
}

void uv__timer_close(uv_timer_t* handle) {
  uv_timer_stop(handle);
}
