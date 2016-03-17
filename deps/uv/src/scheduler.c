#include "scheduler.h"

#include "list.h"
#include "map.h"
#include "mylog.h"

#include "unix/internal.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#define SCHEDULER_MAGIC 8675309 /* Jenny. */

struct scheduler_s
{
  enum schedule_mode mode;
  char schedule_file[256];

  struct list *recorded_schedule; /* List of the registered sched_lcbn_t's; in registration order. */

  /* REPLAY mode. */
  lcbn_t *shadow_root; /* Root of the "shadow tree" -- the registration tree described in the input file. */
  struct map *name_to_lcbn; /* Used to map hash(name) to lcbn. Allows us to re-build the tree. */
  struct list *desired_schedule; /* A tree_as_list list (rooted at shadow_root) of sched_lcbn_t's, expressing desired execution order, first to last. We discard internal nodes (e.g. initial stack node). We left-shift as we execute nodes. */

  int magic;
};
typedef struct scheduler_s scheduler_t;

/* Globals. */
scheduler_t scheduler;

/* Private API declarations. */
static int scheduler_initialized (void);
static int sched_lcbn_is_next (sched_lcbn_t *sched_lcbn);

/* This extracts the type of handle H_OR_R and routes it to the appropriate handler,
   padding with an 'always execute' option if there is no user CB pending
   (e.g. in EXEC_CONTEXT_UV__RUN_CLOSING_HANDLES). */
struct list * uv__ready_handle_lcbns_wrap (handle_or_req_P *h_or_r, enum execution_context context);

/* Return non-zero if SCHED_LCBN is next, else zero. */
static int sched_lcbn_is_next (sched_lcbn_t *ready_lcbn)
{
  lcbn_t *next_lcbn;
  int equal;

  assert(scheduler_initialized());
  assert(ready_lcbn);

  /* RECORD mode: Every queried sched_lcbn is "next". */
  if (scheduler.mode == SCHEDULE_MODE_RECORD)
    return 1;

  assert(scheduler.mode == SCHEDULE_MODE_REPLAY);
  assert(!list_empty(scheduler.desired_schedule));

  next_lcbn = tree_entry(list_entry(list_begin(scheduler.desired_schedule), tree_node_t, tree_as_list_elem),
                         lcbn_t, tree_node);
  assert(next_lcbn);

  equal = lcbn_semantic_equals(next_lcbn, ready_lcbn->lcbn);
  printf("sched_lcbn_is_next: ready_lcbn %p next_lcbn %p equal? %i\n", ready_lcbn->lcbn, next_lcbn, equal);
  return equal;
}

/* Public APIs. */
sched_lcbn_t *sched_lcbn_create (lcbn_t *lcbn)
{
  sched_lcbn_t *sched_lcbn;
  assert(lcbn);

  sched_lcbn = (sched_lcbn_t *) malloc(sizeof *sched_lcbn);
  assert(sched_lcbn != NULL);
  memset(sched_lcbn, 0, sizeof *sched_lcbn);

  sched_lcbn->lcbn = lcbn;

  return sched_lcbn;
} 

void sched_lcbn_destroy (sched_lcbn_t *sched_lcbn)
{
  assert(sched_lcbn);
  memset(sched_lcbn, 0, sizeof *sched_lcbn);
  free(sched_lcbn);
}

sched_context_t *sched_context_create (enum execution_context exec_context, enum callback_context cb_context, void *handle_or_req)
{
  sched_context_t *sched_context;
  assert(handle_or_req);

  sched_context = (sched_context_t *) malloc(sizeof *sched_context);
  assert(sched_context != NULL);
  memset(sched_context, 0, sizeof *sched_context);

  sched_context->exec_context = exec_context;
  sched_context->cb_context = cb_context;
  sched_context->handle_or_req = handle_or_req;

  return sched_context;
}

void sched_context_destroy (sched_context_t *sched_context)
{
  assert(sched_context);
  free(sched_context);
}

void sched_context_list_destroy_func (struct list_elem *e, void *aux){
  sched_context_t *sched_context;
  assert(e);
  sched_context = list_entry(e, sched_context_t, elem);
  sched_context_destroy(sched_context);
}

static int scheduler_initialized (void)
{
  return scheduler.magic == SCHEDULER_MAGIC;
}

