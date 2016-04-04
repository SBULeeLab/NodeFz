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
    INVOKE_CALLBACK_2 (UV_WALK_CB, (any_func) walk_cb, (long) h, (long) arg); 
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

/* Static functions added for unified callback. */
/* For convenience with addr_getnameinfo. */
struct uv_nameinfo
{
  char host[INET6_ADDRSTRLEN];
  char service[64];
};

/* Callback nodes. */
static callback_node_t * cbn_create (void);
static void cbn_start (callback_node_t *cbn);
static void cbn_stop (callback_node_t *cbn);
static void cbn_determine_executing_thread (callback_node_t *cbn);
static int cbn_have_peer_info (const callback_node_t *cbn);

#if 0
static callback_node_t * cbn_climb_to_type (callback_node_t *cbn, enum callback_type type);
static int cbn_is_descendant (callback_node_t *cbn, callback_node_t *maybe_ancestor);
static void cbn_color_tree (callback_node_t *cbn, int client_id);
#endif

static int cbn_have_peer_info (const callback_node_t *cbn);
static int cbn_is_active_pending_cb (const callback_node_t *cbn);
static int cbn_is_active (const callback_node_t *cbn);
static void cbn_execute_callback (callback_node_t *cbn);

static void cbn_walk (callback_node_t *cbn, enum callback_tree_type tree_type, void (*f)(callback_node_t *, void *), void *aux);

/* Functions for cbn_walk. */
static void cbn_print_logical_parentage_gv (callback_node_t *cbn, void *fdP);
#if 0
static void cbn_update_level (callback_node_t *cbn, void *aux);
#endif

static int cbn_got_client_input (callback_node_t *cbn);
static int cbn_sent_client_output (callback_node_t *cbn);

static void addr_getnameinfo (struct sockaddr_storage *addr, struct uv_nameinfo *nameinfo);

lcbn_t * get_init_stack_lcbn (void);
void lcbn_determine_executing_thread (lcbn_t *lcbn);

/* Misc. */
static int is_zeros (void *buffer, size_t size);
static void print_buf (void *buffer, size_t size);
static void timespec_sub (const struct timespec *stop, const struct timespec *start, struct timespec *res);

/* Global time. */
static void mark_global_start (void);
static void calc_relative_time (struct timespec *start, struct timespec *res);

/* Output. */
static void dump_callback_node_gv (int fd, callback_node_t *cbn);
static void dump_callback_node (int fd, callback_node_t *cbn, char *prefix, int do_indent);
static void dump_callback_tree_gv (int fd, callback_node_t *cbn);

/* NB these are not thread safe. Use a mutex to ensure that exec_id actually matches exec order, and so on. */
static unsigned lcbn_next_exec_id (void);
static unsigned lcbn_next_reg_id (void);

#if 0
static void dump_callback_tree (int fd, callback_node_t *cbn);
#endif

/* Variables for tracking the unified callback queue. */
pthread_mutex_t metadata_lock;
struct list *root_list;
struct list *global_order_list;

unsigned lcbn_global_exec_counter = 0;
unsigned lcbn_global_reg_counter = 0;

pthread_mutex_t invoke_callback_lcbn_lock; /* Recursive. */

struct list *lcbn_global_reg_order_list;

/* We identify unique clients based on the associated 'peer info' (struct sockaddr).
   A map 'peer -> client ID' tells us whether a given peer is an already-known client.
   A map 'client ID -> peer' gives us information about the internal client ID 1, 2, 3, ... . */
/* hash(sockaddr_storage) -> (void *) client ID */
static struct map *peer_info_to_id;
/* client ID -> (void *) &sockaddr_storage */
static struct map *id_to_peer_info;

/* Maps CB (address) -> struct callback_origin *.
   As V8 will perform JIT compilation and GC, the address of a CB 
   is only valid until it is "used up". 
   If uv__register_callback is called with a CB already in the map,
   we discard the previous entry; we will have already recorded the
   origin in the appropriate CBN as needed. 

   Used by uv__register_callback and uv__callback_origin. */
struct map *callback_to_origin_map;

/* Maps pthread_t to struct callback_node *, accommodating callbacks
     being executed by the threadpool and looper threads.
   The entry for a given pthread_t is the callback_node currently being executed
     by that thread.
   If the value is NULL, the next callback by that node will be a root
     (unless the callback is an asynchronous one for which the parent is already known). */
struct map *tid_to_current_cbn;
struct map *tid_to_current_lcbn;

/* Maps pthread_t to internal id, yielding human-readable thread IDs. */
struct map *pthread_to_tid;

/* Printing nodes with colors based on client ID. */
#define RESERVED_IDS 6
enum reserved_id
{
  ID_NODEJS_INTERNAL = -1*RESERVED_IDS, /* Callbacks originating internally -- node. */
  ID_LIBUV_INTERNAL, /* Callbacks originating internally -- libuv. */
  ID_INITIAL_STACK,  /* Callbacks registered during the initial stack. */
  ID_UNKNOWN,        /* Unknown origin -- e.g. close() prior to connection being completed? */
  IO_NETWORK_INPUT,  /* CB received network input. */
  IO_NETWORK_OUTPUT  /* CB delivered network output. */
};

/* graphviz notation: node [style="filled" colorscheme="SVG" color="aliceblue"] */
char *graphviz_colorscheme = "SVG";
char *graphviz_colors[] = { "aliceblue", "antiquewhite", "aqua", "aquamarine", "azure", "beige", "bisque", "black", "blanchedalmond", "blue", "blueviolet", "brown", "burlywood", "cadetblue", "chartreuse", "chocolate", "coral", "cornflowerblue", "cornsilk", "crimson", "cyan", "darkblue", "darkcyan", "darkgoldenrod", "darkgray", "darkgreen", "darkgrey", "darkkhaki", "darkmagenta", "darkolivegreen", "darkorange", "darkorchid", "darkred", "darksalmon", "darkseagreen", "darkslateblue", "darkslategray", "darkslategrey", "darkturquoise", "darkviolet", "deeppink", "deepskyblue", "dimgray", "dimgrey", "dodgerblue", "firebrick", "floralwhite", "forestgreen", "fuchsia", "gainsboro", "ghostwhite", "gold", "goldenrod", "gray", "grey", "green", "greenyellow", "honeydew", "hotpink", "indianred", "indigo", "ivory", "khaki", "lavender", "lavenderblush", "lawngreen", "lemonchiffon", "lightblue", "lightcoral", "lightcyan", "lightgoldenrodyellow", "lightgray", "lightgreen", "lightgrey", "lightpink", "lightsalmon", "lightseagreen", "lightskyblue", "lightslategray", "lightslategrey", "lightsteelblue", "lightyellow", "lime", "limegreen", "linen", "magenta", "maroon", "mediumaquamarine", "mediumblue", "mediumorchid", "mediumpurple", "mediumseagreen", "mediumslateblue", "mediumspringgreen", "mediumturquoise", "mediumvioletred", "midnightblue", "mintcream", "mistyrose", "moccasin", "navajowhite", "navy", "oldlace", "olive", "olivedrab", "orange", "orangered", "orchid", "palegoldenrod", "palegreen", "paleturquoise", "palevioletred", "papayawhip", "peachpuff", "peru", "pink", "plum", "powderblue", "purple", "red", "rosybrown", "royalblue", "saddlebrown", "salmon", "sandybrown", "seagreen", "seashell", "sienna", "silver", "skyblue", "slateblue", "slategray", "slategrey", "snow", "springgreen", "steelblue", "tan", "teal", "thistle", "tomato", "turquoise", "violet", "wheat", "white", "whitesmoke", "yellow", "yellowgreen" }; /* ~150 colors. */

/* APIs for the metadata mutex. */

static void uv__metadata_lock (void)
{
  pthread_mutex_lock(&metadata_lock);
}

static void uv__metadata_unlock (void)
{
  pthread_mutex_unlock(&metadata_lock);
}

static void uv__invoke_callback_lcbn_lock (void)
{
  pthread_mutex_lock(&invoke_callback_lcbn_lock);
}

static void uv__invoke_callback_lcbn_unlock (void)
{
  pthread_mutex_unlock(&invoke_callback_lcbn_lock);
}

static void uv__invoke_callback_lcbn_cond_wait (pthread_cond_t *cond)
{
  assert(cond);
  assert(pthread_cond_wait(cond, &invoke_callback_lcbn_lock) == 0);
}

static void uv__invoke_callback_lcbn_cond_broadcast (pthread_cond_t *cond)
{
  assert(cond);
  assert(pthread_cond_broadcast(cond) == 0);
}

/* Callback node APIs. */

#if 0
/* Returns 1 if CBN is a root node for a tree of TYPE, else 0. */
static int cbn_is_root (const callback_node_t *cbn, enum callback_tree_type type)
{
  int is_root;
  assert(cbn != NULL);

  switch (type)
  {
    case CALLBACK_TREE_PHYSICAL:
      is_root = (cbn->physical_parent == NULL);
      break;
    case CALLBACK_TREE_LOGICAL:
      is_root = (cbn->logical_parent == NULL);
      break;
    default:
      NOT_REACHED;
  }

  return is_root;
}
#endif

/* Returns 1 if we know the peer info of CBN already, else 0. */
static int cbn_have_peer_info (const callback_node_t *cbn)
{
  assert(cbn != NULL);
  assert(cbn->peer_info != NULL);
  return !is_zeros(cbn->peer_info, sizeof *cbn->peer_info);
}

/* Mark CBN as active and update its start and relative_start fields. */
static void cbn_start (callback_node_t *cbn)
{
  assert(cbn != NULL);
  assert(!cbn->active);

  cbn->active = 1;

  assert(clock_gettime(CLOCK_MONOTONIC, &cbn->start) == 0);
  calc_relative_time(&cbn->start, &cbn->relative_start);
}

/* Mark CBN as inactive and set its stop and duration fields. */
static void cbn_stop (callback_node_t *cbn)
{
  assert(cbn != NULL);
  assert(cbn->active);

  cbn->active = 0;

  assert(clock_gettime (CLOCK_MONOTONIC, &cbn->stop) == 0);
  /* Must end after it began. */
  assert(cbn->start.tv_sec < cbn->stop.tv_sec || cbn->start.tv_nsec <= cbn->stop.tv_nsec);
  timespec_sub(&cbn->stop, &cbn->start, &cbn->duration);
}

#if 0
/* Some asynchronous CBs have relationships that must be specially tracked;
     can't just use current_callback_node_get. 
   This function may modify the parent field of one of the args in CBN->info
     for use by a child in cbn_determine_parentage. 
    Returns non-zero if we changed anything, else 0. */
