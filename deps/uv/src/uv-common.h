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

#ifndef UV_COMMON_H_
#define UV_COMMON_H_

#include <assert.h>
#include <stdarg.h>
#include <stddef.h>

#if defined(_MSC_VER) && _MSC_VER < 1600
# include "stdint-msvc2008.h"
#else
# include <stdint.h>
#endif

#include "uv.h"
#include "tree.h"
#include "queue.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define container_of(ptr, type, member) \
  ((type *) ((char *) (ptr) - offsetof(type, member)))

#define STATIC_ASSERT(expr)                                                   \
  void uv__static_assert(int static_assert_failed[1 - 2 * !(expr)])

#ifndef _WIN32
enum {
  UV__HANDLE_INTERNAL = 0x8000,
  UV__HANDLE_ACTIVE   = 0x4000,
  UV__HANDLE_REF      = 0x2000,
  UV__HANDLE_CLOSING  = 0 /* no-op on unix */
};
#else
# define UV__HANDLE_INTERNAL  0x80
# define UV__HANDLE_ACTIVE    0x40
# define UV__HANDLE_REF       0x20
# define UV__HANDLE_CLOSING   0x01
#endif

int uv__loop_configure(uv_loop_t* loop, uv_loop_option option, va_list ap);

void uv__loop_close(uv_loop_t* loop);

int uv__tcp_bind(uv_tcp_t* tcp,
                 const struct sockaddr* addr,
                 unsigned int addrlen,
                 unsigned int flags);

int uv__tcp_connect(uv_connect_t* req,
                   uv_tcp_t* handle,
                   const struct sockaddr* addr,
                   unsigned int addrlen,
                   uv_connect_cb cb);

int uv__udp_bind(uv_udp_t* handle,
                 const struct sockaddr* addr,
                 unsigned int  addrlen,
                 unsigned int flags);

int uv__udp_send(uv_udp_send_t* req,
                 uv_udp_t* handle,
                 const uv_buf_t bufs[],
                 unsigned int nbufs,
                 const struct sockaddr* addr,
                 unsigned int addrlen,
                 uv_udp_send_cb send_cb);

int uv__udp_try_send(uv_udp_t* handle,
                     const uv_buf_t bufs[],
                     unsigned int nbufs,
                     const struct sockaddr* addr,
                     unsigned int addrlen);

int uv__udp_recv_start(uv_udp_t* handle, uv_alloc_cb alloccb,
                       uv_udp_recv_cb recv_cb);

int uv__udp_recv_stop(uv_udp_t* handle);

void uv__fs_poll_close(uv_fs_poll_t* handle);

int uv__getaddrinfo_translate_error(int sys_err);    /* EAI_* error. */

void uv__work_submit(uv_loop_t* loop,
                     struct uv__work *w,
                     void (*work)(struct uv__work *w),
                     void (*done)(struct uv__work *w, int status));

void uv__work_done(uv_async_t* handle);

size_t uv__count_bufs(const uv_buf_t bufs[], unsigned int nbufs);

int uv__socket_sockopt(uv_handle_t* handle, int optname, int* value);

void uv__fs_scandir_cleanup(uv_fs_t* req);

#define uv__has_active_reqs(loop)                                             \
  (QUEUE_EMPTY(&(loop)->active_reqs) == 0)

#define uv__req_register(loop, req)                                           \
  do {                                                                        \
    QUEUE_INSERT_TAIL(&(loop)->active_reqs, &(req)->active_queue);            \
  }                                                                           \
  while (0)

#define uv__req_unregister(loop, req)                                         \
  do {                                                                        \
    assert(uv__has_active_reqs(loop));                                        \
    QUEUE_REMOVE(&(req)->active_queue);                                       \
  }                                                                           \
  while (0)

#define uv__has_active_handles(loop)                                          \
  ((loop)->active_handles > 0)

#define uv__active_handle_add(h)                                              \
  do {                                                                        \
    (h)->loop->active_handles++;                                              \
  }                                                                           \
  while (0)

