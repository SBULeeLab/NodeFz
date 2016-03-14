/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *
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
#include "logical-callback-node.h"
#include "unified_callback.h"
#include "list.h"
#include "scheduler.h"

#define UV_LOOP_WATCHER_DEFINE(name, type)                                    \
  int uv_##name##_init(uv_loop_t* loop, uv_##name##_t* handle) {              \
    uv__handle_init(loop, (uv_handle_t*)handle, UV_##type);                   \
    handle->name##_cb = NULL;                                                 \
    return 0;                                                                 \
  }                                                                           \
                                                                              \
  int uv_##name##_start(uv_##name##_t* handle, uv_##name##_cb cb) {           \
    if (uv__is_active(handle)) return 0;                                      \
    if (cb == NULL) return -EINVAL;                                           \
    uv__register_callback(handle, cb, UV_##type##_CB);                        \
    /* JD: anti-FIFO order, not sure why */                                   \
    QUEUE_INSERT_HEAD(&handle->loop->name##_handles, &handle->queue);         \
    handle->name##_cb = cb;                                                   \
    uv__handle_start(handle);                                                 \
    /*                                                                        \ 
    if (handle->logical_parent == NULL)                                       \ 
      handle->logical_parent = current_callback_node_get();                   \
    assert(handle->logical_parent != NULL);                                   \
    */                                                                        \
    handle->self_parent = 0;                                                  \
    return 0;                                                                 \
  }                                                                           \
                                                                              \
  int uv_##name##_stop(uv_##name##_t* handle) {                               \
    if (!uv__is_active(handle)) return 0;                                     \
    QUEUE_REMOVE(&handle->queue);                                             \
    handle->logical_parent = NULL;                                            \
    handle->self_parent = 0;                                                  \
    uv__handle_stop(handle);                                                  \
    return 0;                                                                 \
  }                                                                           \
                                                                              \
/* Returns a list of sched_context_t's describing the ready timers.           \
   Callers are responsible for cleaning up the list, perhaps like this:       \
     list_destroy_full(ready_handles, sched_context_destroy_func, NULL) */    \
  static struct list * uv__ready_##name##s(uv_loop_t *loop, enum execution_context exec_context) { \
    uv_##name##_t* handle;                                                    \
    struct list *ready_handles;                                               \
    sched_context_t *sched_context;                                           \
    QUEUE* q;                                                                 \
                                                                              \
    ready_handles = list_create();                                            \
                                                                              \
    /* All registered handles are always ready. */                            \
    QUEUE_FOREACH(q, &loop->name##_handles) {                                 \
      handle = QUEUE_DATA(q, uv_##name##_t, queue);                           \
      sched_context = sched_context_create(exec_context, CALLBACK_CONTEXT_HANDLE, handle);  \
      list_push_back(ready_handles, &sched_context->elem);                    \
    }                                                                         \
                                                                              \
    return ready_handles;                                                     \
  }                                                                           \
                                                                              \
  struct list * uv__ready_##name##_lcbns(void *h, enum execution_context exec_context) { \
    uv_handle_t *handle;                                                      \
    lcbn_t *lcbn;                                                             \
    struct list *ready_lcbns;                                                 \
                                                                              \
    handle = (uv_handle_t *) h;                                               \
    assert(handle);                                                           \
                                                                              \
    ready_lcbns = list_create();                                              \
    switch (exec_context)                                                     \
    {                                                                         \
      case EXEC_CONTEXT_UV__RUN_##type:                                       \
        lcbn = lcbn_get(handle->cb_type_to_lcbn, UV_##type##_CB);             \
        assert(lcbn);                                                         \
        list_push_back(ready_lcbns, &sched_lcbn_create(lcbn)->elem);          \
        break;                                                                \
      case EXEC_CONTEXT_UV__RUN_CLOSING_HANDLES:                              \
        lcbn = lcbn_get(handle->cb_type_to_lcbn, UV_CLOSE_CB);                \
        assert(lcbn && lcbn->cb == handle->close_cb);                         \
        list_push_back(ready_lcbns, &sched_lcbn_create(lcbn)->elem);          \
        break;                                                                \
      default:                                                                \
        assert(!"uv__ready_##name##_lcbns: Error, unexpected context");       \
    }                                                                         \
                                                                              \
    return ready_lcbns;                                                       \
  }                                                                           \
                                                                              \
  void uv__run_##name(uv_loop_t* loop) {                                      \
    struct list *ready_handles;                                               \
    sched_context_t *next_sched_context;                                      \
    sched_lcbn_t *next_sched_lcbn;                                            \
    uv_##name##_t *next_handle;                                               \
                                                                              \
    lcbn_t *orig, *tmp;                                                       \
                                                                              \
    orig = lcbn_current_get();                                                \
    ready_handles = uv__ready_##name##s(loop, EXEC_CONTEXT_UV__RUN_##type); \
    while (ready_handles)                                                     \
    {                                                                         \
      next_sched_context = scheduler_next_context(ready_handles);             \
      if (list_empty(ready_handles) || !next_sched_context)                   \
        break;                                                                \
                                                                              \
      /* Run the next handle. */                                              \
      next_sched_lcbn = scheduler_next_lcbn(next_sched_context);              \
      next_handle = (uv_##name##_t *) next_sched_context->handle_or_req;      \
                                                                              \
      assert(next_sched_lcbn->lcbn == lcbn_get(next_handle->cb_type_to_lcbn, UV_##type##_CB)); \
                                                                              \
      INVOKE_CALLBACK_1(UV_##type##_CB, next_handle->name##_cb, next_handle); \
                                                                              \ 
      /* "Handle inheritance": Re-register the CB with the just-executed LCBN as the new LCBN's parent. */     \
      tmp = lcbn_get(next_handle->cb_type_to_lcbn, UV_##type##_CB);           \
      assert(tmp != NULL);                                                    \
      lcbn_current_set(tmp);                                                  \
      uv__register_callback(next_handle, next_handle->name##_cb, UV_##type##_CB); \
                                                                              \
      /* Each handle is a candidate once per loop iter. */                    \
      list_remove (ready_handles, &next_sched_context->elem);                 \
      sched_context_destroy(next_sched_context);                              \
    }                                                                         \
                                                                              \
    if (ready_handles)                                                        \
      list_destroy_full(ready_handles, sched_context_list_destroy_func, NULL); \
                                                                              \
    lcbn_current_set(orig);                                                   \
  }                                                                           \
                                                                              \
  void uv__##name##_close(uv_##name##_t* handle) {                            \
    uv_##name##_stop(handle);                                                 \
  }

UV_LOOP_WATCHER_DEFINE(prepare, PREPARE)
UV_LOOP_WATCHER_DEFINE(check, CHECK)
UV_LOOP_WATCHER_DEFINE(idle, IDLE)
