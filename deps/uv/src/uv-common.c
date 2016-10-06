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
#include "uv-common.h"
#include "list.h"
#include "map.h"
#include "logical-callback-node.h"
#include "unified-callback-enums.h"
#include "scheduler.h"

#if defined(ENABLE_SCHEDULER_VANILLA)
  #include "scheduler_Vanilla.h"
#endif

#if defined(ENABLE_SCHEDULER_FUZZING_TIME)
  #include "scheduler_Fuzzing_Timer.h"
#endif

#if defined(ENABLE_SCHEDULER_TP_FREEDOM)
  #include "scheduler_TP_Freedom.h"
#endif

#include <stdio.h>
#include <assert.h>
#include <stdarg.h>
#include <stddef.h> /* NULL */
#include <stdlib.h> /* malloc */
#include <string.h> /* memset */
#include <assert.h>

#include <sys/types.h> /* getpid */
#include <unistd.h> /* getpid */
#include <sys/time.h> /* timersub */

#include <signal.h> /* For signal handling. */

#if defined(_WIN32)
# include <malloc.h> /* malloc */
#else
# include <net/if.h> /* if_nametoindex */
#endif

/* For determining peer info. */
#include <sys/types.h>
#include <sys/socket.h>

/* Private helpers. */
static void initialize_scheduler (void);

typedef struct {
  uv_malloc_func local_malloc;
  uv_realloc_func local_realloc;
  uv_calloc_func local_calloc;
  uv_free_func local_free;
} uv__allocator_t;

static uv__allocator_t uv__allocator = {
  malloc,
  realloc,
  calloc,
  free,
};

char* uv__strdup(const char* s) {
  size_t len = strlen(s) + 1;
  char* m = uv__malloc(len);
  if (m == NULL)
    return NULL;
  return memcpy(m, s, len);
}

char* uv__strndup(const char* s, size_t n) {
  char* m;
  size_t len = strlen(s);
  if (n < len)
    len = n;
  m = uv__malloc(len + 1);
  if (m == NULL)
    return NULL;
  m[len] = '\0';
  return memcpy(m, s, len);
}

void* uv__malloc(size_t size) {
  void *ret = uv__allocator.local_malloc(size);
#ifdef JD_DEBUG_FULL
  memset(ret, 'c', size);
#endif
  return ret;
}

void uv__free(void* ptr) {
  uv__allocator.local_free(ptr);
}

void* uv__calloc(size_t count, size_t size) {
  return uv__allocator.local_calloc(count, size);
}

void* uv__realloc(void* ptr, size_t size) {
  return uv__allocator.local_realloc(ptr, size);
}

int uv_replace_allocator(uv_malloc_func malloc_func,
                         uv_realloc_func realloc_func,
                         uv_calloc_func calloc_func,
                         uv_free_func free_func) {
  if (malloc_func == NULL || realloc_func == NULL ||
      calloc_func == NULL || free_func == NULL) {
    return UV_EINVAL;
  }

  uv__allocator.local_malloc = malloc_func;
  uv__allocator.local_realloc = realloc_func;
  uv__allocator.local_calloc = calloc_func;
  uv__allocator.local_free = free_func;

  return 0;
}

#define XX(uc, lc) case UV_##uc: return sizeof(uv_##lc##_t);

size_t uv_handle_size(uv_handle_type type) {
  switch (type) {
    UV_HANDLE_TYPE_MAP(XX)
    default:
      return -1;
  }
}

size_t uv_req_size(uv_req_type type) {
  switch(type) {
    UV_REQ_TYPE_MAP(XX)
    default:
      return -1;
  }
}

#undef XX


size_t uv_loop_size(void) {
  return sizeof(uv_loop_t);
}


uv_buf_t uv_buf_init(char* base, unsigned int len) {
  uv_buf_t buf;
  buf.base = base;
  buf.len = len;
  return buf;
}


static const char* uv__unknown_err_code(int err) {
  char buf[32];
  char* copy;

#ifndef _WIN32
  snprintf(buf, sizeof(buf), "Unknown system error %d", err);
#else
  _snprintf(buf, sizeof(buf), "Unknown system error %d", err);
#endif
  copy = uv__strdup(buf);

  return copy != NULL ? copy : "Unknown system error";
}


#define UV_ERR_NAME_GEN(name, _) case UV_ ## name: return #name;
const char* uv_err_name(int err) {
  switch (err) {
    UV_ERRNO_MAP(UV_ERR_NAME_GEN)
  }
  return uv__unknown_err_code(err);
}
#undef UV_ERR_NAME_GEN


#define UV_STRERROR_GEN(name, msg) case UV_ ## name: return msg;
const char* uv_strerror(int err) {
  switch (err) {
    UV_ERRNO_MAP(UV_STRERROR_GEN)
  }
  return uv__unknown_err_code(err);
}
#undef UV_STRERROR_GEN


int uv_ip4_addr(const char* ip, int port, struct sockaddr_in* addr) {
  memset(addr, 0, sizeof(*addr));
  addr->sin_family = AF_INET;
  addr->sin_port = htons(port);
  return uv_inet_pton(AF_INET, ip, &(addr->sin_addr.s_addr));
}


int uv_ip6_addr(const char* ip, int port, struct sockaddr_in6* addr) {
  char address_part[40];
  size_t address_part_size;
  const char* zone_index;

  memset(addr, 0, sizeof(*addr));
  addr->sin6_family = AF_INET6;
  addr->sin6_port = htons(port);

  zone_index = strchr(ip, '%');
  if (zone_index != NULL) {
    address_part_size = zone_index - ip;
    if (address_part_size >= sizeof(address_part))
      address_part_size = sizeof(address_part) - 1;

    memcpy(address_part, ip, address_part_size);
    address_part[address_part_size] = '\0';
    ip = address_part;

    zone_index++; /* skip '%' */
    /* NOTE: unknown interface (id=0) is silently ignored */
#ifdef _WIN32
    addr->sin6_scope_id = atoi(zone_index);
#else
    addr->sin6_scope_id = if_nametoindex(zone_index);
#endif
  }

  return uv_inet_pton(AF_INET6, ip, &addr->sin6_addr);
}


int uv_ip4_name(const struct sockaddr_in* src, char* dst, size_t size) {
  return uv_inet_ntop(AF_INET, &src->sin_addr, dst, size);
}


int uv_ip6_name(const struct sockaddr_in6* src, char* dst, size_t size) {
  return uv_inet_ntop(AF_INET6, &src->sin6_addr, dst, size);
}


int uv_tcp_bind(uv_tcp_t* handle,
                const struct sockaddr* addr,
                unsigned int flags) {
  unsigned int addrlen;

  if (handle->type != UV_TCP)
    return UV_EINVAL;

  if (addr->sa_family == AF_INET)
    addrlen = sizeof(struct sockaddr_in);
  else if (addr->sa_family == AF_INET6)
    addrlen = sizeof(struct sockaddr_in6);
  else
    return UV_EINVAL;

  return uv__tcp_bind(handle, addr, addrlen, flags);
}


int uv_udp_bind(uv_udp_t* handle,
                const struct sockaddr* addr,
                unsigned int flags) {
  unsigned int addrlen;

  if (handle->type != UV_UDP)
    return UV_EINVAL;

  if (addr->sa_family == AF_INET)
    addrlen = sizeof(struct sockaddr_in);
  else if (addr->sa_family == AF_INET6)
    addrlen = sizeof(struct sockaddr_in6);
  else
    return UV_EINVAL;

  return uv__udp_bind(handle, addr, addrlen, flags);
}


int uv_tcp_connect(uv_connect_t* req,
                   uv_tcp_t* handle,
                   const struct sockaddr* addr,
                   uv_connect_cb cb) {
  unsigned int addrlen;

  if (handle->type != UV_TCP)
    return UV_EINVAL;

  if (addr->sa_family == AF_INET)
    addrlen = sizeof(struct sockaddr_in);
  else if (addr->sa_family == AF_INET6)
    addrlen = sizeof(struct sockaddr_in6);
  else
    return UV_EINVAL;

  return uv__tcp_connect(req, handle, addr, addrlen, cb);
}


int uv_udp_send(uv_udp_send_t* req,
                uv_udp_t* handle,
                const uv_buf_t bufs[],
                unsigned int nbufs,
                const struct sockaddr* addr,
                uv_udp_send_cb send_cb) {
  unsigned int addrlen;

  if (handle->type != UV_UDP)
    return UV_EINVAL;

  if (addr->sa_family == AF_INET)
    addrlen = sizeof(struct sockaddr_in);
  else if (addr->sa_family == AF_INET6)
    addrlen = sizeof(struct sockaddr_in6);
  else
    return UV_EINVAL;

  return uv__udp_send(req, handle, bufs, nbufs, addr, addrlen, send_cb);
}


int uv_udp_try_send(uv_udp_t* handle,
                    const uv_buf_t bufs[],
                    unsigned int nbufs,
                    const struct sockaddr* addr) {
  unsigned int addrlen;

  if (handle->type != UV_UDP)
    return UV_EINVAL;

  if (addr->sa_family == AF_INET)
    addrlen = sizeof(struct sockaddr_in);
  else if (addr->sa_family == AF_INET6)
    addrlen = sizeof(struct sockaddr_in6);
  else
    return UV_EINVAL;

  return uv__udp_try_send(handle, bufs, nbufs, addr, addrlen);
}


