#ifndef UV_LOGICAL_CALLBACK_NODE_H_
#define UV_LOGICAL_CALLBACK_NODE_H_

/* Nodes that comprise a logical callback tree. */
typedef struct lcbn_s
{
  int tree_number; /* index in the sequence of trees */
  int tree_level; /* level in the tree */
  int level_entry; /* what number lcbn in this level of the tree? */

  int global_id; /* the order in which it was executed relative to all other lcbns */

  struct callback_info info;

  struct lcbn *parent;
  struct list children;

  struct timespec start;
  struct timespec stop;
  int active;
  int finished;
  
  struct list_elem global_order_elem; /* For inclusion in the global callback order list. */
  struct list_elem child_elem; /* For inclusion in logical parent's list of children. */
  struct list_elem root_elem; /* For root nodes: inclusion in the list of logical root nodes. */
} lcbn_t;

lcbn_t * lcbn_create (void);
lcbn_t * lcbn_init (lcbn_t *parent);
void lcbn_destroy (lcbn_t *lcbn);

#endif /* UV_LOGICAL_CALLBACK_NODE_H_ */
