#include "logical-callback-node.h"

#include "list.h"
#include "mylog.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "uv-common.h" /* Allocators */

#define LCBN_MAGIC 33229988
#define LCBN_DEPENDENCY_MAGIC 9127364

struct lcbn_dependency_s
{
  int magic;
  lcbn_t *dependency;
  struct list_elem elem;
};

static lcbn_dependency_t * lcbn_dependency_create (lcbn_t *dep)
{
  lcbn_dependency_t *lcbn_dep = NULL;

  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_dependency_create: begin: dep %p\n", dep));

  lcbn_dep = (lcbn_dependency_t *) uv__malloc(sizeof *lcbn_dep);
  assert(lcbn_dep);
  memset(lcbn_dep, 0, sizeof *lcbn_dep);

  lcbn_dep->magic = LCBN_DEPENDENCY_MAGIC;
  lcbn_dep->dependency = dep;

  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_dependency_create: returning lcbn_dep %p\n", lcbn_dep));
  return lcbn_dep;
}

static int lcbn_dependency_looks_valid (lcbn_dependency_t *lcbn_dep)
{
  int valid = 1;

  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_dependency_looks_valid: begin: lcbn_dep %p\n", lcbn_dep));
  if (!lcbn_dep)
  {
    valid = 0;
    goto DONE;
  }

  if (lcbn_dep->magic != LCBN_DEPENDENCY_MAGIC)
  {
    valid = 0;
    goto DONE;
  }

  valid = 1;
  DONE:
    ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_dependency_looks_valid: returning valid %i\n", valid));
    return valid;
}

/* Private declarations. */
static void lcbn_mark_registration_time (lcbn_t *lcbn)
{
  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_mark_registration_time: begin: lcbn %p\n", lcbn));
  assert(lcbn_looks_valid(lcbn));

  assert(clock_gettime(CLOCK_REALTIME, &lcbn->registration_time) == 0);
  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_mark_registration_time: returning\n", lcbn));
}

/* Helper for parsing input.
   If STR is of the form <X>, replace it with X. */
static char * str_peel_carats (char *str)
{
  size_t len = 0, new_len = 0;

  ENTRY_EXIT_LOG((LOG_LCBN, 9, "str_peel_carats: begin: str %p (%s)\n", str, str));
  assert(str);

  len = strlen(str);
  if (!len)
    goto DONE;

  if (str[0] == '<' && str[len-1] == '>')
  {
    str[len-1] = '\0'; /* > */
    memmove(str, str+1, len-1); /* Left-shift, including the trailing \0. */
    new_len = len - 2;
    assert(strnlen(str, len) == new_len);
  }

  DONE:
    ENTRY_EXIT_LOG((LOG_LCBN, 9, "str_peel_carats: returning str %p (%s)\n", str, str));
    return str;
}

/* Change any ' ' to '_' in STR. */
static char * str_space_to_underscore (char *str)
{
  char *strP = NULL;
  ENTRY_EXIT_LOG((LOG_LCBN, 9, "str_space_to_underscore: begin: str %p (%s)\n", str, str));
  assert(str);

  strP = str;
  while (*strP != '\0')
  {
    if (*strP == ' ')
      *strP = '_';
    strP++;
  }

  ENTRY_EXIT_LOG((LOG_LCBN, 9, "str_space_to_underscore: returning str %p (%s)\n", str, str));
  return str;
}

/* Change any '_' to ' ' in STR. */
static char * str_underscore_to_space (char *str)
{
  char *strP = NULL;
  ENTRY_EXIT_LOG((LOG_LCBN, 9, "str_underscore_to_space: begin: str %p (%s)\n", str, str));
  assert(str);

  strP = str;
  while (*strP != '\0')
  {
    if (*strP == '_')
      *strP = ' ';
    strP++;
  }

  ENTRY_EXIT_LOG((LOG_LCBN, 9, "str_underscore_to_space: returning str %p (%s)\n", str, str));
  return str;
}