/* TODO DEBUGGING. */
static void dump_lcbn_tree_list_func (struct list_elem *e, void *aux)
{
  lcbn_t *lcbn;
  int fd;
  char buf[2048];

  assert(e);

  lcbn = tree_entry(list_entry(e, tree_node_t, tree_as_list_elem), 
                    lcbn_t, tree_node);
  assert(lcbn);

  lcbn_to_string(lcbn, buf, sizeof buf);
  printf("%s\n", buf);
}

void scheduler_init (enum schedule_mode mode, char *schedule_file)
{
  FILE *f;
  char *line;
  size_t line_len;
  sched_lcbn_t *sched_lcbn;
  lcbn_t *parent;
  int found;
  struct list *filtered_nodes;

  assert(schedule_file != NULL);
  assert(!scheduler_initialized());

  scheduler.shadow_root = NULL;
  scheduler.name_to_lcbn = map_create();

  /* Allocate a line for SCHEDULE_MODE_REPLAY. */
  line_len = 2048;
  line = (char *) malloc(line_len*sizeof(char));
  assert(line);
  memset(line, 0, line_len);

  scheduler.mode = mode;
  strncpy(scheduler.schedule_file, schedule_file, sizeof scheduler.schedule_file);
  scheduler.magic = SCHEDULER_MAGIC;
  scheduler.recorded_schedule = list_create();
  scheduler.desired_schedule = NULL;

  if (scheduler.mode == SCHEDULE_MODE_RECORD)
  {
    /* Verify that we can open and close the file. */
    f = fopen(scheduler.schedule_file, "w");
    assert(f);
    assert(fclose(f) == 0);
  }
  else if (scheduler.mode == SCHEDULE_MODE_REPLAY)
  {
    f = fopen(scheduler.schedule_file, "r");
    assert(f);
    memset(line, 0, line_len);
    while (0 < getline(&line, &line_len, f))
    {
      /* Remove trailing newline. */
      if(line[strlen(line)-1] == '\n')
        line[strlen(line)-1] = '\0';
      /* Parse line as an lcbn_t and wrap with a sched_lcbn. */
      sched_lcbn = sched_lcbn_create(lcbn_from_string(line));
      if (!scheduler.shadow_root)
      {
        assert(sched_lcbn->lcbn->cb_type == CALLBACK_TYPE_INITIAL_STACK);
        scheduler.shadow_root = sched_lcbn->lcbn;
      }

      /* Add the new lcbn to the name map. */
      map_insert(scheduler.name_to_lcbn, map_hash(sched_lcbn->lcbn->name, sizeof sched_lcbn->lcbn->name), sched_lcbn->lcbn);

      if (sched_lcbn->lcbn->cb_type != CALLBACK_TYPE_INITIAL_STACK)
      {
        /* Locate the parent by name; update the tree. */
        parent = map_lookup(scheduler.name_to_lcbn, map_hash(sched_lcbn->lcbn->parent_name, sizeof sched_lcbn->lcbn->parent_name), &found);
        assert(found && parent);
        tree_add_child(&parent->tree_node, &sched_lcbn->lcbn->tree_node);
      }

      memset(line, 0, line_len);
    }
    assert(fclose(f) == 0);

    /* Calculate desired_schedule based on the execution order of the tree we've parsed. */ 
    scheduler.desired_schedule = tree_as_list(&scheduler.shadow_root->tree_node);

    /* TODO DEBUG: First sort by registration order and print out what we've parsed. */
    list_sort(scheduler.desired_schedule, lcbn_sort_by_reg_id, NULL);
    printf("scheduler_init: Printing parsed nodes in registration order.\n");
    list_apply(scheduler.desired_schedule, dump_lcbn_tree_list_func, NULL);

    /* Sort by exec order so that we can efficiently handle scheduler queries. 
       Remove unexecuted nodes. */
    list_sort(scheduler.desired_schedule, lcbn_sort_by_exec_id, NULL);
    filtered_nodes = list_filter(scheduler.desired_schedule, lcbn_remove_unexecuted, NULL); 
    list_destroy(filtered_nodes);

    /* Remove more "unexecuted" nodes: internal nodes like the initial stack node are just placeholders, 
         and will never be executed through invoke_callback.
       TODO If we have more than one 'internal node', use another filter instead of this more direct approach. */
    assert(&scheduler.shadow_root->tree_node == list_entry(list_front(scheduler.desired_schedule), tree_node_t, tree_as_list_elem));
    list_pop_front(scheduler.desired_schedule);

    printf("scheduler_init: Printing parsed nodes in exec order.\n");
    list_apply(scheduler.desired_schedule, dump_lcbn_tree_list_func, NULL);
  }

  free(line);
}

