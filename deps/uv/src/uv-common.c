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

/* Tracking the unified callback queue. */
int has_init_stack_finished = 0;
/* Note that we've entered the main libuv loop. Call at most once. */
void signal_init_stack_finished(void)
{
  assert(has_init_stack_finished == 0);
  has_init_stack_finished = 1;
  printf("The initial stack has finished!\n");
}

/* Returns 1 if we've entered the main libuv loop, 0 else. */
int init_stack_finished(void)
{
  return has_init_stack_finished;
}
static int unified_callback_initialized = 0;
static struct list root_list;
static struct list global_order_list;

/* A two-hash system allows us to maintain "nice" internal client IDs 1, 2, 3, ... . */
/* hash(sockaddr_storage) -> addr_hash_to_id_entry */
static struct map *addr_hash_to_id;
/* unique client ID -> hash(sockaddr_storage) */
static struct map *id_to_addr_hash;
struct addr_hash_to_id_entry
{
  int id;
  struct sockaddr_storage addr;
};

/* CBN has a real client ID. 
   Propagate (bubble) this up the tree, all the way up to the
   root node. */
static void bubble_client_id (struct callback_node *cbn)
{
  struct callback_node *cur, *par;
  assert(cbn != NULL);
  assert(0 <= cbn->client_id);

  cur = cbn;
  par = cbn->parent;
  while(par != NULL)
  {
    if (par->client_id < 0)
    {
      /* Update and climb the tree. */
      par->client_id = cur->client_id;
      cur = par;
      par = par->parent;
    }
    else if (par->client_id == cur->client_id)
      /* No need to climb higher, parent already knows. */
      break;
    else
      /* Parent's ID must either have been unknown or have matched. */
      NOT_REACHED;
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

/* Returns the CBN associated with the initial stack. */
static struct callback_node *init_stack_cbn = NULL;
struct callback_node * get_init_stack_callback_node (void)
{
  if (init_stack_cbn == NULL)
  {
    init_stack_cbn = malloc(sizeof *init_stack_cbn);
    assert(init_stack_cbn != NULL);
    memset(init_stack_cbn, 0, sizeof *init_stack_cbn);

    init_stack_cbn->info = NULL;
    init_stack_cbn->level = 0;
    init_stack_cbn->parent = NULL;
    init_stack_cbn->client_id = -2;
    init_stack_cbn->relative_start = 0;
    init_stack_cbn->duration = 0;
    init_stack_cbn->active = 1;
    list_init (&init_stack_cbn->children);
  }

  return init_stack_cbn;
}

/* Returns the ID of the client described in CLIENT_ENTRY.
   If the corresponding client is already in addr_hash_to_id, we return that ID and free CLIENT_ENTRY.
   Otherwise we add a new entry to addr_hash_to_id and id_to_addr_hash and return the new ID. 
   The ID is also embedded in CLIENT_ENTRY->ID.
   If there is not a connected client, we return -1 to signify unknown. */
int lookup_or_add_client (struct addr_hash_to_id_entry *client_entry)
{
  struct addr_hash_to_id_entry *map_entry;
  int addr_hash, client_id, found;

  assert(client_entry != NULL);
  /* If already present, return the ID. Else register in the map with a new ID. */ 
  addr_hash = map_hash (&client_entry->addr, sizeof client_entry->addr);
  assert(addr_hash != 0); /* There must be something in addr. */
  client_entry->id = map_size(addr_hash_to_id);

  if ((map_entry = map_lookup(addr_hash_to_id, addr_hash, &found)) != NULL)
  {
    client_id = map_entry->id;
    printf("Existing client\n");
    free(client_entry);
  }
  else
  {
    client_id = client_entry->id;
    map_insert(id_to_addr_hash, client_id, addr_hash);
    map_insert(addr_hash_to_id, addr_hash, client_entry);
    printf("New client\n");
  }

  return client_id;
}

/* Compute the client ID based on a uv_tcp_t*.
   Returns the client ID, possibly modifying addr_hash_to_id.
   If no discernible client, returns -1. */
int get_client_id (uv_tcp_t *client)
{
  char host[INET6_ADDRSTRLEN];
  char service[64];
  socklen_t addr_len;
  int err, found, client_id, addr_hash;
  struct addr_hash_to_id_entry *new_entry, *map_entry;

  assert(client != NULL);

  new_entry = malloc(sizeof *new_entry);
  assert(new_entry != NULL);
  memset(new_entry, 0, sizeof *new_entry);
  addr_len = sizeof new_entry->addr;

  /* If we are not yet connected to CLIENT, CLIENT may not yet know the peer name. For example, on UV_CONNECTION_CB, the server has received an incoming connection, but uv_accept() may not yet have been called. In the case of UV_CONNECTION_CB, CLIENT->ACCEPTED_FD contains the ID of the connecting client, and we can use that instead. */
  err = uv_tcp_getpeername(client, &new_entry->addr, &addr_len);
  if (err != 0)
  {
    err = uv_tcp_getpeername_direct(client->accepted_fd, &new_entry->addr, &addr_len);
    if (err != 0)
      return -1;
  }

  client_id = lookup_or_add_client(new_entry);

  /* Describe what we found. */
  addr_hash = map_lookup(id_to_addr_hash, client_id, &found);
  assert(found == 1);

  map_entry = map_lookup(addr_hash_to_id, addr_hash, &found);
  assert(found == 1);

  err = getnameinfo(&map_entry->addr, addr_len, host, sizeof(host), service, sizeof(service), NI_NUMERICHOST|NI_NUMERICSERV);
  if (err != 0)
  { 
    printf ("get_client_id: Error, getnameinfo failed: %s\n", gai_strerror(err));
    assert(err == 0);
  }

  printf("get_client_id: Client: %i -> %i -> %s:%s (id %i)\n", client_id, addr_hash, host, service, client_id);

  return client_id;
}

/* Compute the client ID based on a raw FD.
   Returns the client ID, possibly modifying addr_hash_to_id.
   If no discernible client, returns -1. */
int get_client_id_direct (int fd)
{
  char host[INET6_ADDRSTRLEN];
  char service[64];
  socklen_t addr_len;
  int err, found;
  unsigned key;
  int client_id;
  struct addr_hash_to_id_entry *new_entry, *map_entry;

  if (fd < 0)
    return -1;

  new_entry = malloc(sizeof *new_entry);
  assert(new_entry != NULL);
  memset(new_entry, 0, sizeof *new_entry);
  addr_len = sizeof new_entry->addr;

  /* If we are not yet connected to CLIENT, we may not know the peer name. For example, on UV_CONNETION_CB, the server has received an incoming connection, but uv_accept() may not yet have been called. */
  err = uv_tcp_getpeername_direct(fd, &new_entry->addr, &addr_len);
  if (err != 0)
    return -1;

  err = getnameinfo(&new_entry->addr, addr_len, host, sizeof(host), service, sizeof(service), NI_NUMERICHOST|NI_NUMERICSERV);
  assert(err == 0);

  client_id = lookup_or_add_client(new_entry);
  return client_id;
}

/* Invoke the callback described by CBI. 
   Returns the CBN allocated for the callback. */
struct callback_node * invoke_callback (struct callback_info *cbi)
{
  struct callback_node *orig_cbn, *new_cbn;

  assert (cbi != NULL);  

  /* Potentially racey but very unlikely. */
  if (!unified_callback_initialized)
  {
    printf("DEBUG: Testing list\n");
    list_UT();
    printf("DEBUG: Testing map\n");
    map_UT();

    list_init (&global_order_list);
    list_init (&root_list);
    addr_hash_to_id = map_create();
    assert(addr_hash_to_id != NULL);
    id_to_addr_hash = map_create();
    assert(id_to_addr_hash != NULL);
    signal(SIGUSR1, dump_callback_global_order_sighandler);
    signal(SIGUSR2, dump_callback_trees_sighandler);
    signal(SIGINT, dump_all_trees_and_exit_sighandler);
    unified_callback_initialized = 1;
  }
  
  new_cbn = malloc (sizeof *new_cbn);
  assert (new_cbn != NULL);
  memset(new_cbn, 0, sizeof *new_cbn);

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
        orig_cbn = ((struct uv__work *) cbi->args[0])->parent;
        assert (orig_cbn != NULL);
        /* We are the direct parent of the UV__WORK_DONE that follows us. */
        ((struct uv__work *) cbi->args[0])->parent = new_cbn;
        break;
      case UV__WORK_DONE:
        orig_cbn = ((struct uv__work *) cbi->args[0])->parent;
        assert (orig_cbn != NULL);
        break;
      case UV_TIMER_CB:
        /* A uv_timer_t* is a uv_handle_t. 
           There may not be an orig_cbn if the callback was registered by
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
#if 1
    new_cbn->client_id = -1;
#else
    new_cbn->client_id = orig_cbn->client_id;
    new_cbn->client_addr = orig_cbn->client_addr;
#endif
  }
  else
  {
    new_cbn->level = 0;
    new_cbn->parent = NULL;
    /* Unknown until later on in the function. */
    new_cbn->client_id = -1;
    new_cbn->client_addr = (struct sockaddr_storage *) malloc(sizeof *new_cbn->client_addr);
    assert(new_cbn->client_addr != NULL);
    memset(new_cbn->client_addr, 0, sizeof *new_cbn->client_addr);
  }

  if (new_cbn->parent)
  {
    list_lock (&new_cbn->parent->children);
    list_push_back (&new_cbn->parent->children, &new_cbn->child_elem); 
    list_unlock (&new_cbn->parent->children);
    /* By default, inherit client id from parent. */
    new_cbn->client_id = new_cbn->parent->client_id;
  }
  else
  {
    list_lock (&root_list);
    list_push_back (&root_list, &new_cbn->root_elem); 
    list_unlock (&root_list);
  }

  new_cbn->relative_start = get_relative_time();
  assert (gettimeofday (&new_cbn->start, NULL) == 0);
  new_cbn->duration = -1;
  new_cbn->active = 1;
  list_init (&new_cbn->children);

  list_lock (&global_order_list);
  list_push_back (&global_order_list, &new_cbn->global_order_elem);
  new_cbn->id = list_size (&global_order_list);
  list_unlock (&global_order_list);

  if (!new_cbn->parent && cbi->type == UV__WORK_WORK)
  {
    /* Not clear how this can happen. Probably an error. */
    mylog ("invoke_callback: root node is a UV__WORK_WORK item??\n");
    assert(0 == 1);
  }

  mylog ("invoke_callback: invoking callback_node %i: %p cbi %p type %s cb %p level %i parent %p\n",
    new_cbn->id, new_cbn, (void *) cbi, callback_type_to_string(cbi->type), cbi->cb, new_cbn->level, new_cbn->parent);
  assert ((new_cbn->level == 0 && new_cbn->parent == NULL)
       || (0 < new_cbn->level && new_cbn->parent != NULL));

  /* If UV__WORK_WORK, can be called in parallel, resulting in an unclear current_callback_node. 
     We resolve the issue by simply not tracking WORK_WORK CBs as active. 
     TODO In standard Node.js use, they are not expected to register new callbacks of their own, though this should be verified.  
     To address this, we could perhaps track a per-tid current_callback_node. */
  if (cbi->type != UV__WORK_WORK)
    current_callback_node_set (new_cbn);

  uv_handle_t *uvht = NULL;
  char handle_type[64];
  snprintf(handle_type, 64, "(no handle type)");
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
      new_cbn->client_id = get_client_id((uv_stream_t *) uvht);
      cbi->cb ((uv_stream_t *) cbi->args[0], (ssize_t) cbi->args[1], (const uv_buf_t *) cbi->args[2]); /* uv_handle_t */
      break;
    case UV_WRITE_CB:
      /* Pointer to the stream where this write request is running. */
      uvht = (uv_handle_t *) ((uv_write_t *) cbi->args[0])->handle;
      strncpy(handle_type, "uv_stream_t", 64); 
      new_cbn->client_id = get_client_id((uv_stream_t *) uvht);
      cbi->cb ((uv_write_t *) cbi->args[0], (int) cbi->args[1]); /* uv_write_t, has pointer to two uv_stream_t's (uv_handle_t)  */
      break;
    case UV_CONNECT_CB:
      uvht = (uv_handle_t *) ((uv_connect_t *) cbi->args[0])->handle;
      strncpy(handle_type, "uv_stream_t", 64); 
      new_cbn->client_id = get_client_id((uv_stream_t *) uvht);
      cbi->cb ((uv_connect_t *) cbi->args[0], (int) cbi->args[1]); /* uv_req_t, has pointer to uv_stream_t (uv_handle_t) */
      break;
    case UV_SHUTDOWN_CB:
      uvht = (uv_handle_t *) ((uv_shutdown_t *) cbi->args[0])->handle;
      strncpy(handle_type, "uv_stream_t", 64); 
      new_cbn->client_id = get_client_id((uv_stream_t *) uvht);
      cbi->cb ((uv_shutdown_t *) cbi->args[0], (int) cbi->args[1]); /* uv_req_t, has pointer to uv_stream_t (uv_handle_t) */
      break;
    case UV_CONNECTION_CB:
      uvht = (uv_handle_t *) cbi->args[0];
      strncpy(handle_type, "uv_stream_t", 64); 
      new_cbn->client_id = get_client_id((uv_stream_t *)uvht);
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

  if (new_cbn->parent != NULL && 0 <= new_cbn->parent->client_id && new_cbn->parent->client_id != new_cbn->client_id)
  {
    printf ("Interesting: cbn's client ID does not match its parent's.\n");
    assert(0 == 1);
  }

  if (0 <= new_cbn->client_id)
    bubble_client_id(new_cbn);

  mylog ("handle type <%s>\n", handle_type);

  mylog ("invoke_callback: Done with callback_node %p cbi %p uvht <%p>\n",
    new_cbn, new_cbn->info, uvht); 
  new_cbn->active = 0;
  assert (gettimeofday (&new_cbn->stop, NULL) == 0);
  /* Must end after it began. This can fail if the system clock is set backward during the callback. */
  assert (new_cbn->start.tv_sec < new_cbn->stop.tv_sec || new_cbn->start.tv_usec <= new_cbn->stop.tv_usec);
  struct timeval diff;
  timersub (&new_cbn->stop, &new_cbn->start, &diff);
  new_cbn->duration = diff.tv_sec*1000000 + diff.tv_usec;

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

#if 0
  /* DEBUGGING */
  dump_callback_global_order ();
  dump_callback_trees (0);
  printf ("\n\n\n\n\n");
#endif
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
  assert (cbn != NULL);

  /* Example listing:
    1 [label="client 12\ntype UV_ALLOC_CB\nactive 0\nstart 1 duration 5\nID 1"];
    */
  dprintf (fd, "    %i [label=\"client %i\\ntype %s\\nactive %i\\nstart %i duration %lli\\nID %i\\nclient ID %i\"];\n",
    cbn->id, cbn->client_id, callback_type_to_string (cbn->info->type), cbn->active, (int) cbn->relative_start, cbn->duration, cbn->id, cbn->client_id);
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

  dprintf (fd, "%s%s | <cbn> <%p>> | <id> <%i>> | <info> <%p>> | <type> <%s>> | <level> <%i>> | <parent> <%p>> | <parent_id> <%i>> | <active> <%i>> | <n_children> <%i>> | <client_id> <%i>> | <start> <%li>> | <duration> <%lli>> |\n", 
    do_indent ? spaces : "", prefix, (void *) cbn, cbn->id, (void *) cbn->info, callback_type_to_string (cbn->info->type), cbn->level, (void *) cbn->parent, cbn->parent ? cbn->parent->id : -1, cbn->active, list_size (&cbn->children), cbn->client_id, cbn->relative_start, cbn->duration);
}

/* Dumps all callbacks in the order in which they were called. 
   Locks and unlocks GLOBAL_ORDER_LIST. */
void dump_callback_global_order (void)
{
  struct list_elem *e;
  int cbn_num;
  char prefix[64];
  int fd;
  char out_file[128];

  snprintf (out_file, 128, "/tmp/callback_global_order_%i_%i.txt", (int) time(NULL), getpid());
  printf ("Dumping all %i callbacks in their global order to %s\n", list_size (&global_order_list), out_file);

  fd = open (out_file, O_CREAT|O_TRUNC|O_RDWR, S_IRWXU|S_IRWXG|S_IRWXO);
  if (fd < 0)
  {
    printf ("Error, open (%s, O_CREAT|O_TRUNC|O_RDWR, S_IRWXU|S_IRWXG|S_IRWXO) failed, returning %i. errno %i: %s\n",
      out_file, fd, errno, strerror (errno));
    fflush (NULL);
    exit (1);
  }

  list_lock (&global_order_list);

  cbn_num = 0;
  for (e = list_begin (&global_order_list); e != list_end (&global_order_list); e = list_next (e))
  {
    struct callback_node *cbn = list_entry (e, struct callback_node, global_order_elem);
    snprintf (prefix, 64, "Callback %i: ", cbn_num);
    dump_callback_node (fd, cbn, prefix, 0);
    cbn_num++;
  }
  fflush (NULL);

  list_unlock (&global_order_list);
  close (fd);
}

/* Dumps the callback tree rooted at CBN to FD. */
static void dump_callback_tree (int fd, struct callback_node *cbn)
{
  int child_num;
  struct list_elem *e;
  struct callback_node *node;
  assert (cbn != NULL);

  dump_callback_node (fd, cbn, "", 1);
  child_num = 0;
  for (e = list_begin (&cbn->children); e != list_end (&cbn->children); e = list_next (e))
  {
    node = list_entry (e, struct callback_node, child_elem);
    /* printf ("Parent cbn %p child %i: %p\n", cbn, child_num, node); */
    dump_callback_tree (fd, node);
    child_num++;
  }
}

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
  for (e = list_begin (&cbn->children); e != list_end (&cbn->children); e = list_next (e))
  {
    node = list_entry (e, struct callback_node, child_elem);
    /* printf ("Parent cbn %p child %i: %p\n", cbn, child_num, node); */
    /* Print the relationship between parent and child. */
    dprintf (fd, "    %i -> %i;\n", cbn->id, node->id); 
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
  for (e = list_begin (&root->children); e != list_end (&root->children); e = list_next (e))
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
  snprintf (out_file, 128, "/tmp/individual_callback_trees_%i_%i.gv", (int) time(NULL), getpid());
  printf ("Dumping all %i callback trees to %s\n", list_size (&root_list), out_file);

  fd = open (out_file, O_CREAT|O_TRUNC|O_RDWR, S_IRWXU|S_IRWXG|S_IRWXO);
  assert (0 <= fd);
#endif

  /* Print as individual trees. */
  meta_size = 0;
  tree_num = 0;
  for (e = list_begin (&root_list); e != list_end (&root_list); e = list_next (e))
  {
    struct callback_node *root = list_entry (e, struct callback_node, root_elem);
#if GRAPHVIZ
    tree_size = callback_tree_size (root);
    meta_size += tree_size;
    dprintf (fd, "digraph %i {\n    /* size %i */\n", tree_num, tree_size);
    dump_callback_tree_gv (fd, root);
    dprintf (fd, "}\n\n");
#else
    printf ("Tree %i:\n", tree_num);
    dump_callback_tree (root);
#endif
    ++tree_num;
  }
  close (fd);

#if GRAPHVIZ
  snprintf (out_file, 128, "/tmp/meta_callback_trees_%i_%i.gv", (int) time(NULL), getpid());
  printf ("Dumping the %i callback trees as a meta-tree to %s\n  dot -Tdot %s -o /tmp/graph.dot\n  xdot /tmp/graph.dot\n", list_size (&root_list), out_file, out_file);

  fd = open (out_file, O_CREAT|O_TRUNC|O_RDWR, S_IRWXU|S_IRWXG|S_IRWXO);
  assert (0 <= fd);

  printf ("Dumping META_TREE\n");
  /* Print as one giant tree with a null root. 
     Nodes use their global ID as a node ID so trees can coexist happily in the same meta-tree. */
  dprintf (fd, "digraph META_TREE {\n    /* size %i */\n", meta_size);
  dprintf (fd, "  -1 [label=\"meta-root node\"]\n");
  tree_num = 0;
  for (e = list_begin (&root_list); e != list_end (&root_list); e = list_next (e))
  {
    struct callback_node *root = list_entry (e, struct callback_node, root_elem);
    assert (0 <= fd);
    tree_size = callback_tree_size (root);
    dprintf (fd, "  subgraph %i {\n    /* size %i */\n", tree_num, tree_size);
    dump_callback_tree_gv (fd, root);
    dprintf (fd, "  }\n");
    dprintf (fd, "  -1 -> %i\n", root->id); /* Link to the meta-root node. */
    ++tree_num;
  }
  dprintf (fd, "}\n");
  close (fd);
#else
  printf ("No meta tree for you\n");
#endif

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

void dump_all_trees_and_exit_sighandler (int signum)
{
  printf ("Got signal %i. Dumping all trees and exiting\n", signum);
  printf ("Callback global order\n");
  dump_callback_global_order ();
  printf ("Callback trees\n");
  dump_callback_trees ();
  fflush (NULL);
  exit (0);
}

/* Returns time relative to the time at which the first CB was invoked. */
static time_t init_time = 0;
time_t get_relative_time (void)
{
  if (init_time == 0)
    init_time = time(NULL);
  return time(NULL) - init_time;
}
