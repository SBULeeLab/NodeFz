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

/* This file contains both the uv__async internal infrastructure and the
 * user-facing uv_async_t functions.
 */

#include "uv.h"
#include "internal.h"
#include "scheduler.h"
#include "atomic-ops.h"

#include <errno.h>
#include <stdio.h>  /* snprintf() */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void uv__async_event(uv_loop_t* loop,
                            struct uv__async* w,
                            unsigned int nevents);
static int uv__async_eventfd(void);
static void uv__async_io(uv_loop_t* loop,
                         uv__io_t* w,
                         unsigned int events);

void * uv_uv__async_io_ptr (void)
{
  return (void *) uv__async_io;
}

void * uv_uv__async_event_ptr (void)
{
  return (void *) uv__async_event;
}

int uv_async_init(uv_loop_t* loop, uv_async_t* handle, uv_async_cb async_cb) {
  int err;

  err = uv__async_start(loop, &loop->async_watcher, uv__async_event);
  if (err)
    return err;

  uv__handle_init(loop, (uv_handle_t*)handle, UV_ASYNC);
  handle->async_cb = async_cb;
  handle->pending = 0;

  uv__register_callback(handle, handle->async_cb, UV_ASYNC_CB);

  QUEUE_INSERT_TAIL(&loop->async_handles, &handle->queue);
  uv__handle_start(handle);

  return 0;
}


int uv_async_send(uv_async_t* handle) {
  /* Do a cheap read first. */
  /* JD: If already pending, coalesce. */
  if (ACCESS_ONCE(int, handle->pending) != 0)
    return 0;

  /* JD: Mark this handle as pending. Write a byte to loop->async_watcher's wfd so that it will be discovered by epoll. */
  if (cmpxchgi(&handle->pending, 0, 1) == 0)
    uv__async_send(&handle->loop->async_watcher);

  return 0;
}


void uv__async_close(uv_async_t* handle) {
  QUEUE_REMOVE(&handle->queue);
  uv__handle_stop(handle);
}


static void uv__async_event(uv_loop_t* loop,
                            struct uv__async* w,
                            unsigned int nevents) {
  QUEUE* q;
  uv_async_t* handle;

  QUEUE aq;
  struct list *async_handles;
  sched_context_t *sched_context;

  assert(loop->magic == UV_LOOP_MAGIC);
  if (QUEUE_EMPTY(&loop->async_handles))
    return;

  /* Interpret loop->async_handles as a list of uv_handle_t's. */
  QUEUE_INIT(&aq);
  q = QUEUE_HEAD(&loop->async_handles);
  QUEUE_SPLIT(&loop->async_handles, q, &aq);

  async_handles = list_create();
  QUEUE_FOREACH(q, &aq) {
    handle = QUEUE_DATA(q, uv_async_t, queue);
    if (ACCESS_ONCE(int, handle->pending) != 0)
    {
      sched_context = sched_context_create(EXEC_CONTEXT_UV__IO_POLL, CALLBACK_CONTEXT_HANDLE, handle);
      list_push_back(async_handles, &sched_context->elem);
    }
  }

  /* Find, remove, and execute the handle next in the schedule.   
     One of these handles is the async handle for the threadpool. 
     In REPLAY mode, if it is not scheduled next then we will not invoke
      it even if there are pending threadpool events. */
  while (!list_empty(async_handles))
  {
    assert(!QUEUE_EMPTY(&aq));
    sched_context = scheduler_next_context(async_handles);
    if (sched_context)
    {
      list_remove(async_handles, &sched_context->elem);
      handle = (uv_async_t *) sched_context->wrapper;
      sched_context_destroy(sched_context);

      /* Remove from aq and return to loop->async_handles */
      q = &handle->queue;
      QUEUE_REMOVE(q);
      QUEUE_INIT(q);
      QUEUE_INSERT_TAIL(&loop->async_handles, &handle->queue);

      /* Run the handle.
         Must set pending to 0 first, in case handle re-sets pending a la threadpool. */
      assert(cmpxchgi(&handle->pending, 1, 0) == 1);
      if (handle->async_cb)
      {
        INVOKE_CALLBACK_1 (UV_ASYNC_CB, handle->async_cb, (long) handle);
      }
    }
    else
      break;
  }

  /* Repair: add any handles we didn't run back onto the front of async_handles. */
  while (!QUEUE_EMPTY(&aq)) {
    q = QUEUE_HEAD(&aq);
    QUEUE_REMOVE(q);
    QUEUE_INIT(q);
    QUEUE_INSERT_HEAD(&loop->async_handles, q);
    handle = container_of(q, uv_async_t, queue);
    /* If we didn't execute all pending async handles, make sure we come back and check them
       again in the next loop iteration. */
    if (ACCESS_ONCE(int, handle->pending) != 0)
      uv__async_send(&loop->async_watcher);
  }
  list_destroy_full(async_handles, sched_context_list_destroy_func, NULL); 
}

