#include "logical-callback-node.h"

#include "list.h"
#include "mylog.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "uv-common.h" /* Allocators */

/* Private declarations. */
static void lcbn_mark_registration_time (lcbn_t *lcbn);

/* Helper for parsing input. */
static void str_peel_carats (char *str);

/* Dynamically allocate a new lcbn, memset'd to 0. Tree, lists are initialized. 
   name defaults to its own pointer. */
static lcbn_t * lcbn_create_raw (void)
{
  lcbn_t *lcbn;

  lcbn = (lcbn_t *) uv__malloc(sizeof *lcbn);
  assert(lcbn != NULL);
  memset(lcbn, 0, sizeof *lcbn);

  lcbn->magic = LCBN_MAGIC;
  sprintf(lcbn->name, "%p", (void *) lcbn);
  sprintf(lcbn->parent_name, "NULL");
  tree_init(&lcbn->tree_node);
  lcbn->dependencies = list_create();

  return lcbn;
}

static int lcbn__looks_valid (lcbn_t *lcbn)
{
  if (!lcbn)
    return 0;
  if (lcbn->magic != LCBN_MAGIC)
    return 0;
  return 1;
}

/* If STR is of the form <X>, replace it with X. */
static void str_peel_carats (char *str)
{
  int i, len;
  assert(str != NULL);

  len = strlen(str);
  if (!len)
    return;

  if (str[0] == '<' && str[len-1] == '>')
  {
    str[len-1] = '\0'; /* > */
    /* Left-shift to replace <. */
    for(i = 1; i < len; i++)
      str[i-1] = str[i];
  }
}

/* Returns a new logical CBN. 
   id=-1, peer_info is allocated, {orig,true}_client_id=ID_UNKNOWN. 
   registration time is set.
   All other fields are NULL or 0. */
lcbn_t * lcbn_create (void *context, any_func cb, enum callback_type cb_type)
{
  lcbn_t *lcbn;

  lcbn = lcbn_create_raw();

  lcbn->context = context;
  lcbn->cb = cb;
  lcbn->cb_type = cb_type;

  lcbn->global_exec_id = -1;
  lcbn->global_reg_id = -1;

  lcbn->active = 0;
  lcbn->finished = 0;

  lcbn_mark_registration_time(lcbn);

  assert(lcbn__looks_valid(lcbn));

  return lcbn;
}

/* Initialize CHILD as a child of PARENT. */
void lcbn_add_child (lcbn_t *parent, lcbn_t *child)
{
  assert(lcbn__looks_valid(parent));
  assert(lcbn__looks_valid(child));

  tree_add_child(&parent->tree_node, &child->tree_node);
  mylog(LOG_LCBN, 3, "lcbn_add_child: parent %p type %s child %p type %s child level %i child childnum %i\n", parent, callback_type_to_string(parent->cb_type), child, callback_type_to_string(child->cb_type), tree_depth(&child->tree_node), tree_get_child_num(&child->tree_node));
  sprintf(child->parent_name, "%p", (void *) parent);
}

/* Destroy LCBN returned by lcbn_create or lcbn_init. */
void lcbn_destroy (lcbn_t *lcbn)
{
  if (lcbn == NULL)
    return;

  assert(lcbn__looks_valid(lcbn));
  list_destroy(lcbn->dependencies);
  uv__free(lcbn);
}

/* Set the registration_time field. */
static void lcbn_mark_registration_time (lcbn_t *lcbn)
{
  assert(lcbn__looks_valid(lcbn));
  assert(clock_gettime(CLOCK_MONOTONIC, &lcbn->registration_time) == 0);
}

/* Mark LCBN as active and update its start_time field. */
void lcbn_mark_begin (lcbn_t *lcbn)
{
  assert(lcbn__looks_valid(lcbn));
  lcbn->active = 1;
  assert(clock_gettime(CLOCK_MONOTONIC, &lcbn->start_time) == 0);
}

/* Mark LCBN as finished and update its end_time field. */
void lcbn_mark_end (lcbn_t *lcbn)
{
  assert(lcbn__looks_valid(lcbn));
  lcbn->active = 0;
  lcbn->finished = 1;
  assert(clock_gettime(CLOCK_MONOTONIC, &lcbn->end_time) == 0);
}

/* Write a string description of LCBN into BUF of SIZE. 
   NB It does not end with a newline.
   Keep in sync with lcbn_from_string. */