static int cbn_embed_parentage (callback_node_t *cbn)
{
  callback_info_t *info;
  uv_handle_t *handle; /* Has parent field. */
  struct uv__work *work; /* Has parent field. */
  int did_something;

  assert(cbn != NULL);
  assert(cbn->info != NULL);

  mylog(LOG_MAIN, 9, "cbn_embed_parentage: BEGIN: cbn %p type %s\n", cbn, callback_type_to_string(cbn->info->type));

  did_something = 1;
  info = cbn->info;
  switch (info->type)
  {
    /* threadpool.c 
       For all threadpool CBs but UV__WORK_DONE, the parentage can be
         determined by the per-tid current_callback_node.
       UV__WORK_WORK and UV_WORK_CB are the two possible logical parents of UV__WORK_DONE.
       Consult jamie_libuv_src_notes for details. */
    case UV__WORK_WORK: /* Internal wrapper. args[0] is a uv__work* */
      work = (struct uv__work *) info->args[0];
      work->logical_parent = cbn;
      break;
    case UV_WORK_CB: /* User CB. args[0] is a uv_work_t* */
      work = &((uv_work_t *) info->args[0])->work_req;
      work->logical_parent = cbn;
      break;

    /* unix/timer.c 
       If the timer repeats, its parent is itself. */
    case UV_TIMER_CB: /* args[0] is a uv_timer_t* (uv_handle_t*) */
    /* unix/loop-watcher.c 
       These handles are always repeating. Their parents are themselves. */
    case UV_PREPARE_CB: /* args[0] is a uv_X_t* (uv_handle_t*) */
    case UV_CHECK_CB:
    case UV_IDLE_CB:
      handle = (uv_handle_t *) info->args[0];
      handle->logical_parent = cbn;
      handle->self_parent = 1; /* For all descendants, the type of the parent is known. TODO ASSUMES never disabled and re-enabled. */
      break;
    default:
      did_something = 0;
  }

  mylog(LOG_MAIN, 9, "cbn_embed_parentage: END: cbn %p type %s did_something %i\n", cbn, callback_type_to_string(cbn->info->type), did_something);
  return did_something;
}
#endif

#if 0
/* Determine the parentage (physical and logical) of CBN. 
   See also cbn_embed_parentage. 

   CBN->info must be initialized. 
   CBN->{physical,logical}_parent must not be known yet, and is set by us if determined.
   The {physical,logical}_children list of {physical,logical}_parent is updated.

   This function also does any inheritance required (e.g. physical_level).

   CBN should not yet be the current_callback_node. */
static void cbn_determine_parentage (callback_node_t *cbn)
{
  uv_handle_t *handle;
  callback_info_t *info;
  callback_node_t *parent_to_inherit_from;
  uv__io_t *iot;

  assert(cbn != NULL);
  assert(cbn->info != NULL);
  assert(cbn->physical_parent == NULL);
  assert(cbn->logical_parent == NULL);
  assert(cbn != current_callback_node_get());

  handle = NULL;

  info = cbn->info;

  cbn->physical_parent = current_callback_node_get();

  /* Obtain the logical parent. */ 
  cbn->logical_parent = NULL;
  if (cbn_is_active_pending_cb(cbn))
  {
    /* Parent, if any, is set at the time of "callback registration" (uv__io_feed). Callback is being invoked from uv__run_pending. */
    iot = (uv__io_t *) info->args[1];
    assert(iot != NULL); 
    cbn->logical_parent = iot->logical_parent;
  }
  else if (cbn_is_async_cb(cbn))
  {
    /* TODO All handles have a logical_parent set at uv__handle_start time. We could make use of it. */
    switch (info->type)
    {
      /* cbn_is_repeated_cb() */
      /* unix/timer.c */
      case UV_TIMER_CB: /* args[0] is a uv_timer_t* (uv_handle_t*) */
      /* unix/loop-watcher.c */
      case UV_PREPARE_CB: /* args[0] is a uv_X_t* (uv_handle_t*) */
      case UV_CHECK_CB:
      case UV_IDLE_CB:
        handle = (uv_handle_t *) info->args[0];
        assert(handle != NULL);
        cbn->logical_parent = handle->logical_parent;
        break;

      /* cbn_is_async_threadpool_cb() */
      case UV__WORK_WORK: /* Internal wrappers. args[0] is a uv__work* */
      case UV__WORK_DONE:
        cbn->logical_parent = ((struct uv__work *) info->args[0])->logical_parent;
        assert(cbn->logical_parent != NULL);
        break;

      default:
        NOT_REACHED;
    }
    assert(cbn->logical_parent != NULL);
  }

  /* Add self to parents' lists of children. */
  if (cbn->physical_parent)
    list_push_back(cbn->physical_parent->physical_children, &cbn->physical_child_elem); 
  else
    list_push_back(root_list, &cbn->root_elem); 

  if (cbn->logical_parent)
    list_push_back(cbn->logical_parent->logical_children, &cbn->logical_child_elem); 

  /* Inherit level from parent. */
  if (cbn->physical_parent)
    cbn->physical_level = cbn->physical_parent->physical_level + 1;
  else
    cbn->physical_level = 0;

  if (cbn->logical_parent)
    cbn->logical_level = cbn->logical_parent->logical_level + 1;

  /* Verify integrity of CBN. */
  assert(cbn->physical_parent || cbn->physical_level == 0);

  /* Inherit client info from parent, if any, favoring the logical parent. */
  if (cbn->logical_parent)
    parent_to_inherit_from = cbn->logical_parent;
  else if (cbn->physical_parent)
    parent_to_inherit_from = cbn->physical_parent;
  else
    parent_to_inherit_from = NULL;

  if (parent_to_inherit_from)
  {
    /* Inherit client ID. */
    cbn->orig_client_id = parent_to_inherit_from->true_client_id; /* If parent started unknown and was colored, we inherit the color. */
    cbn->true_client_id = parent_to_inherit_from->true_client_id;
    cbn->discovered_client_id = 0;

    /* If the parent is from a client, inherit its peer_info. */
    if (0 <= cbn->true_client_id)
    {
      uv__free(cbn->peer_info);
      cbn->peer_info = parent_to_inherit_from->peer_info;
    }
  }

  assert(cbn->peer_info != NULL);
  if (cbn->true_client_id < 0) 
    assert(!cbn_have_peer_info(cbn));

#if 0
  /* TODO Some issue with UV_TIMER_CB having a UV_READ_CB for a parent while having handle->self_parent true. */
  /* Where possible, verify the type of the parent. 
     This helps build confidence about my model of libuv. */
  switch (info->type)
  {
    case UV_WORK_CB:
      assert(parent->info->type == UV__WORK_WORK);
      break;
    case UV__WORK_DONE:
      assert(parent->info->type == UV__WORK_WORK
          || parent->info->type == UV_WORK_CB);
      break;
    case UV_AFTER_WORK_CB:
    case UV_FS_CB:
    case UV_GETADDRINFO_CB:
    case UV_GETNAMEINFO_CB:
      assert(parent->info->type == UV__WORK_DONE);
      break;
    case UV_TIMER_CB:
    case UV_PREPARE_CB:
    case UV_CHECK_CB:
    case UV_IDLE_CB:
      assert(handle != NULL);
      if (handle->self_parent)
        assert(parent->info->type == info->type);
      break;
  }
#endif
}
#endif

#if 0
/* Return non-zero if CBN represents an async CB, zero else.
   async CB: the current_callback_node may not be its logical_parent.
   Examples: timers, uv__run_pending, threadpool.
   CBN->info must be initialized. */
static int cbn_is_async_cb (const callback_node_t *cbn)
{
  assert(cbn != NULL);
  return (cbn_is_repeated_cb(cbn) 
       || cbn_is_async_threadpool_cb(cbn)
       || cbn_is_active_pending_cb(cbn)
       );
}
#endif

#if 0
/* Return non-zero if CBN is a repeated CB, zero else.
   CBN->info must be initialized. */
static int cbn_is_repeated_cb (const callback_node_t *cbn)
{
  enum callback_type type;
  int maybe_repeated;

  assert(cbn != NULL);
  assert(cbn->info != NULL);

  type = cbn->info->type;
  maybe_repeated = (type == UV_TIMER_CB /* May repeat. */
              || type == UV_PREPARE_CB || type == UV_CHECK_CB || type == UV_IDLE_CB /* Repeat every loop. */
              );
  return maybe_repeated;
}
#endif

#if 0
/* Return non-zero if CBN is an asynchronous CB from the threadpool, zero else.
   See cbn_is_async_cb for definition of asynchronous.
   CBN->info must be initialized. */
static int cbn_is_async_threadpool_cb (const callback_node_t *cbn)
{
  enum callback_type type;
  int is_async_threadpool;

  assert(cbn != NULL);
  assert(cbn->info != NULL);

  type = cbn->info->type;
  is_async_threadpool = (type == UV__WORK_WORK || type == UV__WORK_DONE);
  return is_async_threadpool;
}
#endif

/* Return non-zero if CBN is currently being executed by uv__run_pending, zero else.
   CBN->info must be initialized. */
static int cbn_is_active_pending_cb (const callback_node_t *cbn)
{
  assert(cbn != NULL);
  assert(cbn->info != NULL);

  return (uv__uv__run_pending_active() && 
          cbn->info->cb == uv__uv__run_pending_get_active_cb());
}

/* Returns non-zero if CBN is active, zero else. */
static int cbn_is_active (const callback_node_t *cbn)
{
  assert(cbn != NULL);
  return cbn->active;
}

#if 0
/* CBN inherits the appropriate fields from logical/physical parents. */
static void cbn_inherit (callback_node_t *cbn)
{
  int original_peer_info;
  assert(cbn != NULL);

  original_peer_info = 1;
  if (cbn->physical_parent)
  {
    cbn->physical_level = cbn->physical_parent->physical_level + 1;

    /* Inherit client ID. */
    cbn->orig_client_id = cbn->physical_parent->true_client_id; /* If parent started unknown and was colored, we inherit the color. */
    cbn->true_client_id = cbn->physical_parent->true_client_id;
    cbn->discovered_client_id = 0;

    /* Inherit peer_info. */
    uv__free(cbn->peer_info);
    cbn->peer_info = cbn->physical_parent->peer_info;
    assert(cbn->peer_info != NULL);
    original_peer_info = 0;
  }
  else
    cbn->physical_level = 0;

  /* Do this second: preferentially inherit client info from logical parent. */
  if (cbn->logical_parent)
  {
    cbn->logical_level = cbn->logical_parent->logical_level + 1;

    /* Inherit client ID. */
    cbn->orig_client_id = cbn->logical_parent->true_client_id; /* If parent started unknown and was colored, we inherit the color. */
    cbn->true_client_id = cbn->logical_parent->true_client_id;
    cbn->discovered_client_id = 0;

    /* Inherit peer_info. */
    if (original_peer_info)
    {
      uv__free(cbn->peer_info);
      original_peer_info = 0;
    }
    cbn->peer_info = cbn->logical_parent->peer_info;
    assert(cbn->peer_info != NULL);
  }
  else
    cbn->logical_level = 0;

#if 0
  /* TODO This assert is a bit tricky. In nodejs-mud, the parents might be e.g. "ID_NODEJS_INTERNAL" and "client 0", as the mongoDB server is contacted during the initial stack. */
  if (cbn->physical_parent && cbn->logical_parent)
    /* If both parents know their ID, they'd both better be the same.
       Otherwise I'll have to think carefully about this. */
    assert(cbn->physical_parent->true_client_id == ID_UNKNOWN || cbn->logical_parent->true_client_id == ID_UNKNOWN
        || cbn->physical_parent->true_client_id == cbn->logical_parent->true_client_id);
#endif
}
#endif

#if 0
/* Return the root of the callback-node tree containing CBN. */
static callback_node_t * cbn_get_root (callback_node_t *cbn, enum callback_tree_type type)
{
  callback_node_t *parent;
  char child_buf[1024], parent_buf[1024];

  assert(cbn != NULL);
  mylog(LOG_MAIN, 9, "cbn_get_root: Finding root for cbn %p\n", cbn);
  while (!cbn_is_root(cbn, type))
  {
    parent = cbn_get_parent(cbn, type);
    mylog(LOG_MAIN, 9, "cbn_get_root: %p is not root, climbing. child %p (%s) parent (type %i) %p (%s)\n",
      cbn, cbn, cbn_to_string(cbn, child_buf, 1024), 
      type, parent, cbn_to_string(parent, parent_buf, 1024));
    cbn = parent;
  }
  return cbn;
}
#endif

