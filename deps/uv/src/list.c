#include "list.h"

#include <assert.h>
#include <stddef.h> /* NULL */
#include <stdlib.h> /* malloc */

#define LIST_MAGIC 12345678

static void list_init (struct list *list);
static struct list_elem * list_tail (const struct list *list);
static void list_insert (struct list_elem *, struct list_elem *);
static void list__lock (struct list *list);
static void list__unlock (struct list *list);

/* Allocate and initialize a list. */
struct list * list_create (void)
{
  struct list *ret;
  ret = (struct list *) malloc(sizeof *ret);
  assert(ret != NULL);
  memset(ret, 0, sizeof *ret);
  list_init(ret);

  return ret;
}

/* Initialize HEAD and TAIL to be members of an empty list. */
static void list_init (struct list *list)
{
  pthread_mutexattr_t attr;

  assert(list != NULL);

  /* mylog ("list_init: Initializing list %p (done_list %p)\n", list, &done_list); */
  list->magic = LIST_MAGIC;

  list->head.next = &list->tail;
  list->head.prev = NULL;
  list->tail.next = NULL;
  list->tail.prev = &list->head;

  pthread_mutex_init (&list->lock, NULL);

  /* Recursive internal lock. */
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init (&list->_lock, &attr);

  list->n_elts = 0;
}

/* Cleans up LIST. Does not modify any nodes contained in LIST. 
   You must call list_init again if you with to re-use LIST. */
void list_destroy (struct list *list)
{
  assert(list != NULL);
  list__lock(list);

  list->magic = 0;
  list->n_elts = 0;
  list->head.next = &list->tail;
  list->head.prev = NULL;
  list->tail.next = NULL;
  list->tail.prev = &list->head;

  list__unlock(list);

  pthread_mutex_destroy(&list->lock);
  pthread_mutex_destroy(&list->_lock);
}

void list_destroy_full (struct list *list, list_destroy_func f, void *aux)
{
  struct list_elem *e;

  assert(list);
  if (f)
  {
    while (!list_empty(list))
    {
      e = list_pop_front(list);
      (*f)(e, aux);
    }
  }

  list_destroy(list);
}

/* Insert NEW just before NEXT. */
static void list_insert (struct list_elem *new_elem, struct list_elem *next)
{
  assert(new_elem != NULL);
  assert(next != NULL);

  next->prev->next = new_elem;

  new_elem->prev = next->prev;
  new_elem->next = next;

  next->prev = new_elem;
}

/* Remove ELEM from its current list. */
struct list_elem * list_remove (struct list *list, struct list_elem *elem)
{
  struct list_elem *pred, *succ; 

  assert(list != NULL);
  assert(elem != NULL);

  list__lock(list);

  assert(!list_empty(list));
  pred = elem->prev;
  assert(pred != NULL);
  succ = elem->next;
  assert(succ != NULL);

  pred->next = succ;
  succ->prev = pred;
  list->n_elts--;

  list__unlock(list);

  return elem;
}

/* Put ELEM at the end of the list. ELEM must not be NULL. */
void list_push_back (struct list *list, struct list_elem *elem)
{
  assert(list != NULL);
  assert(elem != NULL);

  list__lock(list);
  assert(list_looks_valid(list));

  list_insert (elem, list_tail (list));
  list->n_elts++;

  list__unlock(list);
}

/* Put ELEM at the front of the list. ELEM must not be NULL. */
void list_push_front (struct list *list, struct list_elem *elem)
{
  assert(list != NULL);
  assert(elem != NULL);

  list__lock(list);
  assert(list_looks_valid(list));

  list_insert (elem, list_begin (list));
  list->n_elts++;

  list__unlock(list);
}

/* Return the element after ELEM. */
struct list_elem * list_next (const struct list_elem *elem)
{
  return elem->next;
}

/* Return the list_elem at the front of LIST.
   Returns NULL if LIST is empty. 

   Look but don't touch. */
struct list_elem * list_front (const struct list *list)
{
  struct list_elem *node;

  assert(list != NULL);

  list__lock(list);
  assert(list_looks_valid(list));

  node = list->head.next;
  if (node == &list->tail)
    node = NULL;

  list__unlock(list);
  return node;
}

/* Return the list_elem at the back of LIST.
   Returns NULL if LIST is empty. 

   Look but don't touch. */
struct list_elem * list_back (const struct list *list)
{
  struct list_elem *node;

  assert(list != NULL);

  list__lock(list);
  assert(list_looks_valid(list));

  if (list_empty (list))
    node = NULL;
  else
    node = list->tail.prev;

  list__unlock(list);
  return node;
}

/* Return the element at the front of the queue, or NULL if empty. */
struct list_elem * list_pop_front (struct list *list)
{
  struct list_elem *ret;

  assert(list != NULL);

  list__lock(list);
  assert(list_looks_valid(list));

  if (list_empty (list))
    ret = NULL;
  else
    ret = list_remove(list, list_front (list));

  list__unlock(list);
  return ret;
}

/* Return the element at the back of the queue, or NULL if empty. */
struct list_elem * list_pop_back (struct list *list)
{
  struct list_elem *ret;
  assert(list != NULL);

  list__lock(list);
  assert(list_looks_valid(list));

  if (list_empty (list))
    ret = NULL;
  else
    ret = list_remove(list, list_back (list));

