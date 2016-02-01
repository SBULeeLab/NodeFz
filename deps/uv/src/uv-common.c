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

#ifdef UNIFIED_CALLBACK
  uv__register_callback((void *) cb, UV_CONNECT_CB);
#endif

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

#ifdef UNIFIED_CALLBACK
  uv__register_callback((void *) send_cb, UV_UDP_SEND_CB);
#endif

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
      uv__register_callback((void *) alloc_cb, UV_ALLOC_CB);
      uv__register_callback((void *) recv_cb, UV_UDP_RECV_CB);
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

#ifdef UNIFIED_CALLBACK
  uv__register_callback((void *) walk_cb, UV_WALK_CB);
#endif

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
  printf("%s %s gen %i process %i: ", indents, now_s, generation, my_pid);
#else
  my_pid = getpid ();
  printf("%s process %i: ", now_s, my_pid);
#endif

  va_start(args, format);
  vprintf(format, args);
  va_end(args);

  fflush(NULL);
}

/* Static functions added for unified callback. */
/* For convenience with addr_getnameinfo. */
struct uv_nameinfo
{
  char host[INET6_ADDRSTRLEN];
  char service[64];
};

/* Callback nodes. */
static struct callback_node * cbn_create (void);
static void cbn_start (struct callback_node *cbn);
static void cbn_stop (struct callback_node *cbn);

static struct callback_node * cbn_determine_parent (struct callback_node *cbn);
static int cbn_have_peer_info (const struct callback_node *cbn);
static int cbn_is_root (const struct callback_node *cbn);
static struct callback_node * cbn_get_root (struct callback_node *cbn);
static void cbn_color_tree (struct callback_node *cbn, int client_id);
static int cbn_have_peer_info (const struct callback_node *cbn);
static int cbn_is_async (const struct callback_node *cbn);
static int cbn_is_active (const struct callback_node *cbn);
static int cbn_discover_id (struct callback_node *cbn);

/* Peer addresses and clients. */
static int look_for_peer_info (const struct callback_info *cbi, struct callback_node *cbn, uv_handle_t **uvhtPP);
static void get_peer_info (uv_handle_t *client, struct sockaddr_storage *peer_info);
static int get_client_id (struct sockaddr_storage *addr);

static void addr_getnameinfo (struct sockaddr_storage *addr, struct uv_nameinfo *nameinfo);
static unsigned addr_calc_hash (struct sockaddr_storage *addr);

/* Misc. */
static void init_unified_callback (void);
static int is_zeros (void *buffer, size_t size);
static void print_buf (void *buffer, size_t size);
static void timespec_sub (const struct timespec *stop, const struct timespec *start, struct timespec *res);

/* Global time. */
static void mark_global_start (void);
static void calc_relative_time (struct timespec *start, struct timespec *res);

/* Output. */
static void dump_callback_node_gv (int fd, struct callback_node *cbn);
static void dump_callback_node (int fd, struct callback_node *cbn, char *prefix, int do_indent);
static void dump_callback_tree_gv (int fd, struct callback_node *cbn);
#if 0
static void dump_callback_tree (int fd, struct callback_node *cbn);
#endif

/* Variables for tracking the unified callback queue. */
static int unified_callback_initialized = 0;

static struct list root_list;
static struct list global_order_list;

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

/* Printing nodes with colors based on client ID. */
#define RESERVED_IDS 4
enum reserved_id
{
  ID_NODEJS_INTERNAL = -4, /* Callbacks originating internally -- node. */
  ID_LIBUV_INTERNAL,       /* Callbacks originating internally -- libuv. */
  ID_INITIAL_STACK,        /* Callbacks registered during the initial stack. */
  ID_UNKNOWN               /* Unknown origin -- e.g. close() prior to connection being completed? */
};

/* graphviz notation: node [style="filled" colorscheme="SVG" color="aliceblue"] */
char *graphviz_colorscheme = "SVG";
char *graphviz_colors[] = { "aliceblue", "antiquewhite", "aqua", "aquamarine", "azure", "beige", "bisque", "black", "blanchedalmond", "blue", "blueviolet", "brown", "burlywood", "cadetblue", "chartreuse", "chocolate", "coral", "cornflowerblue", "cornsilk", "crimson", "cyan", "darkblue", "darkcyan", "darkgoldenrod", "darkgray", "darkgreen", "darkgrey", "darkkhaki", "darkmagenta", "darkolivegreen", "darkorange", "darkorchid", "darkred", "darksalmon", "darkseagreen", "darkslateblue", "darkslategray", "darkslategrey", "darkturquoise", "darkviolet", "deeppink", "deepskyblue", "dimgray", "dimgrey", "dodgerblue", "firebrick", "floralwhite", "forestgreen", "fuchsia", "gainsboro", "ghostwhite", "gold", "goldenrod", "gray", "grey", "green", "greenyellow", "honeydew", "hotpink", "indianred", "indigo", "ivory", "khaki", "lavender", "lavenderblush", "lawngreen", "lemonchiffon", "lightblue", "lightcoral", "lightcyan", "lightgoldenrodyellow", "lightgray", "lightgreen", "lightgrey", "lightpink", "lightsalmon", "lightseagreen", "lightskyblue", "lightslategray", "lightslategrey", "lightsteelblue", "lightyellow", "lime", "limegreen", "linen", "magenta", "maroon", "mediumaquamarine", "mediumblue", "mediumorchid", "mediumpurple", "mediumseagreen", "mediumslateblue", "mediumspringgreen", "mediumturquoise", "mediumvioletred", "midnightblue", "mintcream", "mistyrose", "moccasin", "navajowhite", "navy", "oldlace", "olive", "olivedrab", "orange", "orangered", "orchid", "palegoldenrod", "palegreen", "paleturquoise", "palevioletred", "papayawhip", "peachpuff", "peru", "pink", "plum", "powderblue", "purple", "red", "rosybrown", "royalblue", "saddlebrown", "salmon", "sandybrown", "seagreen", "seashell", "sienna", "silver", "skyblue", "slateblue", "slategray", "slategrey", "snow", "springgreen", "steelblue", "tan", "teal", "thistle", "tomato", "turquoise", "violet", "wheat", "white", "whitesmoke", "yellow", "yellowgreen" }; /* ~150 colors. */

