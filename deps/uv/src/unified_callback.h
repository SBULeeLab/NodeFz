#ifndef UV_UNIFIED_CALLBACK_H_
#define UV_UNIFIED_CALLBACK_H_

#include "list.h"
#include "map.h"
#include "mylog.h"
#include "logical-callback-node.h"
#include "unified-callback-enums.h"
#include <sys/types.h> /* struct sockaddr_storage */
#include <sys/socket.h>

/* Unified callback queue. */
#define UNIFIED_CALLBACK 1
#define GRAPHVIZ 1

#ifndef NOT_REACHED
#define NOT_REACHED assert (0 == 1);
#endif

struct callback_origin
{
  enum callback_origin_type origin;
  enum callback_type type;
  void (*cb)();
};

/* Description of an instance of a callback. */
struct callback_info
{
  enum callback_type type;
  enum callback_origin_type origin;
  void (*cb)();
  long args[MAX_CALLBACK_NARGS]; /* Must be wide enough for the widest arg type. Seems to be 8 bytes. */
};

/* Nodes that comprise a callback tree. */
struct callback_node
{
  struct callback_info *info; /* Description of this callback. */
  int physical_level; /* What level in the physical callback tree is it? For root nodes this is 0. */
  int logical_level; /* What level in the logical callback tree is it? For root nodes this is 0. */
  struct callback_node *physical_parent; /* What callback ACTUALLY started us? For root nodes this is NULL. */
  struct callback_node *logical_parent; /* What callback was LOGICALLY responsible for starting us? NULL means that physical parent is also logical parent. */

  /* These fields are to track our internal ID of the client incurring this CB. 
     The first client has ID 0, the second ID 1, ... -1 == unknown. -2 == originating from the initial stack. 
     Trees from the initial stack will all have ID -2. 
     Subsequent trees will begin with orig_client_id = true_client_id = -1 (unknown).
     Once the client ID is discovered, the tree will be "colored" with that ID. 
     The discovering node will have discovered_client_id == 1.

     For trees originating from the initial stack, the color is always known.
     For trees originating from client input, the color is discovered "at some point".
       Nodes with (orig_client_id, true_client_id, discovered_client_id) = (-1, !-1, 0) were originally "colorless" -- the color (i.e. the client) had not yet been discovered.
       The node with (orig_client_id, true_client_id, discovered_client_id) = (-1, !-1, 0) was the node that discovered the color (i.e. the client). This discovery is made with a CONNECTION_CB or a READ_CB.
       Nodes with (orig_client_id, true_client_id) = (!-1, !-1) were in generations with the color (i.e. the client) already known.
     */
  int orig_client_id; /* The original ID inherited from the parent. */
  int true_client_id; /* The true ID. */
  int discovered_client_id; /* Whether or not this node "discovered" the client ID. */

  int was_pending_cb; /* Was this CB invoked from uv__run_pending? */

  struct sockaddr_storage *peer_info; /* Info about the peer associated with this node. The root of a tree allocates this, and descendants share it. The discovered_client_id node sets it. */ 

  struct timespec start;
  struct timespec stop;
  struct timespec relative_start; /* START - the time at which execution began. */
  struct timespec duration; /* STOP - START. */
  int active; /* 1 if callback active, 0 if finished. */

  int id; /* Unique ID for this node. This is the index of the node in global_order_list, i.e. the order in which it was evaluated relative to the other nodes. */

  int executing_thread; /* Which thread ran me? This is an internal tid beginning at 0. -1 means no thread ran me (implies a 'marker' CBN). */

  struct list physical_children;
  struct list logical_children;

  lcbn_t *lcbn; /* The logical CBN associated with this CBN. */
  
  struct list_elem global_order_elem; /* For inclusion in the global callback order. */
  struct list_elem physical_child_elem; /* For inclusion in physical parent's list of children. */
  struct list_elem logical_child_elem; /* For inclusion in logical parent's list of children. */
  struct list_elem root_elem; /* For root nodes: inclusion in list of root nodes. */
};

void current_callback_node_set (struct callback_node *);
struct callback_node * current_callback_node_get (void);
struct callback_node * get_init_stack_callback_node (void);
lcbn_t * get_init_stack_lcbn (void);
struct callback_node * invoke_callback (struct callback_info *);

/* Instantiate a struct callback_info * named cbi_p. */
#define INIT_CBI(_cb_type, _cb_p)                      \
  struct callback_info *cbi_p = malloc(sizeof *cbi_p); \
  assert(cbi_p != NULL);                               \
  memset(cbi_p, 0, sizeof(*cbi_p));                    \
  cbi_p->type = (_cb_type);                            \
  cbi_p->cb = (_cb_p);                                    