#if 0
/* Return the parent of CBN of the specified TYPE. */
static callback_node_t * cbn_get_parent (callback_node_t *cbn, enum callback_tree_type type)
{
  callback_node_t *parent;
  assert(cbn != NULL);

  switch (type)
  {
    case CALLBACK_TREE_PHYSICAL:
      parent = cbn->physical_parent;
      break;
    case CALLBACK_TREE_LOGICAL:
      parent = cbn->logical_parent;
      break;
    default:
      NOT_REACHED;
  }

  return parent;
}
#endif

#if 0
/* Return the closest ancestor of CBN of type TYPE.
   Returns one of the ancestors of CBN, or NULL if no matching ancestor is found. */
static callback_node_t * cbn_climb_to_type (callback_node_t *cbn, enum callback_type type)
{
  assert(cbn != NULL);

  while (cbn->parent != NULL)
  {
    /* Check. */
    if (cbn->parent == get_init_stack_callback_node())
    {
      cbn = NULL;
      break; 
    }

    assert(cbn->parent->info != NULL);
    if (cbn->parent->info->type == type)
    {
      cbn = cbn->parent;
      break;
    }

    /* Climb. */
    cbn = cbn->parent;
  }

  return cbn;
}

/* Returns non-zero if MAYBE_ANCESTOR is one of the ancestors of CBN. Else zero. */
static int cbn_is_descendant (callback_node_t *cbn, callback_node_t *maybe_ancestor)
{
  assert(cbn != NULL);
  assert(maybe_ancestor != NULL);

  while (cbn->parent != NULL)
  {
    /* Check. */
    if (cbn->parent == maybe_ancestor)
      return 1;

    /* Climb. */
    cbn = cbn->parent;
  }

  return 0;
}

/* The color (i.e. CLIENT_ID) of the tree rooted at CBN has been discovered.
   Color CBN and all of its descendants this color. */
static void cbn_color_tree (callback_node_t *cbn, int client_id)
{
  callback_node_t *child;
  struct list_elem *e;
  assert(cbn != NULL);

  /* If CBN is already colored, its descendants must also be colored due to inheritance. */
  if (cbn->true_client_id == client_id)
    return;

  /* Color CBN. */
  cbn->true_client_id = client_id;

  /* Color descendants. */
  /* TODO What does it mean to color logical vs. physical descendants? */
  for (e = list_begin(cbn->physical_children); e != list_end(cbn->physical_children); e = list_next(e))
  {
    child = list_entry (e, callback_node_t, physical_child_elem);
    cbn_color_tree(child, client_id);
  }
}
#endif

/* APIs that let us build lineage trees: nodes can identify their parents. 
   current_callback_node_{set,get} are thread safe and maintain per-thread mappings. */

/* Sets the current callback node for this thread to CBN. 
   NULL signifies the end of a callback tree. */
void current_callback_node_set (callback_node_t *cbn)
{
  /* My maps are thread safe. */
  map_insert(tid_to_current_cbn, (int) pthread_self(), (void *) cbn);
  if (cbn == NULL)
    mylog(LOG_MAIN, 9, "current_callback_node_set: Next callback will be a root\n");
  else
    mylog(LOG_MAIN, 9, "current_callback_node_set: Current CBN is %p\n", (void *) cbn);
}

/* Retrieves the current callback node for this thread, or NULL if no such node. 
   This function is thread safe. */
callback_node_t *current_callback_node_get (void)
{
  int found;
  callback_node_t *ret;

  /* My maps are thread safe. */
  ret = (callback_node_t *) map_lookup(tid_to_current_cbn, (int) pthread_self(), &found);

  if (!found)
    assert(ret == NULL);
  return ret;
}

/* Returns a new CBN. 
   id=-1, peer_info is allocated, {orig,true}_client_id=ID_UNKNOWN. 
   All other fields are NULL or 0. */
static callback_node_t * cbn_create (void)
{
  callback_node_t *cbn;

  cbn = (callback_node_t *) uv__malloc(sizeof *cbn);
  assert(cbn != NULL);
  memset(cbn, 0, sizeof *cbn);

  cbn->info = NULL;

  cbn->physical_level = 0;
  cbn->physical_parent = NULL;

  cbn->logical_level = 0;
  cbn->logical_parent = NULL;

  cbn->orig_client_id = ID_UNKNOWN;
  cbn->true_client_id = ID_UNKNOWN;
  cbn->discovered_client_id = 0;

  cbn->active = 0;
  cbn->physical_children = list_create();
  cbn->logical_children = list_create();
  cbn->lcbn = NULL;

  cbn->peer_info = (struct sockaddr_storage *) uv__malloc(sizeof *cbn->peer_info);
  assert(cbn->peer_info != NULL);
  memset(cbn->peer_info, 0, sizeof *cbn->peer_info);

  cbn->id = -1;
  cbn->executing_thread = 0;

  return cbn;
}

/* Set the executing_thread member of CBN 
   (to the internal tid of the currently-executing thread). 
   Call in invoke_callback prior to execution.

   Thread safe. */
static void cbn_determine_executing_thread (callback_node_t *cbn)
{
  cbn->executing_thread = pthread_self_internal();
}

/* Set the executing_thread member of LCBN 
   (to the internal tid of the currently-executing thread). 
   Call in invoke_callback prior to execution.

   Thread safe. */
void lcbn_determine_executing_thread (lcbn_t *lcbn)
{
  lcbn->executing_thread = pthread_self_internal();
}

/* Code in support of tracking the initial stack. */

/* Returns the CBN associated with the initial stack.
   Not thread safe the first time it is called. */
callback_node_t * get_init_stack_callback_node (void)
{
  static callback_node_t *init_stack_cbn = NULL;
  if (!init_stack_cbn)
  {
    init_stack_cbn = cbn_create();
    assert(init_stack_cbn != NULL);

    init_stack_cbn->orig_client_id = ID_INITIAL_STACK;
    init_stack_cbn->true_client_id = ID_INITIAL_STACK;
    init_stack_cbn->discovered_client_id = 1;

    assert(init_stack_cbn->peer_info != NULL);
    memset(init_stack_cbn->peer_info, 0, sizeof *init_stack_cbn->peer_info);

    /* Add to global order list and to root list. */
    list_push_back(global_order_list, &init_stack_cbn->global_order_elem);
    init_stack_cbn->id = list_size(global_order_list);

    list_push_back(root_list, &init_stack_cbn->root_elem); 
  }

  return init_stack_cbn;
}

/* Returns the CBN associated with the initial stack.
   Not thread safe the first time it is called. */
lcbn_t * get_init_stack_lcbn (void)
{
  static lcbn_t *init_stack_lcbn = NULL;
  if (!init_stack_lcbn)
  {
    init_stack_lcbn = lcbn_create(NULL, NULL, 0);

    init_stack_lcbn->cb_type = CALLBACK_TYPE_INITIAL_STACK;

    init_stack_lcbn->global_exec_id = lcbn_next_exec_id();
    init_stack_lcbn->global_reg_id = lcbn_next_reg_id();
    scheduler_record(sched_lcbn_create(init_stack_lcbn));
  }

  return init_stack_lcbn;
}

/* Returns 1 if BUFFER is all 0's, else 0. */
static int is_zeros (void *buffer, size_t size)
{
  size_t i;
  char *buffer_c;
  assert(buffer != NULL);

  buffer_c = buffer;
  for (i = 0; i < size; i++)
  {
    if (buffer_c[i] != 0)
      return 0;
  }
  return 1;
}

/* Prints BUFFER of SIZE rendered as chars. Adds a newline at the end. */
static void print_buf (void *buffer, size_t size)
{
  size_t i;
  char *buffer_c;
  assert(buffer != NULL);

  printf ("Printing %p of size %lu\n", buffer, (unsigned long) size);

  buffer_c = buffer;
  for (i = 0; i < size; i++)
    printf("%c", buffer_c[i]);
  printf("\n");
}

/* Populate NAMEINFO using getnameinfo on ADDR. */
static void addr_getnameinfo (struct sockaddr_storage *addr, struct uv_nameinfo *nameinfo)
{
  socklen_t addr_len;
  int err;

  assert(addr != NULL);
  assert(nameinfo != NULL);

  addr_len = sizeof *addr;

  err = getnameinfo((struct sockaddr *) addr, addr_len, nameinfo->host, sizeof(nameinfo->host), nameinfo->service, sizeof(nameinfo->service), NI_NUMERICHOST|NI_NUMERICSERV);
  if (err != 0)
  { 
    printf("addr_getnameinfo: Error, getnameinfo failed (family %i AF_UNSPEC %i AF_INET %i AF_INET6 %i): %i: %s\n", ((struct sockaddr *) addr)->sa_family, AF_UNSPEC, AF_INET, AF_INET6, err, gai_strerror(err));
    print_buf(addr, sizeof *addr);
    fflush(NULL);
    NOT_REACHED;
  }

  return;
}

#if 0
/* Prints the nameinfo associated with UVHT, if known. */
void handle_dump_nameinfo (uv_handle_t *uvht)
{
  struct uv_nameinfo nameinfo;
  assert(uvht != NULL);
  if (uvht->peer_info != NULL)
  {
    addr_getnameinfo(uvht->peer_info, &nameinfo);
    printf("handle %p: host %s, service %s\n", uvht, nameinfo.host, nameinfo.service);
  }
  else
    printf("dump_nameinfo: Sorry, uvht %p does not have peer_info\n", uvht);
  fflush(NULL);
}
#endif

#if 0
/* Compute a hash of ADDR for use as a key to peer_info_to_id.
   We classify all traffic with the same IP as from the same client. */
static unsigned addr_calc_hash (struct sockaddr_storage *addr)
{
  struct uv_nameinfo nameinfo;
  unsigned hash;

  assert(addr != NULL);

  memset(&nameinfo, 0, sizeof nameinfo);
  addr_getnameinfo(addr, &nameinfo);
  hash = map_hash(nameinfo.host, sizeof nameinfo.host);

  return hash;
}
#endif

#if 0
/* Sets PEER_INFO based on the provided uv_handle_t*.
   Use in conjunction with get_client_id.
   We can only compute the client ID when the handle encodes it.
   If no discernible client, does not modify PEER_INFO. */
static void get_peer_info (uv_handle_t *client, struct sockaddr_storage *peer_info)
{
  int err;
  socklen_t addr_len;

  assert(client != NULL);
  assert(peer_info != NULL);

  addr_len = sizeof *peer_info;

  switch (client->type) {
    case UV_TCP:
      /* If we are not yet connected to CLIENT, CLIENT may not yet know the peer name. 
         For example, on UV_CONNECTION_CB, the server has received an incoming connection, but uv_accept() may not yet have been called. 
         In the case of UV_CONNECTION_CB, CLIENT->ACCEPTED_FD contains the ID of the connecting client, and we can use that instead. */
      err = uv_tcp_getpeername((const uv_tcp_t *) client, (struct sockaddr *) peer_info, (int *) &addr_len);
      if (err != 0)
        err = uv_tcp_getpeername_direct(((uv_tcp_t *) client)->accepted_fd, (struct sockaddr *) peer_info, (int *) &addr_len);
      break;
    default:
      break;
  }
  return;
}
#endif

#if 0
/* Returns the ID of the client described by ADDR.
   If the corresponding client is already in peer_info_to_id, we return that ID.
   Otherwise we add a new entry to peer_info_to_id and id_to_peer_info and return the new ID. */