/* Returns 1 if CBN is a root node, else 0. */
static int cbn_is_root (const struct callback_node *cbn)
{
  assert(cbn != NULL);
  return (cbn->parent == NULL);
}

/* Returns 1 if we know the peer info of CBN already, else 0. */
static int cbn_have_peer_info (const struct callback_node *cbn)
{
  assert(cbn != NULL);
  assert(cbn->peer_info != NULL);
  return !is_zeros(cbn->peer_info, sizeof *cbn->peer_info);
}

/* Mark CBN as active and update its start and relative_start fields. */
static void cbn_start (struct callback_node *cbn)
{
  assert(cbn != NULL);
  assert(!cbn->active);

  cbn->active = 1;

  assert(clock_gettime(CLOCK_MONOTONIC, &cbn->start) == 0);
  calc_relative_time(&cbn->start, &cbn->relative_start);
}

/* Mark CBN as inactive and set its stop and duration fields. */
static void cbn_stop (struct callback_node *cbn)
{
  struct timespec diff;

  assert(cbn != NULL);
  assert(cbn->active);

  cbn->active = 0;

  assert(clock_gettime (CLOCK_MONOTONIC, &cbn->stop) == 0);
  /* Must end after it began. */
  assert(cbn->start.tv_sec < cbn->stop.tv_sec || cbn->start.tv_nsec <= cbn->stop.tv_nsec);
  timespec_sub(&cbn->stop, &cbn->start, &cbn->duration);
}

/* Determine the parent of CBN. 
   CBN->info must be initialized. 
   CBN->parent must not be known. */ 
static struct callback_node * cbn_determine_parent (struct callback_node *cbn)
{
  struct callback_node *parent;
  struct callback_info *info;
  int sync_cb, async_cb;

  assert(cbn != NULL);
  assert(cbn->info != NULL);
  assert(cbn->parent == NULL);

  info = cbn->info;
  async_cb = cbn_is_async(cbn);
  sync_cb = !async_cb;

  if (sync_cb)
    /* CBN is synchronous, so it's either a root or a child of an active callback. */
    parent = current_callback_node_get();
  else
  {
    /* CBN is asynchronous, extract the cbn at time of registration (if any? So far I think there must be one). */
    switch (info->type)
    {
      case UV__WORK_WORK:
        /* See src/threadpool.c */
        parent = ((struct uv__work *) info->args[0])->parent;
        /* Make ourselves the direct parent of the UV__WORK_DONE that follows us. */
        ((struct uv__work *) info->args[0])->parent = cbn;
        break;
      case UV__WORK_DONE:
        /* See src/threadpool.c
           Parent is the UV__WORK_WORK that preceded us. */
        parent = ((struct uv__work *) info->args[0])->parent;
        assert(parent->info->type == UV__WORK_WORK);
        break;
      case UV_TIMER_CB:
        /* See src/unix/timer.c
           A uv_timer_t* is a uv_handle_t. 
           TODO Might there not be a parent if the timer was registered by 
             internal Node code (e.g. for garbage collection)? */
        parent = ((uv_timer_t *) info->args[0])->parent;
        break;
      /* See src/unix/loop-watcher.c */
      case UV_PREPARE_CB:
      case UV_CHECK_CB:
      case UV_IDLE_CB:
        parent = ((uv_handle_t *) info->args[0])->parent;
        break;
      default:
        NOT_REACHED;
    }
    assert(parent != NULL);
  }

  cbn->parent = parent;
  return parent;
}

/* Return non-zero if CBN is async, zero else.
   CBN->info must be initialized. */
static int cbn_is_async (const struct callback_node *cbn)
{
  int async;
  enum callback_type type;

  assert(cbn != NULL);
  assert(cbn->info != NULL);

  type = cbn->info->type;
  async = (type == UV__WORK_WORK || type == UV__WORK_DONE || 
           type == UV_TIMER_CB || 
           type == UV_PREPARE_CB || type == UV_CHECK_CB || type == UV_IDLE_CB);
  return async;
}

/* Returns non-zero if CBN is active, zero else. */
static int cbn_is_active (const struct callback_node *cbn)
{
  assert(cbn != NULL);
  return cbn->active;
}

