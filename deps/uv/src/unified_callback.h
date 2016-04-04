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

struct callback_info_s;
struct callback_node_s;
typedef struct callback_info_s callback_info_t;
typedef struct callback_node_s callback_node_t;

struct callback_origin
{
  enum callback_origin_type origin;
  enum callback_type type;
  any_func cb;
};

/* Description of an instance of a callback. */
struct callback_info_s
{
  enum callback_type type;
  enum callback_origin_type origin;
  any_func cb;
  long args[MAX_CALLBACK_NARGS]; /* Must be wide enough for the widest arg type. Seems to be 8 bytes. */
};

/* Nodes that comprise a callback tree. */
struct callback_node_s
{
  callback_info_t *info; /* Description of this callback. */
  int physical_level; /* What level in the physical callback tree is it? For root nodes this is 0. */
  int logical_level; /* What level in the logical callback tree is it? For root nodes this is 0. */
  callback_node_t *physical_parent; /* What callback ACTUALLY started us? For root nodes this is NULL. */
  callback_node_t *logical_parent; /* What callback was LOGICALLY responsible for starting us? NULL means that physical parent is also logical parent. */

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

  struct list *physical_children;
  struct list *logical_children;

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

struct callback_info * cbi_create_0 (enum callback_type type, any_func cb);
struct callback_info * cbi_create_1 (enum callback_type type, any_func cb, long arg0);
struct callback_info * cbi_create_2 (enum callback_type type, any_func cb, long arg0, long arg1);
struct callback_info * cbi_create_3 (enum callback_type type, any_func cb, long arg0, long arg1, long arg2);
struct callback_info * cbi_create_4 (enum callback_type type, any_func cb, long arg0, long arg1, long arg2, long arg3);
struct callback_info * cbi_create_5 (enum callback_type type, any_func cb, long arg0, long arg1, long arg2, long arg3, long arg4);

struct callback_node * INVOKE_CALLBACK_0 (enum callback_type type, any_func cb);
struct callback_node * INVOKE_CALLBACK_1 (enum callback_type type, any_func cb, long arg0);
struct callback_node * INVOKE_CALLBACK_2 (enum callback_type type, any_func cb, long arg0, long arg1);
struct callback_node * INVOKE_CALLBACK_3 (enum callback_type type, any_func cb, long arg0, long arg1, long arg2);
struct callback_node * INVOKE_CALLBACK_4 (enum callback_type type, any_func cb, long arg0, long arg1, long arg2, long arg3);
struct callback_node * INVOKE_CALLBACK_5 (enum callback_type type, any_func cb, long arg0, long arg1, long arg2, long arg3, long arg4);

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
