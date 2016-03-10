#include "mytree.h"

#include <assert.h>
#include <stddef.h> /* NULL */
#include <string.h> /* memset */

#define TREE_NODE_MAGIC 88771122

/* Private functions. */
static int tree__looks_valid (const tree_node_t *node);

/* These are helper tree_apply[_ancestors] functions. */
static void tree__add_node_to_list (tree_node_t *node, void *aux);
static void tree__count (tree_node_t *node, void *aux);
static void tree__find_helper (tree_node_t *node, void *aux);

/* For tree_UT, for use with tree_as_list. 
   If *(int *) AUX == 0, sorts on 'a value' < 'b value', else the other way. */
static int tree__UT_value_sort (struct list_elem *a, struct list_elem *b, void *aux);
static int tree__UT_find_func (tree_node_t *e, void *aux);

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
  node->child_num = 0;
  node->children = list_create();
}

/* Interactions. */
void tree_add_child (tree_node_t *parent, tree_node_t *child)
{
  assert(parent);
  assert(child);
  assert(tree__looks_valid(parent));
  assert(tree__looks_valid(child));

  list_lock(parent->children);
  child->child_num = list_size(parent->children);
  list_push_back(parent->children, &child->parent_child_list_elem);
  child->parent = parent;
  list_unlock(parent->children);
}

int tree_depth (tree_node_t *node)
{
  int depth;

  assert(node);
  assert(tree__looks_valid(node));

  depth = -1;
  tree_apply_up(node, tree__count, &depth);
  assert(0 <= depth);
  return depth;
}

/* Utility. */
int tree_is_root (tree_node_t *node)
{
  assert(node);
  assert(tree__looks_valid(node));
  return (node->parent == NULL);
}

unsigned tree_get_child_num (tree_node_t *node)
{
  assert(node);
  assert(tree__looks_valid(node));
  return node->child_num;
}

void tree_apply (tree_node_t *root, tree_apply_func f, void *aux)
{
  struct list_elem *e;

  if (!root)
    return;
  assert(tree__looks_valid(root));

  (*f)(root, aux);
  for (e = list_begin(root->children); e != list_end(root->children); e = list_next(e))
    tree_apply(list_entry(e, tree_node_t, parent_child_list_elem), f, aux);
}

void tree_apply_up (tree_node_t *leaf, tree_apply_func f, void *aux)
{
  struct list_elem *e;

  if (!leaf)
    return;
  assert(tree__looks_valid(leaf));

  (*f)(leaf, aux);
  tree_apply_up(leaf->parent, f, aux);
}

tree_node_t * tree_get_root (tree_node_t *node)
{
  tree_node_t *ret;

  assert(node);
  assert(tree__looks_valid(node));

  if (!node->parent)
    ret = node;
  else
    ret = tree_get_root(node->parent);
  return ret;
}

tree_node_t * tree_get_parent (tree_node_t *node)
{
  assert(node);
  assert(tree__looks_valid(node));
  return (node->parent);
}

static void tree__count (tree_node_t *node, void *aux)
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

tree_node_t * tree_find (tree_node_t *root, tree_find_func f, void *aux)
{
  /* Matches the declaration in tree__find_helper. */
  struct tree__find_info_s
  {
    void *aux;
    tree_find_func f;
    tree_node_t *match;
  } tree__find_info;

  assert(root);
  assert(tree__looks_valid(root));
  assert(f);

  tree__find_info.aux = aux;
  tree__find_info.f = f;
  tree__find_info.match = NULL;

  tree_apply(root, tree__find_helper, &tree__find_info);
  return tree__find_info.match;
}

/* Tests tree APIs. */
typedef struct tree_UT_s
{
  int value;
  tree_node_t elem;
} tree_UT_t;

#define TREE_UT_FANOUT 5
void tree_UT (void)
{
  struct list *list;
  struct list_elem *list_e;
  int i, sort_order, counter, key;
  unsigned total_tree_size;
  tree_UT_t *node;
  tree_UT_t root, children[TREE_UT_FANOUT], grandchildren[TREE_UT_FANOUT*TREE_UT_FANOUT];

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

static void tree__find_helper (tree_node_t *node, void *aux)
{
  int is_match;
  /* Matches the declaration in tree_find. */
  struct tree__find_info_s
  {
    void *aux;
    tree_find_func f;
    tree_node_t *match;
  } *tree__find_infoP;

  assert(node);
  assert(tree__looks_valid(node));

  tree__find_infoP = (struct tree__find_info_s *) aux;

  /* Already a match -- nothing to do. */
  if (tree__find_infoP->match)
    return;

  is_match = (*tree__find_infoP->f)(node, tree__find_infoP->aux);
  if (is_match)
    tree__find_infoP->match = node;
  return;
}

static int tree__UT_find_func (tree_node_t *node, void *aux)
{
  tree_UT_t *wrapper;
  int key;
  assert(node);
  assert(tree__looks_valid(node));

  key = *(int *) aux; 
  wrapper = tree_entry(node, tree_UT_t, elem);
  if (wrapper->value == key)
    return 1;
  return 0;
}