/* Inherit the appropriate fields from CBN->parent. */
static void cbn_inherit (struct callback_node *cbn)
{
  struct callback_node *parent;

  assert(cbn != NULL);
  assert(cbn->parent != NULL);
  parent = cbn->parent;

  cbn->level = parent->level + 1;
  /* Inherit client ID. */
  cbn->orig_client_id = parent->true_client_id; /* If parent started unknown and was colored, we inherit the color. */
  cbn->true_client_id = parent->true_client_id;
  cbn->discovered_client_id = 0;
  /* Inherit parent's peer_info. */
  free(cbn->peer_info);
  cbn->peer_info = parent->peer_info;
  assert(cbn->peer_info != NULL);
}

/* Return the root of the callback-node tree containing CBN. */
static struct callback_node * cbn_get_root (struct callback_node *cbn)
{
  assert(cbn != NULL);
  while (!cbn_is_root(cbn))
    cbn = cbn->parent;
  return cbn;
}

/* The color (i.e. CLIENT_ID) of the tree rooted at CBN has been discovered.
   Color CBN and all of its descendants this color. */
static void cbn_color_tree (struct callback_node *cbn, int client_id)
{
  struct callback_node *child;
  struct list_elem *e;
  assert(cbn != NULL);

  /* If CBN is already colored, its descendants must also be colored due to inheritance. */
  if (cbn->true_client_id == client_id)
    return;

  /* Color CBN. */
  cbn->true_client_id = client_id;

  /* Color descendants. */
  for (e = list_begin(&cbn->children); e != list_end(&cbn->children); e = list_next(e))
  {
    child = list_entry (e, struct callback_node, child_elem);
    cbn_color_tree(child, client_id);
  }
}

/* Sets the current callback node to CBN. */
static struct callback_node *current_callback_node = NULL;
void current_callback_node_set (struct callback_node *cbn)
{
  current_callback_node = cbn;
  if (cbn == NULL)
    mylog ("current_callback_node_set: Next callback will be a root\n");
}

/* Retrieves the current callback node, or NULL if no such node. */
struct callback_node *current_callback_node_get (void)
{
  return current_callback_node;
}

/* Returns a new CBN. 
   id=-1, peer_info is allocated, {orig,true}_client_id=ID_UNKNOWN. 
   All other fields are NULL or 0. */
static struct callback_node * cbn_create (void)
{
  struct callback_node *cbn;

  cbn = malloc(sizeof *cbn);
  assert(cbn != NULL);
  memset(cbn, 0, sizeof *cbn);

  cbn->info = NULL;
  cbn->level = 0;
  cbn->parent = NULL;

  cbn->orig_client_id = ID_UNKNOWN;
  cbn->true_client_id = ID_UNKNOWN;
  cbn->discovered_client_id = 0;

  cbn->active = 0;
  list_init(&cbn->children);

  cbn->peer_info = (struct sockaddr_storage *) malloc(sizeof *cbn->peer_info);
  assert(cbn->peer_info != NULL);
  memset(cbn->peer_info, 0, sizeof *cbn->peer_info);

  cbn->id = -1;
  return cbn;
}

/* Code in support of tracking the initial stack. */

/* Returns the CBN associated with the initial stack. */
struct callback_node *init_stack_cbn = NULL;
struct callback_node * get_init_stack_callback_node (void)
{
  if (!unified_callback_initialized)
    init_unified_callback();

  if (init_stack_cbn == NULL)
  {
    init_stack_cbn = cbn_create();
    assert(init_stack_cbn != NULL);

    init_stack_cbn->orig_client_id = ID_INITIAL_STACK;
    init_stack_cbn->true_client_id = ID_INITIAL_STACK;
    init_stack_cbn->discovered_client_id = 1;

    list_init(&init_stack_cbn->children);

    init_stack_cbn->peer_info = (struct sockaddr_storage *) malloc(sizeof *init_stack_cbn->peer_info);
    assert(init_stack_cbn->peer_info != NULL);
    memset(init_stack_cbn->peer_info, 0, sizeof *init_stack_cbn->peer_info);

    /* Add to global order list and to root list. */
    list_lock(&global_order_list);
    list_lock(&root_list);

    list_push_back(&global_order_list, &init_stack_cbn->global_order_elem);
    init_stack_cbn->id = list_size (&global_order_list);

    list_push_back(&root_list, &init_stack_cbn->root_elem); 

    list_unlock(&global_order_list);
    list_unlock(&root_list);
  }

  return init_stack_cbn;
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
    client_id = (int) entry;
    printf("Existing client\n");
  }
  else
  {
    client_id = next_client_id;
    map_insert(id_to_peer_info, client_id, (void *) addr);
    map_insert(peer_info_to_id, addr_hash, (void *) client_id);
    printf("New client\n");
  }

  /* Print the client's info. */
  addr_getnameinfo(addr, &nameinfo);
  printf("get_peer_info: Client: %i -> %i -> %s:%s (id %i)\n", client_id, addr_hash, nameinfo.host, nameinfo.service, client_id);

  return client_id;
}