/* Macros to prep a CBI for invoke_callback, with 0-5 args. */
#define PREP_CBI_0(_type, _cb)                                          \
  mylog("PREP_CBI_0: type %s cb %p", callback_type_to_string(_type), (_cb));                   \
  INIT_CBI(_type, _cb)                                                  \
  /* Determine the origin of the CB, add it to cbi_p. */                \
  struct callback_origin *co = uv__callback_origin((void *) (_cb));     \
  assert(co != NULL);                                                   \
  /* These are internal wrapper functions and have no origin. */        \
  if ((int) co != WAS_UV__FS_WORK && (int) co != WAS_UV__FS_DONE        \
   && (int) co != WAS_UV__STREAM_IO                                     \
   && (int) co != WAS_UV__ASYNC_IO && (int) co != WAS_UV__ASYNC_EVENT   \
   && (int) co != WAS_UV__SERVER_IO && (int) co != WAS_UV__SIGNAL_EVENT \
   && (int) co != WAS_UV__GETADDRINFO_WORK                              \
   && (int) co != WAS_UV__GETADDRINFO_DONE                              \
   && (int) co != WAS_UV__QUEUE_WORK                                    \
   && (int) co != WAS_UV__QUEUE_DONE                                    \
   && (int) co != WAS_UV__WORK_DONE                                     \
     )                                                                  \
  {                                                                     \
    cbi_p->origin = co->origin;                                         \
    assert(cbi_p->type == co->type);                                    \
  }                                                                     \
  mylog("PREP_CBI_0: CB %p\n", _cb);

#define PREP_CBI_1(type, cb, arg0)                         \
  PREP_CBI_0(type, cb)                                     \
  cbi_p->args[0] = (long) arg0;
#define PREP_CBI_2(type, cb, arg0, arg1)                   \
  PREP_CBI_1(type, cb, arg0)                               \
  cbi_p->args[1] = (long) arg1;
#define PREP_CBI_3(type, cb, arg0, arg1, arg2)             \
  PREP_CBI_2(type, cb, arg0, arg1)                         \
  cbi_p->args[2] = (long) arg2;
#define PREP_CBI_4(type, cb, arg0, arg1, arg2, arg3)       \
  PREP_CBI_3(type, cb, arg0, arg1, arg2)                   \
  cbi_p->args[3] = (long) arg3;
#define PREP_CBI_5(type, cb, arg0, arg1, arg2, arg3, arg4) \
  PREP_CBI_4(type, cb, arg0, arg1, arg2, arg3)             \
  cbi_p->args[4] = (long) arg4;

/* Macros to invoke a callback, with 0-5 args.
   The internally-generated callback node describing the 
   invoked callback is set to callback_cbn. */
#define INVOKE_CALLBACK_0(type, cb)                               \
  PREP_CBI_0(type, cb)                                            \
  struct callback_node *callback_cbn = invoke_callback(cbi_p);
#define INVOKE_CALLBACK_1(type, cb, arg0)                         \
  PREP_CBI_1(type, cb, arg0)                                      \
  struct callback_node *callback_cbn = invoke_callback(cbi_p);
#define INVOKE_CALLBACK_2(type, cb, arg0, arg1)                   \
  PREP_CBI_2(type, cb, arg0, arg1)                                \
  struct callback_node *callback_cbn = invoke_callback(cbi_p);
#define INVOKE_CALLBACK_3(type, cb, arg0, arg1, arg2)             \
  PREP_CBI_3(type, cb, arg0, arg1, arg2)                          \
  struct callback_node *callback_cbn = invoke_callback(cbi_p);
#define INVOKE_CALLBACK_4(type, cb, arg0, arg1, arg2, arg3)       \
  PREP_CBI_4(type, cb, arg0, arg1, arg2, arg3)                    \
  struct callback_node *callback_cbn = invoke_callback(cbi_p);
#define INVOKE_CALLBACK_5(type, cb, arg0, arg1, arg2, arg3, arg4) \
  PREP_CBI_5(type, cb, arg0, arg1, arg2, arg3, arg4)              \
  struct callback_node *callback_cbn = invoke_callback(cbi_p);

time_t get_relative_time (void);

void dump_callback_global_order (void);
void dump_callback_trees (void);

void dump_callback_global_order_sighandler (int);
void dump_callback_trees_sighandler (int);
void dump_all_trees_and_exit_sighandler (int);

/* Register and retrieve an LCBN in its context (handle or req). */
void lcbn_register (struct map *cb_type_to_lcbn, enum callback_type cb_type, lcbn_t *lcbn);
lcbn_t * lcbn_get (struct map *cb_type_to_lcbn, enum callback_type cb_type);

/* Set and get the current LCBN for this thread. */
void lcbn_current_set (lcbn_t *lcbn);
lcbn_t * lcbn_current_get (void);

/* Return the internal ID for the current pthread. */
int pthread_self_internal (void);

#endif /* UV_UNIFIED_CALLBACK_H_ */