int uv_udp_recv_start(uv_udp_t* handle,
                      uv_alloc_cb alloc_cb,
                      uv_udp_recv_cb recv_cb) {
  if (handle->type != UV_UDP || alloc_cb == NULL || recv_cb == NULL)
    return UV_EINVAL;
  else
  {
#ifdef UNIFIED_CALLBACK
    uv__register_callback(handle, (any_func) alloc_cb, UV_ALLOC_CB);
    uv__register_callback(handle, (any_func) recv_cb, UV_UDP_RECV_CB);
    /* ALLOC -> UDP_RECV. */
    lcbn_add_dependency(lcbn_get(handle->cb_type_to_lcbn, UV_ALLOC_CB),
                        lcbn_get(handle->cb_type_to_lcbn, UV_UDP_RECV_CB));
#endif
    return uv__udp_recv_start(handle, alloc_cb, recv_cb);
  }
}


int uv_udp_recv_stop(uv_udp_t* handle) {
  if (handle->type != UV_UDP)
    return UV_EINVAL;
  else
    return uv__udp_recv_stop(handle);
}


void uv_walk(uv_loop_t* loop, uv_walk_cb walk_cb, void* arg) {
  QUEUE* q;
  uv_handle_t* h;

#if 0
/* TODO Do I want to bother with this? Seems irrelevant. Also, the handle it receives changes every time the CB is called, making it unsuitable for embedding. Would need another approach. */
#ifdef UNIFIED_CALLBACK
  uv__register_callback((void *) walk_cb, UV_WALK_CB);
#endif
#endif

  QUEUE_FOREACH(q, &loop->handle_queue) {
    h = QUEUE_DATA(q, uv_handle_t, handle_queue);
    if (h->flags & UV__HANDLE_INTERNAL) continue;
#ifdef UNIFIED_CALLBACK
    invoke_callback_wrap((any_func) walk_cb, UV_WALK_CB, (long) h, (long) arg); 
#else
    walk_cb(h, arg);
#endif
  }
}


#ifndef NDEBUG
static void uv__print_handles(uv_loop_t* loop, int only_active) {
  const char* type;
  QUEUE* q;
  uv_handle_t* h;

  if (loop == NULL)
    loop = uv_default_loop();

  QUEUE_FOREACH(q, &loop->handle_queue) {
    h = QUEUE_DATA(q, uv_handle_t, handle_queue);

    if (only_active && !uv__is_active(h))
      continue;

    switch (h->type) {
#define X(uc, lc) case UV_##uc: type = #lc; break;
      UV_HANDLE_TYPE_MAP(X)
#undef X
      default: type = "<unknown>";
    }

    fprintf(stderr,
            "[%c%c%c] %-8s %p\n",
            "R-"[!(h->flags & UV__HANDLE_REF)],
            "A-"[!(h->flags & UV__HANDLE_ACTIVE)],
            "I-"[!(h->flags & UV__HANDLE_INTERNAL)],
            type,
            (void*)h);
  }
}


void uv_print_all_handles(uv_loop_t* loop) {
  uv__print_handles(loop, 0);
}


void uv_print_active_handles(uv_loop_t* loop) {
  uv__print_handles(loop, 1);
}
#endif


void uv_ref(uv_handle_t* handle) {
  uv__handle_ref(handle);
}


void uv_unref(uv_handle_t* handle) {
  uv__handle_unref(handle);
}


int uv_has_ref(const uv_handle_t* handle) {
  return uv__has_ref(handle);
}


void uv_stop(uv_loop_t* loop) {
  loop->stop_flag = 1;
}


uint64_t uv_now(const uv_loop_t* loop) {
  return loop->time;
}



size_t uv__count_bufs(const uv_buf_t bufs[], unsigned int nbufs) {
  unsigned int i;
  size_t bytes;

  bytes = 0;
  for (i = 0; i < nbufs; i++)
    bytes += (size_t) bufs[i].len;

  return bytes;
}

int uv_recv_buffer_size(uv_handle_t* handle, int* value) {
  return uv__socket_sockopt(handle, SO_RCVBUF, value);
}

int uv_send_buffer_size(uv_handle_t* handle, int *value) {
  return uv__socket_sockopt(handle, SO_SNDBUF, value);
}

int uv_fs_event_getpath(uv_fs_event_t* handle, char* buffer, size_t* size) {
  size_t required_len;

  if (!uv__is_active(handle)) {
    *size = 0;
    return UV_EINVAL;
  }

  required_len = strlen(handle->path);
  if (required_len > *size) {
    *size = required_len;
    return UV_ENOBUFS;
  }

  memcpy(buffer, handle->path, required_len);
  *size = required_len;

  return 0;
}

/* The windows implementation does not have the same structure layout as
 * the unix implementation (nbufs is not directly inside req but is
 * contained in a nested union/struct) so this function locates it.
*/
static unsigned int* uv__get_nbufs(uv_fs_t* req) {
#ifdef _WIN32
  return &req->fs.info.nbufs;
#else
  return &req->nbufs;
#endif
}

void uv__fs_scandir_cleanup(uv_fs_t* req) {
  uv__dirent_t** dents;

  unsigned int* nbufs = uv__get_nbufs(req);

  dents = req->ptr;
  if (*nbufs > 0 && *nbufs != (unsigned int) req->result)
    (*nbufs)--;
  for (; *nbufs < (unsigned int) req->result; (*nbufs)++)
    uv__free(dents[*nbufs]);
}


int uv_fs_scandir_next(uv_fs_t* req, uv_dirent_t* ent) {
  uv__dirent_t** dents;
  uv__dirent_t* dent;

  unsigned int* nbufs = uv__get_nbufs(req);

  dents = req->ptr;

  /* Free previous entity */
  if (*nbufs > 0)
    uv__free(dents[*nbufs - 1]);

  /* End was already reached */
  if (*nbufs == (unsigned int) req->result) {
    uv__free(dents);
    req->ptr = NULL;
    return UV_EOF;
  }

  dent = dents[(*nbufs)++];

  ent->name = dent->d_name;
#ifdef HAVE_DIRENT_TYPES
  switch (dent->d_type) {
    case UV__DT_DIR:
      ent->type = UV_DIRENT_DIR;
      break;
    case UV__DT_FILE:
      ent->type = UV_DIRENT_FILE;
      break;
    case UV__DT_LINK:
      ent->type = UV_DIRENT_LINK;
      break;
    case UV__DT_FIFO:
      ent->type = UV_DIRENT_FIFO;
      break;
    case UV__DT_SOCKET:
      ent->type = UV_DIRENT_SOCKET;
      break;
    case UV__DT_CHAR:
      ent->type = UV_DIRENT_CHAR;
      break;
    case UV__DT_BLOCK:
      ent->type = UV_DIRENT_BLOCK;
      break;
    default:
      ent->type = UV_DIRENT_UNKNOWN;
  }
#else
  ent->type = UV_DIRENT_UNKNOWN;
#endif

  return 0;
}


int uv_loop_configure(uv_loop_t* loop, uv_loop_option option, ...) {
  va_list ap;
  int err;

  va_start(ap, option);
  /* Any platform-agnostic options should be handled here. */
  err = uv__loop_configure(loop, option, ap);
  va_end(ap);

  return err;
}


static uv_loop_t default_loop_struct;
static uv_loop_t* default_loop_ptr;

uv_loop_t* uv_default_loop(void) {
  if (default_loop_ptr != NULL)
    return default_loop_ptr;

  if (uv_loop_init(&default_loop_struct))
    return NULL;

  default_loop_ptr = &default_loop_struct;
  return default_loop_ptr;
}


uv_loop_t* uv_loop_new(void) {
  uv_loop_t* loop;

  loop = uv__malloc(sizeof(*loop));
  if (loop == NULL)
    return NULL;

  if (uv_loop_init(loop)) {
    uv__free(loop);
    return NULL;
  }

  return loop;
}


int uv_loop_close(uv_loop_t* loop) {
  QUEUE* q;
  uv_handle_t* h;

  if (!QUEUE_EMPTY(&(loop)->active_reqs))
    return UV_EBUSY;

  QUEUE_FOREACH(q, &loop->handle_queue) {
    h = QUEUE_DATA(q, uv_handle_t, handle_queue);
    if (!(h->flags & UV__HANDLE_INTERNAL))
      return UV_EBUSY;
  }

  uv__loop_close(loop);

#ifndef NDEBUG
  memset(loop, -1, sizeof(*loop));
#endif
  if (loop == default_loop_ptr)
    default_loop_ptr = NULL;

  return 0;
}


void uv_loop_delete(uv_loop_t* loop) {
  uv_loop_t* default_loop;
  int err;

  default_loop = default_loop_ptr;

  err = uv_loop_close(loop);
  (void) err;    /* Squelch compiler warnings. */
  assert(err == 0);
  if (loop != default_loop)
    uv__free(loop);
}

/* JD: New functions. */
static lcbn_t * get_init_stack_lcbn (void);
static lcbn_t * get_exit_lcbn (void);