  list__unlock(list);
  return ret;
}

/* Return the first SPLIT_SIZE elements of LIST in their own dynamically-allocated list.
   The caller is responsible for free'ing the returned list.
   The rest of the LIST remains in LIST. */
struct list * list_split (struct list *list, unsigned split_size)
{
  int i;
  struct list_elem *elem = NULL;
  struct list *front_list = NULL;
  int orig_size;

  assert(list != NULL);

  orig_size = list_size (list);
  assert(split_size <= orig_size);

  front_list = list_create();

  list__lock(list);
  assert(list_looks_valid(list));

  for (i = 0; i < split_size; i++)
  {
    elem = list_pop_front (list);
    list_push_back (front_list, elem);
  }

  assert(list_size (front_list) == split_size);
  assert(list_size (front_list) + list_size (list) == orig_size);

  list__unlock(list);

  return front_list;
}

/* In a non-empty list, returns the first element.
  In an empty list, returns the tail. */
struct list_elem * list_begin (const struct list *list)
{
  struct list_elem *ret;
  assert(list != NULL);

  list__lock(list);
  assert(list_looks_valid(list));

  ret = list->head.next;

  list__unlock(list);

  return ret;
}

/* Returns the tail (one past the final element). */
struct list_elem * list_end (const struct list *list)
{
  assert(list != NULL);
  return list_tail (list);
}

/* Returns the head of the list. */
struct list_elem * list_head (const struct list *list)
{
  assert(list != NULL);
  return &list->head;
}

/* Returns the tail of the list. */
static struct list_elem * list_tail (const struct list *list)
{
  assert(list != NULL);
  return &list->tail;
}

/* Return the size of the list. */
unsigned list_size (const struct list *list)
{
  int size = 0;
  struct list_elem *e;

  assert(list != NULL);

  list__lock(list);
  assert(list_looks_valid(list));

  /* DEBUG: Verify LIST->N_ELTS is correct. */
  for (e = list_begin (list); e != list_end (list); e = list_next (e))
    size++;
  assert(size == list->n_elts);

  list__unlock(list);

  return list->n_elts;
}

/* Return 1 if empty, 0 else. */
int list_empty (const struct list *list)
{
  assert(list != NULL);

  list__lock(list);
  assert(list_looks_valid(list));

  int empty = (list->head.next == &list->tail);
  if (empty)
    assert(0 == list->n_elts);
  else
    assert(0 < list->n_elts);

  list__unlock(list);

  return empty;
}

/* Return 1 if initialized LIST looks valid, 0 else. */
int list_looks_valid (const struct list *list)
{
  int is_valid; 

  assert(list != NULL);
  /* Magic must be correct. */
  if (list->magic != LIST_MAGIC)
    return 0;

  list__lock(list);

  is_valid = 1;
  /* Head's prev and tail's next should be null. */
  if (list->head.prev != NULL || list->tail.next != NULL)
    is_valid = 0;
  /* Head's next and tail's prev should not be null. */
  if (list->head.next == NULL || list->tail.prev == NULL)
    is_valid = 0;

  list__unlock(list);

  return is_valid;
}

/* For external locking. */
void list_lock (struct list *list)
{
  assert(list != NULL);
  pthread_mutex_lock(&list->lock);
}

/* For external locking. */
void list_unlock (struct list *list)
{
  assert(list != NULL);
  pthread_mutex_unlock (&list->lock);
}

/* For internal locking. */
static void list__lock (struct list *list)
{
  assert(list != NULL);
  pthread_mutex_lock(&list->_lock);
}

/* For internal locking. */
static void list__unlock (struct list *list)
{
  assert(list != NULL);
  pthread_mutex_unlock (&list->_lock);
}

/* Unit test for the list class. */
void list_UT (void)
{
  struct list *l;
  unsigned i, n_entries;
  struct list_elem *e;

  struct UT_elem
  {
    unsigned info;
    struct list_elem elem;
  } entries[100];
  struct UT_elem *entry;

  n_entries = 100;

  /* Create and destroy an empty list. */
  l = list_create();
  assert(list_looks_valid(l) == 1);
  assert(list_size(l) == 0);
  assert(list_empty(l) == 1);

  list_destroy(l);
  assert(list_looks_valid(l) == 0);

  /* Create and populate a list. Iterate over it. */
  l = list_create();
  for (i = 0; i < n_entries; i++)
  {
    entries[i].info = i;
    list_push_back(l, &entries[i].elem);
    assert(list_size(l) == i+1);
  }

   i = 0;
   for (e = list_begin (l); e != list_end (l); e = list_next (e))
   {
      entry = list_entry(e, struct UT_elem, elem);
      assert(entry->info == i);
      assert(entry == &entries[i]);
      i++;
   } 

   i = n_entries-1;
   while (!list_empty(l))
   {
     assert(list_size(l) == i+1);

     e = list_pop_back(l);
     entry = list_entry(e, struct UT_elem, elem);

     assert(entry->info == i);
     assert(entry == &entries[i]);
     i--;
   }

   list_lock(l);
   list_unlock(l);

   list_destroy(l);

}

/* Apply F to each element in LIST. */
void list_apply (struct list *list, list_apply_func f, void *aux)
{
  struct list_elem *e;
  if (f)
  {
    for (e = list_begin (list); e != list_end (list); e = list_next (e))
      (*f)(e, aux);
  }
}
