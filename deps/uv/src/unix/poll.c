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
#include "scheduler.h"

#include <unistd.h>
#include <assert.h>
#include <errno.h>


static void uv__poll_io(uv_loop_t* loop, uv__io_t* w, unsigned int events) {
  uv_poll_t* handle;
  int pevents;

  handle = container_of(w, uv_poll_t, io_watcher);

  if (events & UV__POLLERR) {
    uv__io_stop(loop, w, UV__POLLIN | UV__POLLOUT);
    uv__handle_stop(handle);
#if UNIFIED_CALLBACK
    invoke_callback_wrap ((any_func) handle->poll_cb, UV_POLL_CB, (long) handle, (long) -EBADF, (long) 0);
#else
    handle->poll_cb(handle, -EBADF, 0);
#endif
    return;
  }

  pevents = 0;
  if (events & UV__POLLIN)
    pevents |= UV_READABLE;
  if (events & UV__POLLOUT)
    pevents |= UV_WRITABLE;

#if UNIFIED_CALLBACK
  invoke_callback_wrap ((any_func) handle->poll_cb, UV_POLL_CB, (long) handle, (long) 0, (long) pevents);
#else
  handle->poll_cb(handle, 0, pevents);
#endif
}

any_func uv_uv__poll_io_ptr (void)
{
  return (any_func) uv__poll_io;
}

int uv_poll_init(uv_loop_t* loop, uv_poll_t* handle, int fd) {
  int err;

  err = uv__nonblock(fd, 1);
  if (err)
    return err;

  uv__handle_init(loop, (uv_handle_t*) handle, UV_POLL);
  uv__io_init(&handle->io_watcher, uv__poll_io, fd);
  handle->poll_cb = NULL;
  return 0;
}


int uv_poll_init_socket(uv_loop_t* loop, uv_poll_t* handle,
    uv_os_sock_t socket) {
  return uv_poll_init(loop, handle, socket);
}


static void uv__poll_stop(uv_poll_t* handle) {
  uv__io_stop(handle->loop, &handle->io_watcher, UV__POLLIN | UV__POLLOUT);
  uv__handle_stop(handle);
}


int uv_poll_stop(uv_poll_t* handle) {
  assert(!(handle->flags & (UV_CLOSING | UV_CLOSED)));
  uv__poll_stop(handle);
  return 0;
}


int uv_poll_start(uv_poll_t* handle, int pevents, uv_poll_cb poll_cb) {
  int events;

  assert((pevents & ~(UV_READABLE | UV_WRITABLE)) == 0);
  assert(!(handle->flags & (UV_CLOSING | UV_CLOSED)));

  uv__poll_stop(handle);

  if (pevents == 0)
    return 0;

#ifdef UNIFIED_CALLBACK
  uv__register_callback(handle, (any_func) poll_cb, UV_POLL_CB);
#endif

  events = 0;
  if (pevents & UV_READABLE)
    events |= UV__POLLIN;
  if (pevents & UV_WRITABLE)
    events |= UV__POLLOUT;

  uv__io_start(handle->loop, &handle->io_watcher, events);
  uv__handle_start(handle);
  handle->poll_cb = poll_cb;

  return 0;
}


void uv__poll_close(uv_poll_t* handle) {
  uv__poll_stop(handle);
}

struct list * uv__ready_poll_lcbns(void *h, enum execution_context exec_context)
{
  uv_poll_t *handle;
  lcbn_t *lcbn;
  struct list *ready_poll_lcbns;

  handle = (uv_poll_t *) h;
  assert(handle);
  assert(handle->magic == UV_HANDLE_MAGIC && handle->type == UV_POLL);

  ready_poll_lcbns = list_create();
  switch (exec_context)
  {
    case EXEC_CONTEXT_UV__IO_POLL:
      /* uv__poll_io 
         Either error or not error: invoke UV_POLL_CB appropriately, then return. */
      lcbn = lcbn_get(handle->cb_type_to_lcbn, UV_POLL_CB);
      assert(lcbn && lcbn->cb == (any_func) handle->close_cb);
      assert(lcbn->cb);
      list_push_back(ready_poll_lcbns, &sched_lcbn_create(lcbn)->elem);
      break;
    case EXEC_CONTEXT_UV__RUN_CLOSING_HANDLES:
      lcbn = lcbn_get(handle->cb_type_to_lcbn, UV_CLOSE_CB);
      assert(lcbn && lcbn->cb == (any_func) handle->close_cb);
      if (lcbn->cb)
        list_push_back(ready_poll_lcbns, &sched_lcbn_create(lcbn)->elem);
      break;
    default:
      assert(!"uv__ready_poll_lcbns: Error, unexpected context");
  }
  return ready_poll_lcbns;
}