#if 0
static void dump_lcbn_globalorder (void);
static void dump_lcbn_globalorder(void)
{
  int fd;
  char unique_out_file[128];
  char shared_out_file[128];
  struct list *lcbn_list = NULL, *filtered_nodes = NULL;

  lcbn_list = tree_as_list(&get_init_stack_lcbn()->tree_node);

  /* Registration order (all CBs). */
  snprintf(unique_out_file, 128, "/tmp/lcbn_global_reg_order_%i_%i.txt", (int) time(NULL), getpid());
  mylog(LOG_MAIN, 0, "Dumping all %i registered LCBNs in their global registration order to %s\n", list_size(lcbn_list), unique_out_file);

  fd = open(unique_out_file, O_CREAT|O_TRUNC|O_RDWR, S_IRWXU|S_IRWXG|S_IRWXO);
  if (fd < 0)
    assert(!"dump_lcbn_globalorder: Error, could not open output file");
  list_sort(lcbn_list, lcbn_sort_by_reg_id, NULL);
  list_apply(lcbn_list, lcbn_tree_list_print_f, &fd);
  close(fd);

  snprintf(shared_out_file, 128, "/tmp/lcbn_registered_schedule.txt");
  (void) unlink(shared_out_file);
  assert(symlink(unique_out_file, shared_out_file) == 0);

  /* Exec order (does not include never-executed CBs). */
  snprintf(unique_out_file, 128, "/tmp/lcbn_global_exec_order_%i_%i.txt", (int) time(NULL), getpid());

  fd = open(unique_out_file, O_CREAT|O_TRUNC|O_RDWR, S_IRWXU|S_IRWXG|S_IRWXO);
  if (fd < 0)
    assert(!"dump_lcbn_globalorder: Error, could not open output file");
  list_sort(lcbn_list, lcbn_sort_by_exec_id, NULL);
  filtered_nodes = list_filter(lcbn_list, lcbn_remove_unexecuted, NULL); 
  mylog(LOG_MAIN, 0, "dump_lcbn_globalorder: Dumping all %i executed LCBNs in their global exec order to %s\n", list_size(lcbn_list), unique_out_file);
  list_apply(lcbn_list, lcbn_tree_list_print_f, &fd);
  close(fd);
  /* We applied list_filter, so lcbn_list no longer includes all nodes from the tree. 
     Repair by combining the two lists, destroying filtered_nodes in the process. */
  list_concat(lcbn_list, filtered_nodes);

  snprintf(shared_out_file, 128, "/tmp/lcbn_exec_schedule.txt");
  (void) unlink(shared_out_file);
  assert(symlink(unique_out_file, shared_out_file) == 0);

  list_destroy(lcbn_list);
}
#endif

void dump_and_exit_sighandler (int signum)
{
#if 0
  mylog(LOG_MAIN, 0, "Got signal %i. Dumping and exiting.\n", signum);
  mylog(LOG_MAIN, 0, "lcbn global order\n");
  dump_lcbn_globalorder();
  fflush(NULL);
#endif
  mylog(LOG_MAIN, 0, "dump_and_exit_sighandler: Got signum %i, exiting 1 (should trigger any atexit routines\n", signum);
  fflush(NULL);
  exit(1);
}

/* Indexed by value of 'enum callback_type'. */
int callback_type_to_nargs[] = 
{
  3 /* UV_ALLOC_CB */, 3 /* UV_READ_CB */,
  2 /* UV_WRITE_CB */, 2 /* UV_CONNECT_CB */,
  2 /* UV_SHUTDOWN_CB */, 2 /* UV_CONNECTION_CB */,
  1 /* UV_CLOSE_CB */, 3 /* UV_POLL_CB */,
  1 /* UV_TIMER_CB */, 1 /* UV_ASYNC_CB */,
  1 /* UV_PREPARE_CB */, 1 /* UV_CHECK_CB */,
  1 /* UV_IDLE_CB */, 3 /* UV_EXIT_CB */,
  2 /* UV_WALK_CB */, 1 /* UV_FS_WORK_CB */,
  1 /* UV_FS_CB */, 1 /* UV_WORK_CB */,
  2 /* UV_AFTER_WORK_CB */, 1 /* UV_GETADDRINFO_WORK_CB */,
  3 /* UV_GETADDRINFO_CB */, 1 /* UV_GETNAMEINFO_WORK_CB */,
  4 /* UV_GETNAMEINFO_CB */, 4 /* UV_FS_EVENT_CB */,
  4 /* UV_FS_POLL_CB */, 2 /* UV_SIGNAL_CB */,
  2 /* UV_UDP_SEND_CB */, 5 /* UV_UDP_RECV_CB */,
  1 /* UV_THREAD_CB */, 3 /* UV__IO_CB */,
  3 /* UV__ASYNC_CB */, 1 /* UV__WORK_WORK */,
  2 /* UV__WORK_DONE */
};

/* Execute the callback described by CBN based on CBN->info. */
static void cbi_execute_callback (callback_info_t *cbi)
{
  assert(cbi);
  assert(cbi->cb);

  ENTRY_EXIT_LOG((LOG_MAIN, 9, "cbi_execute_callback: Begin: cbi %p\n", cbi));

  /* Invoke the callback. */
  switch (cbi->type)
  {
    /* User-defined CBs. */
    /* include/uv.h */
    case UV_ALLOC_CB:
      assert(callback_type_to_nargs[cbi->type] == 3);
      ((uv_alloc_cb) cbi->cb)((uv_handle_t *) cbi->args[0], (size_t) cbi->args[1], (uv_buf_t *) cbi->args[2]);
      break;
    case UV_READ_CB:
      assert(callback_type_to_nargs[cbi->type] == 3);
      mylog(LOG_MAIN, 5, "cbi_execute_callback: uv_read_cb(%p, %li, %p)\n", (uv_stream_t *) cbi->args[0], (ssize_t) cbi->args[1], (uv_buf_t *) cbi->args[2]);
      ((uv_read_cb) cbi->cb)((uv_stream_t *) cbi->args[0], (ssize_t) cbi->args[1], (const uv_buf_t *) cbi->args[2]);
      break;
    case UV_WRITE_CB:
      assert(callback_type_to_nargs[cbi->type] == 2);
      ((uv_write_cb) cbi->cb)((uv_write_t *) cbi->args[0], (int) cbi->args[1]);
      break;
    case UV_CONNECT_CB:
      assert(callback_type_to_nargs[cbi->type] == 2);
      ((uv_connect_cb) cbi->cb)((uv_connect_t *) cbi->args[0], (int) cbi->args[1]);
      break;
    case UV_SHUTDOWN_CB:
      assert(callback_type_to_nargs[cbi->type] == 2);
      ((uv_shutdown_cb) cbi->cb)((uv_shutdown_t *) cbi->args[0], (int) cbi->args[1]);
      break;
    case UV_CONNECTION_CB:
      assert(callback_type_to_nargs[cbi->type] == 2);
      ((uv_connection_cb) cbi->cb)((uv_stream_t *) cbi->args[0], (int) cbi->args[1]);
      break;
    case UV_CLOSE_CB:
      assert(callback_type_to_nargs[cbi->type] == 1);
      ((uv_close_cb) cbi->cb)((uv_handle_t *) cbi->args[0]);
      break;
    case UV_POLL_CB:
      assert(callback_type_to_nargs[cbi->type] == 3);
      ((uv_poll_cb) cbi->cb)((uv_poll_t *) cbi->args[0], (int) cbi->args[1], (int) cbi->args[2]);
      break;
    case UV_TIMER_CB:
      assert(callback_type_to_nargs[cbi->type] == 1);
      ((uv_timer_cb) cbi->cb)((uv_timer_t *) cbi->args[0]);
      break;
    case UV_ASYNC_CB:
      assert(callback_type_to_nargs[cbi->type] == 1);
      ((uv_async_cb) cbi->cb)((uv_async_t *) cbi->args[0]);
      break;
    case UV_PREPARE_CB:
      assert(callback_type_to_nargs[cbi->type] == 1);
      ((uv_prepare_cb) cbi->cb)((uv_prepare_t *) cbi->args[0]);
      break;
    case UV_CHECK_CB:
      assert(callback_type_to_nargs[cbi->type] == 1);
      ((uv_check_cb) cbi->cb)((uv_check_t *) cbi->args[0]);
      break;
    case UV_IDLE_CB:
      assert(callback_type_to_nargs[cbi->type] == 1);
      ((uv_idle_cb) cbi->cb)((uv_idle_t *) cbi->args[0]);
      break;
    case UV_EXIT_CB:
      assert(callback_type_to_nargs[cbi->type] == 3);
      ((uv_exit_cb) cbi->cb)((uv_process_t *) cbi->args[0], (int64_t) cbi->args[1], (int) cbi->args[2]);
      break;
    case UV_WALK_CB:
      assert(callback_type_to_nargs[cbi->type] == 2);
      ((uv_walk_cb) cbi->cb)((uv_handle_t *) cbi->args[0], (void *) cbi->args[1]);
      break;
    case UV_FS_WORK_CB:
      assert(callback_type_to_nargs[cbi->type] == 1);
      ((uv_internal_work_cb) cbi->cb)((struct uv__work *) cbi->args[0]);
      break;
    case UV_FS_CB:
      assert(callback_type_to_nargs[cbi->type] == 1);
      ((uv_fs_cb) cbi->cb)((uv_fs_t *) cbi->args[0]);
      break;
    case UV_WORK_CB:
      assert(callback_type_to_nargs[cbi->type] == 1);
      ((uv_work_cb) cbi->cb)((uv_work_t *) cbi->args[0]);
      break;
    case UV_AFTER_WORK_CB:
      assert(callback_type_to_nargs[cbi->type] == 2);
      ((uv_after_work_cb) cbi->cb)((uv_work_t *) cbi->args[0], (int) cbi->args[1]);
      break;
    case UV_GETADDRINFO_WORK_CB:
      assert(callback_type_to_nargs[cbi->type] == 1);
      ((uv_internal_work_cb) cbi->cb)((struct uv__work *) cbi->args[0]);
      break;
    case UV_GETNAMEINFO_WORK_CB:
      assert(callback_type_to_nargs[cbi->type] == 1);
      ((uv_internal_work_cb) cbi->cb)((struct uv__work *) cbi->args[0]);
      break;
    case UV_GETADDRINFO_CB:
      assert(callback_type_to_nargs[cbi->type] == 3);
      ((uv_getaddrinfo_cb) cbi->cb)((uv_getaddrinfo_t *) cbi->args[0], (int) cbi->args[1], (struct addrinfo *) cbi->args[2]);
      break;
    case UV_GETNAMEINFO_CB:
      assert(callback_type_to_nargs[cbi->type] == 4);
      ((uv_getnameinfo_cb) cbi->cb)((uv_getnameinfo_t *) cbi->args[0], (int) cbi->args[1], (const char *) cbi->args[2], (const char *) cbi->args[3]);
      break;
    case UV_FS_EVENT_CB:
      assert(callback_type_to_nargs[cbi->type] == 4);
      ((uv_fs_event_cb) cbi->cb)((uv_fs_event_t *) cbi->args[0], (const char *) cbi->args[1], (int) cbi->args[2], (int) cbi->args[3]);
      break;
    case UV_FS_POLL_CB:
      assert(callback_type_to_nargs[cbi->type] == 4);
      ((uv_fs_poll_cb) cbi->cb)((uv_fs_poll_t *) cbi->args[0], (int) cbi->args[1], (const uv_stat_t *) cbi->args[2], (const uv_stat_t *) cbi->args[3]);
      break;
    case UV_SIGNAL_CB:
      assert(callback_type_to_nargs[cbi->type] == 2);
      ((uv_signal_cb) cbi->cb)((uv_signal_t *) cbi->args[0], (int) cbi->args[1]);
      break;
    case UV_UDP_SEND_CB:
      assert(callback_type_to_nargs[cbi->type] == 2);
      ((uv_udp_send_cb) cbi->cb)((uv_udp_send_t *) cbi->args[0], (int) cbi->args[1]);
      break;
    case UV_UDP_RECV_CB:
      /* Peer cbi is in the sockaddr_storage of cbi->args[3]. */
      assert(callback_type_to_nargs[cbi->type] == 5);
      ((uv_udp_recv_cb) cbi->cb)((uv_udp_t *) cbi->args[0], (ssize_t) cbi->args[1], (const uv_buf_t *) cbi->args[2], (const struct sockaddr *) cbi->args[3], (unsigned) cbi->args[4]);
      break;
    case UV_THREAD_CB:
      assert(callback_type_to_nargs[cbi->type] == 1);
      ((uv_thread_cb) cbi->cb)((void *) cbi->args[0]);
      break;

    /* Internal CBs. */

    /* include/uv-unix.h */
    case UV__IO_CB:
      assert(callback_type_to_nargs[cbi->type] == 3);
      ((uv__io_cb) cbi->cb)((struct uv_loop_s *) cbi->args[0], (struct uv__io_s *) cbi->args[1], (unsigned int) cbi->args[2]);
      break;
    case UV__ASYNC_CB:
      assert(callback_type_to_nargs[cbi->type] == 3);
      ((uv__async_cb) cbi->cb)((struct uv_loop_s *) cbi->args[0], (struct uv__async *) cbi->args[1], (unsigned int) cbi->args[2]);
      break;

    /* include/uv-threadpool.h */
    case UV__WORK_WORK:
      assert(callback_type_to_nargs[cbi->type] == 1);
      ((uv__work_work_cb) cbi->cb)((struct uv__work *) cbi->args[0]);
      break;
    case UV__WORK_DONE:
      assert(callback_type_to_nargs[cbi->type] == 2);
      ((uv__work_done_cb) cbi->cb)((struct uv__work *) cbi->args[0], (int) cbi->args[1]);
      break;

    default:
      assert(!"cbi_execute_callback: ERROR, unsupported type");
  }

  ENTRY_EXIT_LOG((LOG_MAIN, 9, "cbi_execute_callback: returning\n"));
}


