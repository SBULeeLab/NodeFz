#include "list.h"

#include <assert.h>
#include <stddef.h> /* NULL */
#include <stdlib.h> /* malloc */

/* Initialize HEAD and TAIL to be members of an empty list. */
void list_init (struct list *list)
{
  assert(list != NULL);

  //mylog ("list_init: Initializing list %p (done_list %p)\n", list, &done_list);
  list->magic = LIST_MAGIC;
  list->head.next = &list->tail;
  list->head.prev = NULL;

  list->tail.next = NULL;
  list->tail.prev = &list->head;

  pthread_mutex_init (&list->lock, NULL);
}

/* Insert NEW just before NEXT. */
void list_insert (struct list_elem *new_elem, struct list_elem *next)
{
  assert(new_elem != NULL);
  assert(next != NULL);

  next->prev->next = new_elem;

  new_elem->prev = next->prev;
  new_elem->next = next;

  next->prev = new_elem;
}

/* Remove ELEM from its current list. */
struct list_elem * list_remove (struct list_elem *elem)
{
  assert(elem != NULL);

  struct list_elem *pred = elem->prev;
  struct list_elem *succ = elem->next;

  pred->next = succ;
  succ->prev = pred;
  return elem;
}

/* Put ELEM at the end of the list. ELEM must not be NULL.
   Not thread safe. */
void list_push_back (struct list *list, struct list_elem *elem)
{
  assert(list != NULL);
  assert(list_looks_valid (list));
  assert(elem != NULL);

  list_insert (elem, list_tail (list));
}

/* Put ELEM at the front of the list. ELEM must not be NULL.
   Not thread safe. */
void list_push_front (struct list *list, struct list_elem *elem)
{
  assert(list != NULL);
  assert(list_looks_valid (list));
  assert(elem != NULL);

  list_insert (elem, list_begin (list));
}

/* Return the element after ELEM. */
struct list_elem * list_next (struct list_elem *elem)
{
  return elem->next;
}

/* Return the list_elem at the front of LIST.
   Returns NULL if LIST is empty. 

   Look but don't touch. */
struct list_elem * list_front (struct list *list)
{
  struct list_elem *node;

  assert(list != NULL);
  assert(list_looks_valid (list));

  node = list->head.next;
  if (node == &list->tail)
    return NULL;
  return node;
}

/* Return the list_elem at the back of LIST.
   Returns NULL if LIST is empty. 

   Look but don't touch. */
struct list_elem * list_back (struct list *list)
{
  struct list_elem *node;

  assert(list != NULL);
  assert(list_looks_valid (list));

  if (list_empty (list))
    return NULL;
  return list->tail.prev;
}

/* Return the element at the front of the queue, or NULL if empty. 
   Not thread safe. */
struct list_elem * list_pop_front (struct list *list)
{
  assert(list != NULL);
  assert(list_looks_valid (list));

  if (list_empty (list))
    return NULL;
  return list_remove (list_front (list));
}

/* Return the element at the back of the queue, or NULL if empty. 
   Not thread safe. */
struct list_elem * list_pop_back (struct list *list)
{
  assert(list != NULL);
  assert(list_looks_valid (list));

  if (list_empty (list))
    return NULL;
  return list_remove (list_back (list));
}

/* Return the first SPLIT_SIZE elements of LIST in their own dynamically-allocated list.
   The caller is responsible for free'ing the returned list.
   The rest of the LIST remains in LIST. */
struct list * list_split (struct list *list, int split_size)
{
  int i;
  struct list_elem *elem = NULL;
  struct list *front_list = NULL;
  int orig_size;

  assert(list != NULL);
  assert(list_looks_valid (list));

  orig_size = list_size (list);
  assert(split_size <= orig_size);

  front_list = (struct list *) malloc (sizeof (struct list));
  assert(front_list != NULL);
  list_init (front_list);

  for (i = 0; i < split_size; i++)
  {
    elem = list_pop_front (list);
    list_push_back (front_list, elem);
  }

  assert(list_size (front_list) == split_size);
  assert(list_size (front_list) + list_size (list) == orig_size);

  return front_list;
}

/* In a non-empty list, returns the first element.
  In an empty list, returns the tail. */
struct list_elem * list_begin (struct list *list)
{
  assert(list != NULL);
  assert(list_looks_valid (list));

  return list->head.next;
}

/* Returns the tail (one past the final element). */
struct list_elem * list_end (struct list *list)
{
  assert(list != NULL);
  assert(list_looks_valid (list));

  return list_tail (list);
}

/* Returns the head of the list. */
struct list_elem * list_head (struct list *list)
{
  assert(list != NULL);
  assert(list_looks_valid (list));

  return &list->head;
}

/* Returns the tail of the list. */
struct list_elem * list_tail (struct list *list)
{
  assert(list != NULL);
  assert(list_looks_valid (list));

  return &list->tail;
}

/* Return the size of the list. */
int list_size (struct list *list)
{
  int size = 0;
  struct list_elem *n;

  assert(list != NULL);
  assert(list_looks_valid (list));

  for (n = list_begin (list); n != list_end (list); n = n->next)
    size++;
  return size;
}

/* Return 1 if empty, 0 else. */
int list_empty (struct list *list)
{
  assert(list != NULL);
  assert(list_looks_valid (list));

  return (list->head.next == &list->tail);
}

/* Return 1 if initialized LIST looks valid, 0 else. */
int list_looks_valid (struct list *list)
{
  int is_valid; 

  assert(list != NULL);

  is_valid = 1;
  /* Magic must be correct. */
  if (list->magic != LIST_MAGIC)
    is_valid = 0;
  /* Head's prev and tail's next should be null. */
  if (list->head.prev != NULL || list->tail.next != NULL)
    is_valid = 0;
  /* Head's next and tail's prev should not be null. */
  if (list->head.next == NULL || list->tail.prev == NULL)
    is_valid = 0;
  return is_valid;
}

void list_lock (struct list *list)
{
  assert(list != NULL);
  pthread_mutex_lock (&list->lock);
}

void list_unlock (struct list *list)
{
  assert(list != NULL);
  pthread_mutex_unlock (&list->lock);
}
