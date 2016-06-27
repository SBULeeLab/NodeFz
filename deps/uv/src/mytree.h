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
  struct list_elem parent_child_list_elem;

  unsigned child_num; /* Which entry am I in my parent's list of children? Begins with 0. */
  unsigned depth;     /* How deep in the tree am I? root's depth is 0. */

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
typedef int (*tree_find_func)(tree_node_t *e, void *aux);

/* Trees are not allocated (i.e. created with malloc), just initialized from an existing tree_node_t. */
void tree_init (tree_node_t *node);

/* Interactions. */

/* Add CHILD as a child of PARENT. Sets CHILD->child_num. 
   You must have called tree_init on CHILD already. */
void tree_add_child (tree_node_t *parent, tree_node_t *child);

/* How far from NODE to root? root is 0, its children are 1, etc. */
unsigned tree_depth (tree_node_t *node);

tree_node_t * tree_get_parent (tree_node_t *node);
tree_node_t * tree_get_root (tree_node_t *node);
unsigned tree_size (tree_node_t *root);
int tree_is_root (tree_node_t *node);

/* Returns the relative child num NODE is of its parent. */
unsigned tree_get_child_num (tree_node_t *node);

/* Utility. */
/* Apply F to ROOT and its descendants. 
	 Uses a DFS algorithm. */
void tree_apply (tree_node_t *root, tree_apply_func f, void *aux);
/* Apply F to NODE and its direct ancestors. */
void tree_apply_up (tree_node_t *node, tree_apply_func f, void *aux);

/* Return the first node for which F returns non-zero, or NULL if no match found. */
tree_node_t * tree_find (tree_node_t *root, tree_find_func f, void *aux);

/* Only a single instance of the returned list can exist at a time. 
   Finish using the returned list and list_destroy it prior to 
     calling tree_as_list again.
   Do not free the elements of the list, as this will damage the underlying tree.

   Extract the tree nodes using list_entry(elem, tree_node_t, tree_as_list_elem). 
   Extract the wrapping structures using tree_entry(...). */
struct list * tree_as_list (tree_node_t *root);

/* Returns non-zero if tree looks valid. 
   This is a non-recursive test. */
int tree_looks_valid (tree_node_t *node);

/* Tests tree APIs. */
void tree_UT (void);

#endif  /* UV_SRC_TREE_H_ */