/* Static functions added for unified callback. */

/* Global time. */
static void mark_global_start (void);

/* NB these are not thread safe. Use a mutex to ensure that exec_id actually matches exec order, and so on. */
static unsigned lcbn_next_exec_id (void);
static unsigned lcbn_next_reg_id (void);

unsigned lcbn_global_exec_counter = 0;
unsigned lcbn_global_reg_counter = 0;

int is_active_user_cb = 0;

struct list *lcbn_global_reg_order_list = NULL;

/* Maps pthread_t to lcbn_t *, accommodating callbacks
     being executed by the threadpool and looper threads.
   The entry for a given pthread_t is the callback_node currently being executed
     by that thread.
   If the value is NULL, the next callback by that node will be a root
     (unless the callback is an asynchronous one for which the parent is already known). */
struct map *tid_to_current_lcbn;

/* Maps pthread_t to internal id, yielding human-readable thread IDs. */
struct map *pthread_to_tid;

/* Code in support of tracking the initial stack. */

/* Returns the CBN associated with the initial stack.
   Not thread safe the first time it is called. */
int init_stack_lcbn_initialized = 0;
static lcbn_t * get_init_stack_lcbn (void)
{
  static lcbn_t *init_stack_lcbn = NULL;
  if (!init_stack_lcbn)
  {
    init_stack_lcbn = lcbn_create(NULL, NULL, 0);

    init_stack_lcbn->cb_type = INITIAL_STACK;

    init_stack_lcbn->global_exec_id = lcbn_next_exec_id();
    init_stack_lcbn->global_reg_id = lcbn_next_reg_id();
    scheduler_register_lcbn(init_stack_lcbn);
    init_stack_lcbn_initialized = 1;
  }

  assert(lcbn_looks_valid(init_stack_lcbn));
  return init_stack_lcbn;
}

static lcbn_t * get_exit_lcbn (void)
{
  static lcbn_t *exit_lcbn = NULL;
  if (!exit_lcbn)
  {
    exit_lcbn = lcbn_create(NULL, NULL, 0);

    exit_lcbn->cb_type = EXIT;

    exit_lcbn->global_exec_id = lcbn_next_exec_id();
    exit_lcbn->global_reg_id = lcbn_next_reg_id();
    scheduler_register_lcbn(exit_lcbn);
  }

  assert(lcbn_looks_valid(exit_lcbn));
  return exit_lcbn;
}

/* Initialize everything for the record-and-replay code. 
 * This is a catch-all initialization function. 
 * It initializes:
 *    - logging (mylog)
 *    - the scheduler
 *    - unified callback variables
 *
 * It should be called exactly once.
 */
void initialize_record_and_replay (void)
{
  static int initialized = 0;

  assert(!initialized);
  initialized = 1;

  /* mylog */
  mylog_init();
  mylog_set_verbosity(LOG_MAIN, 9);
  mylog_set_verbosity(LOG_LCBN, 7);
  mylog_set_verbosity(LOG_SCHEDULER, 5);
  mylog_set_verbosity(LOG_THREADPOOL, 9);
  mylog_set_verbosity(LOG_TIMER, 9);

  mylog_set_verbosity(LOG_LIST, 9);
  mylog_set_verbosity(LOG_MAP, 5);
  mylog_set_verbosity(LOG_TREE, 5);

  mylog_set_verbosity(LOG_UV_STREAM, 9);
  mylog_set_verbosity(LOG_UV_IO, 9);
  mylog_set_verbosity(LOG_UV_ASYNC, 9);

#ifdef JD_UT
  mylog(LOG_MAIN, 1, "initialize_record_and_replay: Running unit tests\n");
  list_UT();
  map_UT();
  tree_UT();
  lcbn_UT();
  mylog_UT();
  scheduler_UT();
  mylog(LOG_MAIN, 1, "initialize_record_and_replay: Done running unit tests\n");
#endif

  /* scheduler */
  initialize_scheduler();
  scheduler_register_thread(THREAD_TYPE_LOOPER);

  /* invoke_callback */
  tid_to_current_lcbn = map_create();
  assert(tid_to_current_lcbn != NULL);

  pthread_to_tid = map_create();
  assert(pthread_to_tid != NULL);

  (void) get_init_stack_lcbn(); /* Initializes the first time it is called. */

  /* Record initialization time. */
  mark_global_start();
}

