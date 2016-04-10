#include "mytree.h"

#include <assert.h>
#include <stddef.h> /* NULL */
#include <string.h> /* memset */
#include "uv-common.h" /* Allocators */

#define TREE_NODE_MAGIC 88771122

/* These are helper tree_apply[_up] functions. */
static void tree__add_node_to_list (tree_node_t *node, void *aux);
static void tree__count (tree_node_t *node, void *aux);
static void tree__find_helper (tree_node_t *node, void *aux);

/* For tree_UT, for use with tree_as_list. 
   If *(int *) AUX == 0, sorts on 'a value' < 'b value', else the other way. */
static int tree__UT_value_sort (struct list_elem *a, struct list_elem *b, void *aux);
static int tree__UT_find_func (tree_node_t *e, void *aux);

int tree_looks_valid (tree_node_t *node)
{
  int valid = 1;

  mylog(LOG_TREE, 9, "tree_looks_valid: begin: node %p\n", node);
  if (!node)
  {
    valid = 0;
    goto DONE;
  }

  if (node->magic != TREE_NODE_MAGIC)
  {
    valid = 0;
    goto DONE;
  }

  if (!list_looks_valid(node->children))
  {
    valid = 0;
    goto DONE;
  }

  valid = 1;
  DONE:
    mylog(LOG_TREE, 9, "tree_looks_valid: returning valid %i\n", valid);
    return valid;
}

/* Public functions. */
/* Create/destroy APIs. */
void tree_init (tree_node_t *node)
{
  mylog(LOG_TREE, 9, "tree_init: begin\n");
  assert(node);
  memset(node, 0, sizeof *node);

  node->magic = TREE_NODE_MAGIC;
  node->parent = NULL;
  node->child_num = 0;
  node->children = list_create();

  assert(tree_looks_valid(node));
  mylog(LOG_TREE, 9, "tree_init: returning\n");
}

/* Interactions. */
void tree_add_child (tree_node_t *parent, tree_node_t *child)
{
  mylog(LOG_TREE, 9, "tree_add_child: begin: parent %p child %p\n", parent, child);
  assert(tree_looks_valid(parent));
  assert(tree_looks_valid(child));

  list_lock(parent->children);
  child->child_num = list_size(parent->children);
  list_push_back(parent->children, &child->parent_child_list_elem);
  child->parent = parent;
  list_unlock(parent->children);
  mylog(LOG_TREE, 9, "tree_add_child: returning\n");
}

int tree_depth (tree_node_t *node)
{
  int depth = -1;

  mylog(LOG_TREE, 9, "tree_depth: begin: node %p\n", node);
  assert(tree_looks_valid(node));

  depth = -1;
  tree_apply_up(node, tree__count, &depth);
  assert(0 <= depth);

  mylog(LOG_TREE, 9, "tree_depth: returning depth %i\n", depth);
  return depth;
}

/* Utility. */
int tree_is_root (tree_node_t *node)
{
  int is_root = 0;
  mylog(LOG_TREE, 9, "tree_is_root: begin: node %p\n", node);
  assert(tree_looks_valid(node));

  is_root = (node->parent == NULL);

  mylog(LOG_TREE, 9, "tree_is_root: returning is_root %i\n", is_root);
  return is_root;
}

unsigned tree_get_child_num (tree_node_t *node)
{
  mylog(LOG_TREE, 9, "tree_get_child_num: begin: node %p\n", node);
  assert(tree_looks_valid(node));

  mylog(LOG_TREE, 9, "tree_get_child_num: returning child_num %u\n", node->child_num);
  return node->child_num;
}

void tree_apply (tree_node_t *root, tree_apply_func f, void *aux)
{
  struct list_elem *e = NULL;
  tree_node_t *n = NULL;

  mylog(LOG_TREE, 9, "tree_apply: begin: root %p aux %p\n", root, aux);
  if (!root)
    return;
  assert(tree_looks_valid(root));
  assert(f);

  (*f)(root, aux);
  for (e = list_begin(root->children); e != list_end(root->children); e = list_next(e))
  {
    n = list_entry(e, tree_node_t, parent_child_list_elem);
    tree_apply(n, f, aux);
  }

  mylog(LOG_TREE, 9, "tree_apply: returning\n");
}

void tree_apply_up (tree_node_t *node, tree_apply_func f, void *aux)
{
  mylog(LOG_TREE, 9, "tree_apply_up: begin: node %p aux %p\n", node, aux);
  if (!node)
    return;
  assert(tree_looks_valid(node));
  assert(f);

  (*f)(node, aux);
  tree_apply_up(node->parent, f, aux);
  mylog(LOG_TREE, 9, "tree_apply_up: returning\n");
}