void scheduler_record (sched_lcbn_t *sched_lcbn)
{
  assert(scheduler_initialized());
  assert(sched_lcbn != NULL);

  list_push_back(scheduler.recorded_schedule, &sched_lcbn->elem);
}

void scheduler_emit (void)
{
  FILE *f;
  sched_lcbn_t *sched_lcbn;
  struct list_elem *e;
  char lcbn_str_buf[1024];

  assert(scheduler_initialized());
  if (scheduler.mode != SCHEDULE_MODE_RECORD)
    return;

  mylog("scheduler_emit: Writing schedule to %s\n", scheduler.schedule_file);

  f = fopen(scheduler.schedule_file, "w");
  assert(f);
  for (e = list_begin(scheduler.recorded_schedule); e != list_end(scheduler.recorded_schedule); e = list_next(e))
  {
    sched_lcbn = list_entry(e, sched_lcbn_t, elem);
    assert(sched_lcbn);
    lcbn_to_string(sched_lcbn->lcbn, lcbn_str_buf, sizeof lcbn_str_buf);
    assert(fprintf(f, "%s\n", lcbn_str_buf) == strlen(lcbn_str_buf) + 1);
  }
  assert(fclose(f) == 0);
}

sched_context_t * scheduler_next_context (const struct list *sched_context_list)
{
  struct list_elem *e;
  sched_context_t *next_sched_context, *sched_context;

  assert(scheduler_initialized());
  assert(sched_context_list);

  if (list_empty(sched_context_list))
    return NULL;

  next_sched_context = NULL;
  /* RECORD mode: execute the first context in the list. */
  if (scheduler.mode == SCHEDULE_MODE_RECORD)
    next_sched_context = list_entry(list_begin(sched_context_list), sched_context_t, elem);
  /* REPLAY mode: if any context has the next_lcbn in it, return that context. */
  else if (scheduler.mode == SCHEDULE_MODE_REPLAY)
  {
    for (e = list_begin(sched_context_list); e != list_end(sched_context_list); e = list_next(e))
    {
      sched_context = list_entry(e, sched_context_t, elem);
      if (scheduler_next_lcbn(sched_context))
      {
        next_sched_context = sched_context;
        break;
      }
    }
  }
  else
    NOT_REACHED;

  return next_sched_context;
}

/* Indexed by enum uv_handle_type */
ready_lcbns_func handle_lcbn_funcs[UV_HANDLE_TYPE_MAX] = {
  NULL, /* UV_UNKNOWN_HANDLE */
  uv__ready_async_lcbns,
  uv__ready_check_lcbns,
  uv__ready_fs_event_lcbns,
  uv__ready_fs_poll_lcbns,
  NULL, /* UV_HANDLE */
  uv__ready_idle_lcbns,
  uv__ready_pipe_lcbns,
  uv__ready_poll_lcbns,
  uv__ready_prepare_lcbns,
  uv__ready_process_lcbns,
  uv__ready_stream_lcbns,
  uv__ready_tcp_lcbns,
  uv__ready_timer_lcbns,
  uv__ready_tty_lcbns,
  uv__ready_udp_lcbns,
  uv__ready_signal_lcbns,
  NULL  /* UV_FILE ? */
};

/* Indexed by enum uv_req_type */
ready_lcbns_func req_lcbn_funcs[UV_REQ_TYPE_MAX] = {
  NULL, /* UV_UNKNOWN_REQ */
  NULL, /* UV_REQ */
  NULL, /* UV_CONNECT */
  NULL, /* UV_WRITE */
  NULL, /* UV_SHUTDOWN */
  NULL, /* UV_UDP_SEND */
  NULL, /* UV_FS */
  NULL, /* UV_WORK */
  NULL, /* UV_GETADDRINFO */
  NULL  /* UV_GETNAMEINFO */
  /* UV_REQ_TYPE_PRIVATE -- empty in uv-unix.h. */
};

