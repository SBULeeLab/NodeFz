#include "list.h"

#include "mylog.h"

#include <assert.h>
#include <stddef.h> /* NULL */
#include <stdlib.h> /* malloc */
#include <string.h> /* memset */

#include "uv-common.h" /* Allocators */

#define LIST_MAGIC 12345678
#define LIST_ELEM_MAGIC 12345699

/* Private functions. */
static void list_init (struct list *list);
static struct list_elem * list_tail (struct list *list);
static void list_insert (struct list_elem *, struct list_elem *);
static void list__lock (struct list *list);
static void list__unlock (struct list *list);

/* Swap the locations of A and B in the list. */
static void list__swap (struct list_elem *a, struct list_elem *b);

/* Returns non-zero if sorted in least-to-greatest order (a <= b <= c <= ...), else 0. */
static int list__sorted (struct list *list, list_sort_func f, void *aux);

static int list_elem_looks_valid (struct list_elem *e)
{
  int valid = 1;

  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_elem_looks_valid: begin: list_elem %p\n", e));
  if (!e)
  {
    valid = 0;
    goto DONE;
  }

  if (e->magic != LIST_ELEM_MAGIC)
  {
    valid = 0;
    goto DONE;
  }

  valid = 1;
  DONE:
    ENTRY_EXIT_LOG((LOG_LIST, 9, "list_elem_looks_valid: returning valid %i\n", valid));
    return valid;
}

/* Allocate and initialize a list. */
struct list * list_create (void)
{
  struct list *ret = (struct list *) uv__malloc(sizeof *ret);

  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_create: begin\n"));

  assert(ret);
#if JD_DEBUG_FULL
  memset(ret, 0, sizeof *ret);
#endif

  list_init(ret);
  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_create: returning list %p\n", ret));
  return ret;
}

/* Initialize HEAD and TAIL to be members of an empty list. */
static void list_init (struct list *list)
{
  pthread_mutexattr_t attr;

  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_init: begin: list %p\n", list));
  assert(list);

  /* mylog ("list_init: Initializing list %p (done_list %p)\n", list, &done_list); */
  list->magic = LIST_MAGIC;

  list->head.next = &list->tail;
  list->head.prev = NULL;
  list->head.magic = LIST_ELEM_MAGIC;

  list->tail.next = NULL;
  list->tail.prev = &list->head;
  list->tail.magic = LIST_ELEM_MAGIC;

  pthread_mutex_init (&list->lock, NULL);

  /* Recursive internal lock. */
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init (&list->_lock, &attr);
  pthread_mutexattr_destroy(&attr);

  list->n_elts = 0;
  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_init: returning\n"));
}

/* Cleans up LIST. Does not modify any nodes contained in LIST. 
   You must call list_init again if you wish to re-use LIST. */
void list_destroy (struct list *list)
{
  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_destroy: begin: list %p\n", list));
  assert(list_looks_valid(list));

  list__lock(list);

  list->n_elts = 0;
  list->head.next = &list->tail;
  list->head.prev = NULL;
  list->tail.next = NULL;
  list->tail.prev = &list->head;

  list__unlock(list);

  pthread_mutex_destroy(&list->lock);
  pthread_mutex_destroy(&list->_lock);

  memset(list, 'a', sizeof *list);
  uv__free(list);
  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_destroy: returning\n"));
}

void list_destroy_full (struct list *list, list_destroy_func f, void *aux)
{
  struct list_elem *e = NULL;

  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_destroy_full: begin: list %p aux %p\n", list, aux));
  assert(list_looks_valid(list));
  if (f)
  {
    while (!list_empty(list))
    {
      e = list_pop_front(list);
      assert(list_elem_looks_valid(e));
      (*f)(e, aux);
    }
  }

  list_destroy(list);
  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_destroy_full: returning\n"));
}

/* Insert NEW just before NEXT. */
static void list_insert (struct list_elem *new_elem, struct list_elem *next)
{
  assert(new_elem);
  assert(next);

  /* First insertion into a list? */
  if (!list_elem_looks_valid(new_elem))
    new_elem->magic = LIST_ELEM_MAGIC;

  assert(list_elem_looks_valid(new_elem));
  assert(list_elem_looks_valid(next));

  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_insert: begin: new_elem %p next %p\n", new_elem, next));

  next->prev->next = new_elem;

  new_elem->prev = next->prev;
  new_elem->next = next;

  next->prev = new_elem;
  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_insert: returning\n"));
}