/* Initialize the data structures for the unified callback code. */
static void init_unified_callback (void)
{
  if (unified_callback_initialized)
    return;

  printf("DEBUG: Testing list\n");
  list_UT();
  printf("DEBUG: Testing map\n");
  map_UT();

  list_init(&global_order_list);
  list_init(&root_list);

  peer_info_to_id = map_create();
  assert(peer_info_to_id != NULL);

  id_to_peer_info = map_create();
  assert(id_to_peer_info != NULL);

  callback_to_origin_map = map_create();
  assert(callback_to_origin_map != NULL);

  signal(SIGUSR1, dump_callback_global_order_sighandler);
  signal(SIGUSR2, dump_callback_trees_sighandler);
  signal(SIGINT, dump_all_trees_and_exit_sighandler);

  mark_global_start();

  unified_callback_initialized = 1;
}

/* Look for the peer info for CBI, and update CBN->peer_info if possible.
   Returns 1 if we got the peer info, else 0. 
   Do not call this if we already know the peer info. */
static int look_for_peer_info (const struct callback_info *cbi, struct callback_node *cbn, uv_handle_t **uvhtPP)
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
    /* TODO Don't check if status is UV_ECANCELED -- see documentation for uv_close. */
    case UV_CONNECT_CB:
      *uvhtPP = (uv_handle_t *) ((uv_connect_t *) cbi->args[0])->handle;
      break;
    case UV_CONNECTION_CB:
      *uvhtPP = (uv_handle_t *) cbi->args[0];
      break;
    case UV_READ_CB:
      *uvhtPP = (uv_handle_t *) cbi->args[0];
      break;
    case UV_UDP_RECV_CB:
      /* UDP already knows the associated peer. cbi->args[3] is a struct sockaddr_storage * (see uv__udp_recvmsg). */
      memcpy(cbn->peer_info, (const struct sockaddr_storage *) cbi->args[3], sizeof *cbn->peer_info);
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

/* Invoke the callback described by CBI. 
   Returns the CBN allocated for the callback. */
struct callback_node * invoke_callback (struct callback_info *cbi)
{
  struct callback_node *orig_cbn, *new_cbn, *root;
  uv_handle_t *uvht; 
  char handle_type[64];
  int async_cb, sync_cb, got_peer_info;

  assert (cbi != NULL);  

  /* Potentially racey (concurrently called from thread pool)? 
     Seems like that would violate how libuv is supposed to work though. */
  if (!unified_callback_initialized)
    init_unified_callback();
  
  new_cbn = cbn_create();
  new_cbn->info = cbi;
  orig_cbn = cbn_determine_parent(new_cbn);

  async_cb = cbn_is_async(new_cbn);
  sync_cb = !async_cb;

  if (orig_cbn)
    cbn_inherit(new_cbn);
  else
  {
    /* Root. */
    new_cbn->level = 0;
    new_cbn->parent = NULL;

    /* Unknown until (possibly) later on in the function. */
    new_cbn->orig_client_id = ID_UNKNOWN;
    new_cbn->true_client_id = ID_UNKNOWN;
    new_cbn->discovered_client_id = 0;
  }

  if (new_cbn->parent)
  {
    list_lock(&new_cbn->parent->children);
    list_push_back(&new_cbn->parent->children, &new_cbn->child_elem); 
    list_unlock(&new_cbn->parent->children);
  }
  else
  {
    list_lock(&root_list);
    list_push_back(&root_list, &new_cbn->root_elem); 
    list_unlock(&root_list);
  }

  cbn_start(new_cbn);
  list_init(&new_cbn->children);

  list_lock(&global_order_list);
  list_push_back(&global_order_list, &new_cbn->global_order_elem);
  new_cbn->id = list_size (&global_order_list);
  list_unlock(&global_order_list);

  if (!new_cbn->parent && cbi->type == UV__WORK_WORK)
    /* Not clear how this can happen. Probably an error. */
    NOT_REACHED;

  mylog ("invoke_callback: getting ready to invoke callback_node %i: %p cbi %p type %s cb %p level %i parent %p\n",
    new_cbn->id, new_cbn, (void *) cbi, callback_type_to_string(cbi->type), cbi->cb, new_cbn->level, new_cbn->parent);
  assert ((new_cbn->level == 0 && new_cbn->parent == NULL)
       || (0 < new_cbn->level && new_cbn->parent != NULL));

  /* If UV__WORK_WORK, being called by the threadpool, resulting in an unclear current_callback_node. 
     We resolve the issue by simply not tracking WORK_WORK CBs as active. 
     TODO We could perhaps use a map to track a per-tid current_callback_node. */
  if (cbi->type != UV__WORK_WORK)
    current_callback_node_set (new_cbn);

  /* Attempt to discover the client ID associated with this callback. */
  if (new_cbn->true_client_id == ID_UNKNOWN)
    cbn_discover_id(new_cbn);