/* Dynamically allocate a new lcbn, memset'd to 0. 
   Magic, tree, lists are initialized; lcbn_looks_valid will pass on the result. 
   Name defaults to its own pointer. */
static lcbn_t * lcbn_create_raw (void)
{
  lcbn_t *lcbn = NULL;

  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_create_raw: begin\n"));
  lcbn = (lcbn_t *) uv__malloc(sizeof *lcbn);
  assert(lcbn);
  memset(lcbn, 0, sizeof *lcbn);

  lcbn->magic = LCBN_MAGIC;
  snprintf(lcbn->name, sizeof(lcbn->name), "%p", (void *) lcbn);
  snprintf(lcbn->parent_name, sizeof(lcbn->parent_name), "NULL");
  lcbn->context = NULL;
  tree_init(&lcbn->tree_node);
  lcbn->dependencies = list_create();

  lcbn->extra_info[0] = '\0';

  assert(lcbn_looks_valid(lcbn));
  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_create_raw: returning lcbn %p\n", lcbn));
  return lcbn;
}

int lcbn_looks_valid (lcbn_t *lcbn)
{
  int valid = 1;

  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_looks_valid: begin: lcbn %p\n", lcbn));
  if (!lcbn)
  {
    valid = 0;
    goto DONE;
  }

  if (lcbn->magic != LCBN_MAGIC)
  {
    valid = 0;
    goto DONE;
  }

  if (!tree_looks_valid(&lcbn->tree_node))
  {
    valid = 0;
    goto DONE;
  }

  if (!list_looks_valid(lcbn->dependencies))
  {
    valid = 0;
    goto DONE;
  }

  valid = 1;
  DONE:
    ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_looks_valid: returning valid %i\n", valid));
    return valid;
}

/* Returns a new logical CBN. 
   id=-1, peer_info is allocated, {orig,true}_client_id=ID_UNKNOWN. 
   registration time is set.
   All other fields are NULL or 0. */
lcbn_t * lcbn_create (void *context, any_func cb, enum callback_type cb_type)
{
  lcbn_t *lcbn = lcbn = lcbn_create_raw();

  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_create: begin: context %p cb_type %s\n", context, callback_type_to_string(cb_type)));
  lcbn->context = context;
  lcbn->cb = cb;
  lcbn->cb_type = cb_type;

  lcbn->global_exec_id = -1;
  lcbn->global_reg_id = -1;

  lcbn->active = 0;
  lcbn->finished = 0;

  lcbn_mark_registration_time(lcbn);

  assert(lcbn_looks_valid(lcbn));
  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_create: returning lcbn %p\n", lcbn));
  return lcbn;
}

/* Add CHILD as a child of PARENT. */
void lcbn_add_child (lcbn_t *parent, lcbn_t *child)
{
  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_add_child: begin: parent %p child %p\n", parent, child));
  assert(lcbn_looks_valid(parent));
  assert(lcbn_looks_valid(child));
  assert(parent != child);

  tree_add_child(&parent->tree_node, &child->tree_node);
  mylog(LOG_LCBN, 3, "lcbn_add_child: parent %p (type %s) child %p (type %s) child level %i child childnum %i\n", (void *) parent, callback_type_to_string(parent->cb_type), (void *) child, callback_type_to_string(child->cb_type), tree_depth(&child->tree_node), tree_get_child_num(&child->tree_node));
  snprintf(child->parent_name, sizeof(child->parent_name), "%p", (void *) parent);

  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_add_child: returning\n"));
}

/* Destroy LCBN returned by lcbn_create. 
   LCBN should no longer be in a tree. */
void lcbn_destroy (lcbn_t *lcbn)
{
  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_destroy: begin: lcbn %p\n", lcbn));
  if (!lcbn)
    goto DONE;

  assert(lcbn_looks_valid(lcbn));

  list_destroy(lcbn->dependencies);
  memset(lcbn, 'a', sizeof *lcbn);
  uv__free(lcbn);

  DONE:
    ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_destroy: returning\n"));
}

