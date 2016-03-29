#ifndef UV_SRC_LIST_H_
#define UV_SRC_LIST_H_

#include <pthread.h> /* Locks */
#include <stdint.h> /* uint8_t */
#include <stddef.h> /* offsetof */

/* Doubly linked list. 
   This is a reimplementation of the Pintos linked-list scheme,
   with some adaptations.

   Items intended to be placed in a list
   must embed a list_elem element. Each list_elem can be in
   at most one list at a time. 

   For example:

   struct Foo
   {
     struct list_elem elem;
     int bar;
     ...
   };

   struct list_elem *elem = list_pop_front (&foo_list);
   struct Foo *foo = list_entry (elem, struct Foo, elem); 
   
   Most of these list APIs are internally thread-safe. 
   If you wish a higher-level locking mechanism, use list_lock and list_unlock. 
   If you are iterating over a list, you are advised to lock the list. */
struct list_elem
{
  struct list_elem *prev;
  struct list_elem *next;
};

/* Converts pointer to list element LIST_ELEM into a pointer to
   the structure that LIST_ELEM is embedded inside.  Supply the
   name of the outer structure STRUCT and the member name MEMBER
   of the list element. */
#define list_entry(LIST_ELEM, STRUCT, MEMBER)           \
        ((STRUCT *) ((uint8_t *) &(LIST_ELEM)->next     \
                     - offsetof (STRUCT, MEMBER.next)))

struct list
{
  int magic;
  struct list_elem head;
  struct list_elem tail;
  pthread_mutex_t lock; /* For external locking via list_lock and list_unlock. */
  pthread_mutex_t _lock; /* Don't touch this. For internal locking via list__lock and list__unlock. Recursive. */
  unsigned n_elts;
};

typedef void (*list_apply_func)(struct list_elem *e, void *aux);
typedef void (*list_destroy_func)(struct list_elem *e, void *aux);
/* Returns -1 if a < b, 0 if a == b, 1 if a > b. */
typedef int (*list_sort_func)(struct list_elem *a, struct list_elem *b, void *aux);
/* Returns non-zero if a meets the filter condition (keep), else 0 (remove). */
typedef int (*list_filter_func)(struct list_elem *a, void *aux);

struct list * list_create (void);
void list_destroy (struct list *list);
void list_destroy_full (struct list *list, list_destroy_func f, void *aux);

unsigned list_size (struct list *list);
int list_empty (struct list *list);
int list_looks_valid (struct list *list);

struct list * list_split (struct list *list, unsigned split_size);
/* Add the elements of BACK to the end of FRONT. list_destroy's BACK. */
void list_concat (struct list *front, struct list *back);

void list_push_front (struct list *list, struct list_elem *elem);
void list_push_back (struct list *list, struct list_elem *elem);
struct list_elem * list_pop_front (struct list *list);
struct list_elem * list_pop_back (struct list *list);
struct list_elem * list_remove (struct list *list, struct list_elem *elem);

/* For iteration:

   struct list_elem *e;
   for (e = list_begin (&list); e != list_end (&list); e = list_next (e))
   {
      foo
   } 

   Or destructively:

   struct list_elem *e;
   while (!list_empty (&list))
   {
      e = list_pop_front (&list);
      //Do stuff with e
   } 
  
  Iteration is NOT thread-safe, so use list_lock and list_unlock to protect yourself. */
struct list_elem * list_next (struct list_elem *elem);
struct list_elem * list_front (struct list *list);
struct list_elem * list_back (struct list *list);
struct list_elem * list_begin (struct list *list);
struct list_elem * list_end (struct list *list);
struct list_elem * list_head (struct list *list);

void list_apply (struct list *list, list_apply_func f, void *aux);
/* In-place sort. */
void list_sort (struct list *list, list_sort_func sort_func, void *aux);
/* In-place filter: remove all elems for which F is 0. 
   Nodes that are filtered out are returned in their own list for cleanup. */
struct list * list_filter (struct list *list, list_filter_func filter_func, void *aux);

/* For higher-level locking discipline. */
void list_lock (struct list *list);
void list_unlock (struct list *list);

/* Tests list APIs. */
void list_UT (void);

#endif  /* UV_SRC_LIST_H_ */
