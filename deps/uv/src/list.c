#include "list.h"

/* Initialize HEAD and TAIL to be members of an empty list. */
void list_init (struct list *list)
{
  ASSERT (list != NULL);

  //mylog ("list_init: Initializing list %p (done_list %p)\n", list, &done_list);
  list->magic = LIST_MAGIC;
  list->head.elem = NULL;
  list->head.next = &list->tail;
  list->head.prev = NULL;

  list->tail.elem = NULL;
  list->tail.next = NULL;
  list->tail.prev = &list->head;
}

/* Put ELEM at the end of the list. ELEM must not be NULL.
   Not thread safe. */
void list_push_back (struct list *list, void *elem)
{
  struct list_elem *node;

  ASSERT (list != NULL);
  ASSERT (list_looks_valid (list));

  ASSERT (elem != NULL);

  node = (struct list_elem *) malloc (sizeof(struct list_elem));
  ASSERT (node != NULL);

  /* Prepare the node. */
  node->elem = elem;
  node->next = &list->tail;
  node->prev = list->tail.prev;

  /* Insert into list. */
  list->tail.prev->next = node;
  list->tail.prev = node;
}

/* Return the list_elem at the front of LIST.
   Returns NULL if LIST is empty. 

   Look but don't touch. */
struct list_elem * list_front (struct list *list)
{
  struct list_elem *node;

  ASSERT (list != NULL);
  ASSERT (list_looks_valid (list));

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

  ASSERT (list != NULL);
  ASSERT (list_looks_valid (list));

  node = list->tail.prev;
  if (node == &list->head)
    return NULL;
  return node;
}

/* Return the element at the front of the queue, or NULL if empty. 
   Not thread safe. */
void * list_pop_front (struct list *list)
{
  struct list_elem *node;
  void *elem;

  ASSERT (list != NULL);
  ASSERT (list_looks_valid (list));

  if (list->head.next == &list->tail)
    return NULL;

  /* Get the node after list->head. */
  node = list->head.next;

  /* Fix up the list. */
  list->head.next = node->next;
  list->head.next->prev = &list->head;

  elem = node->elem;
  free (node);
  return elem;
}

/* Return the element at the back of the queue, or NULL if empty. 
   Not thread safe. */
void * list_pop_back (struct list *list)
{
  struct list_elem *node;
  void *elem;

  ASSERT (list != NULL);
  ASSERT (list_looks_valid (list));

  if (list->head.next == &list->tail)
    return NULL;

  /* Get the node before list->tail. */
  node = list->tail.prev;

  /* Remove node from the list. */
  list->tail.prev = node->prev;
  list->tail.prev->next = &list->tail;

  elem = node->elem;
  free (node);
  return elem;
}

/* Return the first SPLIT_SIZE elements of LIST in their own dynamically-allocated list.
   The caller is responsible for free'ing the returned list.
   The rest of the LIST remains in LIST. */
struct list * list_split (struct list *list, int split_size)
{
  int i;
  void *elem;
  struct list *front_list;
  int orig_size;

  ASSERT (list != NULL);
  ASSERT (list_looks_valid (list));

  orig_size = list_size (list);
  ASSERT (split_size <= orig_size);

  front_list = (struct list *) malloc (sizeof (struct list));
  ASSERT (front_list != NULL);
  list_init (front_list);

  for (i = 0; i < split_size; i++)
  {
    elem = list_pop_front (list);
    list_push_back (front_list, elem);
  }

  ASSERT (list_size (front_list) == split_size);
  ASSERT (list_size (front_list) + list_size (list) == orig_size);

  return front_list;
}

/* In a non-empty list, returns the first element.
  In an empty list, returns the tail. */
struct list_elem * list_begin (struct list *list)
{
  ASSERT (list != NULL);
  ASSERT (list_looks_valid (list));

  return list->head.next;
}

/* Returns the tail (one past the final element). */
struct list_elem * list_end (struct list *list)
{
  ASSERT (list != NULL);
  ASSERT (list_looks_valid (list));

  return list_tail (list);
}

/* Returns the head of the list. */
struct list_elem * list_head (struct list *list)
{
  ASSERT (list != NULL);
  ASSERT (list_looks_valid (list));

  return &list->head;
}

/* Returns the tail of the list. */
struct list_elem * list_tail (struct list *list)
{
  ASSERT (list != NULL);
  ASSERT (list_looks_valid (list));

  return &list->tail;
}

/* Return the size of the list. */
int list_size (struct list *list)
{
  int size = 0;
  struct list_elem *n;

  ASSERT (list != NULL);
  ASSERT (list_looks_valid (list));

  for (n = list_begin (list); n != list_end (list); n = n->next)
    size++;
  return size;
}

/* Return 1 if empty, 0 else. */
int list_empty (struct list *list)
{
  ASSERT (list != NULL);
  ASSERT (list_looks_valid (list));

  return (list->head.next == &list->tail);
}

/* Return 1 if initialized LIST looks valid, 0 else. */
int list_looks_valid (struct list *list)
{
  int is_valid; 

  ASSERT (list != NULL);

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