lcbn_t * lcbn_parent (lcbn_t *lcbn)
{
  lcbn_t *parent = NULL;

  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_parent: begin: lcbn %p\n", lcbn));
  assert(lcbn_looks_valid(lcbn));

  parent = tree_entry(tree_get_parent(&lcbn->tree_node), lcbn_t, tree_node);

  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_parent: returning parent %p\n", parent));
  return parent;
}

/* Mark LCBN as active and update its start_time field. */
void lcbn_mark_begin (lcbn_t *lcbn)
{
  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_mark_begin: begin: lcbn %p\n", lcbn));
  assert(lcbn_looks_valid(lcbn));

  assert(!lcbn->active && !lcbn->finished);

  lcbn->active = 1;
  assert(clock_gettime(CLOCK_REALTIME, &lcbn->start_time) == 0);
  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_mark_begin: returning\n"));
}

/* Mark LCBN as finished and update its end_time field. */
void lcbn_mark_end (lcbn_t *lcbn)
{
  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_mark_end: end: lcbn %p\n", lcbn));
  assert(lcbn_looks_valid(lcbn));

  lcbn->active = 0;
  lcbn->finished = 1;
  assert(clock_gettime(CLOCK_REALTIME, &lcbn->end_time) == 0);
  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_mark_end: returning\n"));
}

/* Write a string description of LCBN into BUF of SIZE. 
   NB It does not end with a newline.
   Keep in sync with lcbn_from_string. 
   NOT THREAD SAFE. */
static char dependency_buf[2048];
char * lcbn_to_string (lcbn_t *lcbn, char *buf, int size)
{
  lcbn_dependency_t *dep = NULL;
  struct list_elem *e = NULL;
  size_t len = 0;

  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_to_string: begin: lcbn %p buf %p size %i\n", lcbn, buf, size));
  assert(lcbn_looks_valid(lcbn));
  assert(buf);
  buf[0] = '\0';

  /* Enter the dependencies as a space-separated string. */
  dependency_buf[0] = '\0';
  len = 0;
  for (e = list_begin(lcbn->dependencies); e != list_end(lcbn->dependencies); e = list_next(e))
  {
    int ret;
    dep = list_entry(e, lcbn_dependency_t, elem);
    assert(lcbn_dependency_looks_valid(dep));

    ret = snprintf(dependency_buf + len, sizeof(dependency_buf) - len, "%p ", (void *) dep->dependency);
    assert(0 <= ret);
    len += ret;
    assert(len < sizeof(dependency_buf));
  }

  /* Remove trailing space. */
  if (!list_empty(lcbn->dependencies))
  {
    assert(len);
    dependency_buf[len-1] = '\0';
  }

  snprintf(buf, size, "<name> <%s> | <context> <%p> | <context_type> <%s> | <cb_type> <%s> | <cb_behavior> <%s> | <tree_number> <%i> | <tree_level> <%i> | <level_entry> <%i> | <exec_id> <%i> | <reg_id> <%i> | <callback_info> <%p> | <registrar> <%p> | <tree_parent> <%s> | <registration_time> <%lis %lins> | <start_time> <%lis %lins> | <end_time> <%lis %lins> | <executing_thread> <%li> | <active> <%i> | <finished> <%i> | <extra_info> <%s> | <dependencies> <%s>",
    lcbn->name, 
    lcbn->context, callback_context_to_string(callback_type_to_context(lcbn->cb_type)), 
    callback_type_to_string(lcbn->cb_type), 
    callback_behavior_to_string(callback_type_to_behavior(lcbn->cb_type)), 
    0, tree_depth(&lcbn->tree_node), tree_get_child_num(&lcbn->tree_node), lcbn->global_exec_id, lcbn->global_reg_id,
    (void *) lcbn->info, (void *) tree_entry(tree_get_parent(&lcbn->tree_node), lcbn_t, tree_node), lcbn->parent_name,
    (long) lcbn->registration_time.tv_sec, lcbn->registration_time.tv_nsec, (long) lcbn->start_time.tv_sec, lcbn->start_time.tv_nsec, (long) lcbn->end_time.tv_sec, lcbn->end_time.tv_nsec, 
    (long) lcbn->executing_thread, lcbn->active, lcbn->finished,
    str_space_to_underscore(lcbn->extra_info), /* sscanf %s stops on whitespace. */
    dependency_buf);

  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_to_string: returning buf %p\n", buf));
  return buf;
}

