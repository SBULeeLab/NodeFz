#ifndef UV_SRC_LIST_H_
#define UV_SRC_LIST_H_

/* Doubly linked list. 

   Items intended to be placed in a list
   must embed a list_elem element. Each list_elem can be in
   at most one list at a time. 

   Modeled after the Pintos linked-least scheme. */ 
struct list_elem
{
  void *elem; /* No NULL elements are allowed in the list. */
  struct list_elem *next;
  struct list_elem *prev;
};

#define LIST_MAGIC 12345678
struct list
{
  int magic;
  struct list_elem head;
  struct list_elem tail;
};

void list_init (struct list *list);
void list_push_back (struct list *list, void *elem);
struct list_elem * list_front (struct list *list);
struct list_elem * list_back (struct list *list);
void * list_pop_front (struct list *list);
void * list_pop_back (struct list *list);
struct list * list_split (struct list *list, int split_size);
struct list_elem * list_begin (struct list *list);
struct list_elem * list_end (struct list *list);
struct list_elem * list_head (struct list *list);
struct list_elem * list_tail (struct list *list);
int list_size (struct list *list);
int list_empty (struct list *list);
int list_looks_valid (struct list *list);

#endif  /* UV_SRC_LIST_H_ */