/* Initialize the scheduler (scheduler_init).
 * Call exactly once.
 *
 * At the moment, scheduler parameters are provided through the following environment variables:
 *    Environment variable            Details                                   Notes
 * ---------------------------------------------------------------------------------------------------
 *     UV_SCHEDULER_TYPE           Changes the scheduler type.
 *                                 Choose from: VANILLA, FUZZING_TIME, TP_FREEDOM.
 *                                 Each scheduler is parameterized using environment variables.
 *
 *                                 VANILLA                                      Schedule is inviolate. As natural as possible.
 *                                  Parameters
 *                                     [UV_THREADPOOL_SIZE]                     Default 4
 *                                 FUZZING_TIME                                 Schedule order is fuzzed through the insertion of random sleeps
 *                                  Parameters
 *                                     UV_SCHEDULER_MIN_DELAY                   In useconds
 *                                     UV_SCHEDULER_MAX_DELAY                   In useconds
 *                                     UV_SCHEDULER_DELAY_PERC                  The percentage of CBs to delay
 *                                     [UV_THREADPOOL_SIZE]                     Default 4
 *
 *                                 TP_FREEDOM                                   Schedule order is fuzzed through explicitly flipping the order of TP "work" and "done" events
 *                                  Parameters
 *                                     UV_SCHEDULER_TP_DEG_FREEDOM              TP: The number of TP threads to simulate.
 *                                                                              "1" means to simulate 1 thread; TP work will not be re-ordered.
 *                                                                              "2" means to simulate 2 threads; the TP thread will always do one of the first two pending events.
 *                                                                              -1 means "select at random from the entire list", and is essentially a dynamically-sized TP.
 *                                                                              If you give -1, we'll wait for the TP delay you specify to let the queue become maximally full.
 *                                     UV_SCHEDULER_TP_MAX_DELAY                TP: Max delay while waiting for queue to fill. usec.
 *                                     UV_SCHEDULER_TP_EPOLL_THRESHOLD          TP: Max time looper can be in epoll while TP waits for work queue to fill. usec.
 *                                     UV_SCHEDULER_IOPOLL_DEG_FREEDOM          Looper (io_poll): Legal "shuffle distance" of the epoll events.
 *                                                                              The "shuffle distance" is the size of the chunks into which we break the epoll events.
 *                                                                              "1" means that items will not be shuffled.
 *                                                                              "2" means that we'll break up the epoll events into pairs and may shuffle each pair.
 *                                                                              "-1" means "shuffle everything".
 *                                     UV_SCHEDULER_IOPOLL_DEFER_PERC           Looper (io_poll): Percentage of epoll events to defer each loop
 *                                     [UV_SCHEDULER_TIMER_EARLY_EXEC_TPERC]    Probability of executing a timer early. Give in tenths of a percent (so 1 = 0.1%, 10 = 1%, 1000 = 100%).
 *                                                                              Default is 0: never execute timers early.
 *                                     [UV_SCHEDULER_TIMER_MAX_EARLY_MULTIPLE]  Goes with UV_SCHEDULER_TIMER_EARLY_EXEC_TPERC. We'll only execute a timer early once we're "close enough"
 *                                                                              to its actual timeout. This is expressed as the ratio of the full time to how long since it was registered
 *                                                                              (so 4 means "execute it up to 4x early [100 sec -> can be executed once 25 sec have passed]).
 *                                                                              -1 means that there's no limit on how early we might execute a timer.
 *                                                                              Default is 2: "execute it up to 2x early".
 *                                     [UV_SCHEDULER_TIMER_LATE_EXEC_TPERC]     Probability of executing a timer late. Give in tenths of a percent (so 1 = 0.1%, 10 = 1%, 1000 = 100%).
 *                                                                              This means we'll probabilistically break out of timer execution and proceed through the event loop.
 *                                                                              Default is 0: never execute timers late.
 *                                     UV_THREADPOOL_SIZE                       Must be 1
 *
 *     UV_SCHEDULER_MODE           Choose from: RECORD[, REPLAY]                Defaults to RECORD
 *
 *     UV_SCHEDULER_SCHEDULE_FILE  Where to emit or load schedule             Defaults to /tmp/f.sched
 */
static void initialize_scheduler (void)
{
  char *scheduler_typeP = NULL, *scheduler_modeP = NULL, *schedule_fileP = NULL;
  scheduler_type_t scheduler_type;
  scheduler_mode_t scheduler_mode;
  struct stat stat_buf;

  scheduler_vanilla_args_t vanilla_args;
  scheduler_fuzzing_timer_args_t fuzzing_timer_args;
  scheduler_tp_freedom_args_t tp_freedom_args;
  void *args;

  memset(&vanilla_args, 0, sizeof vanilla_args);
  memset(&fuzzing_timer_args, 0, sizeof fuzzing_timer_args);
  memset(&tp_freedom_args, 0, sizeof tp_freedom_args);

  /* Scheduler type. */
  scheduler_typeP = getenv("UV_SCHEDULER_TYPE");
  if (!scheduler_typeP)
    assert(!"Error, you must provide UV_SCHEDULER_TYPE");

  if (strcmp(scheduler_typeP, "VANILLA") == 0)
  {
    scheduler_type = SCHEDULER_TYPE_VANILLA;
    args = &fuzzing_timer_args;
  }
  else if (strcmp(scheduler_typeP, "FUZZING_TIME") == 0 || strcmp(scheduler_typeP, "FUZZING_TIMER") == 0)
  {
    char *scheduler_min_delayP = NULL, *scheduler_max_delayP = NULL, *scheduler_delay_percP = NULL;

    scheduler_type = SCHEDULER_TYPE_FUZZING_TIME;

    scheduler_min_delayP = getenv("UV_SCHEDULER_MIN_DELAY");
    scheduler_max_delayP = getenv("UV_SCHEDULER_MAX_DELAY");
    scheduler_delay_percP = getenv("UV_SCHEDULER_DELAY_PERC");
    if (scheduler_min_delayP == NULL || scheduler_max_delayP == NULL || scheduler_delay_percP == NULL)
      assert(!"Error, for scheduler FUZZING_TIME, you must provide env. vars: UV_SCHEDULER_{MIN,MAX}_DELAY, UV_SCHEDULER_DELAY_PERC");

    fuzzing_timer_args.min_delay = atoi(scheduler_min_delayP);
    fuzzing_timer_args.max_delay = atoi(scheduler_max_delayP);
    fuzzing_timer_args.delay_perc = atoi(scheduler_delay_percP);
    args = &fuzzing_timer_args;
  }
  else if (strcmp(scheduler_typeP, "TP_FREEDOM") == 0)
  {
    char *scheduler_tp_deg_freedomP = NULL, *scheduler_tp_max_delayP = NULL, *scheduler_tp_epoll_thresholdP = NULL,
         *scheduler_iopoll_deg_freedomP = NULL, *scheduler_iopoll_defer_percP = NULL,
         *scheduler_timer_early_exec_tpercP = NULL, *scheduler_timer_max_early_multipleP = NULL, *scheduler_timer_late_exec_tpercP = NULL, 
         *tp_sizeP = NULL;

    /* Defaults. */
    int scheduler_timer_early_exec_tperc = 0, scheduler_timer_max_early_multiple = 2, scheduler_timer_late_exec_tperc = 0;

    scheduler_type = SCHEDULER_TYPE_TP_FREEDOM;

    scheduler_tp_deg_freedomP = getenv("UV_SCHEDULER_TP_DEG_FREEDOM");
    if (scheduler_tp_deg_freedomP == NULL)
      assert(!"Error, for scheduler TP_FREEDOM, you must provide UV_SCHEDULER_TP_DEG_FREEDOM");

    scheduler_tp_max_delayP = getenv("UV_SCHEDULER_TP_MAX_DELAY");
    if (scheduler_tp_max_delayP == NULL)
      assert(!"Error, for scheduler TP_FREEDOM, you must provide UV_SCHEDULER_TP_MAX_DELAY");

    scheduler_tp_epoll_thresholdP = getenv("UV_SCHEDULER_TP_EPOLL_THRESHOLD");
    if (scheduler_tp_epoll_thresholdP == NULL)
      assert(!"Error, for scheduler TP_FREEDOM, you must provide UV_SCHEDULER_TP_EPOLL_THRESHOLD");

    scheduler_iopoll_deg_freedomP = getenv("UV_SCHEDULER_IOPOLL_DEG_FREEDOM");
    if (scheduler_iopoll_deg_freedomP == NULL)
      assert(!"Error, for scheduler TP_FREEDOM, you must provide UV_SCHEDULER_IOPOLL_DEG_FREEDOM");

    scheduler_iopoll_defer_percP = getenv("UV_SCHEDULER_IOPOLL_DEFER_PERC");
    if (scheduler_iopoll_defer_percP == NULL)
      assert(!"Error, for scheduler TP_FREEDOM, you must provide UV_SCHEDULER_IOPOLL_DEFER_PERC");

    scheduler_timer_early_exec_tpercP = getenv("UV_SCHEDULER_TIMER_EARLY_EXEC_TPERC");
    if (scheduler_timer_early_exec_tpercP != NULL)
      scheduler_timer_early_exec_tperc = atoi(scheduler_timer_early_exec_tpercP);

    scheduler_timer_max_early_multipleP = getenv("UV_SCHEDULER_TIMER_MAX_EARLY_MULTIPLE");
    if (scheduler_timer_max_early_multipleP != NULL)
      scheduler_timer_max_early_multiple = atoi(scheduler_timer_max_early_multipleP );

    scheduler_timer_late_exec_tpercP = getenv("UV_SCHEDULER_TIMER_LATE_EXEC_TPERC");
    if (scheduler_timer_late_exec_tpercP != NULL)
      scheduler_timer_late_exec_tperc = atoi(scheduler_timer_late_exec_tpercP);

    tp_sizeP = getenv("UV_THREADPOOL_SIZE");
    if (tp_sizeP == NULL || atoi(tp_sizeP) != 1)
      assert(!"Error, for scheduler TP_FREEDOM, you must provide UV_THREADPOOL_SIZE=1");

    tp_freedom_args.tp_degrees_of_freedom = atoi(scheduler_tp_deg_freedomP);
    tp_freedom_args.tp_max_delay_us = atol(scheduler_tp_max_delayP);
    tp_freedom_args.tp_epoll_threshold = atol(scheduler_tp_epoll_thresholdP);
    tp_freedom_args.iopoll_degrees_of_freedom = atoi(scheduler_iopoll_deg_freedomP);
    tp_freedom_args.iopoll_defer_perc = atoi(scheduler_iopoll_defer_percP);
    tp_freedom_args.timer_early_exec_tperc = scheduler_timer_early_exec_tperc;
    tp_freedom_args.timer_max_early_multiple = scheduler_timer_max_early_multiple;
    tp_freedom_args.timer_late_exec_tperc = scheduler_timer_late_exec_tperc;
    args = &tp_freedom_args;
  }
  else
    assert(!"Error, unsupported UV_SCHEDULER_TYPE");

  /* Scheduler mode. */
  scheduler_modeP = getenv("UV_SCHEDULER_MODE");
  if (!scheduler_modeP)
    scheduler_modeP = "RECORD";

  if (strcmp(scheduler_modeP, "RECORD") == 0)
    scheduler_mode = SCHEDULER_MODE_RECORD; 
  else if (strcmp(scheduler_modeP, "REPLAY") == 0)
    scheduler_mode = SCHEDULER_MODE_REPLAY; 
  else
    assert(!"Error, UV_SCHEDULER_MODE was neither RECORD nor REPLAY");

  /* Scheduler file. */
  schedule_fileP = getenv("UV_SCHEDULER_SCHEDULE_FILE");
  if (!schedule_fileP)
    schedule_fileP = "/tmp/f.sched";
  if (scheduler_mode == SCHEDULER_MODE_REPLAY && stat(schedule_fileP, &stat_buf))
    assert(!"Error, scheduler_mode REPLAY but schedule_file does not exist");

  mylog(LOG_MAIN, 1, "scheduler_type %s scheduler_mode %s schedule_file %s\n", scheduler_type_to_string(scheduler_type), scheduler_mode_to_string(scheduler_mode), schedule_fileP);
  scheduler_init(scheduler_type, scheduler_mode, schedule_fileP, args);
}