/* Keep in sync with lcbn_to_string.
   NOT THREAD SAFE. */
lcbn_t * lcbn_from_string (char *buf, int size)
{
  static char context_str[64], cb_type_str[64], cb_behavior_str[64], extra_info_str[64];
  long reg_sec, reg_nsec, start_sec, start_nsec, end_sec, end_nsec;
  long executing_thread;
  int nfields = 18, rc;

  lcbn_t *lcbn = NULL;

  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_from_string: begin: buf %p size %i\n", buf, size));
  assert(buf);
  memset(dependency_buf, 0, sizeof dependency_buf); /* static char buf */
  
  lcbn = lcbn_create_raw();

  mylog(LOG_LCBN, 7, "lcbn_from_string: buf <%s>\n", buf);

  rc = sscanf(buf, "<name> %s | <context> %*s | <context_type> %s | <cb_type> %s | <cb_behavior> %s | <tree_number> <%*i> | <tree_level> <%*i> | <level_entry> <%*i> | <exec_id> <%i> | <reg_id> <%i> | <callback_info> %*s | <registrar> %*s | <tree_parent> %s | <registration_time> <%lis %lins> | <start_time> <%lis %lins> | <end_time> <%lis %lins> | <executing_thread> <%li> | <active> <%i> | <finished> <%i> | <extra_info> %s | <dependencies> <%s>",
    lcbn->name, context_str, cb_type_str, cb_behavior_str,
    &lcbn->global_exec_id, &lcbn->global_reg_id,
    lcbn->parent_name, /* tree_parent */
    &reg_sec, &reg_nsec, &start_sec, &start_nsec, &end_sec, &end_nsec,
    &executing_thread, &lcbn->active, &lcbn->finished, 
    extra_info_str,
    dependency_buf);
  assert(rc == nfields); 

  str_peel_carats(lcbn->name);
  str_peel_carats(lcbn->parent_name);
  str_peel_carats(context_str);
  str_peel_carats(cb_type_str);
  str_peel_carats(cb_behavior_str);
  str_peel_carats(extra_info_str);
  str_underscore_to_space(extra_info_str);
  
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

  memcpy(lcbn->extra_info, extra_info_str, sizeof lcbn->extra_info);

  assert(lcbn_looks_valid(lcbn));

  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_from_string: returning lcbn %p\n", lcbn));
  return lcbn;
}

void lcbn_tree_list_print_f (struct list_elem *e, void *_fd)
{
  lcbn_t *lcbn = NULL;
  int buf_len = 1024;
  char *buf = (char *) uv__malloc(1024*sizeof(char));
  int *fd = (int *) _fd;

  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_tree_list_print_f: begin: e %p *_fd %i\n", e, *fd));
  assert(e);
  assert(fd);
  assert(buf);
  memset(buf, 0, buf_len*sizeof(char));

  lcbn = tree_entry(list_entry(e, tree_node_t, tree_as_list_elem),
                    lcbn_t, tree_node); 
  assert(lcbn_looks_valid(lcbn));
  lcbn_to_string(lcbn, buf, buf_len);

  dprintf(*fd, "%s\n", buf);
  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_tree_list_print_f: returning\n"));
}

/* Return the context of LCBN. */
void * lcbn_get_context (lcbn_t *lcbn)
{
  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_get_context: begin: lcbn %p\n", lcbn));
  assert(lcbn_looks_valid(lcbn));

  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_get_context: returning context %p\n", lcbn->context));
  return lcbn->context;
}