static int get_client_id (struct sockaddr_storage *addr)
{
  struct uv_nameinfo nameinfo;
  int addr_hash, next_client_id, found, client_id, *entry;

  assert(addr != NULL);
  assert(!is_zeros(addr, sizeof *addr));

  found = 0;
  addr_hash = addr_calc_hash(addr);
  assert(addr_hash != 0); /* There must be something in ADDR. */

  next_client_id = map_size(peer_info_to_id);

  entry = map_lookup(peer_info_to_id, addr_hash, &found);
  if (found)
  {
    client_id = (int) (long) entry;
    mylog(LOG_MAIN, 9, "get_peer_info: Existing client\n");
  }
  else
  {
    client_id = next_client_id;
    map_insert(id_to_peer_info, client_id, (void *) addr);
    map_insert(peer_info_to_id, addr_hash, (void *) (long) client_id);
    mylog(LOG_MAIN, 9, "get_peer_info: New client\n");
  }

  /* Print the client's info. */
  addr_getnameinfo(addr, &nameinfo);
  mylog(LOG_MAIN, 9, "get_peer_info: Client: %i -> %i -> %s:%s (id %i)\n", client_id, addr_hash, nameinfo.host, nameinfo.service, client_id);

  return client_id;
}
#endif

/* Initialize the data structures for the unified callback code. */
void unified_callback_init (void)
{
  char *schedule_modeP, *schedule_fileP;
  enum schedule_mode schedule_mode;
  static int initialized = 0;
  pthread_mutexattr_t attr;

  if (initialized)
    return;

  init_log();
  set_verbosity(LOG_MAIN, 5);
  set_verbosity(LOG_LCBN, 1);
  set_verbosity(LOG_SCHEDULER, 5);
  set_verbosity(LOG_THREADPOOL, 5);
  set_verbosity(LOG_STREAM, 1);

  mylog(LOG_MAIN, 9, "DEBUG: Testing list\n");
  list_UT();
  mylog(LOG_MAIN, 9, "DEBUG: Testing map\n");
  map_UT();
  mylog(LOG_MAIN, 9, "DEBUG: Testing tree\n");
  tree_UT();

  schedule_modeP = getenv("UV_SCHEDULE_MODE");
  schedule_fileP = getenv("UV_SCHEDULE_FILE");
  assert(schedule_modeP && schedule_fileP);
  mylog(LOG_MAIN, 1, "schedule_mode %s schedule_file %s\n", schedule_modeP, schedule_fileP);
  schedule_mode = (strcmp(schedule_modeP, "RECORD" ) == 0) ? SCHEDULE_MODE_RECORD : SCHEDULE_MODE_REPLAY;
  scheduler_init(schedule_mode, schedule_fileP);
  atexit(scheduler_emit);

  pthread_mutex_init(&metadata_lock, NULL);

  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&invoke_callback_lcbn_lock, &attr);
  pthread_mutexattr_destroy(&attr);

  global_order_list = list_create();
  root_list = list_create();

  peer_info_to_id = map_create();
  assert(peer_info_to_id != NULL);

  id_to_peer_info = map_create();
  assert(id_to_peer_info != NULL);

  callback_to_origin_map = map_create();
  assert(callback_to_origin_map != NULL);

  tid_to_current_cbn = map_create();
  assert(tid_to_current_cbn != NULL);

  tid_to_current_lcbn = map_create();
  assert(tid_to_current_lcbn != NULL);

  pthread_to_tid = map_create();
  assert(pthread_to_tid != NULL);

  (void) get_init_stack_callback_node();
  (void) get_init_stack_lcbn();

#if 1
  signal(SIGUSR1, dump_all_trees_and_exit_sighandler);
  signal(SIGUSR2, dump_all_trees_and_exit_sighandler);
#else
  signal(SIGUSR1, dump_callback_global_order_sighandler);
  signal(SIGUSR2, dump_callback_trees_sighandler);
#endif
  signal(SIGINT, dump_all_trees_and_exit_sighandler);

  mark_global_start();

  initialized = 1;
}

#if 0
/* Look for the peer info for CBI, and update CBN->peer_info if possible.
   Returns 1 if we got the peer info, else 0. 
   Do not call this if we already know the peer info. 
   
   We may find peer info for callbacks which do client (i.e. network) input or output:
     CONNECT
     CONNECTION
     READ
     WRITE
     CLOSE
   */
static int look_for_peer_info (const callback_info_t *cbi, callback_node_t *cbn, uv_handle_t **uvhtPP)
{
  uv_handle_t *uvht;

  assert(cbi != NULL);
  assert(cbn != NULL);
  assert(uvhtPP != NULL);

  /* Should not be looking for peer info if we already have it. */
  assert(!cbn_have_peer_info(cbn));

  *uvhtPP = NULL;
  switch (cbi->type)
  {
    /* TODO If status is UV_ECANCELED, don't check many of these -- see documentation for uv_close. */

    case UV_CONNECT_CB:
      *uvhtPP = (uv_handle_t *) ((uv_connect_t *) cbi->args[0])->handle;
      break;
    case UV_CONNECTION_CB:
      *uvhtPP = (uv_handle_t *) cbi->args[0];
      break;
    /* For TCP READs and WRITEs we may be able to extract the peer in get_peer_info. */
    case UV_READ_CB:
      *uvhtPP = (uv_handle_t *) cbi->args[0];
      break;
    case UV_WRITE_CB:
      *uvhtPP = (uv_handle_t *) ((uv_write_t *) cbi->args[0])->handle;
      break;
    /* UDP send, recv: the associated peer is known. cbi->args[3] is a struct sockaddr_storage * (see uv__udp_recvmsg). */
    case UV_UDP_RECV_CB:
      memcpy(cbn->peer_info, (const struct sockaddr_storage *) cbi->args[3], sizeof *cbn->peer_info);
      break;
    case UV_UDP_SEND_CB:
      memcpy(cbn->peer_info, (const struct sockaddr_storage *) cbi->args[4], sizeof *cbn->peer_info);
      break;
    case UV_CLOSE_CB:
      /* Attempt to retrieve the peer_info from the handle. If we previously identified the peer associated with this handle, we embedded the peer info in it already. */
      uvht = (uv_handle_t *) cbi->args[0];
      if (uvht != NULL && uvht->peer_info != NULL)
        memcpy(cbn->peer_info, (const struct sockaddr_storage *) uvht->peer_info, sizeof *cbn->peer_info);
    default:
      /* Cannot determine peer info. */
      break;
  }

  if (*uvhtPP != NULL)
    /* We got a handle. Extract the peer info from it, if possible. */
    get_peer_info(*uvhtPP, cbn->peer_info);
  return cbn_have_peer_info(cbn);
}
#endif

/* Our metadata structures assume that only one thread will be calling invoke_callback synchronously.
   The first time we see a CB other than UV_WORK_CB or UV__WORK_WORK, we set sync_cb_thread.
   Subsequently, we assert that every thread calling UV_WORK_CB or UV__WORK_WORK is NOT this thread,
    and that all other CBs are from that thread. */
pthread_t sync_cb_thread = 0;
int sync_cb_thread_initialized = 0;

/* Invoke the callback described by CBI. 
   Returns the CBN allocated for the callback. */
callback_node_t * invoke_callback (callback_info_t *cbi)
{
  callback_node_t *cbn = NULL;
  int is_threadpool_CB = 0;

  void *context = NULL;
  uv_handle_t *context_handle = NULL;
  uv_req_t *context_req = NULL;
  struct map *cb_type_to_lcbn = NULL;
  enum callback_context cb_context;
  int is_logical_cb;
  lcbn_t *lcbn_orig = NULL, *lcbn_new = NULL, *lcbn_par = NULL;
  sched_lcbn_t *sched_lcbn = NULL;

  assert(cbi);

  mylog(LOG_MAIN, 7, "invoke_callback: cbi %p type %s\n", cbi, callback_type_to_string(cbi->type));

  /* Determine the context. */
  cb_context = callback_type_to_context(cbi->type);
  is_logical_cb = (cb_context != CALLBACK_CONTEXT_UNKNOWN);
  lcbn_orig = lcbn_new = NULL;
  if (is_logical_cb)
  {
    context = (void *) cbi->args[0];
    if (cb_context == CALLBACK_CONTEXT_HANDLE)
    {
      context_handle = (uv_handle_t *) context;
      assert(context_handle && context_handle->magic == UV_HANDLE_MAGIC);
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
      assert(context_req && context_req->magic == UV_REQ_MAGIC);
      cb_type_to_lcbn = context_req->cb_type_to_lcbn;
    }
    else
      NOT_REACHED;
    assert(cb_type_to_lcbn && map_looks_valid(cb_type_to_lcbn)); 
  }

  is_threadpool_CB = (cbi->type == UV__WORK_WORK || cbi->type == UV_WORK_CB || 
                      cbi->type == UV_FS_WORK_CB || cbi->type == UV_GETADDRINFO_WORK_CB || cbi->type == UV_GETNAMEINFO_WORK_CB);
  if (sync_cb_thread == 0 && !is_threadpool_CB)
  {
    /* First non-threadpool CB: note the tid. 
       This must be the source of all future non-threadpool CBs, and never
       the source of threadpool CBs. */
    sync_cb_thread = pthread_self();
    sync_cb_thread_initialized = 1;
    assert(sync_cb_thread != 0);
  }

  if (sync_cb_thread_initialized)
  {
    /* Non-threadpool callbacks should be called by sync_cb_thread.
       Threadpool callbacks must not be that thread. */
    if (is_threadpool_CB)
      assert(pthread_self() != sync_cb_thread);
    else
      assert(pthread_self() == sync_cb_thread);
  }

  /* Prep a new callback node (CBN). */
  cbn = cbn_create();
  cbn->info = cbi;

  /* Atomically update the metadata structures.
     Prevents races in the order of a parent's children vs. global order. 
     Not too concerned about performance:
      - none of these operations is particularly expensive
      - there's only the looper thread and threadpool threads to worry about
      - invoking a CB is the expensive part */
  uv__metadata_lock();

#if 0
  cbn_determine_parentage(cbn);
#endif

  cbn->was_pending_cb = cbn_is_active_pending_cb(cbn);

  list_push_back(global_order_list, &cbn->global_order_elem);
  cbn->id = list_size(global_order_list);

  /* Tell asynchronous children their parentage. 
     For some CB types this is a no-op. */
#if 0
  cbn_embed_parentage(cbn);

  /* Attempt to discover the client ID associated with this callback. */
  if (cbn->true_client_id == ID_UNKNOWN)
    cbn_discover_id(cbn);
#endif

  cbn_determine_executing_thread(cbn);

  /* Is CBN coherent? */
  assert((cbn->physical_level == 0 && cbn->physical_parent == NULL)
      || (0 < cbn->physical_level && cbn->physical_parent != NULL));
  assert((cbn->logical_level == 0 && cbn->logical_parent == NULL)
      || (0 < cbn->logical_level && cbn->logical_parent != NULL));
#if 0
  if (cbi->type == UV__WORK_WORK && !cbn->logical_parent)
    NOT_REACHED;
#endif
  uv__metadata_unlock();

  /* Run the callback. */

  /* If CB is a logical callback, retrieve and update the LCBN. */
  if (is_logical_cb)
  {
    uv__invoke_callback_lcbn_lock();

    lcbn_new = lcbn_get(cb_type_to_lcbn, cbi->type);
    assert(lcbn_new);
    assert(lcbn_new->cb_type == cbi->type);
    assert(tree_get_parent(&lcbn_new->tree_node));
    lcbn_par = tree_entry(tree_get_parent(&lcbn_new->tree_node), lcbn_t, tree_node);

    lcbn_new->info = cbi;

    lcbn_orig = lcbn_current_get();
    lcbn_current_set(lcbn_new);

    mylog(LOG_MAIN, 3, "invoke_callback: invoking lcbn %p (type %s) context %p parent %p (parent type %s) cb %p type %s lcbn_orig %p\n",
      lcbn_new, callback_type_to_string(cbi->type), context, lcbn_par, callback_type_to_string(lcbn_par->cb_type), cbi->cb, lcbn_orig);
    lcbn_mark_begin(lcbn_new);

    lcbn_new->global_exec_id = lcbn_next_exec_id();

    if (callback_type_to_behavior(lcbn_new->cb_type) == CALLBACK_BEHAVIOR_RESPONSE)
    {
      /* If this LCBN is a response, it may repeat. If so, the next response must come after this response,
         and is in some sense caused by this response. Consequently, register after setting LCBN so that it becomes a child of this LCBN. */
      mylog(LOG_MAIN, 5, "invoke_callback: registering cb %p under context %p with type %s as child of LCBN %p\n",
        lcbn_get_cb(lcbn_new), lcbn_get_context(lcbn_new), callback_type_to_string(lcbn_get_cb_type(lcbn_new)), lcbn_new);
      uv__register_callback(lcbn_get_context(lcbn_new), lcbn_get_cb(lcbn_new), lcbn_get_cb_type(lcbn_new));
    }

    lcbn_determine_executing_thread(lcbn_new);

    /* Verify that this LCBN is next in the schedule. 
       If not, in REPLAY mode something has gone amiss in the input schedule. */
    sched_lcbn = sched_lcbn_create(lcbn_new);
    if (!sched_lcbn_is_next(sched_lcbn))
    {
      mylog(LOG_MAIN, 1, "invoke_callback: Error, lcbn %p (type %s) is not the next scheduled LCBN. I have run %i callbacks.\n", lcbn_new, callback_type_to_string(lcbn_new->cb_type), scheduler_already_run());
      assert(!"Error, the requested CB is not being scheduled appropriately");
    }
    sched_lcbn_destroy(sched_lcbn);

    /* Advance the scheduler prior to invoking the CB. 
       This way, if multiple LCBNs are nested (e.g. artificially for FS operations),
       the nested ones will perceive themselves as 'next'. */
    mylog(LOG_MAIN, 5, "invoke_callback: lcbn %p (type %s); advancing the scheduler\n", 
      lcbn_new, callback_type_to_string(lcbn_new->cb_type));
    scheduler_advance();
  }

  cbn_start(cbn);

  current_callback_node_set(cbn); /* Thread-safe. */
  cbn_execute_callback(cbn); 

  cbn_stop(cbn);

  mylog(LOG_MAIN, 7, "invoke_callback: Done invoking cbi %p (type %s)\n", cbi, callback_type_to_string(cbi->type));

  /* Done with the callback. */
  current_callback_node_set(cbn->physical_parent);

  /* If a logical callback, restore the previous lcbn. */
  if (is_logical_cb)
  {
    mylog(LOG_MAIN, 5, "invoke_callback: Done with lcbn %p (type %s)\n",
      lcbn_new, callback_type_to_string(lcbn_new->cb_type));
    lcbn_mark_end(lcbn_new);

    lcbn_current_set(lcbn_orig);

    uv__invoke_callback_lcbn_unlock();
  }

  return cbn;
}

