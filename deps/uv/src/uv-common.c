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

#include <stdio.h>
#include <assert.h>
#include <stdarg.h>
#include <stddef.h> /* NULL */
#include <stdlib.h> /* malloc */
#include <string.h> /* memset */
#include <assert.h>

#include <sys/types.h> /* getpid */
#include <unistd.h> /* getpid */

#include <signal.h> /* For signal handling. */

#if defined(_WIN32)
# include <malloc.h> /* malloc */
#else
# include <net/if.h> /* if_nametoindex */
#endif


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
  return uv__allocator.local_malloc(size);
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
    return uv__udp_recv_start(handle, alloc_cb, recv_cb);
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

  QUEUE_FOREACH(q, &loop->handle_queue) {
    h = QUEUE_DATA(q, uv_handle_t, handle_queue);
    if (h->flags & UV__HANDLE_INTERNAL) continue;
#ifdef UNIFIED_CALLBACK
    INVOKE_CALLBACK_2 (UV_WALK_CB, walk_cb, h, arg); 
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

static int generation = 0;

void incr_generation ()
{
  generation++;
}

int get_generation ()
{
  return generation;
}

void mylog (const char *format, ...)
{
  pid_t my_pid;
  va_list args;
  char indents[512];
  int i;
  int generation;
  time_t now;
  char *now_s;

  now = time (NULL);
  now_s = ctime (&now);
  now_s [strlen (now_s) - 1] = '\0'; /* Remove the trailing newline. */
#if 0
  generation = get_generation ();
  indents[0] = '\0';
  for (i = 0; i < generation; i++)
    strncat (indents, "  ", 512);

  my_pid = getpid ();
  printf ("%s %s gen %i process %i: ", indents, now_s, generation, my_pid);
#else
  my_pid = getpid ();
  printf ("%s process %i: ", now_s, my_pid);
#endif

  va_start(args, format);
  vprintf(format, args);
  va_end(args);

  fflush (NULL);
}

/* Unified callback queue. */
static struct list root_list;
static struct list global_order_list;

static struct callback_node *current_callback_node = NULL;
void current_callback_node_set (struct callback_node *cbn)
{
  current_callback_node = cbn;
  if (cbn == NULL)
    mylog ("current_callback_node_set: Next callback will be a root\n");
}

struct callback_node *current_callback_node_get (void)
{
  return current_callback_node;
}

void invoke_callback (struct callback_info *cbi)
{
  struct callback_node *orig_cbn, *new_cbn;

  assert (cbi != NULL);  

  /* Potentially racey but very unlikely. */
  if (!list_looks_valid (&global_order_list))
  {
    list_init (&global_order_list);
    signal(SIGUSR1, dump_callback_global_order_sighandler);
    signal(SIGUSR2, dump_callback_trees_sighandler);
  }
  
  new_cbn = malloc (sizeof *new_cbn);
  assert (new_cbn != NULL);
  new_cbn->info = cbi;

  /* If CBI is an asynchronous type, extract the orig_cbn at time
     of registration (if any). */
  int async_cb = (cbi->type == UV__WORK_WORK || cbi->type == UV__WORK_DONE || cbi->type == UV_TIMER_CB);
  int sync_cb = !async_cb;

  if (sync_cb)
    /* CBI is synchronous, so it's either a root or a child of an active callback. */
    orig_cbn = current_callback_node_get();
  else
  {
    switch (cbi->type)
    {
      case UV__WORK_WORK:
      case UV__WORK_DONE:
        orig_cbn = ((struct uv__work *) cbi->args[0])->parent;
        assert (orig_cbn != NULL);
        break;
      case UV_TIMER_CB:
        /* There may not be an orig_cbn if the callback was registered by
           the initial stack. For example, the longPoll in simple-node.js-mud. */
        orig_cbn = ((uv_timer_t *) cbi->args[0])->parent;
        break;
      default:
        NOT_REACHED;
    }
  }

  /* Initialize NEW_CBN. */
  if (orig_cbn)
  {
    assert (orig_cbn != NULL);
    new_cbn->level = orig_cbn->level + 1;
    new_cbn->parent = orig_cbn;
  }
  else
  {
    new_cbn->level = 0;
    new_cbn->parent = NULL;
  }

  if (new_cbn->parent)
  {
    list_lock (&new_cbn->parent->children);
    list_push_back (&new_cbn->parent->children, &new_cbn->child_elem); 
    list_unlock (&new_cbn->parent->children);
  }
  else
  {
    if (!list_looks_valid (&root_list))
    {
      /* This initialization can race if being added by a UV__WORK_WORK item, though this seems impossible. */
      assert (cbi->type != UV__WORK_WORK && cbi->type != UV__WORK_DONE);
      list_init (&root_list);
    }

    list_lock (&root_list);
    list_push_back (&root_list, &new_cbn->root_elem); 
    list_unlock (&root_list);
  }

  new_cbn->start = get_relative_time ();
  new_cbn->duration = -1;
  new_cbn->active = 1;
  list_init (&new_cbn->children);

  list_lock (&global_order_list);
  list_push_back (&global_order_list, &new_cbn->global_order_elem);
  new_cbn->id = list_size (&global_order_list);
  list_unlock (&global_order_list);

  if (new_cbn->parent && new_cbn->parent->info->type == UV__WORK_WORK)
    mylog ("invoke_callback: Child of a UV__WORK_WORK item\n");
  if (!new_cbn->parent && cbi->type == UV__WORK_WORK)
    mylog ("invoke_callback: root node is a UV__WORK_WORK item??\n");

  mylog ("invoke_callback: invoking callback_node %p cbi %p type %s cb %p level %i parent %p\n",
    new_cbn, (void *) cbi, callback_type_to_string(cbi->type), cbi->cb, new_cbn->level, new_cbn->parent);
  assert ((new_cbn->level == 0 && new_cbn->parent == NULL)
       || (0 < new_cbn->level && new_cbn->parent != NULL));

  /* TODO If UV__WORK_WORK, can be called in parallel. Matching problem. Bad news bears. Perhaps track per-tid current_callback_node? */
  if (cbi->type != UV__WORK_WORK)
    current_callback_node_set (new_cbn);

  uv_handle_t *uvht = NULL;
  int fileno_rc;
  uv_os_fd_t fd = -1;
  char *handle_type;
  switch (cbi->type)
  {
    /* include/uv.h */
    case UV_ALLOC_CB:
      uvht = (uv_handle_t *) cbi->args[0];
      fileno_rc = uv_fileno (uvht, &fd);
      if (fileno_rc != 0)
        fd = -1;
      handle_type = "uv_handle_t"; 
      cbi->cb ((uv_handle_t *) cbi->args[0], (size_t) cbi->args[1], (uv_buf_t *) cbi->args[2]); /* uv_handle_t */
      break;
    case UV_READ_CB:
      uvht = (uv_stream_t *) cbi->args[0];
      fileno_rc = uv_fileno (uvht, &fd);
      if (fileno_rc != 0)
        fd = -1;
      handle_type = "uv_stream_t"; 
      cbi->cb ((uv_stream_t *) cbi->args[0], (ssize_t) cbi->args[1], (const uv_buf_t *) cbi->args[2]); /* uv_handle_t */
      break;
    case UV_WRITE_CB:
      cbi->cb ((uv_write_t *) cbi->args[0], (int) cbi->args[1]); /* uv_req_t, has pointer to two uv_stream_t's (uv_handle_t)  */
      break;
    case UV_CONNECT_CB:
      cbi->cb ((uv_connect_t *) cbi->args[0], (int) cbi->args[1]); /* uv_req_t, has pointer to uv_stream_t (uv_handle_t) */
      break;
    case UV_SHUTDOWN_CB:
      cbi->cb ((uv_shutdown_t *) cbi->args[0], (int) cbi->args[1]); /* uv_req_t, has pointer to uv_stream_t (uv_handle_t) */
      break;
    case UV_CONNECTION_CB:
      uvht = (uv_stream_t *) cbi->args[0];
      fileno_rc = uv_fileno (uvht, &fd);
      if (fileno_rc != 0)
        fd = -1;
      handle_type = "uv_stream_t"; 
      cbi->cb ((uv_stream_t *) cbi->args[0], (int) cbi->args[1]); /* uv_handle_t */
      break;
    case UV_CLOSE_CB:
      uvht = (uv_handle_t *) cbi->args[0];
      fileno_rc = uv_fileno (uvht, &fd);
      if (fileno_rc != 0)
        fd = -1;
      handle_type = "uv_handle_t"; 
      cbi->cb ((uv_handle_t *) cbi->args[0]); /* uv_handle_t */
      break;
    case UV_POLL_CB:
      uvht = (uv_poll_t *) cbi->args[0];
      fileno_rc = uv_fileno (uvht, &fd);
      if (fileno_rc != 0)
        fd = -1;
      handle_type = "uv_poll_t"; 
      cbi->cb ((uv_poll_t *) cbi->args[0], (int) cbi->args[1], (int) cbi->args[2]); /* uv_handle_t */
      break;
    case UV_TIMER_CB:
      uvht = (uv_timer_t *) cbi->args[0];
      fileno_rc = uv_fileno (uvht, &fd);
      if (fileno_rc != 0)
        fd = -1;
      handle_type = "uv_timer_t"; 
      cbi->cb ((uv_timer_t *) cbi->args[0]); /* uv_handle_t */
      break;
    case UV_ASYNC_CB:
      uvht = (uv_async_t *) cbi->args[0];
      fileno_rc = uv_fileno (uvht, &fd);
      if (fileno_rc != 0)
        fd = -1;
      handle_type = "uv_async_t"; 
      cbi->cb ((uv_async_t *) cbi->args[0]); /* uv_handle_t */
      break;
    case UV_PREPARE_CB:
      uvht = (uv_prepare_t *) cbi->args[0];
      fileno_rc = uv_fileno (uvht, &fd);
      if (fileno_rc != 0)
        fd = -1;
      handle_type = "uv_prepare_t"; 
      cbi->cb ((uv_prepare_t *) cbi->args[0]); /* uv_handle_t */
      break;
    case UV_CHECK_CB:
      uvht = (uv_check_t *) cbi->args[0];
      fileno_rc = uv_fileno (uvht, &fd);
      if (fileno_rc != 0)
        fd = -1;
      handle_type = "uv_check_t"; 
      cbi->cb ((uv_check_t *) cbi->args[0]); /* uv_handle_t */
      break;
    case UV_IDLE_CB:
      uvht = (uv_idle_t *) cbi->args[0];
      fileno_rc = uv_fileno (uvht, &fd);
      if (fileno_rc != 0)
        fd = -1;
      handle_type = "uv_idle_t"; 
      cbi->cb ((uv_idle_t *) cbi->args[0]); /* uv_handle_t */
      break;
    case UV_EXIT_CB:
      uvht = (uv_process_t *) cbi->args[0];
      fileno_rc = uv_fileno (uvht, &fd);
      if (fileno_rc != 0)
        fd = -1;
      handle_type = "uv_process_t"; 
      cbi->cb ((uv_process_t *) cbi->args[0], (int64_t) cbi->args[1], (int) cbi->args[2]); /* uv_handle_t */
      break;
    case UV_WALK_CB:
      uvht = (uv_handle_t *) cbi->args[0];
      fileno_rc = uv_fileno (uvht, &fd);
      if (fileno_rc != 0)
        fd = -1;
      handle_type = "uv_handle_t"; 
      cbi->cb ((uv_handle_t *) cbi->args[0], (void *) cbi->args[1]); /* uv_handle_t */
      break;
    case UV_FS_CB:
      cbi->cb ((uv_fs_t *) cbi->args[0]); /* uv_req_t, no information about handle or parent */
      break;
    case UV_WORK_CB:
      cbi->cb ((uv_work_t *) cbi->args[0]); /* uv_req_t, no information about handle or parent */
      break;
    case UV_AFTER_WORK_CB:
      cbi->cb ((uv_work_t *) cbi->args[0], (int) cbi->args[1]); /* uv_req_t, no information about handle or parent */
      break;
    case UV_GETADDRINFO_CB:
      cbi->cb ((uv_getaddrinfo_t *) cbi->args[0], (int) cbi->args[1], (struct addrinfo *) cbi->args[2]); /* uv_req_t, no information about handle or parent */
      break;
    case UV_GETNAMEINFO_CB:
      cbi->cb ((uv_getnameinfo_t *) cbi->args[0], (int) cbi->args[1], (const char *) cbi->args[2], (const char *) cbi->args[3]); /* uv_req_t, no information about handle or parent */
      break;
    case UV_FS_EVENT_CB:
      uvht = (uv_fs_event_t *) cbi->args[0];
      fileno_rc = uv_fileno (uvht, &fd);
      if (fileno_rc != 0)
        fd = -1;
      handle_type = "uv_fs_event_t"; 
      cbi->cb ((uv_fs_event_t *) cbi->args[0], (const char *) cbi->args[1], (int) cbi->args[2], (int) cbi->args[3]); /* uv_handle_t */
      break;
    case UV_FS_POLL_CB:
      uvht = (uv_fs_poll_t *) cbi->args[0];
      fileno_rc = uv_fileno (uvht, &fd);
      if (fileno_rc != 0)
        fd = -1;
      handle_type = "uv_fs_poll_t"; 
      cbi->cb ((uv_fs_poll_t *) cbi->args[0], (int) cbi->args[1], (const uv_stat_t *) cbi->args[2], (const uv_stat_t *) cbi->args[3]); /* uv_handle_t */
      break;
    case UV_SIGNAL_CB:
      uvht = (uv_signal_t *) cbi->args[0];
      fileno_rc = uv_fileno (uvht, &fd);
      if (fileno_rc != 0)
        fd = -1;
      handle_type = "uv_signal_t"; 
      cbi->cb ((uv_signal_t *) cbi->args[0], (int) cbi->args[1]); /* uv_handle_t */
      break;
    case UV_UDP_SEND_CB:
      cbi->cb ((uv_udp_send_t *) cbi->args[0], (int) cbi->args[1]); /* uv_req_t, has pointer to uv_udp_t (uv_handle_t) */
      break;
    case UV_UDP_RECV_CB:
      uvht = (uv_udp_t *) cbi->args[0];
      fileno_rc = uv_fileno (uvht, &fd);
      if (fileno_rc != 0)
        fd = -1;
      handle_type = "uv_udp_t"; 
      cbi->cb ((uv_udp_t *) cbi->args[0], (ssize_t) cbi->args[1], (const uv_buf_t *) cbi->args[2], (const struct sockaddr *) cbi->args[3], (unsigned) cbi->args[4]); /* uv_handle_t */
      break;
    case UV_THREAD_CB:
      cbi->cb ((void *) cbi->args[0]); /* ?? */
      break;

    /* include/uv-unix.h */
    case UV__IO_CB:
      cbi->cb ((struct uv_loop_s *) cbi->args[0], (struct uv__io_s *) cbi->args[1], (unsigned int) cbi->args[2]); /* Global loop */
      break;
    case UV__ASYNC_CB:
      cbi->cb ((struct uv_loop_s *) cbi->args[0], (struct uv__async *) cbi->args[1], (unsigned int) cbi->args[2]); /* Global loop */
      break;

    /* include/uv-threadpool.h */
    case UV__WORK_WORK:
      cbi->cb ((struct uv__work *) cbi->args[0]); /* Knows parent. */
      break;
    case UV__WORK_DONE:
      cbi->cb ((struct uv__work *) cbi->args[0], (int) cbi->args[1]); /* Knows parent. */
      break;

    default:
      mylog ("invoke_callback: ERROR, unsupported type\n");
      assert (0 == 1);
  }

  if (uvht && fileno_rc == 0)
    mylog ("handle type <%s> fd %i\n", handle_type, fd);

  mylog ("invoke_callback: Done with callback_node %p cbi %p\n",
    new_cbn, new_cbn->info); 
  new_cbn->active = 0;
  new_cbn->duration = get_relative_time () - new_cbn->start;
  new_cbn->client_id = fd;

  if (sync_cb)
    current_callback_node_set (orig_cbn);
  else
  {
    /* Async CB. If we're being called synchronously, we can safely say there is no current CB. */ 
    if (cbi->type != UV__WORK_WORK)
      current_callback_node_set(NULL);
  }

#if 0
  /* DEBUGGING */
  dump_callback_global_order ();
  dump_callback_trees (0);
  printf ("\n\n\n\n\n");
#endif
}

static char *callback_type_strings[] = {
  "UV_ALLOC_CB", "UV_READ_CB", "UV_WRITE_CB", "UV_CONNECT_CB", "UV_SHUTDOWN_CB", 
  "UV_CONNECTION_CB", "UV_CLOSE_CB", "UV_POLL_CB", "UV_TIMER_CB", "UV_ASYNC_CB", 
  "UV_PREPARE_CB", "UV_CHECK_CB", "UV_IDLE_CB", "UV_EXIT_CB", "UV_WALK_CB", 
  "UV_FS_CB", "UV_WORK_CB", "UV_AFTER_WORK_CB", "UV_GETADDRINFO_CB", "UV_GETNAMEINFO_CB", 
  "UV_FS_EVENT_CB", "UV_FS_POLL_CB", "UV_SIGNAL_CB", "UV_UDP_SEND_CB", "UV_UDP_RECV_CB", 
  "UV_THREAD_CB", 

  /* include/uv-unix.h */
  "UV__IO_CB", "UV__ASYNC_CB", 

  /* include/uv-threadpool.h */
  "UV__WORK_WORK", "UV__WORK_DONE" 
};


char *callback_type_to_string (enum callback_type type)
{
  assert (CALLBACK_TYPE_MIN <= type && type < CALLBACK_TYPE_MAX);
  return callback_type_strings[type];
}

/* Prints callback node CBN using printf. */
static void dump_callback_node (struct callback_node *cbn, char *prefix)
{
  assert (cbn != NULL);
  printf ("%s cbn %p info %p type %s level %i parent %p active %i n_children %i\n", 
    prefix, cbn, cbn->info, callback_type_to_string (cbn->info->type), cbn->level, cbn->parent, cbn->active, list_size (&cbn->children));
}

/* Dumps all callbacks in the order in which they were called. 
   Locks and unlocks GLOBAL_ORDER_LIST. */
void dump_callback_global_order (void)
{
  struct list_elem *e;
  int cbn_num;
  char prefix[64];

  list_lock (&global_order_list);

  printf ("Dumping all %i callbacks we have invoked, in order\n", list_size (&global_order_list));
  cbn_num = 0;
  for (e = list_begin (&global_order_list); e != list_end (&global_order_list); e = list_next (e))
  {
    struct callback_node *cbn = list_entry (e, struct callback_node, global_order_elem);
    snprintf (prefix, 64, "Callback %i: ", cbn_num);
    dump_callback_node (cbn, prefix);
    cbn_num++;
  }
  fflush (NULL);

  list_unlock (&global_order_list);
}

static void dump_callback_tree (struct callback_node *cbn)
{
  int child_num;
  struct list_elem *e;
  struct callback_node *node;
  assert (cbn != NULL);

  dump_callback_node (cbn, "");
  child_num = 0;
  for (e = list_begin (&cbn->children); e != list_end (&cbn->children); e = list_next (e))
  {
    node = list_entry (e, struct callback_node, child_elem);
    printf ("Parent cbn %p child %i: %p\n", cbn, child_num, node);
    dump_callback_tree (node);
    child_num++;
  }
}

/* Dumps each callback tree. */
void dump_callback_trees (void)
{
  struct list_elem *e;
  int tree_num;

  printf ("Dumping all %i callback trees\n", list_size (&root_list));

  tree_num = 0;
  for (e = list_begin (&root_list); e != list_end (&root_list); e = list_next (e))
  {
    printf ("Tree %i:\n", tree_num);
    dump_callback_tree (list_entry (e, struct callback_node, root_elem));
    ++tree_num;
  }
  fflush (NULL);
}

void dump_callback_global_order_sighandler (int signum)
{
  printf ("Got signal %i. Dumping callback global order\n", signum);
  dump_callback_global_order ();
}

void dump_callback_trees_sighandler (int signum)
{
  printf ("Got signal %i. Dumping callback trees\n", signum);
  dump_callback_trees ();
}

/* Returns time relative to the time at which the first CB was invoked. */
static time_t init_time = 0;
time_t get_relative_time (void)
{
  if (init_time == 0)
    init_time = time(NULL);
  return time(NULL) - init_time;
}