any_func lcbn_get_cb (lcbn_t *lcbn)
{
  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_get_cb: begin: lcbn %p\n", lcbn));
  assert(lcbn_looks_valid(lcbn));

  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_get_cb: returning\n"));
  return lcbn->cb;
}

enum callback_type lcbn_get_cb_type (lcbn_t *lcbn)
{
  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_get_cb_type: begin: lcbn %p\n", lcbn));
  assert(lcbn_looks_valid(lcbn));

  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_get_cb_type: returning type %i\n", lcbn->cb_type));
  return lcbn->cb_type;
}

void lcbn_add_dependency (lcbn_t *pred, lcbn_t *succ)
{
  lcbn_dependency_t *lcbn_dep = NULL;

  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_add_dependency: begin: pred %p succ %p\n", pred, succ));
  assert(lcbn_looks_valid(pred));
  assert(lcbn_looks_valid(succ));

  lcbn_dep = lcbn_dependency_create(pred);

  list_push_back(succ->dependencies, &lcbn_dep->elem);
  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_add_dependency: returning\n"));
}

int lcbn_semantic_equals (lcbn_t *a, lcbn_t *b)
{
  lcbn_t *a_par = NULL, *b_par = NULL;
  tree_node_t *a_par_tree = NULL, *b_par_tree = NULL;
  int cb_type_equal = 0, child_num_equal = 0, parents_equal = 0, equal = 0;

  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_semantic_equals: begin: a %p b %p\n", a, b));
  assert(lcbn_looks_valid(a));
  assert(lcbn_looks_valid(b));

  /* Base case: if trees are of equal height, they reach the initial_stack dummy LCBN (tree root (parent == NULL)) at the same time. No need to compare the root nodes, since it's the initial stack node. */
  a_par_tree = tree_get_parent(&a->tree_node);
  b_par_tree = tree_get_parent(&b->tree_node);
  if (!a_par_tree && !b_par_tree)
  {
    assert(a->cb_type == INITIAL_STACK);
    assert(b->cb_type == INITIAL_STACK);
    equal = 1;
    mylog(LOG_LCBN, 7, "lcbn_semantic_equals: Ran out of tree on both sides, equal\n");
    goto DONE;
  }
  if (!a_par_tree || !b_par_tree)
  {
    mylog(LOG_LCBN, 7, "lcbn_semantic_equals: Ran out of tree on only one side; mismatched depth, not equal\n");
    equal = 0;
    goto DONE;
  }

  a_par = tree_entry(a_par_tree, lcbn_t, tree_node);
  b_par = tree_entry(b_par_tree, lcbn_t, tree_node);

  cb_type_equal = (a->cb_type == b->cb_type);
  mylog(LOG_LCBN, 5, "lcbn_semantic_equals: a %p type %s == b %p type %s? %i\n", a, callback_type_to_string(a->cb_type), b, callback_type_to_string(b->cb_type), cb_type_equal);

  child_num_equal = (tree_get_child_num(&a->tree_node) == tree_get_child_num(&b->tree_node));
  mylog(LOG_LCBN, 5, "lcbn_semantic_equals: a %p child num %i == b %p child num %i? %i\n", a, tree_get_child_num(&a->tree_node), b, tree_get_child_num(&b->tree_node), child_num_equal);

  if (cb_type_equal && child_num_equal)
    parents_equal = lcbn_semantic_equals(a_par, b_par);
  else
    parents_equal = 0;

  equal = (cb_type_equal && child_num_equal && parents_equal);
  mylog(LOG_LCBN, 5, "lcbn_semantic_equals: equal %i (cb_type_equal %i child_num_equal %i parents_equal %i)\n", equal, cb_type_equal, child_num_equal, parents_equal);

  DONE:
    ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_semantic_equals: returning equal %i\n", equal));
    return equal;
}

/* TODO These should really be defined where they are used -- in scheduler.c.
   However, at the moment we also use them in uv-common.c. */
/* list_sort_func, for use with a tree_as_list list of lcbn_t's. 

   AUX is an int* indicating the offset in an LCBN at which we can find
   an integer on which to sort. */