char * lcbn_to_string (lcbn_t *lcbn, char *buf, int size)
{
  lcbn_dependency_t *dep;
  struct list_elem *e;
  static char dependency_buf[2048];

  assert(lcbn__looks_valid(lcbn));
  assert(buf != NULL);

  /* Enter the dependencies as a space-separated string. */
  dependency_buf[0] = '\0';
  for (e = list_begin(lcbn->dependencies); e != list_end(lcbn->dependencies); e = list_next(e))
  {
    dep = list_entry(e, lcbn_dependency_t, elem);
    assert(dep != NULL);
    snprintf(dependency_buf + strlen(dependency_buf), size, "%p ", (void *) dep->dependency);
  }
  /* Remove trailing space. */
  if (!list_empty(lcbn->dependencies))
    dependency_buf[strlen(dependency_buf)-1] = '\0';

  snprintf(buf, size, "<name> <%s> | <context> <%p> | <context_type> <%s> | <cb> <%p> | <cb_type> <%s> | <cb_behavior> <%s> | <tree_number> <%i> | <tree_level> <%i> | <level_entry> <%i> | <exec_id> <%i> | <reg_id> <%i> | <callback_info> <%p> | <registrar> <%p> | <tree_parent> <%s> | <registration_time> <%is %lins> | <start_time> <%is %lins> | <end_time> <%is %lins> | <executing_thread> <%li> | <active> <%i> | <finished> <%i> | <dependencies> <%s>",
    lcbn->name, 
    lcbn->context, callback_context_to_string(callback_type_to_context(lcbn->cb_type)), 
    (void *) (long) lcbn->cb, callback_type_to_string(lcbn->cb_type), 
    callback_behavior_to_string(callback_type_to_behavior(lcbn->cb_type)), 
    0, tree_depth(&lcbn->tree_node), tree_get_child_num(&lcbn->tree_node), lcbn->global_exec_id, lcbn->global_reg_id,
    (void *) lcbn->info, (void *) tree_entry(tree_get_parent(&lcbn->tree_node), lcbn_t, tree_node), lcbn->parent_name,
    (int) lcbn->registration_time.tv_sec, lcbn->registration_time.tv_nsec, (int) lcbn->start_time.tv_sec, lcbn->start_time.tv_nsec, (int) lcbn->end_time.tv_sec, lcbn->end_time.tv_nsec, 
    (long) lcbn->executing_thread, lcbn->active, lcbn->finished,
    dependency_buf);

  return buf;
}

/* Keep in sync with lcbn_to_string. */
lcbn_t * lcbn_from_string (char *buf)
{
  static char dependency_buf[2048];
  static char context_str[32];
  static char cb_type_str[32];
  static char cb_behavior_str[32];

  long reg_sec, reg_nsec, start_sec, start_nsec, end_sec, end_nsec;
  long executing_thread;

  lcbn_t *lcbn = NULL;
  assert(buf != NULL);
  
  lcbn = lcbn_create_raw();

  sscanf(buf, "<name> %s | <context> %*s | <context_type> %s | <cb> %*s | <cb_type> %s | <cb_behavior> %s | <tree_number> <%*i> | <tree_level> <%*i> | <level_entry> <%*i> | <exec_id> <%i> | <reg_id> <%i> | <callback_info> %*s | <registrar> %*s | <tree_parent> %s | <registration_time> <%lis %lins> | <start_time> <%lis %lins> | <end_time> <%lis %lins> | <executing_thread> <%li> | <active> <%i> | <finished> <%i> | <dependencies> <%s>",
    lcbn->name, context_str, cb_type_str, cb_behavior_str,
    &lcbn->global_exec_id, &lcbn->global_reg_id,
    lcbn->parent_name,
    &reg_sec, &reg_nsec, &start_sec, &start_nsec, &end_sec, &end_nsec,
    &executing_thread, &lcbn->active, &lcbn->finished, dependency_buf);

  str_peel_carats(lcbn->name);
  str_peel_carats(lcbn->parent_name);
  str_peel_carats(context_str);
  str_peel_carats(cb_type_str);
  str_peel_carats(cb_behavior_str);
  
  lcbn->cb_context = callback_context_from_string(context_str);
  lcbn->cb_type = callback_type_from_string(cb_type_str);
  lcbn->cb_behavior = callback_behavior_from_string(cb_behavior_str);

  lcbn->registration_time.tv_sec = reg_sec; 
  lcbn->registration_time.tv_nsec = reg_nsec;
  lcbn->start_time.tv_sec = start_sec;
  lcbn->start_time.tv_nsec = start_nsec;
  lcbn->end_time.tv_sec = end_sec;
  lcbn->end_time.tv_nsec = end_nsec; 

  lcbn->executing_thread = executing_thread;

  return lcbn;
}

void lcbn_tree_list_print_f (struct list_elem *e, void *_fd)
{
  lcbn_t *lcbn;
  static char buf[1024];
  int *fd = (int *) _fd;

  assert(e);
  assert(fd);

  lcbn = tree_entry(list_entry(e, tree_node_t, tree_as_list_elem),
                      lcbn_t, tree_node); 
  assert(lcbn__looks_valid(lcbn));
  lcbn_to_string(lcbn, buf, sizeof buf);

  dprintf(*fd, "%s\n", buf);
}

/* Return the context of LCBN. */
void * lcbn_get_context (lcbn_t *lcbn)
{
  assert(lcbn__looks_valid(lcbn));
  return lcbn->context;
}