/* Remove ELEM from its current list. */
struct list_elem * list_remove (struct list *list, struct list_elem *elem)
{
  struct list_elem *pred = NULL, *succ = NULL; 

  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_remove: begin: list %p elem %p\n", list, elem));
  assert(list_looks_valid(list));
  assert(list_elem_looks_valid(elem));

  list__lock(list);

  assert(!list_empty(list));
  pred = elem->prev;
  assert(pred);
  succ = elem->next;
  assert(succ);

  pred->next = succ;
  succ->prev = pred;
  list->n_elts--;

  list__unlock(list);

  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_remove: returning elem %p\n", elem));
  return elem;
}

/* Put ELEM at the end of the list. ELEM must not be NULL. */
void list_push_back (struct list *list, struct list_elem *elem)
{
  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_push_back: begin: list %p elem %p\n", list, elem));
  assert(list_looks_valid(list));
  assert(elem);

  list__lock(list);

  list_insert(elem, list_tail(list));
  list->n_elts++;

  list__unlock(list);

  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_push_back: returning\n"));
}

/* Put ELEM at the front of the list. ELEM must not be NULL. */
void list_push_front (struct list *list, struct list_elem *elem)
{
  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_push_front: begin: list %p elem %p\n", list, elem));
  assert(list_looks_valid(list));
  assert(elem);

  list__lock(list);

  list_insert(elem, list_begin (list));
  list->n_elts++;

  list__unlock(list);
  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_push_front: returning\n"));
}

/* Return the element after ELEM. */
struct list_elem * list_next (struct list_elem *elem)
{
  struct list_elem *n = NULL;

  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_next: begin: elem %p\n", elem));
  assert(list_elem_looks_valid(elem));
  n = elem->next;
  assert(list_elem_looks_valid(n));
  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_next: returning next %p\n", n));
  return n;
}

/* Return the list_elem at the front of LIST.
   Returns NULL if LIST is empty. 

   Look but don't touch. */
struct list_elem * list_front (struct list *list)
{
  struct list_elem *node = NULL;

  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_front: begin: list %p\n", list));
  assert(list_looks_valid(list));

  list__lock(list);

  node = list->head.next;
  assert(list_elem_looks_valid(node));
  if (node == &list->tail)
    node = NULL;

  list__unlock(list);

  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_front: returning front %p\n", node));
  return node;
}

/* Return the list_elem at the back of LIST.
   Returns NULL if LIST is empty. 

   Look but don't touch. */
struct list_elem * list_back (struct list *list)
{
  struct list_elem *node = NULL;

  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_back: begin: list %p\n", list));
  assert(list_looks_valid(list));

  list__lock(list);

  node = list->tail.prev;
  assert(list_elem_looks_valid(node));
  if (node == &list->head)
    node = NULL;

  list__unlock(list);
  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_back: returning node %p\n", node));
  return node;
}

/* Return the element at the front of the queue, or NULL if empty. */
struct list_elem * list_pop_front (struct list *list)
{
  struct list_elem *ret = NULL;

  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_pop_front: begin: list %p\n", list));
  assert(list_looks_valid(list));

  list__lock(list);

  if (list_empty(list))
    ret = NULL;
  else
  {
    ret = list_remove(list, list_front(list));
    assert(list_elem_looks_valid(ret));
  }

  list__unlock(list);

  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_pop_front: returning front %p\n", ret));
  return ret;
}

/* Return the element at the back of the queue, or NULL if empty. */
struct list_elem * list_pop_back (struct list *list)
{
  struct list_elem *ret = NULL;

  assert(list_looks_valid(list));
  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_pop_back: begin: list %p\n", list));

  list__lock(list);

  if (list_empty(list))
    ret = NULL;
  else
  {
    ret = list_remove(list, list_back(list));
    assert(list_elem_looks_valid(ret));
  }

  list__unlock(list);

  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_pop_back: returning back %p\n", ret));
  return ret;
}

/* Return the first SPLIT_SIZE elements of LIST in their own dynamically-allocated list.
   The caller is responsible for free'ing the returned list.
   The rest of the LIST remains in LIST. */
struct list * list_split (struct list *list, unsigned split_size)
{
  unsigned i = 0, n_elts_moved = 0;
  struct list_elem *elem = NULL;
  struct list *front_list = NULL;
  unsigned orig_size = 0;

  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_split: begin: list %p size %u\n", list, split_size));
  assert(list_looks_valid(list));
  list__lock(list);

  orig_size = list_size(list);
  assert(split_size <= orig_size);

