#ifndef UV_LOGICAL_CALLBACK_NODE_H_
#define UV_LOGICAL_CALLBACK_NODE_H_

#include "unified-callback-enums.h"
#include "list.h"

enum callback_type;
struct callback_info;
struct lcbn_s;
typedef struct lcbn_s lcbn_t;

/* Nodes that comprise a logical callback tree. */
struct lcbn_s
{
  /* Set at registration time. */
  void *context; /* Request or handle with which this LCBN is associated. */
  void *cb;
  enum callback_type cb_type; 

  int tree_number; /* Index in the sequence of trees. First is 0. */
  int tree_level;  /* Level in the tree. Root is 0. */
  int level_entry; /* What number lcbn in this level of the tree? First is 0. */

  int global_exec_id; /* The order in which it was executed relative to all other LCBNs */
  int global_reg_id; /* The order in which it was registered relative to all other LCBNs */

  struct callback_info *info; /* Set at invoke_callback time. */

  lcbn_t *registrar;   /* Which LCBN registered me? For response CBs. */

  lcbn_t *tree_parent; /* Parent in the tree. which tree am I in? For action CBs. */
  struct list children;

  struct list dependencies; /* List of 'struct lcbn_dependency'. This LCBN will only be executed after all of the nodes in this list. */

  struct timespec start;
  struct timespec end;

  int active;   /* Is this LCBN currently executing? */
  int finished; /* Has this LCBN finished executing? */

  pthread_t executing_thread; /* Which thread executes us? */
  
  struct list_elem global_exec_order_elem; /* For inclusion in the global callback order list. */
  struct list_elem global_reg_order_elem; /* For inclusion in the global callback order list. */
  struct list_elem child_elem; /* For inclusion in logical parent's list of children. */
  struct list_elem root_elem; /* For root nodes: inclusion in the list of logical root nodes. */
};

lcbn_t * lcbn_create (void *context, void *cb, enum callback_type cb_type);
void lcbn_add_child (lcbn_t *parent, lcbn_t *child);
void lcbn_destroy (lcbn_t *lcbn);

void lcbn_mark_begin (lcbn_t *lcbn);
void lcbn_mark_end (lcbn_t *lcbn);

char * lcbn_to_string (lcbn_t *cbn, char *buf, int size);

void lcbn_global_exec_list_print_f (struct list_elem *e, int *fd);
void lcbn_global_reg_list_print_f (struct list_elem *e, int *fd);

void * lcbn_get_context (lcbn_t *lcbn);
void * lcbn_get_cb (lcbn_t *lcbn);
enum callback_type lcbn_get_cb_type (lcbn_t *lcbn);

/* Add PRED to SUCC's list of dependencies. */
void lcbn_add_dependency (lcbn_t *pred, lcbn_t *succ);

#endif /* UV_LOGICAL_CALLBACK_NODE_H_ */