static void uv__async_io(uv_loop_t* loop, uv__io_t* w, unsigned int events) {
  struct uv__async* wa;
  char buf[1024];
  unsigned n;
  ssize_t r;

  n = 0;
  for (;;) {
    r = read(w->fd, buf, sizeof(buf));

    if (r > 0)
      n += r;

    if (r == sizeof(buf))
      continue;

    if (r != -1)
      break;

    if (errno == EAGAIN || errno == EWOULDBLOCK)
      break;

    if (errno == EINTR)
      continue;

    abort();
  }

  wa = container_of(w, struct uv__async, io_watcher);

#if defined(__linux__)
  if (wa->wfd == -1) {
    uint64_t val;
    assert(n == sizeof(val));
    memcpy(&val, buf, sizeof(val));  /* Avoid alignment issues. */
#if UNIFIED_CALLBACK
    INVOKE_CALLBACK_3(UV__ASYNC_CB, wa->cb, (long) loop, (long) wa, (long) val);
#else
    wa->cb(loop, wa, val);
#endif
    return;
  }
#endif

#if UNIFIED_CALLBACK
  INVOKE_CALLBACK_3(UV__ASYNC_CB, wa->cb, (long) loop, (long) wa, (long) n);
#else
  wa->cb(loop, wa, n);
#endif
}


void uv__async_send(struct uv__async* wa) {
  const void* buf;
  ssize_t len;
  int fd;
  int r;

  buf = "";
  len = 1;
  fd = wa->wfd;

#if defined(__linux__)
  if (fd == -1) {
    static const uint64_t val = 1;
    buf = &val;
    len = sizeof(val);
    fd = wa->io_watcher.fd;  /* eventfd */
  }
#endif

  do
    r = write(fd, buf, len);
  while (r == -1 && errno == EINTR);

  if (r == len)
    return;

  if (r == -1)
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      return;

  abort();
}


void uv__async_init(struct uv__async* wa) {
  wa->io_watcher.fd = -1;
  wa->wfd = -1;
}


int uv__async_start(uv_loop_t* loop, struct uv__async* wa, uv__async_cb cb) {
  int pipefd[2];
  int err;

  if (wa->io_watcher.fd != -1)
    return 0;

  err = uv__async_eventfd();
  if (err >= 0) {
    pipefd[0] = err;
    pipefd[1] = -1;
  }
  else if (err == -ENOSYS) {
    err = uv__make_pipe(pipefd, UV__F_NONBLOCK);
#if defined(__linux__)
    /* Save a file descriptor by opening one of the pipe descriptors as
     * read/write through the procfs.  That file descriptor can then
     * function as both ends of the pipe.
     */
    if (err == 0) {
      char buf[32];
      int fd;

      snprintf(buf, sizeof(buf), "/proc/self/fd/%d", pipefd[0]);
      fd = uv__open_cloexec(buf, O_RDWR);
      if (fd >= 0) {
        uv__close(pipefd[0]);
        uv__close(pipefd[1]);
        pipefd[0] = fd;
        pipefd[1] = fd;
      }
    }
#endif
  }

  if (err < 0)
    return err;

  /* This sets wa->io_watcher.fd == pipefd[0], so on subsequent calls to uv__async_start
      we return immediately until uv__async_stop is called on wa. */
  uv__io_init(&wa->io_watcher, uv__async_io, pipefd[0]);
  uv__io_start(loop, &wa->io_watcher, UV__POLLIN);
  wa->wfd = pipefd[1];
  wa->cb = cb;

  return 0;
}