  front_list = list_create();

  n_elts_moved = 0;
  for (i = 0; i < split_size; i++)
  {
    elem = list_pop_front(list);
    assert(list_elem_looks_valid(elem));
    list_push_back(front_list, elem);
    n_elts_moved++;

    assert(list_size(list) == (orig_size - n_elts_moved));
    assert(list_size(front_list) == n_elts_moved);
  }

  assert(list_size(front_list) == split_size);
  assert(list_size(front_list) + list_size(list) == orig_size);

  list__unlock(list);

  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_split: returning front_list %p\n", front_list));
  return front_list;
}

void list_concat (struct list *front, struct list *back)
{
  struct list_elem *e = NULL;
  unsigned expected_front_size = 0; 

  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_concat: begin: front %p back %p\n", front, back));
  assert(list_looks_valid(front));
  assert(list_looks_valid(back));
  assert(front != back);

  expected_front_size = list_size(front) + list_size(back);

  /* This isn't the most efficient implementation ever. 
     We could take advantage of the list structure to do this with a few pointer swaps.
     However, this is safer because it uses APIs instead of relying on the internal list design, and I'm in a hurry. */
  while (!list_empty(back))
  {
    e = list_pop_front(back);
    assert(list_elem_looks_valid(e));
    list_push_back(front, e);
  }

  assert(list_empty(back));
  assert(list_size(front) == expected_front_size);

  list_destroy(back);
  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_concat: returning\n"));
}

/* In a non-empty list, returns the first element.
  In an empty list, returns the tail. */
struct list_elem * list_begin (struct list *list)
{
  struct list_elem *ret = NULL;

  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_begin: begin: list %p\n", list));
  assert(list_looks_valid(list));

  list__lock(list);

  ret = list->head.next;
  assert(list_elem_looks_valid(ret));

  list__unlock(list);

  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_begin: returning begin %p\n", ret));
  return ret;
}

/* Returns the tail (one past the final element). */
struct list_elem * list_end (struct list *list)
{
  struct list_elem *ret = NULL;

  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_end: begin\n"));
  assert(list_looks_valid(list));

  ret = list_tail(list);
  assert(list_elem_looks_valid(ret));

  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_end: returning end %p\n", ret));
  return ret;
}

/* Returns the head of the list. */
struct list_elem * list_head (struct list *list)
{
  struct list_elem *ret = NULL;

  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_head: begin: list %p\n", list));
  assert(list_looks_valid(list));

  ret = &list->head;
  assert(list_elem_looks_valid(ret));
  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_head: returning head %p\n", ret));
  return ret;
}

/* Returns the tail of the list. */
static struct list_elem * list_tail (struct list *list)
{
  struct list_elem *ret = NULL;

  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_tail: begin: list %p\n", list));
  assert(list_looks_valid(list));

  ret = &list->tail;
  assert(list_elem_looks_valid(ret));
  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_tail: returning tail %p\n", ret));
  return ret;
}

/* Return the size of the list. */
unsigned list_size (struct list *list)
{
  unsigned size = 0;

  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_size: begin: list %p\n", list));
  assert(list_looks_valid(list));

  list__lock(list);

#if JD_DEBUG_FULL
  {
    struct list_elem *e = NULL;
    /* DEBUG: Verify LIST->N_ELTS is correct. */
    size = 0;
    for (e = list_begin(list); e != list_end(list); e = list_next(e))
    {
      assert(list_elem_looks_valid(e));
      size++;
    }
    assert(size == list->n_elts);
  }
#endif
  size = list->n_elts;

  list__unlock(list);

  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_size: returning size %u\n", size));
  return size;
}

/* Return 1 if empty, 0 else. */
int list_empty (struct list *list)
{
  int empty = 0;

  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_empty: begin: list %p\n", list));
  assert(list_looks_valid(list));

  list__lock(list);

  empty = (list->head.next == &list->tail);
  if (empty)
    assert(0 == list->n_elts);
  else
    assert(0 < list->n_elts);

  list__unlock(list);

  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_empty: returning empty %i\n", empty));
  return empty;
}

/* Return 1 if initialized LIST looks valid, 0 else.
   This is a pretty minimal test. */
