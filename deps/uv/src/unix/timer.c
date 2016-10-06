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

UV_UNUSED(static int uv__timer_ready(uv_timer_t *handle));

UV_UNUSED(static int uv__timer_ready(uv_timer_t *handle))
{
  int ready = 0;

  ENTRY_EXIT_LOG((LOG_TIMER, 9, "uv__timer_ready: begin: handle %p\n", handle));
  assert(handle);

  ready = (handle->timeout < handle->loop->time);
  ENTRY_EXIT_LOG((LOG_TIMER, 9, "uv__timer_ready: returning ready %i (timeout %llu time %llu)\n", ready, handle->timeout, handle->loop->time));
  return ready;
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

  ENTRY_EXIT_LOG((LOG_TIMER, 9, "uv__timer_ready: begin: handle %p timeout %llu repeat %llu\n", handle, timeout, repeat));

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
              timer_less_than);
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
  struct heap_node* heap_node;
  uv_timer_t* handle;

  spd_timer_run_t spd_timer_run;

  for (;;) {
    heap_node = heap_min((struct heap*) &loop->timer_heap);
    if (heap_node == NULL)
      break;

    handle = container_of(heap_node, uv_timer_t, heap_node);

    /* Ask the scheduler whether we should run this timer. */
    spd_timer_run_init(&spd_timer_run);
    spd_timer_run.timer = handle;
    spd_timer_run.now = handle->loop->time;
    spd_timer_run.run = -1;
    scheduler_thread_yield(SCHEDULE_POINT_TIMER_RUN, &spd_timer_run);
    assert(spd_timer_run.run == 0 || spd_timer_run.run == 1);

    mylog(LOG_TIMER, 7, "uv__run_timers: time is %llu, timer has timeout %llu, run %i\n", handle->loop->time, handle->timeout, spd_timer_run.run);
    if (spd_timer_run.run)
    {
      uv_timer_stop(handle);
      uv_timer_again(handle);

     #if UNIFIED_CALLBACK
      invoke_callback_wrap((any_func) handle->timer_cb, UV_TIMER_CB, (long) handle);
     #else
      handle->timer_cb(handle);
     #endif
    }
    else
      break;
  }
}


void uv__timer_close(uv_timer_t* handle) {
  uv_timer_stop(handle);
}