  uvht = NULL;
  snprintf(handle_type, 64, "(no handle type)");
  /* Invoke the callback. */
  switch (cbi->type)
  {
    /* include/uv.h */
    case UV_ALLOC_CB:
      uvht = (uv_handle_t *) cbi->args[0];
      strncpy(handle_type, "uv_handle_t", 64); 
      cbi->cb ((uv_handle_t *) cbi->args[0], (size_t) cbi->args[1], (uv_buf_t *) cbi->args[2]); /* uv_handle_t */
      break;
    case UV_READ_CB:
      uvht = (uv_handle_t *) cbi->args[0];
      strncpy(handle_type, "uv_stream_t", 64); 
      cbi->cb ((uv_stream_t *) cbi->args[0], (ssize_t) cbi->args[1], (const uv_buf_t *) cbi->args[2]); /* uv_handle_t */
      break;
    case UV_WRITE_CB:
      /* Pointer to the stream where this write request is running. */
      uvht = (uv_handle_t *) ((uv_write_t *) cbi->args[0])->handle;
      strncpy(handle_type, "uv_stream_t", 64); 
      cbi->cb ((uv_write_t *) cbi->args[0], (int) cbi->args[1]); /* uv_write_t, has pointer to two uv_stream_t's (uv_handle_t)  */
      break;
    case UV_CONNECT_CB:
      uvht = (uv_handle_t *) ((uv_connect_t *) cbi->args[0])->handle;
      strncpy(handle_type, "uv_stream_t", 64); 
      cbi->cb ((uv_connect_t *) cbi->args[0], (int) cbi->args[1]); /* uv_req_t, has pointer to uv_stream_t (uv_handle_t) */
      break;
    case UV_SHUTDOWN_CB:
      uvht = (uv_handle_t *) ((uv_shutdown_t *) cbi->args[0])->handle;
      strncpy(handle_type, "uv_stream_t", 64); 
      cbi->cb ((uv_shutdown_t *) cbi->args[0], (int) cbi->args[1]); /* uv_req_t, has pointer to uv_stream_t (uv_handle_t) */
      break;
    case UV_CONNECTION_CB:
      uvht = (uv_handle_t *) cbi->args[0];
      strncpy(handle_type, "uv_stream_t", 64); 
      cbi->cb ((uv_stream_t *) cbi->args[0], (int) cbi->args[1]); /* uv_handle_t */
      break;
    case UV_CLOSE_CB:
      uvht = (uv_handle_t *) cbi->args[0];
      strncpy(handle_type, "uv_handle_t", 64); 
      cbi->cb ((uv_handle_t *) cbi->args[0]); /* uv_handle_t */
      break;
    case UV_POLL_CB:
      uvht = (uv_handle_t *) cbi->args[0];
      strncpy(handle_type, "uv_poll_t", 64); 
      cbi->cb ((uv_poll_t *) cbi->args[0], (int) cbi->args[1], (int) cbi->args[2]); /* uv_handle_t */
      break;
    case UV_TIMER_CB:
      uvht = (uv_handle_t *) cbi->args[0];
      strncpy(handle_type, "uv_timer_t", 64); 
      cbi->cb ((uv_timer_t *) cbi->args[0]); /* uv_handle_t */
      break;
    case UV_ASYNC_CB:
      uvht = (uv_handle_t *) cbi->args[0];
      strncpy(handle_type, "uv_async_t", 64); 
      cbi->cb ((uv_async_t *) cbi->args[0]); /* uv_handle_t */
      break;
    case UV_PREPARE_CB:
      uvht = (uv_handle_t *) cbi->args[0];
      strncpy(handle_type, "uv_prepare_t", 64); 
      cbi->cb ((uv_prepare_t *) cbi->args[0]); /* uv_handle_t */
      break;
    case UV_CHECK_CB:
      uvht = (uv_handle_t *) cbi->args[0];
      strncpy(handle_type, "uv_check_t", 64); 
      cbi->cb ((uv_check_t *) cbi->args[0]); /* uv_handle_t */
      break;
    case UV_IDLE_CB:
      uvht = (uv_handle_t *) cbi->args[0];
      strncpy(handle_type, "uv_idle_t", 64); 
      cbi->cb ((uv_idle_t *) cbi->args[0]); /* uv_handle_t */
      break;
    case UV_EXIT_CB:
      uvht = (uv_handle_t *) cbi->args[0];
      strncpy(handle_type, "uv_process_t", 64); 
      cbi->cb ((uv_process_t *) cbi->args[0], (int64_t) cbi->args[1], (int) cbi->args[2]); /* uv_handle_t */
      break;
    case UV_WALK_CB:
      uvht = (uv_handle_t *) cbi->args[0];
      strncpy(handle_type, "uv_handle_t", 64); 
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
      uvht = (uv_handle_t *) cbi->args[0];
      strncpy(handle_type, "uv_fs_event_t", 64); 
      cbi->cb ((uv_fs_event_t *) cbi->args[0], (const char *) cbi->args[1], (int) cbi->args[2], (int) cbi->args[3]); /* uv_handle_t */
      break;
    case UV_FS_POLL_CB:
      uvht = (uv_handle_t *) cbi->args[0];
      strncpy(handle_type, "uv_fs_poll_t", 64); 
      cbi->cb ((uv_fs_poll_t *) cbi->args[0], (int) cbi->args[1], (const uv_stat_t *) cbi->args[2], (const uv_stat_t *) cbi->args[3]); /* uv_handle_t */
      break;
    case UV_SIGNAL_CB:
      uvht = (uv_handle_t *) cbi->args[0];
      strncpy(handle_type, "uv_signal_t", 64); 
      cbi->cb ((uv_signal_t *) cbi->args[0], (int) cbi->args[1]); /* uv_handle_t */
      break;
    case UV_UDP_SEND_CB:
      cbi->cb ((uv_udp_send_t *) cbi->args[0], (int) cbi->args[1]); /* uv_req_t, has pointer to uv_udp_t (uv_handle_t) */
      break;
    case UV_UDP_RECV_CB:
      uvht = (uv_handle_t *) cbi->args[0];
      strncpy(handle_type, "uv_udp_t", 64); 
      /* Peer info is in the sockaddr_storage of cbi->args[3]. */
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
      NOT_REACHED;
  }