int list_looks_valid (struct list *list)
{
  int is_valid = 0;

  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_looks_valid: begin: list %p\n", list));

  if (!list)
  {
    is_valid = 0;
    goto DONE;
  }
  /* Magic must be correct. */
  if (list->magic != LIST_MAGIC)
  {
    is_valid = 0;
    goto DONE;
  }

  /* These tests are thread safe unless the list is concurrently list_destroy'd. */
  is_valid = 1;
  /* Head's next and tail's prev should not be null. */
  if (list->head.next == NULL || list->tail.prev == NULL)
    is_valid = 0;
  /* Head's prev and tail's next should be null. */
  if (list->head.prev != NULL || list->tail.next != NULL)
    is_valid = 0;

  DONE:
    ENTRY_EXIT_LOG((LOG_LIST, 9, "list_looks_valid: returning is_valid %i\n", is_valid));
    return is_valid;
}

/* For external locking. */
void list_lock (struct list *list)
{
  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_lock: begin: list %p\n", list));
  assert(list_looks_valid(list));

  pthread_mutex_lock(&list->lock);
  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_lock: returning\n"));
}

/* For external locking. */
void list_unlock (struct list *list)
{
  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_unlock: begin: list %p\n", list));
  assert(list_looks_valid(list));

  pthread_mutex_unlock (&list->lock);
  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_unlock: returning\n"));
}

/* For internal locking. */
static void list__lock (struct list *list)
{
  ENTRY_EXIT_LOG((LOG_LIST, 9, "list__lock: begin: list %p\n", list));
  assert(list);
  assert(list->magic == LIST_MAGIC);

  pthread_mutex_lock(&list->_lock);
  ENTRY_EXIT_LOG((LOG_LIST, 9, "list__lock: returning\n"));
}

/* For internal locking. */
static void list__unlock (struct list *list)
{
  ENTRY_EXIT_LOG((LOG_LIST, 9, "list__unlock: begin: list %p\n", list));
  assert(list);
  assert(list->magic == LIST_MAGIC);

  pthread_mutex_unlock (&list->_lock);
  ENTRY_EXIT_LOG((LOG_LIST, 9, "list__unlock: returning\n"));
}

/* Apply F to each element in LIST. */
void list_apply (struct list *list, list_apply_func apply_func, void *aux)
{
  struct list_elem *e = NULL;

  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_apply: begin: list %p aux %p\n", list, aux));
  assert(list_looks_valid(list));
  assert(apply_func);

  list__lock(list);

  for (e = list_begin(list); e != list_end(list); e = list_next(e))
  {
    assert(list_elem_looks_valid(e));
    (*apply_func)(e, aux);
  }

  list__unlock(list);

  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_apply: returning\n"));
}

void list_sort (struct list *list, list_sort_func sort_func, void *aux)
{
  struct list_elem *a = NULL, *b = NULL;
  int sorted = 0;

  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_sort: begin: list %p aux %p\n", list, aux));
  assert(list_looks_valid(list));
  assert(sort_func);

  list__lock(list);

  /* Bubble sort.
    Until the list is sorted, find an out-of-order pair and swap them.
    O(n^2), but this is expected to be a run-once operation so NBD. */
  sorted = 0;
  while (!sorted)
  {
    assert(list_looks_valid(list));

    sorted = 1; /* Assume we're done until proved otherwise. */
    /* Each pass swaps all pairs of out-of-order neighbors. */
    for (a = list_begin(list); a != list_end(list); a = list_next(a))
    {
      assert(list_elem_looks_valid(a));
      b = list_next(a);
      assert(list_elem_looks_valid(b));

      if (b == list_end(list))
        break;
      /* a precedes b, but b < a. */
      if ((*sort_func)(a, b, aux) == 1)
      {
        list__swap(a, b);
        sorted = 0;
      }
    }
  }

  /* If we reach this point, we've done a pairwise comparison of every elem in the list.
     Each element is appropriately ordered relative to its neighbor; transitively, the list is sorted. */
  assert(list__sorted(list, sort_func, aux));

  list__unlock(list);
  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_sort: returning\n"));
}

struct list * list_filter (struct list *list, list_filter_func filter_func, void *aux)
{
  struct list *filtered_nodes = NULL;
  struct list_elem *e = NULL, *next = NULL;

  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_filter: begin: list %p aux %p\n", list, aux));
  assert(list_looks_valid(list));
  assert(filter_func);

  filtered_nodes = list_create();

  list__lock(list);
  for(e = list_begin(list); e != list_end(list); e = next)
  {
    next = list_next(e);
    assert(list_elem_looks_valid(e));
    if (! (*filter_func)(e, aux))
    {
      list_remove(list, e);
      list_push_back(filtered_nodes, e);
    }
  }
  list__unlock(list);