sched_lcbn_t * scheduler_next_lcbn (sched_context_t *sched_context)
{
  uv_handle_t *handle;
  uv_handle_type handle_type;
  uv_req_t *req;
  uv_req_type req_type;

  struct list *ready_lcbns;
  ready_lcbns_func lcbns_func;
  void *handle_or_req;

  struct list_elem *e;
  sched_lcbn_t *sched_lcbn, *next_lcbn;

  assert(scheduler_initialized());
  assert(sched_context);
  assert(sched_context->handle_or_req);

  if (sched_context->cb_context == CALLBACK_CONTEXT_HANDLE)
  {
    handle = (uv_handle_t *) sched_context->handle_or_req;
    handle_type = handle->type;

    handle_or_req = handle;
    lcbns_func = uv__ready_handle_lcbns_wrap;
 }
  else if (sched_context->cb_context == CALLBACK_CONTEXT_REQ)
  {
    req = (uv_req_t *) sched_context->handle_or_req;
    req_type = req->type;

    handle_or_req = req;
    lcbns_func = req_lcbn_funcs[req_type];
  }
  else
    NOT_REACHED;

  assert(handle_or_req);
  assert(lcbns_func);

  /* NB This must return lcbns in the order in which they will be invoked by the handle. */
  ready_lcbns = (*lcbns_func)(handle_or_req, sched_context->exec_context);
  assert(!list_empty(ready_lcbns));

  next_lcbn = NULL;
  for (e = list_begin(ready_lcbns); e != list_end(ready_lcbns); e = list_next(e))
  {
    sched_lcbn = list_entry(e, sched_lcbn_t, elem);
    if (sched_lcbn_is_next(sched_lcbn))
    {
      next_lcbn = sched_lcbn;
      break;
    }
  }
  /* If being called from scheduler_next_context, there may not be a match.
     Either way, clean up. */
  if (next_lcbn)
    next_lcbn = sched_lcbn_create(next_lcbn->lcbn);
  list_destroy_full(ready_lcbns, sched_lcbn_list_destroy_func, NULL);

  /* RECORD: next_lcbn must be defined.
     REPLAY: we may not have found it. */
  assert(next_lcbn || scheduler.mode == SCHEDULE_MODE_REPLAY);

  return next_lcbn;
}

void scheduler_advance (void)
{
  lcbn_t *lcbn;
  assert(scheduler_initialized());
  if (scheduler.mode != SCHEDULE_MODE_REPLAY)
    return;

  assert(!list_empty(scheduler.desired_schedule));

  lcbn = tree_entry(list_entry(list_pop_front(scheduler.desired_schedule),
                               tree_node_t, tree_as_list_elem),
                    lcbn_t, tree_node);
  printf("schedule_advance: discarding lcbn %p (type %s)\n",
    lcbn, callback_type_to_string(lcbn->cb_type));
  fflush(NULL);
  lcbn = NULL;
}

void sched_lcbn_list_destroy_func (struct list_elem *e, void *aux)
{
  sched_lcbn_t *sched_lcbn;
  assert(e);
  sched_lcbn = list_entry(e, sched_lcbn_t, elem);
  sched_lcbn_destroy(sched_lcbn);
}

struct list * uv__ready_handle_lcbns_wrap (handle_or_req_P *h_or_r, enum execution_context context)
{
  struct list *ret;
  uv_handle_t *handle;
  ready_lcbns_func func;

  assert(h_or_r);
  handle = (uv_handle_t *) h_or_r;
  assert(handle->magic == UV_HANDLE_MAGIC);

  func = handle_lcbn_funcs[handle->type];
  assert(func);

  ret = (*func)(handle, context);

  if (list_empty(ret))
  {
    /* TODO We need to always run such handles, else we'll never schedule them. 
       For example, handles processed in uv__run_closing_handles may not have a UV_CLOSE_CB CB, but should
       still be closed to free up internal resources. */
     assert(!"uv__ready_handle_lcbns_wrap: No ready LCBNs means we should invoke this one!");
  }

  return ret;
}