tree_node_t * tree_get_root (tree_node_t *node)
{
  tree_node_t *root = NULL;

  mylog(LOG_TREE, 9, "tree_get_root: begin: node %p\n", node);
  assert(tree_looks_valid(node));

  if (tree_is_root(node))
    root = node;
  else
  {
    tree_node_t *par = tree_get_parent(node);
    assert(tree_looks_valid(par));
    root = tree_get_root(par);
  }

  mylog(LOG_TREE, 9, "tree_init: returning root %p\n", root);
  assert(tree_looks_valid(root));
  return root;
}

tree_node_t * tree_get_parent (tree_node_t *node)
{
  mylog(LOG_TREE, 9, "tree_get_parent: begin: node %p\n", node);
  assert(tree_looks_valid(node));

  mylog(LOG_TREE, 9, "tree_get_parent: returning parent %p\n", node->parent);
  return (node->parent);
}

static void tree__count (tree_node_t *node, void *aux)
{
  unsigned *counter = (unsigned *) aux;

  mylog(LOG_TREE, 9, "tree__count: begin: node %p aux %p counter %u\n", node, aux, *counter);
  assert(tree_looks_valid(node));

  *counter = *counter + 1;

  mylog(LOG_TREE, 9, "tree__count: returning (counter %u)\n", *counter);
}

unsigned tree_size (tree_node_t *root)
{
  unsigned size = 0;

  mylog(LOG_TREE, 9, "tree_size: begin: root %p\n", root);
  assert(tree_looks_valid(root));

  size = 0;
  tree_apply(root, tree__count, &size);
  assert(1 <= size);

  mylog(LOG_TREE, 9, "tree_size: returning size %u\n", size);
  return size;
}

struct list * tree_as_list (tree_node_t *root)
{
  struct list *list = NULL;
  unsigned size = 0;

  mylog(LOG_TREE, 9, "tree_as_list: begin: root %p\n", root);
  assert(tree_looks_valid(root));

  list = list_create();
  tree_apply(root, tree__add_node_to_list, list);

  /* Verify list looks OK. */
  assert(list_looks_valid(list));
  size = tree_size(root);
  assert(size == list_size(list));

  mylog(LOG_TREE, 9, "tree_as_list: returning list %p (size %u)\n", list, size);
  return list;
}

static void tree__add_node_to_list (tree_node_t *node, void *aux)
{
  struct list *list = (struct list *) aux;

  mylog(LOG_TREE, 9, "tree__add_node_to_list: begin: node %p aux %p\n", node, aux);
  assert(tree_looks_valid(node));
  assert(list_looks_valid(list));

  list_push_back(list, &node->tree_as_list_elem);
  mylog(LOG_TREE, 9, "tree__add_node_to_list: returning\n");
}

typedef struct tree__find_info_s
{
  void *aux;
  tree_find_func f;
  tree_node_t *match;
} tree__find_info_t;

tree_node_t * tree_find (tree_node_t *root, tree_find_func f, void *aux)
{
  tree__find_info_t tree__find_info;

  mylog(LOG_TREE, 9, "tree_find: begin: root %p aux %p\n", root, aux);

  assert(tree_looks_valid(root));
  assert(f);

  tree__find_info.aux = aux;
  tree__find_info.f = f;
  tree__find_info.match = NULL;

  tree_apply(root, tree__find_helper, &tree__find_info);

  mylog(LOG_TREE, 9, "tree_find: returning match %p\n", tree__find_info.match);
  return tree__find_info.match;
}

static void tree__find_helper (tree_node_t *node, void *aux)
{
  tree__find_info_t *tree__find_infoP = (tree__find_info_t *) aux;
  int is_match = 0;

  mylog(LOG_TREE, 9, "tree__find_helper: begin: node %p aux %p\n", node, aux);
  assert(tree_looks_valid(node));

  /* Already a match -- nothing to do. */
  if (tree__find_infoP->match)
    return;

  is_match = (*tree__find_infoP->f)(node, tree__find_infoP->aux);
  if (is_match)
    tree__find_infoP->match = node;

  mylog(LOG_TREE, 9, "tree__find_helper: returning (match %p)\n", tree__find_infoP->match);
  return;
}

/* TREE UNIT TEST. */
/* Tests tree APIs. */
typedef struct tree_UT_s
{
  int value;
  tree_node_t elem;
} tree_UT_t;