/* Prints callback node CBN to FD in graphviz format. 
   The caller must have prepared the outer graph declaration; we just
   print node/edge info in graphviz format. 
   TODO Increase the amount of information embedded here? */
static void dump_callback_node_gv (int fd, callback_node_t *cbn)
{
  char client_info[256], shape[16], penwidth[16];
  struct uv_nameinfo nameinfo;
  assert (cbn != NULL);

  switch (cbn->true_client_id)
  {
    case ID_NODEJS_INTERNAL:
      snprintf(client_info, 256, "internal (nodeJS)");
      break;
    case ID_LIBUV_INTERNAL:
      snprintf(client_info, 256, "internal (libuv)");
      break;
    case ID_INITIAL_STACK:
      snprintf(client_info, 256, "initial stack");
      break;
    case ID_UNKNOWN:
      /* Originated from the application, but not known which user it's associated with. */
      snprintf(client_info, 256, "user code, unknown user");
      break;
    default:
      assert(cbn_have_peer_info(cbn));
      addr_getnameinfo(cbn->peer_info, &nameinfo);
      snprintf(client_info, 256, "%s [port %s]", nameinfo.host, nameinfo.service);
  }

  /* Change shape based on whether it was client input, client output, or neither. */
  if (cbn_got_client_input(cbn))
    sprintf(shape, "circle");
  else if (cbn_sent_client_output(cbn))
    sprintf(shape, "diamond");
  else
    sprintf(shape, "square");

  /* Change thickness if we ran client code. */
  sprintf(penwidth, "1.0");

  /* Example listing:
    1 [label="client 12\ntype UV_ALLOC_CB\nactive 0\nstart 1 (s) duration 10s50ns\nID 1..."];
  */
  dprintf(fd, "    %i [label=\"client %i\\ntype %s\\nactive %i\\nstart %is %lins (since beginning) duration %is %lins\\nID %i\\nexecuting_thread %i\\nclient ID %i (%s)\\n was_pending_cb %i\" style=\"filled\" colorscheme=\"%s\" color=\"%s\" shape=\"%s\" penwidth=\"%s\"];\n",
    cbn->id, cbn->true_client_id, cbn->info ? callback_type_to_string (cbn->info->type) : "<unknown>", cbn->active, (int) cbn->relative_start.tv_sec, cbn->relative_start.tv_nsec, (int) cbn->duration.tv_sec, cbn->duration.tv_nsec, cbn->id, cbn->executing_thread, cbn->true_client_id, client_info, cbn->was_pending_cb, graphviz_colorscheme, graphviz_colors[cbn->true_client_id + RESERVED_IDS], shape, penwidth);
}

/* Prints callback node CBN to FD.
   If INDENT, we indent it according to its level. */
static void dump_callback_node (int fd, callback_node_t *cbn, char *prefix, int do_indent)
{
  char spaces[512];
  int i, n_spaces;

  assert (cbn != NULL);
  if (do_indent)
  {
    memset (spaces, 0, 512);
    n_spaces = cbn->physical_level;
    for (i = 0; i < n_spaces; i++)
      strcat (spaces, " ");
  }

  dprintf(fd, "%s%s | <cbn> <%p> | <id> <%i> | <info> <%p> | <type> <%s> | <logical level> <%i> | <physical level> <%i> | <logical parent> <%p> | <logical parent_id> <%i> | <logical parent> <%p> | <logical parent_id> <%i> |<active> <%i> | <n_physical_children> <%i> | <n_logical_children> <%i> | <client_id> <%i> | <relative start> <%is %lins> | <duration> <%is %lins> | <executing_thread> <%i> |\n", 
    do_indent ? spaces : "", prefix, (void *) cbn, cbn->id, (void *) cbn->info, cbn->info ? callback_type_to_string (cbn->info->type) : "<unknown>", cbn->logical_level, cbn->physical_level, (void *) cbn->logical_parent, cbn->logical_parent ? cbn->logical_parent->id : -1, (void *) cbn->logical_parent, cbn->logical_parent ? cbn->logical_parent->id : -1, cbn->active, list_size (cbn->physical_children), list_size (cbn->logical_children), cbn->true_client_id, (int) cbn->relative_start.tv_sec, cbn->relative_start.tv_nsec, (int) cbn->duration.tv_sec, cbn->duration.tv_nsec, cbn->executing_thread);
}

/* Dumps all callbacks in the order in which they were called. 
   Locks and unlocks GLOBAL_ORDER_LIST. */
void dump_callback_globalorder (void)
{
  struct list_elem *e;
  int cbn_num;
  char prefix[64];
  int fd;
  char out_file[128];
  callback_node_t *cbn;

  snprintf(out_file, 128, "/tmp/callback_global_order_%i_%i.txt", (int) time(NULL), getpid());
  printf("Dumping all %i callbacks in their global order to %s\n", list_size(global_order_list), out_file);

  fd = open (out_file, O_CREAT|O_TRUNC|O_RDWR, S_IRWXU|S_IRWXG|S_IRWXO);
  if (fd < 0)
  {
    printf("Error, open (%s, O_CREAT|O_TRUNC|O_RDWR, S_IRWXU|S_IRWXG|S_IRWXO) failed, returning %i. errno %i: %s\n",
      out_file, fd, errno, strerror (errno));
    fflush(NULL);
    exit (1);
  }

  list_lock(global_order_list);

  cbn_num = 0;
  for (e = list_begin(global_order_list); e != list_end(global_order_list); e = list_next(e))
  {
    assert(e != NULL);
    cbn = list_entry(e, callback_node_t, global_order_elem);
    /* Don't emit the "fake" initial stack callback node. */
    if (cbn == get_init_stack_callback_node())
      continue;
    snprintf(prefix, 64, "Callback %i: ", cbn_num);
    dump_callback_node (fd, cbn, prefix, 0);
    cbn_num++;
  }
  fflush(NULL);

  list_unlock(global_order_list);
  close(fd);
}