void uv__async_stop(uv_loop_t* loop, struct uv__async* wa) {
  if (wa->io_watcher.fd == -1)
    return;

  if (wa->wfd != -1) {
    if (wa->wfd != wa->io_watcher.fd)
      uv__close(wa->wfd);
    wa->wfd = -1;
  }

  uv__io_stop(loop, &wa->io_watcher, UV__POLLIN);
  uv__close(wa->io_watcher.fd);
  wa->io_watcher.fd = -1;
}


static int uv__async_eventfd() {
#if defined(__linux__)
  static int no_eventfd2;
  static int no_eventfd;
  int fd;

  if (no_eventfd2)
    goto skip_eventfd2;

  fd = uv__eventfd2(0, UV__EFD_CLOEXEC | UV__EFD_NONBLOCK);
  if (fd != -1)
    return fd;

  if (errno != ENOSYS)
    return -errno;

  no_eventfd2 = 1;

skip_eventfd2:

  if (no_eventfd)
    goto skip_eventfd;

  fd = uv__eventfd(0);
  if (fd != -1) {
    uv__cloexec(fd, 1);
    uv__nonblock(fd, 1);
    return fd;
  }

  if (errno != ENOSYS)
    return -errno;

  no_eventfd = 1;

skip_eventfd:

#endif

  return -ENOSYS;
}

struct list * uv__ready_async_event_lcbns (void *l, enum execution_context exec_context)
{
  uv_loop_t *loop = (uv_loop_t *) l;
  struct list *ready_lcbns = NULL;
  QUEUE *q = NULL;
  uv_async_t *h = NULL;

  assert(exec_context == EXEC_CONTEXT_UV__IO_POLL);
    
  ready_lcbns = list_create();
  /* uv__async_event: Each of the loop's async_handles with non-zero pending will be invoked. */
  QUEUE_FOREACH(q, &loop->async_handles) {
    h = QUEUE_DATA(q, uv_async_t, queue);
    assert(h && h->magic == UV_HANDLE_MAGIC && h->type == UV_ASYNC);
    if (h->pending)
      list_concat(ready_lcbns, uv__ready_async_lcbns(h, exec_context));
  }
  return ready_lcbns;
}

struct list * uv__ready_async_lcbns(void *h, enum execution_context exec_context)
{
  uv_async_t *handle;
  lcbn_t *lcbn;
  struct list *ready_async_lcbns;

  handle = (uv_async_t *) h;
  assert(handle);
  assert(handle->type == UV_ASYNC);

  ready_async_lcbns = list_create();
  switch (exec_context)
  {
    case EXEC_CONTEXT_UV__IO_POLL:
      lcbn = lcbn_get(handle->cb_type_to_lcbn, UV_ASYNC_CB);
      assert(lcbn && lcbn->cb == handle->async_cb);
      if (lcbn->cb)
        list_push_back(ready_async_lcbns, &sched_lcbn_create(lcbn)->elem);
      break;
    case EXEC_CONTEXT_UV__RUN_CLOSING_HANDLES:
      lcbn = lcbn_get(handle->cb_type_to_lcbn, UV_CLOSE_CB);
      assert(lcbn && lcbn->cb == handle->close_cb);
      if (lcbn->cb)
        list_push_back(ready_async_lcbns, &sched_lcbn_create(lcbn)->elem);
      break;
    default:
      assert(!"uv__ready_async_lcbns: Error, unexpected context");
  }
  return ready_async_lcbns;
}
