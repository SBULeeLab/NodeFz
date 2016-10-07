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

/*
 * This file is private to libuv. It provides common functionality to both
 * Windows and Unix backends.
 */

#ifndef UV_THREADPOOL_H_
#define UV_THREADPOOL_H_

#include "../src/unified_callback.h"

struct uv__work;

/* This structure lets us dynamically allocate uv_async_t for asynchronous uv_close,
 * while still offering an equivalent to container_of via the uv__work pointer.
 * Allocate enough space for a uv__work_async_t plus a uv_async_t, and use async_buf
 * as a uv_async_t.
 */
struct uv__work_async_s
{
  struct uv__work *uv__work; /* The uv__work that allocated us. */
  /* Pointer to uv_async_t for signaling "done". Handled entirely by threadpool.c.
   * Must be dynamically allocated because we uv_close(async) after the caller has had the opportunity to
   * delete the uv_work_t (uv__work) within which any "within-struct" structure would have resided.
   */
  char async_buf[1];
};
typedef struct uv__work_async_s uv__work_async_t;

struct uv__work {
  void (*work)(struct uv__work *w);
  void (*done)(struct uv__work *w, int status);
  struct uv_loop_s* loop;
  void* wq[2];
  /* This is a pointer to a uv__work_async_t allocated with enough space for a uv_async_t. */
  uv__work_async_t *ptr_and_async;
};

#endif /* UV_THREADPOOL_H_ */
