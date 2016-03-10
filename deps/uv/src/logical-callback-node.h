#ifndef UV_LOGICAL_CALLBACK_NODE_H_
#define UV_LOGICAL_CALLBACK_NODE_H_

#include "unified-callback-enums.h"
#include "list.h"
#include "mytree.h"

struct callback_info;
struct lcbn_s;
typedef struct lcbn_s lcbn_t;

struct lcbn_dependency_s
{
  lcbn_t *dependency;
  struct list_elem elem;
};
typedef struct lcbn_dependency_s lcbn_dependency_t;

/* Nodes that comprise a logical callback tree. */
struct lcbn_s
{
  /* Set at registration time. */
  void *context; /* Request or handle with which this LCBN is associated. */
  void *cb;
  enum callback_context cb_context;
  enum callback_type cb_type; 
  enum callback_behavior cb_behavior;

  tree_node_t tree_node; /* This tree reflects LCBN registration dependencies. */
  struct list *dependencies; /* List of 'lcbn_dependency_t'. This LCBN will only be executed after all of the nodes in this list. For semantic dependencies like 'WORK -> AFTER_WORK'. */

  int global_exec_id; /* The order in which it was executed relative to all other LCBNs */
  int global_reg_id; /* The order in which it was registered relative to all other LCBNs */

  struct callback_info *info; /* Set at invoke_callback time. */

  struct timespec registration_time;
  struct timespec start_time;
  struct timespec end_time;

  int active;   /* Is this LCBN currently executing? */
  int finished; /* Has this LCBN finished executing? */

  pthread_t executing_thread; /* Which thread executes us? */
};

lcbn_t * lcbn_create (void *context, void *cb, enum callback_type cb_type);
void lcbn_add_child (lcbn_t *parent, lcbn_t *child);
void lcbn_destroy (lcbn_t *lcbn);

void lcbn_mark_begin (lcbn_t *lcbn);
void lcbn_mark_end (lcbn_t *lcbn);

char * lcbn_to_string (lcbn_t *cbn, char *buf, int size);
lcbn_t * lcbn_from_string (char *buf);

/* For use on LCBNs contained in a tree_as_list list. */
void lcbn_tree_list_print_f (struct list_elem *e, int *fd);

void * lcbn_get_context (lcbn_t *lcbn);
void * lcbn_get_cb (lcbn_t *lcbn);
enum callback_type lcbn_get_cb_type (lcbn_t *lcbn);

/* Add PRED to SUCC's list of dependencies. */
void lcbn_add_dependency (lcbn_t *pred, lcbn_t *succ);

/* Returns non-zero if equal, else zero.
   Equality is measured by matching: <type, tree_number, tree_level, tree_entry>. */
int lcbn_equals (lcbn_t *a, lcbn_t *b);

#endif /* UV_LOGICAL_CALLBACK_NODE_H_ */