#define TREE_UT_FANOUT 5
void tree_UT (void)
{
  struct list *list = NULL;
  struct list_elem *list_e = NULL;
  int i, sort_order, counter;
  unsigned total_tree_size;
  tree_UT_t *node = NULL;
  tree_UT_t root, children[TREE_UT_FANOUT], grandchildren[TREE_UT_FANOUT*TREE_UT_FANOUT];

  mylog(LOG_TREE, 5, "tree_UT: begin\n"); 

  total_tree_size = 0;
  tree_init(&root.elem);
  root.value = total_tree_size;
  total_tree_size++;

  /* tree_add_child */

  /* Root gets TREE_UT_FANOUT children. */
  for (i = 0; i < TREE_UT_FANOUT; i++)
  {
    tree_init(&children[i].elem);
    children[i].value = total_tree_size;
    tree_add_child(&root.elem, &children[i].elem);
    total_tree_size++;
  }
  /* Each child gets TREE_UT_FANOUT children. */
  for (i = 0; i < TREE_UT_FANOUT*TREE_UT_FANOUT; i++)
  {
    tree_init(&grandchildren[i].elem);
    grandchildren[i].value = total_tree_size;
    tree_add_child(&children[i/TREE_UT_FANOUT].elem, &grandchildren[i].elem);
    total_tree_size++;
  }

  /* tree_is_root */
  assert(tree_is_root(&root.elem));
  assert(!tree_is_root(&children[0].elem));
  assert(!tree_is_root(&grandchildren[0].elem));

  /* tree_get_root */
  assert(tree_get_root(&root.elem) == &root.elem);
  assert(tree_get_root(&children[0].elem) == &root.elem);
  assert(tree_get_root(&grandchildren[0].elem) == &root.elem);

  /* tree_size */
  assert(tree_size(&root.elem) == total_tree_size);

  /* tree_as_list */
  list = tree_as_list(&root.elem);
  assert(list);
  assert(list_size(list) == total_tree_size);

  /* Let's try sorting! */
  sort_order = 0;
  list_sort(list, tree__UT_value_sort, &sort_order);
  node = tree_entry(list_entry(list_begin(list), tree_node_t, tree_as_list_elem), 
                    tree_UT_t, elem);
  assert(node == &root);
  counter = 0;
  for (list_e = list_begin(list); list_e != list_end(list); list_e = list_next(list_e))
  {
    node = tree_entry(list_entry(list_e, tree_node_t, tree_as_list_elem), 
                      tree_UT_t, elem);
    assert(node->value == counter);
    counter++;
  }

  sort_order = 1;
  list_sort(list, tree__UT_value_sort, &sort_order);
  node = tree_entry(list_entry(list_begin(list), tree_node_t, tree_as_list_elem), 
                    tree_UT_t, elem);
  assert(node == &grandchildren[TREE_UT_FANOUT*TREE_UT_FANOUT-1]);
  counter = total_tree_size - 1;
  for (list_e = list_begin(list); list_e != list_end(list); list_e = list_next(list_e))
  {
    node = tree_entry(list_entry(list_e, tree_node_t, tree_as_list_elem), 
                      tree_UT_t, elem);
    assert(node->value == counter);
    counter--;
  }

  list_destroy(list);

  /* tree_depth */
  assert(tree_depth(&root.elem) == 0);
  assert(tree_depth(&children[TREE_UT_FANOUT-1].elem) == 1);
  assert(tree_depth(&grandchildren[TREE_UT_FANOUT*TREE_UT_FANOUT-1].elem) == 2);

  /* tree_find: find each elem, then ensure that "no such elem" returns NULL. */
  counter = 0; /* This takes on the range of tree_UT_t.value's present in the tree. */
  assert(tree_find(&root.elem, tree__UT_find_func, &counter) == &root.elem); 
  for (i = 0; i < TREE_UT_FANOUT; i++)
  {
    counter++;
    assert(tree_find(&root.elem, tree__UT_find_func, &counter) == &children[i].elem); 
  }
  for (i = 0; i < TREE_UT_FANOUT*TREE_UT_FANOUT; i++)
  {
    counter++;
    assert(tree_find(&root.elem, tree__UT_find_func, &counter) == &grandchildren[i].elem); 
  }
  counter++;
  assert(tree_find(&root.elem, tree__UT_find_func, &counter) == NULL);

  /* tree_apply is implicitly exercised by tree_size and tree_as_list.
     tree_apply_up is implicitly exercised by tree_depth. */
  mylog(LOG_TREE, 5, "tree_UT: passed\n"); 
}

static int tree__UT_find_func (tree_node_t *node, void *aux)
{
  tree_UT_t *wrapper;
  int key;
  assert(node);
  assert(tree_looks_valid(node));

  key = *(int *) aux; 
  wrapper = tree_entry(node, tree_UT_t, elem);
  if (wrapper->value == key)
    return 1;
  return 0;
}

static int tree__UT_value_sort (struct list_elem *a, struct list_elem *b, void *aux)
{
  tree_node_t *a_tree, *b_tree;
  tree_UT_t *a_node, *b_node;
  int ret, sort_order;

  assert(a);
  assert(b);
  assert(aux);

  sort_order = *(int *) aux;

  a_tree = list_entry(a, tree_node_t, tree_as_list_elem);
  b_tree = list_entry(b, tree_node_t, tree_as_list_elem);

  a_node = tree_entry(a_tree, tree_UT_t, elem);
  b_node = tree_entry(b_tree, tree_UT_t, elem);

  if (a_node->value < b_node->value)
    ret = -1;
  else if (a_node->value == b_node->value)
    ret = 0;
  else
    ret = 1;

  if (sort_order == 0)
    return ret;
  else
    return -1*ret;
}