any_func lcbn_get_cb (lcbn_t *lcbn)
{
  assert(lcbn__looks_valid(lcbn));
  return lcbn->cb;
}

enum callback_type lcbn_get_cb_type (lcbn_t *lcbn)
{
  assert(lcbn__looks_valid(lcbn));
  return lcbn->cb_type;
}

void lcbn_add_dependency (lcbn_t *pred, lcbn_t *succ)
{
  lcbn_dependency_t *dep;

  assert(lcbn__looks_valid(pred));
  assert(lcbn__looks_valid(succ));

  dep = (lcbn_dependency_t *) uv__malloc(sizeof *dep);
  assert(dep);
  dep->dependency = pred;

  list_push_back(succ->dependencies, &dep->elem);
}

int lcbn_semantic_equals (lcbn_t *a, lcbn_t *b)
{
  tree_node_t *a_par = NULL, *b_par = NULL;

  assert(lcbn__looks_valid(a));
  assert(lcbn__looks_valid(b));

  /* Base case: if trees are of equal height, they reach the initial_stack dummy LCBN (tree root (parent == NULL)) at the same time. No need to compare the root nodes, since it's the initial stack node. */
  a_par = tree_get_parent(&a->tree_node);
  b_par = tree_get_parent(&b->tree_node);
  if (!a_par && !b_par)
  {
    assert(a->cb_type == CALLBACK_TYPE_INITIAL_STACK);
    assert(b->cb_type == CALLBACK_TYPE_INITIAL_STACK);
    return 1;
  }
  if (!a_par || !b_par)
    return 0;

  mylog(LOG_LCBN, 5, "lcbn_semantic_equals: a %p type %s == b %p type %s? %i\n", a, callback_type_to_string(a->cb_type), b, callback_type_to_string(b->cb_type), a->cb_type == b->cb_type);
  mylog(LOG_LCBN, 5, "lcbn_semantic_equals: a child num %i == b child num %i? %i\n", tree_get_child_num(&a->tree_node), tree_get_child_num(&b->tree_node), tree_get_child_num(&a->tree_node) == tree_get_child_num(&b->tree_node));

  return (a->cb_type == b->cb_type
       && tree_get_child_num(&a->tree_node) == tree_get_child_num(&b->tree_node)
       && lcbn_semantic_equals(tree_entry(a_par, lcbn_t, tree_node),
                               tree_entry(b_par, lcbn_t, tree_node))
         );
}

/* TODO These should really be defined where they are used -- in scheduler.c.
   However, at the moment we also use them in uv-common.c. */
/* list_sort_func, for use with a tree_as_list list of lcbn_t's. 

   AUX is an int* indicating the offset in an LCBN at which we can find
   an integer on which to sort. */
int lcbn_sort_on_int (struct list_elem *a, struct list_elem *b, void *aux)
{
  unsigned offset;
  lcbn_t *lcbn_a, *lcbn_b;
  void *void_lcbn_a, *void_lcbn_b;
  int a_val, b_val;
  assert(a);
  assert(b);

  lcbn_a = tree_entry(list_entry(a, tree_node_t, tree_as_list_elem),
                      lcbn_t, tree_node); 
  lcbn_b = tree_entry(list_entry(b, tree_node_t, tree_as_list_elem),
                      lcbn_t, tree_node); 

  assert(lcbn__looks_valid(lcbn_a));
  assert(lcbn__looks_valid(lcbn_b));

  void_lcbn_a = (void *) lcbn_a;
  void_lcbn_b = (void *) lcbn_b;
  offset = *(unsigned *) aux;

  a_val = *(int *) ((char *) void_lcbn_a + offset);
  b_val = *(int *) ((char *) void_lcbn_b + offset);

  if (a_val < b_val)
    return -1;
  else if (a_val == b_val)
    return 0;
  else
    return 1;
}

/* list_sort_func, for use with a tree_as_list list of lcbn_t's. */
int lcbn_sort_by_reg_id (struct list_elem *a, struct list_elem *b, void *aux)
{
  unsigned offset;
  offset = offsetof(lcbn_t, global_reg_id);
  return lcbn_sort_on_int(a, b, &offset);
}

/* list_sort_func, for use with a tree_as_list list of lcbn_t's. */
int lcbn_sort_by_exec_id (struct list_elem *a, struct list_elem *b, void *aux)
{
  unsigned offset;
  offset = offsetof(lcbn_t, global_exec_id);
  return lcbn_sort_on_int(a, b, &offset);
}

/* list_filter_func, for use with a tree_as_list list of lcbn_t's. */
int lcbn_remove_unexecuted (struct list_elem *e, void *aux)
{
  lcbn_t *lcbn = NULL;

  assert(e);
  lcbn = tree_entry(list_entry(e, tree_node_t, tree_as_list_elem),
                    lcbn_t, tree_node); 
  assert(lcbn__looks_valid(lcbn));

  return (0 <= lcbn->global_exec_id);
}
