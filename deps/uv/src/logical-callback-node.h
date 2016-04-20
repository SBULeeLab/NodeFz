#ifndef UV_LOGICAL_CALLBACK_NODE_H_
#define UV_LOGICAL_CALLBACK_NODE_H_

#include "misc-decls.h"
#include "unified-callback-enums.h"
#include "list.h"
#include "mytree.h"

struct callback_info_s;
struct lcbn_s;
typedef struct lcbn_s lcbn_t;
typedef struct lcbn_dependency_s lcbn_dependency_t;

/* Nodes that comprise a logical callback tree. */
struct lcbn_s
{
  int magic;
  /* TODO Move name, parent_name into sched_lcbn_t? */
  char name[64];
  char parent_name[64];

  void *context; /* Request or handle with which this LCBN is associated. */
  any_func cb;
  enum callback_context cb_context;
  enum callback_type cb_type; 
  enum callback_behavior cb_behavior;

  tree_node_t tree_node; /* This tree reflects LCBN registration dependencies. */
  struct list *dependencies; /* List of 'lcbn_dependency_t'. This LCBN will only be executed after all of the nodes in this list. For semantic dependencies like 'WORK -> AFTER_WORK'. */

  int global_exec_id; /* The order in which it was executed relative to all other LCBNs */
  int global_reg_id; /* The order in which it was registered relative to all other LCBNs */

  struct callback_info_s *info; /* Set at invoke_callback time. */

  struct timespec registration_time;
  struct timespec start_time;
  struct timespec end_time;

  int active;   /* Is this LCBN currently executing? */
  int finished; /* Has this LCBN finished executing? */

  char extra_info[64]; /* An extra string you want to embed. Included in toString/fromString. */

  pthread_t executing_thread; /* Which thread executes us? */
};

lcbn_t * lcbn_create (void *context, any_func cb, enum callback_type cb_type);
void lcbn_add_child (lcbn_t *parent, lcbn_t *child);
void lcbn_destroy (lcbn_t *lcbn);

lcbn_t * lcbn_parent (lcbn_t *lcbn);

void lcbn_mark_begin (lcbn_t *lcbn);
void lcbn_mark_end (lcbn_t *lcbn);

char * lcbn_to_string (lcbn_t *lcbn, char *buf, int size);
lcbn_t * lcbn_from_string (char *buf, int size);

/* For use on LCBNs contained in a tree_as_list list. */
void lcbn_tree_list_print_f (struct list_elem *e, void *fd);

void * lcbn_get_context (lcbn_t *lcbn);
any_func lcbn_get_cb (lcbn_t *lcbn);
enum callback_type lcbn_get_cb_type (lcbn_t *lcbn);

/* Add PRED to SUCC's list of dependencies. */
void lcbn_add_dependency (lcbn_t *pred, lcbn_t *succ);

/* Returns non-zero if equal, else zero.
   Equality is measured by recursively matching: <type, tree_level, child_num>
   In other words, A and B are equal if they and pairwise on all of their ancestors have
   matching type, tree_level, and child_num.  */
int lcbn_semantic_equals (lcbn_t *a, lcbn_t *b);

/* Returns non-zero if lcbn has been executed, else zero. */
int lcbn_executed (lcbn_t *lcbn);

/* TODO Define at the caller layer. See notes in logical-callback-node.c for details. */
int lcbn_sort_on_int (struct list_elem *a, struct list_elem *b, void *aux);
int lcbn_sort_by_reg_id (struct list_elem *a, struct list_elem *b, void *aux);
int lcbn_sort_by_exec_id (struct list_elem *a, struct list_elem *b, void *aux);
int lcbn_remove_unexecuted (struct list_elem *e, void *aux);

int lcbn_looks_valid (lcbn_t *lcbn);

int lcbn_is_active (lcbn_t *lcbn);

/* Tests lcbn APIs. */
void lcbn_UT (void);

#endif /* UV_LOGICAL_CALLBACK_NODE_H_ */
