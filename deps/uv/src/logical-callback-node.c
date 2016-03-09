#include "logical-callback-node.h"

#include "list.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

struct lcbn_dependency
{
  lcbn_t *dependency;
  struct list_elem elem;
};

static void lcbn_mark_registration_time (lcbn_t *lcbn);
static lcbn_t * lcbn_create_raw (void);
static void str_peel_carats (char *str);

/* Dynamically allocate a new lcbn, memset'd to 0. Lists are initialized. */
static lcbn_t * lcbn_create_raw (void)
{
  lcbn_t *lcbn;

  lcbn = malloc(sizeof *lcbn);
  assert(lcbn != NULL);
  memset(lcbn, 0, sizeof *lcbn);

  lcbn->children = list_create();
  lcbn->dependencies = list_create();

  return lcbn;
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
lcbn_t * lcbn_create (void *context, void *cb, enum callback_type cb_type)
{
  lcbn_t *lcbn;

  lcbn = lcbn_create_raw();

  lcbn->context = context;
  lcbn->cb = cb;
  lcbn->cb_type = cb_type;

  lcbn->tree_number = -1;
  lcbn->tree_level = -1;
  lcbn->level_entry = -1;

  lcbn->global_exec_id = -1;
  lcbn->global_reg_id = -1;

  lcbn->registrar = NULL;
  lcbn->tree_parent = NULL;

  lcbn->active = 0;
  lcbn->finished = 0;

  lcbn_mark_registration_time(lcbn);

  return lcbn;
}

/* Initialize CHILD as a child of PARENT. */
void lcbn_add_child (lcbn_t *parent, lcbn_t *child)
{
  assert(parent != NULL);
  assert(child != NULL);

  child->tree_number = parent->tree_number;
  child->tree_level = parent->tree_level + 1;
  child->tree_parent = parent;

  list_lock(parent->children);
  child->level_entry = list_size(parent->children);
  list_push_back(parent->children, &child->child_elem);
  printf("parent %p child %p child->level_entry %i\n", parent, child, child->level_entry);
  list_unlock(parent->children);
}

/* Destroy LCBN returned by lcbn_create or lcbn_init. */
void lcbn_destroy (lcbn_t *lcbn)
{
  if (lcbn == NULL)
    return;

  list_destroy(lcbn->children);
  free(lcbn);
}

/* Set the registration_time field. */
static void lcbn_mark_registration_time (lcbn_t *lcbn)
{
  assert(lcbn != NULL);
  assert(clock_gettime(CLOCK_MONOTONIC, &lcbn->registration_time) == 0);
}

/* Mark LCBN as active and update its start_time field. */
void lcbn_mark_begin (lcbn_t *lcbn)
{
  assert(lcbn != NULL);
  lcbn->active = 1;
  assert(clock_gettime(CLOCK_MONOTONIC, &lcbn->start_time) == 0);
}

/* Mark LCBN as finished and update its end_time field. */
void lcbn_mark_end (lcbn_t *lcbn)
{
  assert(lcbn != NULL);
  lcbn->active = 0;
  lcbn->finished = 1;
  assert(clock_gettime(CLOCK_MONOTONIC, &lcbn->end_time) == 0);
}

/* Write a string description of LCBN into BUF of SIZE. 
   NB It does not end with a newline.
   Keep in sync with lcbn_from_string. */
char * lcbn_to_string (lcbn_t *lcbn, char *buf, int size)
{
  struct lcbn_dependency *dep;
  struct list_elem *e;
  static char dependency_buf[2048];

  assert(lcbn != NULL);
  assert(buf != NULL);

  /* Enter the dependencies as a space-separated string. */
  dependency_buf[0] = '\0';
  for (e = list_begin(lcbn->dependencies); e != list_end(lcbn->dependencies); e = list_next(e))
  {
    dep = list_entry(e, struct lcbn_dependency, elem);
    assert(dep != NULL);
    snprintf(dependency_buf + strlen(dependency_buf), size, "%p ", dep->dependency);
  }
  /* Remove trailing space. */
  if (!list_empty(lcbn->dependencies))
    dependency_buf[strlen(dependency_buf)-1] = '\0';

  snprintf(buf, size, "<name> <%p> | <context> <%p> | <context_type> <%s> | <cb> <%p> | <cb_type> <%s> | <cb_behavior> <%s> | <tree_number> <%i> | <tree_level> <%i> | <level_entry> <%i> | <exec_id> <%i> | <reg_id> <%i> | <callback_info> <%p> | <registrar> <%p> | <tree_parent> <%p> | <registration_time> <%is %lins> | <start_time> <%is %lins> | <end_time> <%is %lins> | <executing_thread> <%i> | <active> <%i> | <finished> <%i> | <dependencies> <%s>",
    lcbn, 
    lcbn->context, callback_context_to_string(callback_type_to_context(lcbn->cb_type)), 
    lcbn->cb, callback_type_to_string(lcbn->cb_type), 
    callback_behavior_to_string(callback_type_to_behavior(lcbn->cb_type)), 
    lcbn->tree_number, lcbn->tree_level, lcbn->level_entry, lcbn->global_exec_id, lcbn->global_reg_id,
    lcbn->info, lcbn->registrar, lcbn->tree_parent, 
    lcbn->registration_time.tv_sec, lcbn->registration_time.tv_nsec, lcbn->start_time.tv_sec, lcbn->start_time.tv_nsec, lcbn->end_time.tv_sec, lcbn->end_time.tv_nsec, 
    lcbn->executing_thread, lcbn->active, lcbn->finished,
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
  lcbn_t *lcbn;
  assert(buf != NULL);
  
  lcbn = lcbn_create_raw();
  sscanf(buf, "<name> %*s | <context> %*s | <context_type> %s | <cb> %*s | <cb_type> %s | <cb_behavior> %s | <tree_number> <%i> | <tree_level> <%i> | <level_entry> <%i> | <exec_id> <%i> | <reg_id> <%i> | <callback_info> %*s | <registrar> %*s | <tree_parent> %*s | <registration_time> <%is %lins> | <start_time> <%is %lins> | <end_time> <%is %lins> | <executing_thread> <%i> | <active> <%i> | <finished> <%i> | <dependencies> <%s>",
    context_str, cb_type_str, cb_behavior_str,
    &lcbn->tree_number, &lcbn->tree_level, &lcbn->level_entry, &lcbn->global_exec_id, &lcbn->global_reg_id,
    &lcbn->registration_time.tv_sec, &lcbn->registration_time.tv_nsec, &lcbn->start_time.tv_sec, &lcbn->start_time.tv_nsec, &lcbn->end_time.tv_sec, &lcbn->end_time.tv_nsec, 
    &lcbn->executing_thread, &lcbn->active, &lcbn->finished, dependency_buf);

  str_peel_carats(context_str);
  str_peel_carats(cb_type_str);
  str_peel_carats(cb_behavior_str);
  
  lcbn->cb_context = callback_context_from_string(context_str);
  lcbn->cb_type = callback_type_from_string(cb_type_str);
  lcbn->cb_behavior = callback_behavior_from_string(cb_behavior_str);

  return lcbn;
}

/* A print function for use with list_apply on a list of LCBNs using their global_exec_order_elem.
   Not thread safe. */
void lcbn_global_exec_list_print_f (struct list_elem *e, int *fd)
{
  lcbn_t *lcbn;
  static char buf[1024];

  assert(e != NULL);
  assert(fd != NULL);

  lcbn = list_entry(e, lcbn_t, global_exec_order_elem);
  assert(lcbn != NULL);
  lcbn_to_string(lcbn, buf, sizeof buf);

  dprintf(*fd, "%s\n", buf);
}

/* A print function for use with list_apply on a list of LCBNs using their global_reg_order_elem.
   Not thread safe. */
void lcbn_global_reg_list_print_f (struct list_elem *e, int *fd)
{
  lcbn_t *lcbn;
  static char buf[1024];

  assert(e != NULL);
  assert(fd != NULL);

  lcbn = list_entry(e, lcbn_t, global_reg_order_elem);
  assert(lcbn != NULL);
  lcbn_to_string(lcbn, buf, sizeof buf);

  dprintf(*fd, "%s\n", buf);
}

/* Return the context of LCBN. */
void * lcbn_get_context (lcbn_t *lcbn)
{
  assert(lcbn != NULL);
  return lcbn->context;
}

void * lcbn_get_cb (lcbn_t *lcbn)
{
  assert(lcbn != NULL);
  return lcbn->cb;
}

enum callback_type lcbn_get_cb_type (lcbn_t *lcbn)
{
  assert(lcbn != NULL);
  return lcbn->cb_type;
}

void lcbn_add_dependency (lcbn_t *pred, lcbn_t *succ)
{
  struct lcbn_dependency *dep;

  assert(pred != NULL);
  assert(succ != NULL);

  dep = (struct lcbn_dependency *) malloc(sizeof *dep);
  assert(dep != NULL);
  dep->dependency = pred;

  list_push_back(succ->dependencies, &dep->elem);
}
