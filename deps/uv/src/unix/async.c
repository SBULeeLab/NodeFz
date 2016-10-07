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

/* Returns a nonblocking read-write'able fd.
 * It's either from eventfd or from pipe. 
 */
static int uv__async_eventfd(void);

/* Handler passed to uv__io_start. 
 * w is part of a uv_async_t. 
 */
static void uv__async_io(uv_loop_t* loop,
                         uv__io_t* w,
                         unsigned int events);

/* Begin monitoring for uv_async_send's on handle. */
static int uv__async_start(uv_loop_t* loop, uv_async_t *handle);

any_func uv_uv__async_io_ptr (void)
{
  return (any_func) uv__async_io;
}

int uv_async_init(uv_loop_t* loop, uv_async_t* handle, uv_async_cb async_cb) {
  int err;

  mylog(LOG_UV_ASYNC, 1, "uv_async_init: initializing handle %p\n", handle);
  memset(handle, 0, sizeof(*handle));

  uv__handle_init(loop, (uv_handle_t*)handle, UV_ASYNC);
  handle->async_cb = async_cb;
  handle->pending = 0;
  handle->io_watcher.fd = -1;

  uv__register_callback(handle, (any_func) handle->async_cb, UV_ASYNC_CB);

  err = uv__async_start(loop, handle);

  assert(handle->type == UV_ASYNC);
  return err;
}


int uv_async_send(uv_async_t* handle) {

  assert(0 <= handle->io_watcher.fd);
  mylog(LOG_UV_ASYNC, 1, "uv_async_send: handle %p\n", handle);

  /* If already pending, coalesce. */
  if (ACCESS_ONCE(int, handle->pending) != 0)
    return 0;

  /* Mark this handle as pending in a thread-safe manner. */
  if (cmpxchgi(&handle->pending, 0, 1) == 0)
  {
    /* Write a byte to io_watcher's fd so that epoll will see the event. 
     * Since fd might be an eventfd, write an 8-byte integer. 
     * This is mostly harmless if it's a pipe instead. */
    static const uint64_t val = 1;
    const void *buf = &val;
    ssize_t len = sizeof(val);
    int r;

    mylog(LOG_UV_ASYNC, 1, "uv_async_send: handle %p is now pending, and writing to fd %i\n", handle, handle->io_watcher.fd);
    do
      r = write(handle->io_watcher.fd, buf, len);
    while (r == -1 && errno == EINTR);

    if (r == len)
      return 0;

    if (r == -1)
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        return -1;

    abort();
  }

  return 0;
}


void uv__async_close(uv_async_t* handle) {
  assert(handle->type == UV_ASYNC);

  uv__io_stop(handle->loop, &handle->io_watcher, UV__POLLIN);
  uv__close(handle->io_watcher.fd);
  uv__handle_stop(handle);
}

/* This is the IO function for uv_async_t's after they've been uv_async_send'd.
 * It empties its fd (eventfd or pipe), then invokes the handle's async_cb.
 */
static void uv__async_io(uv_loop_t* loop, uv__io_t* w, unsigned int events) {
  uv_async_t *h = NULL;
  char buf[1024];
  ssize_t r;

  h = container_of(w, uv_async_t, io_watcher);
  mylog(LOG_UV_ASYNC, 1, "uv__async_io: IO on handle %p (fd %i)\n", h, h->io_watcher.fd);
  assert(h->type == UV_ASYNC);

  /* Empty the fd. */
  for (;;) {
    r = read(w->fd, buf, sizeof(buf));

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

  /* Invoke the uv_async_cb. */
#if defined(UNIFIED_CALLBACK)
  invoke_callback_wrap ((any_func) h->async_cb, UV_ASYNC_CB, (long) h);
#else
  h->async_cb(h);
#endif

  return;
}

#if 0
/* TODO Do I want to add 'whodunnit' dependency edges? Would need to include all send'ers, not just the first one. 
   The current dependency edges indicate a list of "X must go before me" nodes.
   In contrast, async edges would indicate "one of these Xes must go before me" nodes. */
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

#endif

void uv__async_init(struct uv__async* wa) {
  wa->io_watcher.fd = -1;
  wa->wfd = -1;
}

/* wa is the loop->async_watcher. 
 * If not already started, uv__io_start it (adding it to the list of fds being monitored by the loop).
 * When any uv_async_send is called (-> uv__async_send), a byte will be written to the 
 * handle being send'd as well as to the loop->async_watcher, causing uv__io_poll to call loop->async_watcher's CB (uv__async_io).
 * wa's cb = cb == uv__async_event, which is invoked in uv__async_io and which loops over the async handles looking for those that are pending.
 */
static int uv__async_start(uv_loop_t* loop, uv_async_t *handle) {
  int pipefd[2];
  int err;

  /* Obtain a nonblocking read-write fd for handle->io_watcher. */
  assert(handle->io_watcher.fd == -1);
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
     * function as both ends of the pipe, and we can close the other fd.
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

  /* Start monitoring the fd. */
  uv__io_init(&handle->io_watcher, uv__async_io, pipefd[0]);
  uv__io_start(loop, &handle->io_watcher, UV__POLLIN);

  assert(handle->io_watcher.fd == pipefd[0]);
  mylog(LOG_UV_ASYNC, 1, "uv__async_start: handle %p fd %i\n", handle, handle->io_watcher.fd);

  return 0;
}

/* Called with wa as loop->async_watcher. Stops async monitoring. */
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