  mylog ("handle type <%s>\n", handle_type);

  mylog ("invoke_callback: Done with callback_node %p cbi %p uvht <%p>\n",
    new_cbn, new_cbn->info, uvht); 
  cbn_stop(new_cbn);

  if (sync_cb)
    /* Synchronous CB has finished, and the orig_cb is still active. */
    current_callback_node_set (orig_cbn);
  else
  {
    /* Async CB has finished. In general there is no current CB.
       However, if this callback was potentially concurrent with other callbacks (i.e. a threadpool WORK callback),
         then we shouldn't modify the current CB. 
         In this case we correspondingly did not set this node as the current CBN above. */
    if (cbi->type != UV__WORK_WORK)
      current_callback_node_set(NULL);
  }

  return new_cbn;
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

/* Prints callback node CBN to FD in graphviz format. 
   The caller must have prepared the outer graph declaration; we just
   print node/edge info in graphviz format. 
   TODO Increase the amount of information embedded here? */
static void dump_callback_node_gv (int fd, struct callback_node *cbn)
{
  char client_info[256];
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
      snprintf(client_info, 256, "unknown??");
      break;
    default:
      assert(cbn_have_peer_info(cbn));
      addr_getnameinfo(cbn->peer_info, &nameinfo);
      snprintf(client_info, 256, "%s [port %s]", nameinfo.host, nameinfo.service);
  }

  /* Example listing:
    1 [label="client 12\ntype UV_ALLOC_CB\nactive 0\nstart 1 (s) duration 10s50ns\nID 1..."];
  */
  dprintf(fd, "    %i [label=\"client %i\\ntype %s\\nactive %i\\nstart %is %lins (since beginning) duration %is %lins\\nID %i\\nclient ID %i (%s)\" style=\"filled\" colorscheme=\"%s\" color=\"%s\"];\n",
    cbn->id, cbn->true_client_id, cbn->info ? callback_type_to_string (cbn->info->type) : "<unknown>", cbn->active, cbn->relative_start.tv_sec, cbn->relative_start.tv_nsec, cbn->duration.tv_sec, cbn->duration.tv_nsec, cbn->id, cbn->true_client_id, client_info, graphviz_colorscheme, graphviz_colors[cbn->true_client_id + RESERVED_IDS]);
}

/* Prints callback node CBN to FD.
   If INDENT, we indent it according to its level. */
static void dump_callback_node (int fd, struct callback_node *cbn, char *prefix, int do_indent)
{
  char spaces[512];
  int i, n_spaces;

  assert (cbn != NULL);
  if (do_indent)
  {
    memset (spaces, 0, 512);
    n_spaces = cbn->level;
    for (i = 0; i < n_spaces; i++)
      strcat (spaces, " ");
  }

  dprintf(fd, "%s%s | <cbn> <%p>> | <id> <%i>> | <info> <%p>> | <type> <%s>> | <level> <%i>> | <parent> <%p>> | <parent_id> <%i>> | <active> <%i>> | <n_children> <%i>> | <client_id> <%i>> | <relative start> <%is %lins>> | <duration> <%is %lins>> |\n", 
    do_indent ? spaces : "", prefix, (void *) cbn, cbn->id, (void *) cbn->info, cbn->info ? callback_type_to_string (cbn->info->type) : "<unknown>", cbn->level, (void *) cbn->parent, cbn->parent ? cbn->parent->id : -1, cbn->active, list_size (&cbn->children), cbn->true_client_id, cbn->relative_start.tv_sec, cbn->relative_start.tv_nsec, cbn->duration.tv_sec, cbn->duration.tv_nsec);
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
  struct callback_node *cbn;

  snprintf(out_file, 128, "/tmp/callback_global_order_%i_%i.txt", (int) time(NULL), getpid());
  printf("Dumping all %i callbacks in their global order to %s\n", list_size (&global_order_list), out_file);

  fd = open (out_file, O_CREAT|O_TRUNC|O_RDWR, S_IRWXU|S_IRWXG|S_IRWXO);
  if (fd < 0)
  {
    printf("Error, open (%s, O_CREAT|O_TRUNC|O_RDWR, S_IRWXU|S_IRWXG|S_IRWXO) failed, returning %i. errno %i: %s\n",
      out_file, fd, errno, strerror (errno));
    fflush(NULL);
    exit (1);
  }

  list_lock(&global_order_list);

  cbn_num = 0;
  for (e = list_begin(&global_order_list); e != list_end(&global_order_list); e = list_next(e))
  {
    assert(e != NULL);
    cbn = list_entry (e, struct callback_node, global_order_elem);
    snprintf(prefix, 64, "Callback %i: ", cbn_num);
    dump_callback_node (fd, cbn, prefix, 0);
    cbn_num++;
  }
  fflush(NULL);

  list_unlock(&global_order_list);
  close(fd);
}