  assert(list_looks_valid(filtered_nodes));
  ENTRY_EXIT_LOG((LOG_LIST, 9, "list_filter: returning filtered-out nodes %p\n", filtered_nodes));
  return filtered_nodes;
}

/* Return non-zero if LIST is sorted based on F and AUX.
   Not thread safe. */
static int list__sorted (struct list *list, list_sort_func sort_func, void *aux)
{
  struct list_elem *a = NULL, *b = NULL;
  int sorted = 1;

  ENTRY_EXIT_LOG((LOG_LIST, 9, "list__sorted: begin: list %p aux %p\n", list, aux));
  assert(list_looks_valid(list));
  assert(sort_func);

  if (list_size(list) <= 1)
  {
    sorted = 1;
    goto DONE;
  }

  a = list_begin(list);
  b = list_next(a);
  while (b != list_end(list))
  {
    assert(list_elem_looks_valid(a));
    assert(list_elem_looks_valid(b));
    if ((*sort_func)(a, b, aux) == 1)
    {
      /* a > b: the list is not sorted. */
      sorted = 0;
      break;
    }
    a = b;
    b = list_next(b);
  }

  DONE:
    ENTRY_EXIT_LOG((LOG_LIST, 9, "list__sorted: returning sorted %i\n", sorted));
    return sorted;
}

/* Exchange the positions of A and B in their list.
   A and B must be in the same list.
   Not thread safe. */
static void list__swap (struct list_elem *a, struct list_elem *b)
{
  struct list_elem *orig_a_next = NULL, *orig_a_prev = NULL, *orig_b_next = NULL, *orig_b_prev = NULL, *e = NULL;
  int neighbors = 0;

  ENTRY_EXIT_LOG((LOG_LIST, 9, "list__swap: begin: a %p b %p\n", a, b));
  assert(list_elem_looks_valid(a));
  assert(list_elem_looks_valid(b));

  neighbors = (a->next == b || b->next == a);
  if (neighbors)
  {
    /* The full treatment below gets us all tangled because the same node fills multiple roles
        (e.g. orig_a_next == b). */
    if (b->next == a)
    {
      /* Arrange it so that a -> b for clarity. */
      e = a;
      a = b;
      b = e;
    }
    assert(a->next == b);
    assert(b->prev == a);

    /* Update outer neighbors. */
    a->prev->next = b; 
    b->next->prev = a;

    /* Update inner pointers. */
    a->next = b->next;
    b->prev = a->prev;

    b->next = a;
    a->prev = b;
  }
  else
  {
    /* Remember original neighbors. */
    orig_a_next = a->next;
    orig_a_prev = a->prev;
    orig_b_next = b->next;
    orig_b_prev = b->prev;

    /* Update next/prev of neighbors. */
    orig_a_prev->next = b;
    orig_a_next->prev = b;
    
    orig_b_prev->next = a;
    orig_b_next->prev = a;

    /* Swap neighbor pointers. */
    a->prev = orig_b_prev;
    a->next = orig_b_next;

    b->prev = orig_a_prev;
    b->next = orig_a_next;
  }

  ENTRY_EXIT_LOG((LOG_LIST, 9, "list__swap: returning\n"));
}

/* UNIT TESTING */
typedef struct list_UT_elem_s
{
  unsigned info;
  struct list_elem elem;
} list_UT_elem_t;

static void list_UT_apply_doubler (struct list_elem *e, void *aux)
{
  list_UT_elem_t *item = NULL; 

  assert(e);
  item = list_entry(e, list_UT_elem_t, elem);
  assert(item);
  item->info *= 2;
}

static int list_UT_sort_large_to_small (struct list_elem *a, struct list_elem *b, void *aux)
{
  int a_bigger = 0, a_smaller = 0;
  list_UT_elem_t *item_a = NULL, *item_b = NULL; 

  assert(a);
  assert(b);

  item_a = list_entry(a, list_UT_elem_t, elem);
  item_b = list_entry(b, list_UT_elem_t, elem);
  assert(item_a);
  assert(item_b);

  a_bigger  = (item_b->info < item_a->info);
  a_smaller = (item_a->info < item_b->info);

  if (a_bigger)
    return -1;
  else if (!a_bigger && !a_smaller)
    return 0;
  else
    return 1;
}

static int list_UT_filter_out_evens (struct list_elem *e, void *aux)
{
  list_UT_elem_t *item = NULL; 

  assert(e);
  item = list_entry(e, list_UT_elem_t, elem);
  assert(item);

  return (item->info % 2 == 1);
}