#define uv__active_handle_rm(h)                                               \
  do {                                                                        \
    (h)->loop->active_handles--;                                              \
  }                                                                           \
  while (0)

#define uv__is_active(h)                                                      \
  (((h)->flags & UV__HANDLE_ACTIVE) != 0)

#define uv__is_closing(h)                                                     \
  (((h)->flags & (UV_CLOSING |  UV_CLOSED)) != 0)

#define uv__handle_start(h)                                                   \
  do {                                                                        \
    assert(((h)->flags & UV__HANDLE_CLOSING) == 0);                           \
    if (((h)->flags & UV__HANDLE_ACTIVE) != 0) break;                         \
    (h)->flags |= UV__HANDLE_ACTIVE;                                          \
    if (((h)->flags & UV__HANDLE_REF) != 0) uv__active_handle_add(h);         \
  }                                                                           \
  while (0)

#define uv__handle_stop(h)                                                    \
  do {                                                                        \
    assert(((h)->flags & UV__HANDLE_CLOSING) == 0);                           \
    if (((h)->flags & UV__HANDLE_ACTIVE) == 0) break;                         \
    (h)->flags &= ~UV__HANDLE_ACTIVE;                                         \
    if (((h)->flags & UV__HANDLE_REF) != 0) uv__active_handle_rm(h);          \
  }                                                                           \
  while (0)

#define uv__handle_ref(h)                                                     \
  do {                                                                        \
    if (((h)->flags & UV__HANDLE_REF) != 0) break;                            \
    (h)->flags |= UV__HANDLE_REF;                                             \
    if (((h)->flags & UV__HANDLE_CLOSING) != 0) break;                        \
    if (((h)->flags & UV__HANDLE_ACTIVE) != 0) uv__active_handle_add(h);      \
  }                                                                           \
  while (0)

#define uv__handle_unref(h)                                                   \
  do {                                                                        \
    if (((h)->flags & UV__HANDLE_REF) == 0) break;                            \
    (h)->flags &= ~UV__HANDLE_REF;                                            \
    if (((h)->flags & UV__HANDLE_CLOSING) != 0) break;                        \
    if (((h)->flags & UV__HANDLE_ACTIVE) != 0) uv__active_handle_rm(h);       \
  }                                                                           \
  while (0)

#define uv__has_ref(h)                                                        \
  (((h)->flags & UV__HANDLE_REF) != 0)

#if defined(_WIN32)
# define uv__handle_platform_init(h) ((h)->u.fd = -1)
#else
# define uv__handle_platform_init(h) ((h)->next_closing = NULL)
#endif

#define uv__handle_init(loop_, h, type_)                                      \
  do {                                                                        \
    (h)->loop = (loop_);                                                      \
    (h)->type = (type_);                                                      \
    (h)->flags = UV__HANDLE_REF;  /* Ref the loop when active. */             \
    QUEUE_INSERT_TAIL(&(loop_)->handle_queue, &(h)->handle_queue);            \
    uv__handle_platform_init(h);                                              \
  }                                                                           \
  while (0)


/* Allocator prototypes */
void *uv__calloc(size_t count, size_t size);
char *uv__strdup(const char* s);
char *uv__strndup(const char* s, size_t n);
void* uv__malloc(size_t size);
void uv__free(void* ptr);
void* uv__realloc(void* ptr, size_t size);

/* Logging. */
void incr_generation ();
int get_generation ();
void mylog (const char *format, ...);

/* Unified callback queue. */
#define MAX_CALLBACK_NARGS 5
enum callback_type
{
  /* include/uv.h */
  UV_ALLOC_CB,
  UV_READ_CB,
  UV_WRITE_CB,
  UV_CONNECT_CB,
  UV_SHUTDOWN_CB,
  UV_CONNECTION_CB,
  UV_CLOSE_CB,
  UV_POLL_CB,
  UV_TIMER_CB,
  UV_ASYNC_CB,
  UV_PREPARE_CB,
  UV_CHECK_CB,
  UV_IDLE_CB,
  UV_EXIT_CB,
  UV_WALK_CB,
  UV_FS_CB,
  UV_WORK_CB,
  UV_AFTER_WORK_CB,
  UV_GETADDRINFO_CB,
  UV_GETNAMEINFO_CB,
  UV_FS_EVENT_CB,
  UV_FS_POLL_CB,
  UV_SIGNAL_CB,
  UV_UDP_SEND_CB,
  UV_UDP_RECV_CB,
  UV_THREAD_CB,