int lcbn_sort_on_int (struct list_elem *a, struct list_elem *b, void *aux)
{
  unsigned offset = 0;
  lcbn_t *lcbn_a = NULL, *lcbn_b = NULL;
  void *void_lcbn_a = NULL, *void_lcbn_b = NULL;
  int a_val = 0, b_val = 0, cmp = 0;

  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_sort_on_int: begin: a %p b %p aux %p\n", a, b, aux));
  assert(a);
  assert(b);

  lcbn_a = tree_entry(list_entry(a, tree_node_t, tree_as_list_elem),
                      lcbn_t, tree_node); 
  lcbn_b = tree_entry(list_entry(b, tree_node_t, tree_as_list_elem),
                      lcbn_t, tree_node); 
  assert(lcbn_looks_valid(lcbn_a));
  assert(lcbn_looks_valid(lcbn_b));

  /* Extract the requested value from the lcbn's. */
  void_lcbn_a = (void *) lcbn_a;
  void_lcbn_b = (void *) lcbn_b;
  offset = *(unsigned *) aux;

  a_val = *(int *) ((char *) void_lcbn_a + offset);
  b_val = *(int *) ((char *) void_lcbn_b + offset);

  if (a_val < b_val)
    cmp = -1;
  else if (a_val == b_val)
    cmp = 0;
  else
    cmp = 1;

  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_sort_on_int: returning cmp%i\n", cmp));
  return cmp;
}

/* list_sort_func, for use with a tree_as_list list of lcbn_t's. */
int lcbn_sort_by_reg_id (struct list_elem *a, struct list_elem *b, void *aux)
{
  unsigned offset = 0;
  int cmp = 0;

  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_sort_by_reg_id: begin: a %p b %p aux %p\n", a, b, aux));
  assert(a);
  assert(b);

  offset = offsetof(lcbn_t, global_reg_id);
  cmp = lcbn_sort_on_int(a, b, &offset);

  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_sort_by_reg_id: returning cmp %i\n", cmp));
  return cmp;
}

/* list_sort_func, for use with a tree_as_list list of lcbn_t's. */
int lcbn_sort_by_exec_id (struct list_elem *a, struct list_elem *b, void *aux)
{
  unsigned offset = 0;
  int cmp = 0;

  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_sort_by_exec_id: begin: a %p b %p aux %p\n", a, b, aux));
  assert(a);
  assert(b);

  offset = offsetof(lcbn_t, global_exec_id);
  cmp = lcbn_sort_on_int(a, b, &offset);

  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_sort_by_exec_id: returning cmp %i\n", cmp));
  return cmp;
}

int lcbn_executed (lcbn_t *lcbn)
{
  int executed = 0;
  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_executed: begin: lcbn %p\n", lcbn));
  assert(lcbn_looks_valid(lcbn));

  executed = (0 <= lcbn->global_exec_id);
  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_executed: returning executed %i\n", executed));
  return executed;
}

/* list_filter_func, for use with a tree_as_list list of lcbn_t's. */
int lcbn_remove_unexecuted (struct list_elem *e, void *aux)
{
  lcbn_t *lcbn = NULL;
  int executed = 0;

  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_remove_unexecuted: begin\n"));
  assert(e);
  lcbn = tree_entry(list_entry(e, tree_node_t, tree_as_list_elem),
                    lcbn_t, tree_node); 
  assert(lcbn_looks_valid(lcbn));

  executed = lcbn_executed(lcbn);

  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_remove_unexecuted: returning executed %i\n", executed));
  return executed;
}

int lcbn_internal (lcbn_t *lcbn)
{
  int internal = 0;
  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_internal: begin: lcbn %p\n", lcbn));
  assert(lcbn_looks_valid(lcbn));

  internal = is_internal_event(lcbn->cb_type);
  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_internal: returning internal %i\n", internal));
  return internal;
}