/* Invoke the callback described by CBI.
   Returns the CBN allocated for the callback.

   A condition variable is used to prevent multiple threads from
   invoking LCBNs concurrently. */
void invoke_callback (callback_info_t *cbi)
{
  /* JD: TODO Add calls to scheduler. */
  void *context = NULL;
  enum callback_context cb_context;
  lcbn_t *lcbn_orig = NULL, /* The LCBN executing at the time of this call (i.e. this call is a nested CB). */
         *lcbn_cur = NULL,  /* The LCBN executed by this call. */
         *lcbn_par = NULL;  /* The LCBN that registered lcbn_cur. */
  tree_node_t *tree_par = NULL;
  int is_logical_cb = 0; /* 1 if it's a UV_*CB, 0 if it's a UV__*CB. */
  int is_user_cb = 0;    /* if (is_logical_cb): 1 if it's a user UV_*CB, else 0. Only 0 if it's the UV_ASYNC_CB associated with the TP's done items. */
  /* if (is_user_cb), then is_logical_cb, too. */

  /* Scheduler supplies. */
  spd_before_exec_cb_t spd_before_exec_cb;
  spd_after_exec_cb_t spd_after_exec_cb;

  assert(cbi);

  ENTRY_EXIT_LOG((LOG_MAIN, 9, "invoke_callback: Begin: cbi %p (type %s)\n", cbi, callback_type_to_string(cbi->type)));

  cb_context = callback_type_to_context(cbi->type);
  is_logical_cb = (cb_context != CALLBACK_CONTEXT_UNKNOWN);

  if (is_logical_cb)
  {
    struct map *cb_type_to_lcbn = NULL;
    uv_handle_t *context_handle = NULL;
    uv_req_t *context_req = NULL;

    /* Determine the context. */
    context = (void *) cbi->args[0];
    if (cb_context == CALLBACK_CONTEXT_HANDLE)
    {
      context_handle = (uv_handle_t *) context;
      assert(context_handle != NULL && context_handle->magic == UV_HANDLE_MAGIC);
      cb_type_to_lcbn = context_handle->cb_type_to_lcbn;
    }
    else if (cb_context == CALLBACK_CONTEXT_REQ)
    {
      if (cbi->type == UV_FS_WORK_CB) /* args[0] is the uv__work of a uv_fs_t request. */
        context_req = (uv_req_t *) container_of(cbi->args[0], uv_fs_t, work_req);
      else if (cbi->type == UV_GETADDRINFO_WORK_CB) /* args[0] is the uv__work of a uv_getaddrinfo_t request. */
        context_req = (uv_req_t *) container_of(cbi->args[0], uv_getaddrinfo_t, work_req);
      else if (cbi->type == UV_GETNAMEINFO_WORK_CB) /* args[0] is the uv__work of a uv_getnameinfo_t request. */
        context_req = (uv_req_t *) container_of(cbi->args[0], uv_getnameinfo_t, work_req);
      else
        context_req = (uv_req_t *) context;
      assert(context_req != NULL && context_req->magic == UV_REQ_MAGIC);
      cb_type_to_lcbn = context_req->cb_type_to_lcbn;
    }
    else
      assert(!"invoke_callback: Error, unexpected cb_context type");

    /* Is it a user CB?
     * The threadpool uses the async cb to signal pending 'done' items.
     */
    if (cb_context == CALLBACK_CONTEXT_HANDLE && context_handle->type == UV_ASYNC && ((uv_async_t *) context_handle)->async_cb == uv__work_done)
      is_user_cb = 0;
    else
      is_user_cb = 1;

    mylog(LOG_MAIN, 7, "invoke_callback: cb_context %i is_logical_cb %i is_user_cb %i\n", cb_context, is_logical_cb, is_user_cb);

    /* If we are invoking a user-provided callback, retrieve and update the lcbn. */
    assert(cb_type_to_lcbn && map_looks_valid(cb_type_to_lcbn)); 

    /* Extract lcbn (and registration parent info, for logging). */
    lcbn_cur = lcbn_get(cb_type_to_lcbn, cbi->type);
    assert(lcbn_looks_valid(lcbn_cur));
    assert(lcbn_cur->cb_type == cbi->type);
    tree_par = tree_get_parent(&lcbn_cur->tree_node);
    assert(tree_par);
    lcbn_par = tree_entry(tree_par, lcbn_t, tree_node);
    assert(lcbn_looks_valid(lcbn_par));

    /* Embed any extra info. */
    if (lcbn_cur->cb_type == UV_READ_CB)
    {
      size_t size      = sizeof(lcbn_cur->extra_info),
             len       = strnlen(lcbn_cur->extra_info, size),
             remaining = size - len; 
      /* TODO fd extraction is hack-y. See unix/internal.h: uv__stream_fd. */
      snprintf(lcbn_cur->extra_info + len, remaining, "<%li = read(%i)>", (ssize_t) cbi->args[1], ((uv_stream_t *) cbi->args[0])->io_watcher.fd);
    }
    if (!is_user_cb)
      lcbn_mark_non_user(lcbn_cur);

    /* Execution parent (if nested). */
    lcbn_orig = lcbn_current_get();

    mylog(LOG_MAIN, 3, "invoke_callback: Working with lcbn %p (type %s) context %p parent %p (type %s) lcbn_orig %p; is_user_cb %i\n",
      lcbn_cur, callback_type_to_string(cbi->type), context, lcbn_par, callback_type_to_string(lcbn_par->cb_type), lcbn_orig, is_user_cb);

    lcbn_cur->info = cbi;
    lcbn_cur->executing_thread = pthread_self_internal();

    mylog(LOG_MAIN, 5, "invoke_callback: I am going to invoke LCBN %p (type %s). is_user_cb %i is_active_user_cb %i\n", lcbn_cur, callback_type_to_string(lcbn_cur->cb_type), is_user_cb, is_active_user_cb);

    if (callback_type_to_behavior(lcbn_cur->cb_type) == CALLBACK_BEHAVIOR_RESPONSE)
    {
      /* If this LCBN is a response, it may repeat. If so, the next response must come after this response,
         and is in some sense caused by this response. Consequently, register after setting LCBN so that it becomes a child of this LCBN. */
      lcbn_current_set(lcbn_cur);
      mylog(LOG_MAIN, 5, "invoke_callback: registering cb (context %p type %s) as child of LCBN %p\n",
        lcbn_get_context(lcbn_cur), callback_type_to_string(lcbn_get_cb_type(lcbn_cur)), lcbn_cur);
      uv__register_callback(lcbn_get_context(lcbn_cur), lcbn_get_cb(lcbn_cur), lcbn_get_cb_type(lcbn_cur));
      lcbn_current_set(lcbn_orig);

      /* READ_CBs are dependent on the associated ALLOC_CBs. 
         The ALLOC_CB has already been invoke_callback'd and updated cb_type_to_lcbn. */
      if (lcbn_cur->cb_type == UV_READ_CB)
      {
        lcbn_t *new_alloc_lcbn = lcbn_get(cb_type_to_lcbn, UV_ALLOC_CB);
        lcbn_t *new_read_lcbn  = lcbn_get(cb_type_to_lcbn, UV_READ_CB);
        assert(new_alloc_lcbn && !lcbn_executed(new_alloc_lcbn));
        assert(new_read_lcbn && !lcbn_executed(new_read_lcbn));

        lcbn_add_dependency(new_alloc_lcbn, new_read_lcbn);
      }
    }

    /* Commit to being the active CB. */
    lcbn_current_set(lcbn_cur);
    lcbn_mark_begin(lcbn_cur);
    lcbn_cur->global_exec_id = lcbn_next_exec_id();

    mylog(LOG_MAIN, 7, "invoke_callback: Invoking lcbn %p (type %s) exec_id %i\n", lcbn_cur, callback_type_to_string(lcbn_cur->cb_type), lcbn_cur->global_exec_id);
  } /* is_logical_cb */

  /* Yield to scheduler. */
  spd_before_exec_cb_init(&spd_before_exec_cb);
  spd_before_exec_cb.lcbn = lcbn_cur;
  scheduler_thread_yield(SCHEDULE_POINT_BEFORE_EXEC_CB, &spd_before_exec_cb);

  /* User code or not, run the callback. */
  mylog(LOG_MAIN, 7, "invoke_callback: Invoking cbi %p (type %s)\n", cbi, callback_type_to_string(cbi->type));
  cbi_execute_callback(cbi); 
  mylog(LOG_MAIN, 7, "invoke_callback: Done invoking cbi %p (type %s)\n", cbi, callback_type_to_string(cbi->type));

  /* Done with the callback.
     If was a user callback, restore the previous lcbn. */
  if (is_logical_cb)
  {
    mylog(LOG_MAIN, 5, "invoke_callback: Done with lcbn %p (type %s) exec_id %i\n", lcbn_cur, callback_type_to_string(lcbn_cur->cb_type), lcbn_cur->global_exec_id);
    lcbn_mark_end(lcbn_cur);
    lcbn_current_set(lcbn_orig);
  }

  /* Yield to scheduler. */
  assert(scheduler_current_cb_thread() == uv_thread_self()); /* Only fails if threadpool.c:cleanup ever happens and returns from the CB. */
  spd_after_exec_cb_init(&spd_after_exec_cb);
  spd_after_exec_cb.lcbn = lcbn_cur;
  scheduler_thread_yield(SCHEDULE_POINT_AFTER_EXEC_CB, &spd_after_exec_cb);

  ENTRY_EXIT_LOG((LOG_MAIN, 9, "invoke_callback: returning\n"));
}