  /* include/uv-threadpool.h */
  UV__WORK_WORK,
  UV__WORK_DONE
};

#define INIT_CBI (type) \
  struct callback_info *cbi_p = malloc (sizeof *cbi_p); \
  assert (cbi_p != NULL); \
  memset (cbi_p, 0, sizeof (*cbi_p)); \
  cbi_p->type = type;

/* Macros to prep a CBI for invoke_callback, with 0-5 args. */
#define PREP_CBI_0 (type, cb) \
  INIT_CBI (type) \
  cbi_p->cb = cb;
#define PREP_CBI_1 (type, cb, arg0) \
  INIT_CBI (type) \
  cbi_p->args[0] = arg0;
#define PREP_CBI_2 (type, cb, arg0, arg1) \
  PREP_CBI_1 (type, cb, arg0) \
  cbi_p->args[1] = arg1
#define PREP_CBI_3 (type, cb, arg0, arg1, arg2) \
  PREP_CBI_2 (type, cb, arg0, arg1) \
  cbi_p->args[2] = arg2
#define PREP_CBI_4 (type, cb, arg0, arg1, arg2, arg3) \
  PREP_CBI_3 (type, cb, arg0, arg1, arg2) \
  cbi_p->args[3] = arg3
#define PREP_CBI_5 (type, cb, arg0, arg1, arg2, arg3, arg4) \
  PREP_CBI_4 (type, cb, arg0, arg1, arg2, arg3) \
  cbi_p->args[4] = arg4

/* Macros to invoke a callback, with 0-5 args. */
#define INVOKE_CALLBACK_0 (type, cb) \
  PREP_CBI_0 (type, cb) \
  invoke_callback (cbi_p);
#define INVOKE_CALLBACK_1 (type, cb, arg0) \
  PREP_CBI_1 (type, cb, arg0) \
  invoke_callback (cbi_p);
#define INVOKE_CALLBACK_2 (type, cb, arg0, arg1) \
  PREP_CBI_2 (type, cb, arg0, arg1) \
  invoke_callback (cbi_p);
#define INVOKE_CALLBACK_3 (type, cb, arg0, arg1, arg2) \
  PREP_CBI_3 (type, cb, arg0, arg1, arg2) \
  invoke_callback (cbi_p);
#define INVOKE_CALLBACK_4 (type, cb, arg0, arg1, arg2, arg3) \
  PREP_CBI_4 (type, cb, arg0, arg1, arg2, arg3) \
  invoke_callback (cbi_p);
#define INVOKE_CALLBACK_5 (type, cb, arg0, arg1, arg2, arg3, arg4) \
  PREP_CBI_5 (type, cb, arg0, arg1, arg2, arg3, arg4) \
  invoke_callback (cbi_p);

/* Description of a callback. */
struct callback_info
{
  enum callback_type type;
  void (*cb)();
  long args[MAX_CALLBACK_NARGS]; /* Must be large enough for the widest arg type. Seems to be 8 bytes. */
};

/* Nodes that comprise a callback tree. */
struct callback_node
{
  struct callback_info info; /* Description of this callback. */
  int level; /* What level in the callback tree is it? For root nodes this is 0. */
  struct callback_node *parent; /* Who started us? For root nodes this is NULL. */
  struct callback_node *children; /* Linked list of children. */

  /* If node is a child, NEXT points to the next child in the parent's CHILDREN list. */
  struct callback_node *next_child;
};

/* Nodes that comprise the list of callback trees. */
struct callback_root_node
{
  /* Root of this tree. */
  struct callback_node *info;
  /* Next element. */
  struct callback_root_node *next;
};

void invoke_callback (struct callback_info *);

#endif /* UV_COMMON_H_ */