#if 0
/* Dumps the callback tree rooted at CBN to FD. */
static void dump_callback_tree (int fd, struct callback_node *cbn)
{
  int child_num;
  struct list_elem *e;
  struct callback_node *node;
  assert (cbn != NULL);

  dump_callback_node (fd, cbn, "", 1);
  child_num = 0;
  for (e = list_begin(&cbn->children); e != list_end(&cbn->children); e = list_next(e))
  {
    node = list_entry (e, struct callback_node, child_elem);
    /* printf("Parent cbn %p child %i: %p\n", cbn, child_num, node); */
    dump_callback_tree (fd, node);
    child_num++;
  }
}
#endif

/* Dump the callback tree rooted at CBN to FD in graphviz format.
   The caller must have prepared the outer graph declaration; we just
   print node/edge info in graphviz format. */
static void dump_callback_tree_gv (int fd, struct callback_node *cbn)
{
  int child_num;
  struct list_elem *e;
  struct callback_node *node;
  assert (cbn != NULL);

  dump_callback_node_gv (fd, cbn);
  child_num = 0;
  for (e = list_begin(&cbn->children); e != list_end(&cbn->children); e = list_next(e))
  {
    node = list_entry (e, struct callback_node, child_elem);
    /* printf("Parent cbn %p child %i: %p\n", cbn, child_num, node); */
    /* Print the relationship between parent and child. */
    dprintf(fd, "    %i -> %i;\n", cbn->id, node->id); 
    dump_callback_tree_gv (fd, node);
    child_num++;
  }
}

