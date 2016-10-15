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

#define UV_LOOP_WATCHER_DEFINE(_name, _type)                                  \
  int uv_##_name##_init(uv_loop_t* loop, uv_##_name##_t* handle) {            \
    uv__handle_init(loop, (uv_handle_t*)handle, UV_##_type);                  \
    handle->_name##_cb = NULL;                                                \
    return 0;                                                                 \
  }                                                                           \
                                                                              \
  int uv_##_name##_start(uv_##_name##_t* handle, uv_##_name##_cb cb) {        \
    if (uv__is_active(handle)) return 0;                                      \
    if (cb == NULL) return -EINVAL;                                           \
    /* JD: FIFO order. Was LIFO order. No docs requiring this, and FIFO needed to make setImmediate-as-check work. I think LIFO order was so that uv__run_X could start at front and iterate without an infinite loop? */ \
    QUEUE_INSERT_TAIL(&handle->loop->_name##_handles, &handle->queue);        \
    handle->_name##_cb = cb;                                                  \
    uv__handle_start(handle);                                                 \
    handle->self_parent = 0;                                                  \
    return 0;                                                                 \
  }                                                                           \
                                                                              \
  int uv_##_name##_stop(uv_##_name##_t* handle) {                             \
    if (!uv__is_active(handle)) return 0;                                     \
    QUEUE_REMOVE(&handle->queue);                                             \
    handle->self_parent = 0;                                                  \
    uv__handle_stop(handle);                                                  \
    return 0;                                                                 \
  }                                                                           \
                                                                              \
  void uv__##_name##_close(uv_##_name##_t* handle) {                          \
    uv_##_name##_stop(handle);                                                \
  }

/* Define separate versions of uv__run_X to handle CB invocation. */
#ifdef UNIFIED_CALLBACK
  #define UV_LOOP_WATCHER_DEFINE_2(_name, _type)                                \
    void uv__run_##_name(uv_loop_t* loop) {                                     \
      uv_##_name##_t* h;                                                        \
      QUEUE* q;                                                                 \
                                                                                \
      ENTRY_EXIT_LOG((LOG_MAIN, 9, "uv__run_" #_name ": begin: loop %p\n", loop)); \
                                                                                \
      QUEUE_FOREACH(q, &loop->_name##_handles) {                                \
        h = QUEUE_DATA(q, uv_##_name##_t, queue);                               \
        invoke_callback_wrap((any_func) h->_name##_cb, UV_##_type##_CB, (long) h); \
      }                                                                         \
                                                                                \
      ENTRY_EXIT_LOG((LOG_MAIN, 9, "uv__run_" #_name ": returning\n"));         \
    }
#else
  #define UV_LOOP_WATCHER_DEFINE_2(_name, _type)                                \
    void uv__run_##_name(uv_loop_t* loop) {                                     \
      uv_##_name##_t* h;                                                        \
      QUEUE* q;                                                                 \
                                                                                \
      ENTRY_EXIT_LOG((LOG_MAIN, 9, "uv__run_" #_name ": begin: loop %p\n", loop)); \
                                                                                \
      QUEUE_FOREACH(q, &loop->_name##_handles) {                                \
        h = QUEUE_DATA(q, uv_##_name##_t, queue);                               \
        h->_name##_cb(h);                                                       \
      }                                                                         \
                                                                                \
      ENTRY_EXIT_LOG((LOG_MAIN, 9, "uv__run_" #_name ": returning\n"));         \
    }
#endif

UV_LOOP_WATCHER_DEFINE(prepare, PREPARE)
UV_LOOP_WATCHER_DEFINE_2(prepare, PREPARE)

UV_LOOP_WATCHER_DEFINE(check, CHECK)
UV_LOOP_WATCHER_DEFINE_2(check, CHECK)

UV_LOOP_WATCHER_DEFINE(idle, IDLE)
UV_LOOP_WATCHER_DEFINE_2(idle, IDLE)