int lcbn_threadpool (lcbn_t *lcbn)
{
  int threadpool = 0;
  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_threadpool: begin: lcbn %p\n", lcbn));
  assert(lcbn_looks_valid(lcbn));

  threadpool = is_threadpool_cb(lcbn->cb_type);
  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_threadpool: returning threadpool %i\n", threadpool));
  return threadpool;
}

/* list_filter_func, for use with a tree_as_list list of lcbn_t's. */
int lcbn_remove_internal (struct list_elem *e, void *aux)
{
  lcbn_t *lcbn = NULL;
  int internal = 0;

  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_remove_internal: begin\n"));
  assert(e);
  lcbn = tree_entry(list_entry(e, tree_node_t, tree_as_list_elem),
                    lcbn_t, tree_node); 
  assert(lcbn_looks_valid(lcbn));

  internal = lcbn_internal(lcbn);

  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_remove_internal: returning internal %i\n", internal));
  return internal;
}

int lcbn_is_active (lcbn_t *lcbn)
{
  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_is_active: begin: lcbn %p\n", lcbn));
  assert(lcbn_looks_valid(lcbn));

  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_is_active: returning active %i\n", lcbn->active));
  return lcbn->active;
}

void lcbn_mark_non_user (lcbn_t *lcbn)
{
  size_t size = 0, len = 0, remaining = 0;

  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_mark_non_user: begin: lcbn %p\n", lcbn));
  assert(lcbn_looks_valid(lcbn));

  size      = sizeof(lcbn->extra_info);
  len       = strnlen(lcbn->extra_info, size);
  remaining = size - len; 
  snprintf(lcbn->extra_info + len, remaining, "<non-user>");

  ENTRY_EXIT_LOG((LOG_LCBN, 9, "lcbn_mark_non_user: returning\n"));
}

/* Unit test for the LCBN class. */
void lcbn_UT (void)
{
  int i = 0, n_lcbns = 100;
  lcbn_t *lcbns[100];
  lcbn_t *lcbn_copy = NULL;
  enum callback_type cb_type;

  int lcbn_buf_len = 1024;
  char *lcbn_buf = (char *) uv__malloc(1024*sizeof(char));

  mylog(LOG_LIST, 5, "lcbn_UT: begin\n"); 

  for (i = 0; i < n_lcbns; i++)
  {
    if (i == 0)
      cb_type = INITIAL_STACK;
    else
      cb_type = (UV_ALLOC_CB + i) % (CALLBACK_TYPE_MAX - CALLBACK_TYPE_MIN);
    lcbns[i] = lcbn_create(NULL, lcbn_UT, cb_type);
    assert(lcbn_looks_valid(lcbns[i]));
    /* lcbn 10, 20, ... are children of 0 */
    if (0 < i && i % 10 == 0)
      lcbn_add_child(lcbns[0], lcbns[i]);
    /* the rest are children of 0, 10, 20, ... */
    else if (i % 10 != 0)
      lcbn_add_child(lcbns[i/10], lcbns[i]);

    if (i)
      lcbn_add_dependency(lcbns[i-1], lcbns[i]);
  }

  for (i = 0; i < n_lcbns; i++)
  {
    assert(!lcbn_is_active(lcbns[i]));
    lcbn_mark_begin(lcbns[i]);

    assert(lcbn_is_active(lcbns[i]));

    lcbn_mark_end(lcbns[i]);
    assert(!lcbn_is_active(lcbns[i]));
  }

  for (i = 0; i < n_lcbns; i++)
    assert(lcbn_semantic_equals(lcbns[i], lcbns[i]));

  for (i = 0; i < n_lcbns; i++)
  {
    lcbn_to_string(lcbns[i], lcbn_buf, lcbn_buf_len);
    lcbn_copy = lcbn_from_string(lcbn_buf, lcbn_buf_len);
    assert(lcbn_copy != lcbns[i]);
  }

  for (i = 0; i < n_lcbns; i++)
  {
    lcbn_destroy(lcbns[i]);
  }

  uv__free(lcbn_buf);

  mylog(LOG_LIST, 5, "lcbn_UT: passed\n"); 
}
