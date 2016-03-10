#ifndef UV_SRC_TREE_H_
#define UV_SRC_TREE_H_

#include "list.h"

/* Tree.
   Pretty standard stuff here. The tree is constructed using pointers and lists. 
   Each tree_node is the root of the sub-tree rooted at itself.
   A tree_node with parent == NULL is a root. */

struct tree_node_s;
typedef struct tree_node_s tree_node_t;

struct tree_node_s
{
  int magic;

  tree_node_t *parent;
  struct list *children;

  unsigned child_num; /* Which entry am I in my parent's list of children? Begins with 1. */
  struct list_elem parent_child_list_elem;

  /* For tree_as_list. */
  struct list_elem tree_as_list_elem;
};

/* Converts pointer to tree element TREE_ELEM into a pointer to
   the structure that TREE_ELEM is embedded inside.  Supply the
   name of the outer structure STRUCT and the member name MEMBER
   of the tree element. */
#define tree_entry(TREE_ELEM, STRUCT, MEMBER)               \
        ((STRUCT *) ((uint8_t *) &(TREE_ELEM)->children     \
                     - offsetof (STRUCT, MEMBER.children)))

/* Function typedefs. */
typedef void (*tree_apply_func)(tree_node_t *e, void *aux);
typedef void (*tree_destroy_func)(tree_node_t *e, void *aux);

/* Create/destroy APIs. */
void tree_init (tree_node_t *node);

/* Interactions. */

/* Add CHILD as a child of PARENT. Sets CHILD->child_num. 
   You must have called tree_init on CHILD already. */
void tree_add_child (tree_node_t *parent, tree_node_t *child);

/* Utility. */
int tree_is_root (tree_node_t *node);
void tree_apply (tree_node_t *root, tree_apply_func f, void *aux);
tree_node_t * tree_get_root (tree_node_t *node);
unsigned tree_size (const tree_node_t *root);

/* Only a single instance of the returned list can exist at a time. 
   Finish using the returned list and list_destroy it prior to 
     calling tree_as_list again.
   Do not free the elements of the list (this will damage the underlying tree). 

   You can extract the tree nodes using list_entry(elem, tree_node_t, tree_as_list_elem). 
   You can extract the wrapping structures using tree_entry(...). */
struct list * tree_as_list (tree_node_t *root);

/* Tests tree APIs. */
void tree_UT (void);

#endif  /* UV_SRC_TREE_H_ */