/* Returns the size of the tree rooted at CBN (i.e. # callback_nodes). */
int callback_tree_size (struct callback_node *root)
{
  int size; 
  struct list_elem *e;
  struct callback_node *node;

  assert(root != NULL);

  size = 1;
  for (e = list_begin(&root->children); e != list_end(&root->children); e = list_next(e))
  {
    node = list_entry (e, struct callback_node, child_elem);
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
  printf("Dumping all %i callback trees to %s\n", list_size (&root_list), out_file);

  fd = open (out_file, O_CREAT|O_TRUNC|O_RDWR, S_IRWXU|S_IRWXG|S_IRWXO);
  assert (0 <= fd);
#endif

  /* Print as individual trees. */
  meta_size = 0;
  tree_num = 0;
  for (e = list_begin(&root_list); e != list_end(&root_list); e = list_next(e))
  {
    struct callback_node *root = list_entry (e, struct callback_node, root_elem);
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
  printf("Dumping the %i callback trees as a meta-tree to %s\n  dot -Tdot %s -o /tmp/graph.dot\n  xdot /tmp/graph.dot\n", list_size (&root_list), out_file, out_file);

  fd = open (out_file, O_CREAT|O_TRUNC|O_RDWR, S_IRWXU|S_IRWXG|S_IRWXO);
  assert (0 <= fd);

  printf("Dumping META_TREE\n");
  /* Print as one giant tree with a null root. 
     Nodes use their global ID as a node ID so trees can coexist happily in the same meta-tree. 
     Using ordering="out" seems to make each node be placed by global ID order (i.e. in the same order in which they arrive). We'll see how well this works more generally... */
  dprintf(fd, "digraph META_TREE {\n    graph [ordering=\"out\"]\n     /* size %i */\n", meta_size);
  dprintf(fd, "  -1 [label=\"meta-root node\"]\n");
  tree_num = 0;
  for (e = list_begin(&root_list); e != list_end(&root_list); e = list_next(e))
  {
    struct callback_node *root = list_entry (e, struct callback_node, root_elem);
    assert (0 <= fd);
    tree_size = callback_tree_size (root);
    dprintf(fd, "  subgraph %i {\n    /* size %i */\n", tree_num, tree_size);
    dump_callback_tree_gv (fd, root);
    dprintf(fd, "  }\n");
    dprintf(fd, "  -1 -> %i\n", root->id); /* Link to the meta-root node. */
    ++tree_num;
  }
  dprintf(fd, "}\n");
  close(fd);
#else
  printf("No meta tree for you\n");
#endif

  fflush(NULL);
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
   At callback registration time we can determine the origin. 
   If CB is NULL, CB will never be called, and we ignore it. */
void uv__register_callback (void *cb, enum callback_type cb_type)
{
  static pthread_t registering_thread = -1;
  struct callback_node *cbn;
  struct callback_origin *co;
  enum callback_origin_type origin;

  if (cb == NULL)
    return;

  if (!unified_callback_initialized)
    init_unified_callback();

  /* Are callbacks ever registered by more than one thread? */
  if (registering_thread == -1)
    registering_thread = pthread_self();
  assert(registering_thread == pthread_self());

  co = (struct callback_origin *) malloc(sizeof *co);
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
      NOT_REACHED;
    }
  }
  else
    origin = NODEJS_INTERNAL;

  co->cb = cb;
  co->type = cb_type;
  co->origin = origin;
  map_insert(callback_to_origin_map, (int) cb, co);
}

/* Returns the origin of CB.
   You must have called uv__register_callback on CB first.
   Asserts if CB is NULL or if CB was not registered. 
   
   Exceptions: FS events (src/unix/fs.c) registered in the thread pool are 
    implemented by wrapping the action inside uv__fs_work and the "done" uv_fs_cb 
    in uv__fs_done. The same style is used for a few other internal wrappers
    listed below.
    
    If CB is an internal wrapper, we return (struct callback_origin *) as follows:
      uv__fs_work     -> WAS_UV__FS_WORK
      uv__fs_done     -> WAS_UV__FS_DONE
      uv__stream_io   -> WAS_UV__STREAM_IO
      uv__async_io    -> WAS_UV__ASYNC_IO
      uv__async_event -> WAS_UV__ASYNC_EVENT
      uv__server_io   -> WAS_UV__SERVER_IO
      uv__signal_event -> WAS_UV__SIGNAL_EVENT */
struct callback_origin * uv__callback_origin (void *cb)
{
  struct callback_origin *co;
  int found;

  assert(cb != NULL);
  found = 0;

  co = (struct callback_origin *) map_lookup(callback_to_origin_map, (int) cb, &found);
  if (!found)
  {
    /* See comments above the function. */
    if (cb == uv_uv__fs_done_ptr())
      return (void *) WAS_UV__FS_WORK;
    else if (cb == uv_uv__fs_work_ptr())
      return (void *) WAS_UV__FS_DONE;
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
    else if (cb == uv_uv__getaddrinfo_work_ptr())
      return (void *) WAS_UV__GETADDRINFO_WORK;
    else if (cb == uv_uv__getaddrinfo_done_ptr())
      return (void *) WAS_UV__GETADDRINFO_DONE;
    else
      NOT_REACHED;
  }

  assert(co != NULL);
  return co;
}

/* Implementation for tracking the initial stack. */

/* Note that we've begun the initial application stack. 
   Call once prior to invoking the application code. */
void uv__mark_init_stack_begin (void)
{
  /* Call at most once. */
  static int here = 0;
  assert(!here);
  here = 1;

  cbn_start(get_init_stack_callback_node());
  printf("uv__mark_init_stack_begin: inital stack has begun\n");
}

/* Note that we've finished the initial application stack. 
   Call once after the initial stack is complete. 
   Pair with uv__mark_init_stack_begin. */
void uv__mark_init_stack_end (void)
{
  struct callback_node *init_stack_cbn;

  init_stack_cbn = get_init_stack_callback_node();
  assert(cbn_is_active(init_stack_cbn));
  cbn_stop(init_stack_cbn);
  printf("uv__mark_init_stack_end: inital stack has ended\n");
}

/* Returns non-zero if we're in the application's initial stack, else 0. */
int uv__init_stack_active (void)
{
  return cbn_is_active(get_init_stack_callback_node());
}

int uv_run_active = 0;

/* Note that we've entered libuv's uv_run loop. */
void uv__mark_uv_run_begin (void)
{
  assert(!uv_run_active);
  uv_run_active = 1;
  printf("uv__mark_uv_run_begin: uv_run has begun\n");
}

/* Note that we've finished the libuv uv_run loop.
   Pair with uv__mark_uv_run_begin. */
void uv__mark_uv_run_end (void)
{
  assert(uv_run_active);
  uv_run_active = 0;
  printf("uv__mark_uv_run_end: uv_run has ended\n");
}

/* Returns non-zero if we're in the application's initial stack, else 0. */
int uv__uv_run_active (void)
{
  return uv_run_active;
}

/* Attempt to discover the client ID of CBN.
   CBN->id must be ID_UNKNOWN. 
   Returns non-zero if discovered, else zero. */
static int cbn_discover_id (struct callback_node *cbn)
{
  uv_handle_t *uvht;
  int got_peer_info;
  struct callback_origin *origin;

  assert(cbn != NULL);
  assert(cbn->peer_info != NULL);
  assert(cbn->true_client_id == ID_UNKNOWN);
  assert(!cbn->discovered_client_id);

  mylog("cbn_discover_id: Looking for the ID associated with CBN %p\n", cbn);
  got_peer_info = look_for_peer_info(cbn->info, cbn, &uvht);
  if (got_peer_info)
  {
    mylog("cbn_discover_id: Found peer info\n");
    /* We determined the peer info. Convert to ID and color the tree. */
    cbn->discovered_client_id = 1;
    cbn->true_client_id = get_client_id(cbn->peer_info);
    assert(0 <= cbn->true_client_id);
  }
  else
  {
    origin = uv__callback_origin(cbn->info->cb);
    if (INTERNAL_CALLBACK_WRAPPERS_MAX < origin)
    {
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

  if (cbn->discovered_client_id)
  {
    cbn_color_tree(cbn_get_root(cbn), cbn->true_client_id);

    if (uvht != NULL)
      /* Embed the peer_info for this handle. Used to identify the client on CLOSE_CB. 
         Per nodejs-mud tests, the handle can be re-used on different clients. 
         I'm assuming that this does not happen unsafely, since I cannot imagine how
         it would work if the same handle could be used to talk to two different clients concurrently. 
         However, this is a potential source of error. */
      uvht->peer_info = cbn->peer_info;
  }

  return cbn->discovered_client_id;
}

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