/* Returns time in microseconds (us) relative to the time at which the first CB was invoked. */
struct timespec global_start;
static void mark_global_start (void)
{
  static int here = 0;
  if (!here)
  {
    assert(clock_gettime(CLOCK_MONOTONIC, &global_start) == 0);
    here = 1;
  }
}

/* Call from any libuv function that is passed a user callback.
   At callback registration time we can determine the origin
     and create a new logical node.
   If CB is NULL, CB will never be called, and we ignore it. 

   May be called concurrently due to threadpool. Thread safe. 
   
   CONTEXT is the handle or request associated with this callback.
   Use callback_type_to_context and callback_type_to_behavior to deal with it. 
   If CONTEXT is a uv_req_t *, it must have been uv_req_init'd already. 

   TODO We call lcbn_next_reg_id without mutex. If threadpool 'work' items register new CBs,
     we're in trouble. */
void uv__register_callback (void *context, any_func cb, enum callback_type cb_type)
{
  uv_handle_t *context_handle = NULL;
  uv_req_t *context_req = NULL;
  struct map *cb_type_to_lcbn = NULL;
  enum callback_context cb_context;
  lcbn_t *lcbn_cur = NULL, *lcbn_new = NULL;

  mylog(LOG_MAIN, 5, "uv__register_callback: context %p cb_type %s\n", context, callback_type_to_string(cb_type));
  assert(init_stack_lcbn_initialized);

  /* Identify the context of the callback. */
  assert(context);
  cb_context = callback_type_to_context(cb_type);
  if (cb_context == CALLBACK_CONTEXT_HANDLE)
  {
    context_handle = (uv_handle_t *) context;
    assert(context_handle->magic == UV_HANDLE_MAGIC);
    cb_type_to_lcbn = context_handle->cb_type_to_lcbn;
  }
  else if (cb_context == CALLBACK_CONTEXT_REQ)
  {
    context_req = (uv_req_t *) context;
    assert(context_req->magic == UV_REQ_MAGIC);
    cb_type_to_lcbn = context_req->cb_type_to_lcbn;
  }
  else
    assert(!"uv__register_callback: Error, unexpected cb_context");
  assert(cb_type_to_lcbn && map_looks_valid(cb_type_to_lcbn)); 

  /* Identify the origin of the callback. */
  lcbn_cur = lcbn_current_get();
  /* TODO -- happens during 'npm install' after 'end of loop' is printed by Node. During exit I guess. */
  assert(lcbn_cur); /* All callbacks are registered by application or library code. */

  /* Create a new LCBN. */
  lcbn_new = lcbn_create(context, cb, cb_type);
  /* Register it in its context. */
  lcbn_register(cb_type_to_lcbn, cb_type, lcbn_new);
  lcbn_add_child(lcbn_cur, lcbn_new);

  /* Add to metadata structures. */
  lcbn_new->global_reg_id = lcbn_next_reg_id();
  scheduler_register_lcbn(lcbn_new);

  mylog(LOG_MAIN, 5, "uv__register_callback: lcbn %p context %p type %s registrar %p\n",
    lcbn_new, context, callback_type_to_string(cb_type), lcbn_cur);
}

/* Implementation for tracking the initial stack. */

/* Note that we've begun the initial application stack. 
   Call once prior to invoking the application code. 
   We also set the current LCBN to the init_stack_lcbn so that all LCBNs
   resulting from the initial stack are descended appropriately. */
void uv__mark_init_stack_begin (void)
{
  /* JD: TODO: Call to scheduler. */
  lcbn_t *init_stack_lcbn = NULL;

  /* Call at most once. */
  static int here = 0;
  assert(!here);
  here = 1;

  initialize_record_and_replay();

  init_stack_lcbn = get_init_stack_lcbn();
  assert(lcbn_looks_valid(init_stack_lcbn));

  init_stack_lcbn->executing_thread = pthread_self_internal();
  lcbn_current_set(init_stack_lcbn);
  lcbn_mark_begin(init_stack_lcbn);

  ENTRY_EXIT_LOG((LOG_MAIN, 9, "uv__mark_init_stack_begin: initial stack has begun\n"));
}

/* Note that we've finished the initial application stack. 
   Call once after the initial stack is complete. 
   Pair with uv__mark_init_stack_begin. */
void uv__mark_init_stack_end (void)
{
  /* JD: TODO: Call to scheduler.
   * NB Cannot check for divergence yet because the first marker node and the EXIT node are added to the initial stack node later. 
     Instead, we check for divergence of the initial stack when we generate the EXIT event. 
   */
  lcbn_t *init_stack_lcbn = NULL;

  assert(uv__init_stack_active()); 

  init_stack_lcbn = get_init_stack_lcbn();
  assert(lcbn_looks_valid(init_stack_lcbn));
  lcbn_mark_end(init_stack_lcbn);
  lcbn_current_set(NULL);

  ENTRY_EXIT_LOG((LOG_MAIN, 9, "uv__mark_init_stack_end: INITIAL_STACK has ended\n"));
}

/* Returns non-zero if we're in the application's initial stack, else 0. */
int uv__init_stack_active (void)
{
  return lcbn_is_active(get_init_stack_lcbn());
}

int uv__exit_active (void)
{
  return lcbn_is_active(get_exit_lcbn());
}

/* Note that we've entered Node's "main" uv_run loop. */
void uv__mark_main_uv_run_begin (void)
{
  ENTRY_EXIT_LOG((LOG_MAIN, 9, "uv__mark_main_uv_run_begin: begin\n"));
  emit_marker_event(MARKER_UV_RUN_BEGIN);
  ENTRY_EXIT_LOG((LOG_MAIN, 9, "uv__mark_main_uv_run_begin: returning\n"));
}

/* Note that we've exited Node's "main" uv_run loop. */
void uv__mark_main_uv_run_end (void)
{
  ENTRY_EXIT_LOG((LOG_MAIN, 9, "uv__mark_main_uv_run_end: begin\n"));
  emit_marker_event(MARKER_UV_RUN_END);
  ENTRY_EXIT_LOG((LOG_MAIN, 9, "uv__mark_main_uv_run_end: returning\n"));
}

void uv__mark_exit_begin (void)
{
  lcbn_t *active_lcbn = NULL, *exit_lcbn = NULL;

  /* Can legally be called more than once, just ignore subsequent calls.
       process.on('exit', function(){ process.exit(1); }); */
  static int here = 0;
  if (here)
    return;
  here = 1;

  exit_lcbn = get_exit_lcbn();
  assert(lcbn_looks_valid(exit_lcbn));

  exit_lcbn->executing_thread = pthread_self_internal();
  active_lcbn = lcbn_current_get();

  /* exit lcbn is the *synchronous* child of the caller (if any), or the child of the initial stack. 
     NB Its immediate children are invoked *synchronously*, just like INITIAL_STACK's children.
        We treat it as a separate event for visibility purposes. */
  if (active_lcbn)
  {
    /* TODO Race with threadpool (or what if it's emitted by the threadpool?)? */
    lcbn_add_child(active_lcbn, exit_lcbn);
    lcbn_mark_end(active_lcbn);
  }
  else
    lcbn_add_child(get_init_stack_lcbn(), exit_lcbn);

  lcbn_current_set(exit_lcbn);
  lcbn_mark_begin(exit_lcbn);

  ENTRY_EXIT_LOG((LOG_MAIN, 9, "uv__mark_exit_begin: inital stack has begun\n"));
}