void dump_lcbn_globalorder(void)
{
  int fd;
  char unique_out_file[128];
  char shared_out_file[128];
  struct list *lcbn_list, *filtered_nodes;

  lcbn_list = tree_as_list(&get_init_stack_lcbn()->tree_node);

  /* Registration order (all CBs). */
  snprintf(unique_out_file, 128, "/tmp/lcbn_global_reg_order_%i_%i.txt", (int) time(NULL), getpid());
  printf("Dumping all %i registered LCBNs in their global registration order to %s\n", list_size(lcbn_list), unique_out_file);

  fd = open(unique_out_file, O_CREAT|O_TRUNC|O_RDWR, S_IRWXU|S_IRWXG|S_IRWXO);
  if (fd < 0)
    assert(!"Error, could not open output file");
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
    assert(!"Error, could not open output file");
  list_sort(lcbn_list, lcbn_sort_by_exec_id, NULL);
  filtered_nodes = list_filter(lcbn_list, lcbn_remove_unexecuted, NULL); 
  printf("Dumping all %i executed LCBNs in their global exec order to %s\n", list_size(lcbn_list), unique_out_file);
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

#if 0
/* Dumps the callback tree rooted at CBN to FD. */
static void dump_callback_tree (int fd, callback_node_t *cbn)
{
  int child_num;
  struct list_elem *e;
  callback_node_t *node;
  assert (cbn != NULL);

  dump_callback_node (fd, cbn, "", 1);
  child_num = 0;
  for (e = list_begin(cbn->children); e != list_end(cbn->children); e = list_next(e))
  {
    node = list_entry (e, callback_node_t, child_elem);
    /* printf("Parent cbn %p child %i: %p\n", cbn, child_num, node); */
    dump_callback_tree (fd, node);
    child_num++;
  }
}
#endif

/* Dump the callback tree rooted at CBN to FD in graphviz format.
   The caller must have prepared the outer graph declaration; we just
   print node/edge info in graphviz format. */
static void dump_callback_tree_gv (int fd, callback_node_t *cbn)
{
  int child_num;
  struct list_elem *e;
  callback_node_t *child;
  assert (cbn != NULL);

  dump_callback_node_gv (fd, cbn);
  child_num = 0;
  for (e = list_begin(cbn->physical_children); e != list_end(cbn->physical_children); e = list_next(e))
  {
    child = list_entry(e, callback_node_t, physical_child_elem);
    /* Print the relationship between parent and child. */
    dprintf(fd, "    %i -> %i;\n", cbn->id, child->id); 
    dump_callback_tree_gv (fd, child);
    child_num++;
  }
}

/* Returns the size of the tree rooted at CBN (i.e. # callback_nodes). */
int callback_tree_size (callback_node_t *root)
{
  int size; 
  struct list_elem *e;
  callback_node_t *node;

  assert(root != NULL);

  size = 1;
  for (e = list_begin(root->physical_children); e != list_end(root->physical_children); e = list_next(e))
  {
    node = list_entry(e, callback_node_t, physical_child_elem);
    size += callback_tree_size (node);
  }

  return size;
}

/* Dumps each callback tree in graphviz format to a file. */
void dump_callback_trees (void)
{
  struct list_elem *e;
  int tree_num, tree_size, meta_size;

#if GRAPHVIZ
  int fd = -1;
  char out_file[128];
  snprintf(out_file, 128, "/tmp/individual_callback_trees_%i_%i.gv", (int) time(NULL), getpid());
  printf("Dumping all %i callback trees to %s\n", list_size(root_list), out_file);

  fd = open (out_file, O_CREAT|O_TRUNC|O_RDWR, S_IRWXU|S_IRWXG|S_IRWXO);
  assert (0 <= fd);
#endif

  /* Print as individual trees. */
  meta_size = 0;
  tree_num = 0;
  for (e = list_begin(root_list); e != list_end(root_list); e = list_next(e))
  {
    callback_node_t *root = list_entry(e, callback_node_t, root_elem);
#if GRAPHVIZ
    tree_size = callback_tree_size (root);
    meta_size += tree_size;
    dprintf(fd, "digraph %i {\n    /* size %i */\n", tree_num, tree_size);
    dump_callback_tree_gv (fd, root);
    dprintf(fd, "}\n\n");
#else
    printf("Tree %i:\n", tree_num);
    dump_callback_tree (root);
#endif
    ++tree_num;
  }
  close(fd);

#if GRAPHVIZ
  snprintf(out_file, 128, "/tmp/meta_callback_trees_%i_%i.gv", (int) time(NULL), getpid());
  printf("Dumping the %i callback trees as a meta-tree to %s\n  dot -Tdot %s -o /tmp/graph.dot\n  xdot /tmp/graph.dot\n", list_size(root_list), out_file, out_file);

  fd = open (out_file, O_CREAT|O_TRUNC|O_RDWR, S_IRWXU|S_IRWXG|S_IRWXO);
  assert (0 <= fd);

  printf("Dumping META_TREE\n");
  /* Print as one giant tree with a null root. 
     Nodes use their global ID as a node ID so trees can coexist happily in the same meta-tree. 
     Using ordering="out" seems to make each node be placed by global ID order (i.e. in the same order in which they arrive). We'll see how well this works more generally... */
  dprintf(fd, "digraph META_TREE {\n    graph [ordering=\"out\"]\n     /* size %i */\n", meta_size);
  dprintf(fd, "  -1 [label=\"meta-root node\"]\n");
  tree_num = 0;
  for (e = list_begin(root_list); e != list_end(root_list); e = list_next(e))
  {
    callback_node_t *root = list_entry(e, callback_node_t, root_elem);
    assert (0 <= fd);
    tree_size = callback_tree_size (root);
    dprintf(fd, "  subgraph %i {\n    /* size %i */\n", tree_num, tree_size);
    dump_callback_tree_gv (fd, root);
    dprintf(fd, "  }\n");
    dprintf(fd, "  -1 -> %i\n", root->id); /* Link to the meta-root node. */
    ++tree_num;
  }

  /* Print logical relationships. These may cross subgraphs and so 
      must be done separately. */
  for (e = list_begin(root_list); e != list_end(root_list); e = list_next(e))
  {
    callback_node_t *root = list_entry(e, callback_node_t, root_elem);
    cbn_walk(root, CALLBACK_TREE_PHYSICAL, cbn_print_logical_parentage_gv, (void *) &fd);
  }

  dprintf(fd, "}\n");
  close(fd);
#else
  printf("No meta tree for you\n");
#endif

  fflush(NULL);
}

/* Print the logical parentage of CBN to FDP in graphviz style. */
static void cbn_print_logical_parentage_gv (callback_node_t *cbn, void *fdP)
{
  int *fd = (int *) fdP;
  assert(cbn);
  assert(fdP);

  /* Print relation to logical parent. */
  if (cbn->logical_parent && cbn->logical_parent != cbn->physical_parent)
  {
    assert(0 <= cbn->logical_parent->id);
    assert(cbn->logical_parent->id < cbn->id);
    dprintf(*fd, "  %i -> %i [style=\"dotted\"];\n", cbn->logical_parent->id, cbn->id); 
  }
}

/* Returns non-zero if this CBN received input from a client, else 0. */
static int cbn_got_client_input (callback_node_t *cbn)
{
  int could_be_input;
  assert(cbn != NULL);

  if (!cbn->info)
    return 0;

  could_be_input = (cbn->info->type == UV_CONNECT_CB
                 || cbn->info->type == UV_CONNECTION_CB
                 || cbn->info->type == UV_READ_CB
                 || cbn->info->type == UV_CLOSE_CB);
  return (cbn->discovered_client_id && could_be_input);
}

/* Returns non-zero if this CBN sent output to a client, else 0. */
static int cbn_sent_client_output (callback_node_t *cbn)
{
  int could_be_output;
  assert(cbn != NULL);

  if (!cbn->info)
    return 0;

  could_be_output = (cbn->info->type == UV_WRITE_CB);
  return (cbn->discovered_client_id && could_be_output);
}

void dump_callback_global_order_sighandler (int signum)
{
  printf("Got signal %i. Dumping callback global order\n", signum);
  dump_callback_globalorder();
}

void dump_callback_trees_sighandler (int signum)
{
  printf("Got signal %i. Dumping callback trees\n", signum);
  dump_callback_trees ();
}

void dump_all_trees_and_exit_sighandler (int signum)
{
  printf("Got signal %i. Dumping all trees and exiting\n", signum);
  printf("Callback global order\n");
  dump_callback_globalorder();
  fflush(NULL);
  printf("Callback trees\n");
  dump_callback_trees ();
  fflush(NULL);
  printf("lcbn global order\n");
  dump_lcbn_globalorder();
  fflush(NULL);
  exit (0);
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

/* Set RES = START - global start, giving relative time since execution began. */
static void calc_relative_time (struct timespec *start, struct timespec *res)
{
  timespec_sub(start, &global_start, res);
}

/* Call from any libuv function that is passed a user callback.
   At callback registration time we can determine the origin
     and create a new logical node.
   If CB is NULL, CB will never be called, and we ignore it. 

   May be called concurrently due to threadpool. Thread safe. 
   
   CONTEXT is the handle or request associated with this callback.
   Use callback_type_to_context and callback_type_to_behavior to deal with it. 
   If CONTEXT is a uv_req_t *, it must have been uv_req_init'd already. 
   */
void uv__register_callback (void *context, any_func cb, enum callback_type cb_type)
{
  callback_node_t *cbn;
  struct callback_origin *co;
  enum callback_origin_type origin;
  int key;
  uv_handle_t *context_handle;
  uv_req_t *context_req;
  struct map *cb_type_to_lcbn;
  enum callback_context cb_context;
  lcbn_t *lcbn_cur, *lcbn_new;

  /* NULL callbacks will never be invoked. */
  if (cb == NULL)
    return;

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
#if 1
  /* TODO simple_node_programs/sql/sqlite_simple.js uv_try_write */
  if (lcbn_cur == NULL)
    lcbn_cur = get_init_stack_lcbn();
#else
  assert(lcbn_cur != NULL); /* All callbacks come from user code. */
#endif

  /* Create a new LCBN. */
  lcbn_new = lcbn_create(context, cb, cb_type);
  /* Register it in its context. */
  lcbn_register(cb_type_to_lcbn, cb_type, lcbn_new);
  lcbn_add_child(lcbn_cur, lcbn_new);

  /* Add to metadata structures. */
  lcbn_new->global_reg_id = lcbn_next_reg_id();
  scheduler_record(sched_lcbn_create(lcbn_new));

  mylog(LOG_MAIN, 5, "uv__register_callback: lcbn %p cb %p context %p type %s registrar %p\n",
    lcbn_new, cb, context, callback_type_to_string(cb_type), lcbn_cur);

  /* OLD CODE FOLLOWS. */

  co = (struct callback_origin *) uv__malloc(sizeof *co);
  assert(co != NULL);
  memset(co, 0, sizeof *co);

  /* Initial stack active -> USER.

     If a uv_run is active, the source can be any of NODE_INTERNAL, LIBUV_INTERNAL, and USER.
     We inherit the current callback node's origin if there is one.
     If we're in uv_run and there's no active callback, the source must in LIBUV_INTERNAL (does this ever happen?).

     Neither init stack nor uv_run -> NODEJS_INTERNAL. */
  if (uv__init_stack_active())
    origin = USER;
  else if (uv__uv_run_active())
  {
    cbn = current_callback_node_get();
    if (cbn != NULL)
      origin = cbn->info->origin;
    else
    {
      origin = LIBUV_INTERNAL;
      /* TODO We do reach this, when mucking about with LCBN inheritance in loop-watcher.c (and someday in timer.c). */
      /* NOT_REACHED; */
    }
  }
  else
    origin = NODEJS_INTERNAL;

  co->cb = cb;
  co->type = cb_type;
  co->origin = origin;
  key = (int) (long) cb;
  map_insert(callback_to_origin_map, key, co);
  mylog(LOG_MAIN, 5, "uv__register_callback: registered cb %p as key %i type %s origin %i\n",
    co->cb, key, callback_type_to_string(co->type), co->origin);
}

/* Returns the origin of CB.
   You must have called uv__register_callback on CB first.
   Asserts if CB is NULL or if CB was not registered. 
   
   Exceptions: FS events (src/unix/fs.c) registered in the thread pool are 
    implemented by wrapping the action inside uv__fs_work and the "done" uv_fs_cb 
    in uv__fs_done. The same style is used for a few other internal wrappers
    listed below.
    
    If CB is an internal wrapper, we return (struct callback_origin *) as follows:
      uv__fs_work           -> WAS_UV__FS_WORK
      uv__fs_done           -> WAS_UV__FS_DONE
      uv__getaddrinfo_work  -> WAS_UV__GETADDRINFO_WORK
      uv__getaddrinfo_done  -> WAS_UV__GETADDRINFO_DONE
      uv__getnameinfo_work  -> WAS_UV__GETNAMEINFO_WORK
      uv__getnameinfo_done  -> WAS_UV__GETNAMEINFO_DONE
      uv__queue_work        -> WAS_UV__QUEUE_WORK
      uv__queue_done        -> WAS_UV__QUEUE_DONE
      uv__stream_io         -> WAS_UV__STREAM_IO
      uv__async_io          -> WAS_UV__ASYNC_IO
      uv__async_event       -> WAS_UV__ASYNC_EVENT
      uv__server_io         -> WAS_UV__SERVER_IO
      uv__signal_event      -> WAS_UV__SIGNAL_EVENT 
      uv__work_done         -> WAS_UV__WORK_DONE 
*/
struct callback_origin * uv__callback_origin (any_func cb)
{
  struct callback_origin *co;
  int key;
  int found;

  assert(cb != NULL);
  found = 0;
  
  key = (int) (long) cb;
  mylog(LOG_MAIN, 9, "uv__callback_origin: looking up origin of cb %p\n", cb);

  co = (struct callback_origin *) map_lookup(callback_to_origin_map, key, &found);
  if (!found)
  {
    /* See comments above the function. */
    if (cb == uv_uv__fs_done_ptr())
      return (void *) WAS_UV__FS_WORK;
    else if (cb == uv_uv__fs_work_ptr())
      return (void *) WAS_UV__FS_DONE;

    else if (cb == uv_uv__getaddrinfo_work_ptr())
      return (void *) WAS_UV__GETADDRINFO_WORK;
    else if (cb == uv_uv__getaddrinfo_done_ptr())
      return (void *) WAS_UV__GETADDRINFO_DONE;

    else if (cb == uv_uv__queue_work_ptr())
      return (void *) WAS_UV__QUEUE_WORK;
    else if (cb == uv_uv__queue_done_ptr())
      return (void *) WAS_UV__QUEUE_DONE;

    else if (cb == uv_uv__stream_io_ptr())
      return (void *) WAS_UV__STREAM_IO;

    else if (cb == uv_uv__async_io_ptr())
      return (void *) WAS_UV__ASYNC_IO;
    else if (cb == uv_uv__async_event_ptr())
      return (void *) WAS_UV__ASYNC_EVENT;

    else if (cb == uv_uv__server_io_ptr())
      return (void *) WAS_UV__SERVER_IO;

    else if (cb == uv_uv__signal_event_ptr())
      return (void *) WAS_UV__SIGNAL_EVENT;
    else if (cb == (any_func) uv__work_done)
      return (void *) WAS_UV__WORK_DONE;

    else
      /* TODO Disabling for now. Reachable using prog udp_send.js Not sure if I want to come back here to fix it. */
      /* NOT_REACHED; */
      return (void *) WAS_UV__WORK_DONE;
  }

  assert(co != NULL);
  return co;
}

/* Implementation for tracking the initial stack. */

/* Note that we've begun the initial application stack. 
   Call once prior to invoking the application code. 
   We also set the current CBN to the init_stack_cbn 
     so that all callbacks resulting from the initial stack
     (e.g. warning messages from lib/sys.js) are descended
     appropriately. */
void uv__mark_init_stack_begin (void)
{
  callback_node_t *init_stack_cbn;
  lcbn_t *init_stack_lcbn;

  /* Call at most once. */
  static int here = 0;
  assert(!here);
  here = 1;

  init_stack_cbn = get_init_stack_callback_node();
  current_callback_node_set(init_stack_cbn);
  cbn_start(init_stack_cbn);

  init_stack_lcbn = get_init_stack_lcbn();

  lcbn_determine_executing_thread(init_stack_lcbn);
  lcbn_current_set(init_stack_lcbn);
  lcbn_mark_begin(init_stack_lcbn);

  mylog(LOG_MAIN, 9, "uv__mark_init_stack_begin: inital stack has begun\n");
}

/* Note that we've finished the initial application stack. 
   Call once after the initial stack is complete. 
   Pair with uv__mark_init_stack_begin. */
void uv__mark_init_stack_end (void)
{
  callback_node_t *init_stack_cbn;
  lcbn_t *init_stack_lcbn;

  assert(uv__init_stack_active()); 

  init_stack_cbn = get_init_stack_callback_node();
  assert(cbn_is_active(init_stack_cbn));

  cbn_stop(init_stack_cbn);
  current_callback_node_set(NULL);

  init_stack_lcbn = get_init_stack_lcbn();
  lcbn_mark_end(init_stack_lcbn);
  lcbn_current_set(NULL);

  mylog(LOG_MAIN, 9, "uv__mark_init_stack_end: inital stack has ended\n");
}

/* Returns non-zero if we're in the application's initial stack, else 0. */
int uv__init_stack_active (void)
{
  return cbn_is_active(get_init_stack_callback_node());
}

/* Tracking whether or not we're in uv_run. */
int uv_run_active = 0;
/* Note that we've entered libuv's uv_run loop. */
void uv__mark_uv_run_begin (void)
{
  assert(!uv_run_active);
  uv_run_active = 1;
  mylog(LOG_MAIN, 9, "uv__mark_uv_run_begin: uv_run has begun\n");
}

/* Note that we've finished the libuv uv_run loop.
   Pair with uv__mark_uv_run_begin. */
void uv__mark_uv_run_end (void)
{
  assert(uv_run_active);
  uv_run_active = 0;
  mylog(LOG_MAIN, 9, "uv__mark_uv_run_end: uv_run has ended\n");
}

/* Returns non-zero if we're in the application's initial stack, else 0. */
int uv__uv_run_active (void)
{
  return uv_run_active;
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
  mylog(LOG_MAIN, 9, "uv__mark_uv__run_pending_begin: uv__run_pending has begun\n");
}

/* Note that we've finished the libuv uv__run_pending loop.
   Pair with uv__mark_uv__run_pending_begin. */
void uv__mark_uv__run_pending_end (void)
{
  assert(uv__run_pending_active);
  uv__run_pending_active = 0;
  uv__uv__run_pending_set_active_cb(NULL);
  mylog(LOG_MAIN, 9, "uv__mark_uv__run_pending_end: uv__run_pending has ended\n");
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

#if 0
/* Attempt to discover the client ID of CBN.
   CBN->id must be ID_UNKNOWN. 
   Returns non-zero if discovered, else zero. */
static int cbn_discover_id (callback_node_t *cbn)
{
  uv_handle_t *uvht;
  int got_peer_info;
  struct callback_origin *origin;

  assert(cbn != NULL);
  assert(cbn->peer_info != NULL);
  assert(cbn->true_client_id == ID_UNKNOWN);
  assert(!cbn_have_peer_info(cbn));
  assert(!cbn->discovered_client_id);

  mylog(LOG_MAIN, 9, "cbn_discover_id: Looking for the ID associated with CBN %p (cb %p)\n", cbn, cbn->info->cb);
  got_peer_info = look_for_peer_info(cbn->info, cbn, &uvht);
  if (got_peer_info)
  {
    mylog(LOG_MAIN, 9, "cbn_discover_id: Found peer info\n");
    /* We determined the peer info. Convert to ID. */
    cbn->discovered_client_id = 1;
    cbn->true_client_id = get_client_id(cbn->peer_info);
    assert(0 <= cbn->true_client_id);
  }
  else
  {
    origin = uv__callback_origin(cbn->info->cb);
    if (INTERNAL_CALLBACK_WRAPPERS_MAX < (unsigned long) origin)
    {
      /* A registered CB rather than an internally generated one. 
         Let's see what it is. */
      assert(origin->type == cbn->info->type);
      /* Callbacks originating internally can be colored. */
      switch (origin->origin)
      {
        case NODEJS_INTERNAL:
          cbn->discovered_client_id = 1;
          cbn->true_client_id = ID_NODEJS_INTERNAL;
          break;
        case LIBUV_INTERNAL:
          cbn->discovered_client_id = 1;
          cbn->true_client_id = ID_LIBUV_INTERNAL;
          NOT_REACHED; /* Does this ever happen? */
          break;
        case USER:
        default:
          cbn->discovered_client_id = 0;
      }
    }
  }

  if (cbn->discovered_client_id && uvht)
    /* Embed the peer_info for this handle. Used to identify the client on CLOSE_CB. 
       As seen in nodejs-mud, the handle can be re-used on different clients. 
       I'm assuming that this does not happen unsafely, since I cannot imagine how
       it would work if the same handle could be used to talk to two different clients concurrently. 
       However, this is a potential source of error. */
    uvht->peer_info = cbn->peer_info;

  return cbn->discovered_client_id;
}
#endif

/* Execute the callback described by CBN based on CBN->info. */
static void cbn_execute_callback (callback_node_t *cbn)
{
  callback_info_t *info = NULL;
  assert(cbn != NULL);
  assert(cbn->info != NULL);

  info = cbn->info;
  assert(info->cb);

  mylog(LOG_MAIN, 3, "Executing cb\n");

  /* Invoke the callback. */
  switch (info->type)
  {
    /* include/uv.h */
    case UV_ALLOC_CB:
      ((uv_alloc_cb) info->cb)((uv_handle_t *) info->args[0], (size_t) info->args[1], (uv_buf_t *) info->args[2]);
      break;
    case UV_READ_CB:
      ((uv_read_cb) info->cb)((uv_stream_t *) info->args[0], (ssize_t) info->args[1], (const uv_buf_t *) info->args[2]);
      break;
    case UV_WRITE_CB:
      ((uv_write_cb) info->cb)((uv_write_t *) info->args[0], (int) info->args[1]);
      break;
    case UV_CONNECT_CB:
      ((uv_connect_cb) info->cb)((uv_connect_t *) info->args[0], (int) info->args[1]);
      break;
    case UV_SHUTDOWN_CB:
      ((uv_shutdown_cb) info->cb)((uv_shutdown_t *) info->args[0], (int) info->args[1]);
      break;
    case UV_CONNECTION_CB:
      ((uv_connection_cb) info->cb)((uv_stream_t *) info->args[0], (int) info->args[1]);
      break;
    case UV_CLOSE_CB:
      ((uv_close_cb) info->cb)((uv_handle_t *) info->args[0]);
      break;
    case UV_POLL_CB:
      ((uv_poll_cb) info->cb)((uv_poll_t *) info->args[0], (int) info->args[1], (int) info->args[2]);
      break;
    case UV_TIMER_CB:
      ((uv_timer_cb) info->cb)((uv_timer_t *) info->args[0]);
      break;
    case UV_ASYNC_CB:
      ((uv_async_cb) info->cb)((uv_async_t *) info->args[0]);
      break;
    case UV_PREPARE_CB:
      ((uv_prepare_cb) info->cb)((uv_prepare_t *) info->args[0]);
      break;
    case UV_CHECK_CB:
      ((uv_check_cb) info->cb)((uv_check_t *) info->args[0]);
      break;
    case UV_IDLE_CB:
      ((uv_idle_cb) info->cb)((uv_idle_t *) info->args[0]);
      break;
    case UV_EXIT_CB:
      ((uv_exit_cb) info->cb)((uv_process_t *) info->args[0], (int64_t) info->args[1], (int) info->args[2]);
      break;
    case UV_WALK_CB:
      ((uv_walk_cb) info->cb)((uv_handle_t *) info->args[0], (void *) info->args[1]);
      break;
    case UV_FS_WORK_CB:
      ((uv_internal_work_cb) info->cb)((struct uv__work *) info->args[0]);
      break;
    case UV_FS_CB:
      ((uv_fs_cb) info->cb)((uv_fs_t *) info->args[0]);
      break;
    case UV_WORK_CB:
      ((uv_work_cb) info->cb)((uv_work_t *) info->args[0]);
      break;
    case UV_AFTER_WORK_CB:
      ((uv_after_work_cb) info->cb)((uv_work_t *) info->args[0], (int) info->args[1]);
      break;
    case UV_GETADDRINFO_WORK_CB:
      ((uv_internal_work_cb) info->cb)((struct uv__work *) info->args[0]);
      break;
    case UV_GETNAMEINFO_WORK_CB:
      ((uv_internal_work_cb) info->cb)((struct uv__work *) info->args[0]);
      break;
    case UV_GETADDRINFO_CB:
      ((uv_getaddrinfo_cb) info->cb)((uv_getaddrinfo_t *) info->args[0], (int) info->args[1], (struct addrinfo *) info->args[2]);
      break;
    case UV_GETNAMEINFO_CB:
      ((uv_getnameinfo_cb) info->cb)((uv_getnameinfo_t *) info->args[0], (int) info->args[1], (const char *) info->args[2], (const char *) info->args[3]);
      break;
    case UV_FS_EVENT_CB:
      ((uv_fs_event_cb) info->cb)((uv_fs_event_t *) info->args[0], (const char *) info->args[1], (int) info->args[2], (int) info->args[3]);
      break;
    case UV_FS_POLL_CB:
      ((uv_fs_poll_cb) info->cb)((uv_fs_poll_t *) info->args[0], (int) info->args[1], (const uv_stat_t *) info->args[2], (const uv_stat_t *) info->args[3]);
      break;
    case UV_SIGNAL_CB:
      ((uv_signal_cb) info->cb)((uv_signal_t *) info->args[0], (int) info->args[1]);
      break;
    case UV_UDP_SEND_CB:
      ((uv_udp_send_cb) info->cb)((uv_udp_send_t *) info->args[0], (int) info->args[1]);
      break;
    case UV_UDP_RECV_CB:
      /* Peer info is in the sockaddr_storage of info->args[3]. */
      ((uv_udp_recv_cb) info->cb)((uv_udp_t *) info->args[0], (ssize_t) info->args[1], (const uv_buf_t *) info->args[2], (const struct sockaddr *) info->args[3], (unsigned) info->args[4]);
      break;
    case UV_THREAD_CB:
      ((uv_thread_cb) info->cb)((void *) info->args[0]);
      break;

    /* include/uv-unix.h */
    case UV__IO_CB:
      ((uv__io_cb) info->cb)((struct uv_loop_s *) info->args[0], (struct uv__io_s *) info->args[1], (unsigned int) info->args[2]);
      break;
    case UV__ASYNC_CB:
      ((uv__async_cb) info->cb)((struct uv_loop_s *) info->args[0], (struct uv__async *) info->args[1], (unsigned int) info->args[2]);
      break;

    /* include/uv-threadpool.h */
    case UV__WORK_WORK:
      ((uv__work_work_cb) info->cb)((struct uv__work *) info->args[0]);
      break;
    case UV__WORK_DONE:
      ((uv__work_done_cb) info->cb)((struct uv__work *) info->args[0], (int) info->args[1]);
      break;

    default:
      assert(!"cbn_execute_callback: ERROR, unsupported type");
  }
}

#if 0
/* Debugging routine. To combat the annoying <optimized out> in gdb. */
static char * cbn_to_string (callback_node_t *cbn, char *buf, int size)
{
  assert(buf != NULL);

  if (cbn == NULL)
    snprintf(buf, size, "cbn %p", cbn);
  else
  {
    snprintf(buf, size, "cbn %p (info %p", (void *) cbn, (void *) cbn->info); 
    if (cbn->info)
      snprintf(buf + strlen(buf), size - strlen(buf), " type %s)", callback_type_to_string(cbn->info->type));
    else
      snprintf(buf + strlen(buf), size - strlen(buf), ")");
    snprintf(buf + strlen(buf), size - strlen(buf), " physical_level %i physical_parent %p logical_level %i logical_parent %p true_client_id %i id %i executing_thread %i",
      cbn->physical_level, (void *) cbn->physical_parent, cbn->logical_level, (void *) cbn->logical_parent, cbn->true_client_id, cbn->id, cbn->executing_thread);
  }

  return buf;
}
#endif

#if 0
/* Debugging routine. To combat the annoying <optimized out> in gdb. */
static void cbn_print (callback_node_t *cbn)
{
  char buf[1024];
  mylog(LOG_MAIN, 1, cbn_to_string(cbn, buf, 1024));
}
#endif

#if 0
/* Print CBN and all of its ancestors. */
static void cbn_print_ancestry (callback_node_t *cbn)
{
  char buf[1024];
  callback_node_t *parent;

  mylog(LOG_MAIN, 9, "cbn_print_ancestory: node %s\n", cbn_to_string(cbn, buf, 1024));

  parent = cbn->physical_parent;
  while (parent)
  {
    mylog(LOG_MAIN, 9, "cbn_print_ancestory: physical parent %s\n", cbn_to_string(parent, buf, 1024));
    parent = parent->physical_parent;
  }

  parent = cbn->logical_parent;
  while (parent)
  {
    mylog(LOG_MAIN, 9, "cbn_print_ancestory: logical parent %s\n", cbn_to_string(parent, buf, 1024));
    parent = parent->logical_parent;
  }
}
#endif

/* Walk the CBN tree rooted at CBN, applying F to each node with args (node, AUX). 
   F is applied to parents before children. */
static void cbn_walk (callback_node_t *cbn, enum callback_tree_type tree_type, void (*f)(callback_node_t *, void *), void *aux)
{
  struct list *children;
  struct list_elem *e;
  callback_node_t *child;

  assert(cbn != NULL);

  (*f)(cbn, aux);

  children = (tree_type == CALLBACK_TREE_PHYSICAL) ? cbn->physical_children : cbn->logical_children;
  assert(children != NULL);

  for (e = list_begin(children); e != list_end(children); e = list_next(e))
  {
    child = (tree_type == CALLBACK_TREE_PHYSICAL ?
             list_entry(e, callback_node_t, physical_child_elem) :
             list_entry(e, callback_node_t, logical_child_elem)
            );
    cbn_walk(child, tree_type, f, aux);
  }
}

#if 0
/* Function for cbn_walk. Set the level of each node to parent's level + 1. 
   CBN and all of its children must have their PARENT field set. */
static void cbn_update_level (callback_node_t *cbn, void *aux)
{
  assert(cbn != NULL);
  assert(cbn->parent != NULL);
  cbn->level = cbn->parent->level + 1;
}
#endif

/* RES = STOP - START. 
   STOP must be after START. */
static void timespec_sub (const struct timespec *stop, const struct timespec *start, struct timespec *res)
{
  assert(start != NULL);
  assert(stop != NULL);
  assert(res != NULL);

  /* START < STOP-> */
  assert(start->tv_sec < stop->tv_sec || start->tv_nsec <= stop->tv_nsec);

  if (stop->tv_nsec < start->tv_nsec)
  {
    /* Borrow. */
    res->tv_nsec = 1000000000 + stop->tv_nsec - start->tv_nsec; /* Inline to avoid overflow. */
    res->tv_sec = stop->tv_sec - start->tv_sec - 1;
  }
  else
  {
    res->tv_nsec = stop->tv_nsec - start->tv_nsec;
    res->tv_sec = stop->tv_sec - start->tv_sec;
  }

  return;
}

/* Associate LCBN with CALLBACK_TYPE in CB_TYPE_TO_LCBN. */
void lcbn_register (struct map *cb_type_to_lcbn, enum callback_type cb_type, lcbn_t *lcbn)
{
  assert(cb_type_to_lcbn != NULL);
  assert(lcbn != NULL);
  map_insert(cb_type_to_lcbn, cb_type, lcbn);
}

/* Return the LCBN stored in CB_TYPE_TO_LCBN for CALLBACK_TYPE. */
lcbn_t * lcbn_get (struct map *cb_type_to_lcbn, enum callback_type cb_type)
{
  int found;
  assert(cb_type_to_lcbn);
  return (lcbn_t *) map_lookup(cb_type_to_lcbn, cb_type, &found);
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
    mylog(LOG_MAIN, 5, "lcbn_current_set: Current LCBN is %p (type %s)\n", (void *) lcbn, callback_type_to_string(lcbn->cb_type));
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

static unsigned lcbn_next_exec_id (void)
{
  lcbn_global_exec_counter++;
  return lcbn_global_exec_counter - 1;
}

static unsigned lcbn_next_reg_id (void)
{
  lcbn_global_reg_counter++;
  return lcbn_global_reg_counter - 1;
}

/* TODO varargs */
struct callback_info * cbi_create_0 (enum callback_type type, any_func cb)
{
  struct callback_info *cbi = uv__malloc(sizeof *cbi);
  assert(cbi);
  memset(cbi, 0, sizeof(*cbi));                   

  cbi->type = type;                           
  cbi->cb = cb;                                    

  mylog(LOG_MAIN, 5, "cbi_create_0: cbi %p type %s cb %p\n", cbi, callback_type_to_string(type), cb);
  return cbi;
}

struct callback_info * cbi_create_1 (enum callback_type type, any_func cb, long arg0)
{
  struct callback_info *cbi = cbi_create_0(type, cb);
  cbi->args[0] = arg0;
  return cbi;
}

struct callback_info * cbi_create_2 (enum callback_type type, any_func cb, long arg0, long arg1)
{
  struct callback_info *cbi = cbi_create_1(type, cb, arg0);
  cbi->args[1] = arg1;
  return cbi;
}

struct callback_info * cbi_create_3 (enum callback_type type, any_func cb, long arg0, long arg1, long arg2)
{
  struct callback_info *cbi = cbi_create_2(type, cb, arg0, arg1);
  cbi->args[2] = arg2;
  return cbi;
}

struct callback_info * cbi_create_4 (enum callback_type type, any_func cb, long arg0, long arg1, long arg2, long arg3)
{
  struct callback_info *cbi = cbi_create_3(type, cb, arg0, arg1, arg2);
  cbi->args[3] = arg3;
  return cbi;
}

struct callback_info * cbi_create_5 (enum callback_type type, any_func cb, long arg0, long arg1, long arg2, long arg3, long arg4)
{
  struct callback_info *cbi = cbi_create_4(type, cb, arg0, arg1, arg2, arg3);
  cbi->args[4] = arg4;
  return cbi;
}

struct callback_node * INVOKE_CALLBACK_0 (enum callback_type type, any_func cb)
{
  struct callback_info *cbi = cbi_create_0(type, cb); 
  mylog(LOG_MAIN, 5, "INVOKE_CALLBACK_0: cbi %p type %s\n", cbi, callback_type_to_string(cbi->type));
  return invoke_callback(cbi);
}

struct callback_node * INVOKE_CALLBACK_1 (enum callback_type type, any_func cb, long arg0)
{
  struct callback_info *cbi = cbi_create_1(type, cb, arg0); 
  mylog(LOG_MAIN, 5, "INVOKE_CALLBACK_1: cbi %p type %s\n", cbi, callback_type_to_string(cbi->type));
  return invoke_callback(cbi);
}

struct callback_node * INVOKE_CALLBACK_2 (enum callback_type type, any_func cb, long arg0, long arg1)
{
  struct callback_info *cbi = cbi_create_2(type, cb, arg0, arg1); 
  mylog(LOG_MAIN, 5, "INVOKE_CALLBACK_2: cbi %p type %s\n", cbi, callback_type_to_string(cbi->type));
  return invoke_callback(cbi);
}

struct callback_node * INVOKE_CALLBACK_3 (enum callback_type type, any_func cb, long arg0, long arg1, long arg2)
{
  struct callback_info *cbi = cbi_create_3(type, cb, arg0, arg1, arg2); 
  mylog(LOG_MAIN, 5, "INVOKE_CALLBACK_3: cbi %p type %s\n", cbi, callback_type_to_string(cbi->type));
  return invoke_callback(cbi);
}

struct callback_node * INVOKE_CALLBACK_4 (enum callback_type type, any_func cb, long arg0, long arg1, long arg2, long arg3)
{
  struct callback_info *cbi = cbi_create_4(type, cb, arg0, arg1, arg2, arg3); 
  mylog(LOG_MAIN, 5, "INVOKE_CALLBACK_4: cbi %p type %s\n", cbi, callback_type_to_string(cbi->type));
  return invoke_callback(cbi);
}

struct callback_node * INVOKE_CALLBACK_5 (enum callback_type type, any_func cb, long arg0, long arg1, long arg2, long arg3, long arg4)
{
  struct callback_info *cbi = cbi_create_5(type, cb, arg0, arg1, arg2, arg3, arg4);
  mylog(LOG_MAIN, 5, "INVOKE_CALLBACK_5: cbi %p type %s\n", cbi, callback_type_to_string(cbi->type));
  return invoke_callback(cbi);
}
