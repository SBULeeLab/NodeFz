#include "mytree.h"

#include <assert.h>
#include <stddef.h> /* NULL */
#include <string.h> /* memset */

#define TREE_NODE_MAGIC 88771122

/* Private functions. */
static int tree__looks_valid (const tree_node_t *node);

/* These are helper tree_apply functions. */
static void tree__add_node_to_list (tree_node_t *node, void *aux);
void tree__count (tree_node_t *node, void *aux);

/* For tree_UT, for use with tree_as_list. 
   If *(int *) AUX == 0, sorts on 'a value' < 'b value', else the other way. */
static int tree__UT_value_sort (struct list_elem *a, struct list_elem *b, void *aux);

static int tree__looks_valid (const tree_node_t *node)
{
  if (!node)
    return 0;
  if (node->magic != TREE_NODE_MAGIC)
    return 0;
  return 1;
}

/* Public functions. */
/* Create/destroy APIs. */
void tree_init (tree_node_t *node)
{
  assert(node);
  memset(node, 0, sizeof *node);

  node->magic = TREE_NODE_MAGIC;
  node->parent = NULL;
  node->children = list_create();
}

/* Interactions. */
void tree_add_child (tree_node_t *parent, tree_node_t *child)
{
  assert(parent);
  assert(child);
  assert(tree__looks_valid(parent));
  assert(tree__looks_valid(child));

  list_push_back(parent->children, &child->parent_child_list_elem);
  child->parent = parent;
  child->child_num = list_size(parent->children);
}

/* Utility. */
int tree_is_root (tree_node_t *node)
{
  assert(tree__looks_valid(node));
  return (node->parent == NULL);
}

void tree_apply (tree_node_t *root, tree_apply_func f, void *aux)
{
  struct list_elem *e;
  if (!root)
    return;
  (*f)(root, aux);
  for (e = list_begin(root->children); e != list_end(root->children); e = list_next(e))
    tree_apply(list_entry(e, tree_node_t, parent_child_list_elem), f, aux);
}

tree_node_t * tree_get_root (tree_node_t *node)
{
  tree_node_t *ret;
  assert(node);
  if (!node->parent)
    ret = node;
  else
    ret = tree_get_root(node->parent);
  return ret;
}

void tree__count (tree_node_t *node, void *aux)
{
  unsigned *counter;
  counter = (unsigned *) aux;
  *counter = *counter + 1;
}

unsigned tree_size (const tree_node_t *root)
{
  unsigned size;

  assert(root);
  assert(tree__looks_valid(root));

  size = 0;
  tree_apply(root, tree__count, &size);
  assert(1 <= size);
  return size;
}

struct list * tree_as_list (tree_node_t *root)
{
  struct list *list;

  assert(root);
  assert(tree__looks_valid(root));

  list = list_create();
  tree_apply(root, tree__add_node_to_list, list);
  return list;
}

/* Tests tree APIs. */
typedef struct tree_UT_s
{
  int value;
  tree_node_t elem;
} tree_UT_t;

void tree_UT (void)
{
  struct list *list;
  struct list_elem *list_e;
  #define FANOUT 5
  int i, sort_order, counter;
  unsigned total_tree_size;
  tree_UT_t *node;
  tree_UT_t root, children[FANOUT], grandchildren[FANOUT*FANOUT];

  total_tree_size = 0;
  tree_init(&root.elem);
  root.value = total_tree_size;
  total_tree_size++;

  /* tree_add_child */

  /* Root gets FANOUT children. */
  for (i = 0; i < FANOUT; i++)
  {
    tree_init(&children[i].elem);
    children[i].value = total_tree_size;
    tree_add_child(&root.elem, &children[i].elem);
    total_tree_size++;
  }
  /* Each child gets FANOUT children. */
  for (i = 0; i < FANOUT*FANOUT; i++)
  {
    tree_init(&grandchildren[i].elem);
    grandchildren[i].value = total_tree_size;
    tree_add_child(&children[i/FANOUT].elem, &grandchildren[i].elem);
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
  assert(node == &grandchildren[FANOUT*FANOUT-1]);
  counter = total_tree_size - 1;
  for (list_e = list_begin(list); list_e != list_end(list); list_e = list_next(list_e))
  {
    node = tree_entry(list_entry(list_e, tree_node_t, tree_as_list_elem), 
                      tree_UT_t, elem);
    assert(node->value == counter);
    counter--;
  }

  list_destroy(list);

  /* tree_apply is implicitly exercised by tree_size and tree_as_list */
}

static void tree__add_node_to_list (tree_node_t *node, void *aux)
{
  struct list *list;
  assert(node);
  assert(aux);

  list = (struct list *) aux;
  list_push_back(list, &node->tree_as_list_elem);
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