/* Note that we've finished the initial application stack. 
   Call once after the initial stack is complete. 
   Pair with uv__mark_exit_begin. */
void uv__mark_exit_end (void)
{
  lcbn_t *exit_lcbn = NULL;

  assert(uv__exit_active()); 

  exit_lcbn = get_exit_lcbn();
  assert(lcbn_looks_valid(exit_lcbn));
  lcbn_mark_end(exit_lcbn);
  lcbn_current_set(NULL);

  if (scheduler_get_scheduler_mode() == SCHEDULER_MODE_REPLAY)
  {
    /* JD: TODO: scheduler calls. */
    /*
    scheduler_check_lcbn_for_divergence(get_init_stack_lcbn());
    scheduler_check_lcbn_for_divergence(exit_lcbn);
    */
  }

  ENTRY_EXIT_LOG((LOG_MAIN, 9, "uv__mark_exit_end: EXIT has ended\n"));
}

/* Tracking whether or not we're in uv__run_pending. */
int uv__run_pending_active = 0;
any_func uv__run_pending_active_cb = NULL;
/* Note that we've entered libuv's uv__run_pending loop. */
void uv__mark_uv__run_pending_begin (void)
{
  assert(!uv__run_pending_active);
  uv__run_pending_active = 1;
  uv__uv__run_pending_set_active_cb(NULL);
  ENTRY_EXIT_LOG((LOG_MAIN, 9, "uv__mark_uv__run_pending_begin: uv__run_pending has begun\n"));
}

/* Note that we've finished the libuv uv__run_pending loop.
   Pair with uv__mark_uv__run_pending_begin. */
void uv__mark_uv__run_pending_end (void)
{
  assert(uv__run_pending_active);
  uv__run_pending_active = 0;
  uv__uv__run_pending_set_active_cb(NULL);
  ENTRY_EXIT_LOG((LOG_MAIN, 9, "uv__mark_uv__run_pending_end: uv__run_pending has ended\n"));
}

/* Returns non-zero if we're in uv__run_pending, else 0. */
int uv__uv__run_pending_active (void)
{
  return uv__run_pending_active;
}

/* Set and get the active CB in uv__run_pending. */
any_func uv__uv__run_pending_get_active_cb (void)
{
  return uv__run_pending_active_cb;
}

void uv__uv__run_pending_set_active_cb (any_func cb)
{
  uv__run_pending_active_cb = cb;
}

/* Associate LCBN with CALLBACK_TYPE in CB_TYPE_TO_LCBN. */
void lcbn_register (struct map *cb_type_to_lcbn, enum callback_type cb_type, lcbn_t *lcbn)
{
  assert(cb_type_to_lcbn);
  assert(lcbn);
  map_insert(cb_type_to_lcbn, cb_type, lcbn);
}

/* Return the LCBN stored in CB_TYPE_TO_LCBN for CALLBACK_TYPE. */
lcbn_t * lcbn_get (struct map *cb_type_to_lcbn, enum callback_type cb_type)
{
  int found = 0;
  lcbn_t *lcbn = NULL;

  assert(cb_type_to_lcbn);

  lcbn = (lcbn_t *) map_lookup(cb_type_to_lcbn, cb_type, &found);
  assert(found && lcbn_looks_valid(lcbn));
  return lcbn;
}

/* APIs that let us build lineage trees: nodes can identify their parents. 
   current_callback_node_{set,get} are thread safe and maintain per-thread mappings. */

/* Sets the current callback node for this thread to LCBN.
   NULL signifies the end of a callback tree. */
void lcbn_current_set (lcbn_t *lcbn)
{
  map_lock(tid_to_current_lcbn);
  /* TODO My maps are thread safe. */
  map_insert(tid_to_current_lcbn, (int) pthread_self(), (void *) lcbn);
  if (lcbn == NULL)
    mylog(LOG_MAIN, 5, "lcbn_current_set: Next callback will be a root\n");
  else
    mylog(LOG_MAIN, 5, "lcbn_current_set: Current LCBN is %p (type %s)\n", lcbn, callback_type_to_string(lcbn->cb_type));
  map_unlock(tid_to_current_lcbn);
}

/* Retrieves the current callback node for this thread, or NULL if no such node. 
   This function is thread safe. */
lcbn_t * lcbn_current_get (void)
{
  int found = 0;
  lcbn_t *ret = NULL;

  /* My maps are thread safe. */
  ret = (lcbn_t *) map_lookup(tid_to_current_lcbn, (int) pthread_self(), &found);

  if (!found)
    assert(!ret);
  return ret;
}

/* Returns an internal (small) thread identifier. */
int pthread_self_internal (void)
{
  int found, pthread_id, internal_id;
  
  pthread_id = (int) pthread_self();

  map_lock(pthread_to_tid);
  internal_id = (int) (long) map_lookup(pthread_to_tid, pthread_id, &found);
  if (!found)
  {
    internal_id = map_size(pthread_to_tid);
    map_insert(pthread_to_tid, pthread_id, (void *) (long) internal_id);
  }
  map_unlock(pthread_to_tid);

  return internal_id;
}

/* Obtain the next available exec_id.
 * Not thread safe, so caller should ensure mutex somehow.
 */
static unsigned lcbn_next_exec_id (void)
{
  lcbn_global_exec_counter++;
  return lcbn_global_exec_counter - 1;
}

/* Not thread safe. */
static unsigned lcbn_next_reg_id (void)
{
  lcbn_global_reg_counter++;
  return lcbn_global_reg_counter - 1;
}

void invoke_callback_wrap (any_func cb, enum callback_type type, ...)
{
  int i, nargs;
  va_list ap;
  callback_info_t *cbi = NULL;

  assert(UV_ALLOC_CB <= type && type <= UV__WORK_DONE);
  nargs = callback_type_to_nargs[type];

  /* Prep a CBI with args. */
  cbi = (callback_info_t *) uv__malloc(sizeof *cbi);
  assert(cbi);
  memset(cbi, 0, sizeof(*cbi));                   
  cbi->type = type;                           
  cbi->cb = cb;                                    

  va_start(ap, type);
  for (i = 0; i < nargs; i++)
    cbi->args[i] = va_arg(ap, long);
  va_end(ap);

  invoke_callback(cbi);
}

/* Go through the motions of executing LCBN
 *  - mark it begin/end'ed
 *  - acquire an execution ID
 *  - advance the scheduler 
 * Since it's internal, it doesn't actually have an associated CB.
 *
 * TODO This should really be going through invoke_callback to avoid code repetition.
 *      Set the CB associated with the LCBN to a dummy function.
 */
static void execute_internal_lcbn (lcbn_t *lcbn)
{
  assert(lcbn);
  assert(lcbn_internal(lcbn));

  ENTRY_EXIT_LOG((LOG_MAIN, 9, "execute_internal_lcbn: begin: lcbn %p\n", lcbn));

  /* Get an exec id, fake an execution, tell any waiters we're done. */
  mylog(LOG_MAIN, 7, "execute_internal_lcbn: Invoking lcbn %p (type %s) exec_id %i\n", lcbn, callback_type_to_string(lcbn->cb_type), lcbn->global_exec_id);
  lcbn_mark_begin(lcbn);
  lcbn->global_exec_id = lcbn_next_exec_id();
  lcbn_mark_end(lcbn);
  mylog(LOG_MAIN, 5, "execute_internal_lcbn: Done with lcbn %p (type %s) exec_id %i\n", lcbn, callback_type_to_string(lcbn->cb_type), lcbn->global_exec_id);

  ENTRY_EXIT_LOG((LOG_MAIN, 9, "execute_internal_lcbn: returning\n"));
}

/* Marker events are in a registration chain. 
   The first uv_run is a child of the initial stack.
   Each subsequent marker event is a child of the previous one. */
lcbn_t *prev_marker_event = NULL;
void emit_marker_event (enum callback_type cbt)
{
  lcbn_t *lcbn = lcbn_create(NULL, NULL, cbt), *parent = NULL;

  ENTRY_EXIT_LOG((LOG_MAIN, 9, "emit_marker_event: begin: cbt %s\n", callback_type_to_string(cbt)));
  assert(is_marker_event(cbt));

  lcbn_mark_non_user(lcbn);

  /* Register as child of the parent. */
  if (prev_marker_event)
    parent = prev_marker_event;
  else
  {
    assert(cbt == MARKER_UV_RUN_BEGIN);
    parent = get_init_stack_lcbn();
  }
  lcbn_add_child(parent, lcbn);
  lcbn->global_reg_id = lcbn_next_reg_id();
  scheduler_register_lcbn(lcbn);

  if (scheduler_get_scheduler_mode() == SCHEDULER_MODE_REPLAY)
  {
    enum callback_type next_cb_type = scheduler_next_lcbn_type();
    mylog(LOG_MAIN, 5, "emit_marker_event: cbt %s next_cb_type %s\n", callback_type_to_string(cbt), callback_type_to_string(next_cb_type));
    assert(is_threadpool_cb(next_cb_type) || next_cb_type == cbt);
    /* JD: TODO: Call to scheduler. */
  }

  execute_internal_lcbn(lcbn);

  /* JD: TODO: Call to scheduler. */

  prev_marker_event = lcbn;
  ENTRY_EXIT_LOG((LOG_MAIN, 9, "emit_marker_event: returning\n"));
}