/* Unit test for the list class. */
void list_UT (void)
{
  struct list *l = NULL, *l_evens = NULL;
  unsigned i, n_entries, expected_value;
  struct list_elem *e = NULL;
  list_UT_elem_t entries[100];
  list_UT_elem_t *entry = NULL;

  n_entries = 100;

  mylog(LOG_LIST, 5, "list_UT: begin\n"); 

  /* Create and destroy an empty list. */
  l = list_create();
  assert(list_looks_valid(l) == 1);
  assert(list_size(l) == 0);
  assert(list_empty(l) == 1);

  list_destroy(l);
  assert(list_looks_valid(l) == 0);

  /* Create and populate a list. */
  l = list_create();
  for (i = 0; i < n_entries; i++)
  {
    entries[i].info = i;
    list_push_back(l, &entries[i].elem);
    assert(list_size(l) == i+1);
  }
  assert(list_size(l) == n_entries);

  /* Iterate over it forwards and verify each element. */
  i = 0;
  for (e = list_begin(l); e != list_end(l); e = list_next(e))
  {
    entry = list_entry(e, list_UT_elem_t, elem);
    assert(entry->info == i);
    assert(entry == &entries[i]);
    i++;
  }

  /* Sort in reverse order. */
  list_sort(l, list_UT_sort_large_to_small, NULL);
  assert(list__sorted(l, list_UT_sort_large_to_small, NULL));

  i = n_entries-1;
  for (e = list_begin(l); e != list_end(l); e = list_next(e))
  {
    entry = list_entry(e, list_UT_elem_t, elem);
    assert(entry->info == i);
    assert(entry == &entries[i]);
    i--;
  } 
  
  /* Filter out evens. */
  l_evens = list_filter(l, list_UT_filter_out_evens, NULL);
  assert(list_size(l) == n_entries/2);
  assert(list_size(l_evens) == n_entries/2);

  /* l: 99, 97, ... , 1 */
  /* l_evens: 98, 96, ..., 2 */
  i = n_entries-1;
  for (e = list_begin(l); e != list_end(l); e = list_next(e))
  {
    entry = list_entry(e, list_UT_elem_t, elem);
    assert(entry->info == i);
    assert(entry == &entries[i]);
    i -= 2;
  }

  i = n_entries-2;
  for (e = list_begin(l_evens); e != list_end(l_evens); e = list_next(e))
  {
    entry = list_entry(e, list_UT_elem_t, elem);
    assert(entry->info == i);
    assert(entry == &entries[i]);
    i -= 2;
  }

  /* Combine lists: 99, 97, ..., 1, 98, 96, ..., 2 */
  list_concat(l, l_evens);
  assert(!list_looks_valid(l_evens));

  i = 0;
  for (e = list_begin(l); e != list_end(l); e = list_next(e))
  {
    if (i < n_entries/2)
      expected_value = ((n_entries-1) - 2*i);
    else
      expected_value = ((n_entries-2) - 2*(i - n_entries/2));

    entry = list_entry(e, list_UT_elem_t, elem);
    assert(entry);
    assert(entry->info == expected_value);
    assert(entry == &entries[expected_value]);

    i++;
  }

  /* Double the value of each entry in the list, then sort for ease of verification. */
  list_apply(l, list_UT_apply_doubler, NULL);
  list_sort(l, list_UT_sort_large_to_small, NULL);

  i = n_entries-1;
  for (e = list_begin(l); e != list_end(l); e = list_next(e))
  {
    expected_value = i*2;
    entry = list_entry(e, list_UT_elem_t, elem);
    assert(entry->info == expected_value);
    assert(entry == &entries[i]);
    i--;
  } 

  /* Empty the list, popping off the back, and verify each element. 
     Recall that now the list is sorted with large at the front and small at the back. */
  assert(list_size(l) == n_entries);
  i = 0;
  while (!list_empty(l))
  {
    expected_value = i*2;
    assert(list_size(l) == n_entries - i);

    e = list_pop_back(l);
    entry = list_entry(e, list_UT_elem_t, elem);

    assert(entry->info == expected_value);
    assert(entry == &entries[i]);
    i++;
  }
  assert(list_empty(l) == 1);
  assert(list_size(l) == 0);

  /* Lock and unlock. */
  list_lock(l);
  list_unlock(l);

  list_destroy(l);
  assert(list_looks_valid(l) == 0);

  mylog(LOG_LIST, 5, "list_UT: passed\n"); 
}
