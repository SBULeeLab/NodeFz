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

/* Globals. */
struct
{
  /* RECORD, REPLAY modes. */
  enum schedule_mode mode;
  char schedule_file[256];
  int magic;

  struct list *recorded_schedule; /* List of the registered sched_lcbn_t's; in registration order. */
  struct list *executed_schedule; /* List of the executed sched_lcbn_t's, in order of execution. */

  /* REPLAY mode. */
  lcbn_t *shadow_root; /* Root of the "shadow tree" -- the registration tree described in the input file. */
  struct map *name_to_lcbn; /* Used to map hash(name) to lcbn. Allows us to re-build the tree. */
  struct list *desired_schedule; /* A tree_as_list list (rooted at shadow_root) of sched_lcbn_t's, expressing desired execution order, first to last. We discard internal nodes (e.g. initial stack node). We left-shift as we execute nodes. */
  int n_executed;
} scheduler;

/* Private API declarations. */
static int scheduler_initialized (void);
/* This extracts the type of handle H_OR_R and routes it to the appropriate handler,
   padding with an 'always execute' option if there is no user CB pending
   (e.g. in EXEC_CONTEXT_UV__RUN_CLOSING_HANDLES). */
struct list * uv__ready_handle_lcbns_wrap (void *wrapper, enum execution_context context);

static int sched_lcbn_looks_valid (sched_lcbn_t *sched_lcbn)
{
  if (!sched_lcbn)
    return 0;
  if (sched_lcbn->magic != SCHED_LCBN_MAGIC)
    return 0;
  return 1;
}

static int sched_context_looks_valid (sched_context_t *sched_context)
{
  if (!sched_context)
    return 0;
  if (sched_context->magic != SCHED_CONTEXT_MAGIC)
    return 0;
  return 1;
}

/* Public APIs. */
lcbn_t * scheduler_next_scheduled_lcbn (void)
{
  lcbn_t *next_lcbn = NULL;
  assert(scheduler_initialized());

  if (scheduler.mode != SCHEDULE_MODE_REPLAY)
    return NULL;
  if (!list_empty(scheduler.desired_schedule))
  {
    next_lcbn = tree_entry(list_entry(list_begin(scheduler.desired_schedule), tree_node_t, tree_as_list_elem),
                           lcbn_t, tree_node);
    assert(lcbn_looks_valid(next_lcbn));
  }
  return next_lcbn;
}

enum callback_type scheduler_next_lcbn_type (void)
{
  lcbn_t *next_lcbn = NULL;
  enum callback_type cb_type = CALLBACK_TYPE_ANY;
  assert(scheduler_initialized());

  next_lcbn = scheduler_next_scheduled_lcbn();
  if (next_lcbn)
    cb_type = next_lcbn->cb_type;
  return cb_type;
}

/* Return non-zero if SCHED_LCBN is next, else zero. */
int sched_lcbn_is_next (sched_lcbn_t *ready_lcbn)
{
  lcbn_t *next_lcbn = NULL;
  int equal, verbosity;

  assert(scheduler_initialized());
  assert(sched_lcbn_looks_valid(ready_lcbn));

  /* RECORD mode: Every queried sched_lcbn is "next". */
  if (scheduler.mode == SCHEDULE_MODE_RECORD)
    return 1;

  next_lcbn = scheduler_next_scheduled_lcbn();
  /* If nothing left in the schedule, we can't run this. */
  if (!next_lcbn)
  {
    /* TODO At the moment, I'm only testing replay-ability of a recorded schedule. Consequently this should only happen because we always leave the UV_ASYNC_CB for the threadpool done queue pending. */
    assert(ready_lcbn->lcbn->cb_type == UV_ASYNC_CB);
    return 0;
  }

  equal = lcbn_semantic_equals(next_lcbn, ready_lcbn->lcbn);
  verbosity = equal ? 5 : 7;
  mylog(LOG_SCHEDULER, verbosity, "sched_lcbn_is_next: exec_id %i next_lcbn %p (name %s) ready_lcbn %p type %s equal? %i\n", next_lcbn->global_exec_id, next_lcbn, next_lcbn->name, ready_lcbn->lcbn, callback_type_to_string(ready_lcbn->lcbn->cb_type), equal);
  return equal;
}

sched_lcbn_t *sched_lcbn_create (lcbn_t *lcbn)
{
  sched_lcbn_t *sched_lcbn;
  assert(lcbn);

  sched_lcbn = (sched_lcbn_t *) uv__malloc(sizeof *sched_lcbn);
  assert(sched_lcbn != NULL);
  memset(sched_lcbn, 0, sizeof *sched_lcbn);

  sched_lcbn->magic = SCHED_LCBN_MAGIC;
  sched_lcbn->lcbn = lcbn;

  assert(sched_lcbn_looks_valid(sched_lcbn));

  return sched_lcbn;
} 

void sched_lcbn_destroy (sched_lcbn_t *sched_lcbn)
{
  assert(sched_lcbn_looks_valid(sched_lcbn));
#ifdef JD_DEBUG
  memset(sched_lcbn, 'c', sizeof *sched_lcbn);
#endif
  uv__free(sched_lcbn);
}

sched_context_t *sched_context_create (enum execution_context exec_context, enum callback_context cb_context, void *wrapper)
{
  sched_context_t *sched_context;
  assert(wrapper);

  sched_context = (sched_context_t *) uv__malloc(sizeof *sched_context);
  assert(sched_context != NULL);
  memset(sched_context, 0, sizeof *sched_context);

  sched_context->magic = SCHED_CONTEXT_MAGIC;
  sched_context->exec_context = exec_context;
  sched_context->cb_context = cb_context;
  sched_context->wrapper = wrapper;

  assert(sched_context_looks_valid(sched_context));
  return sched_context;
}

void sched_context_destroy (sched_context_t *sched_context)
{
  assert(sched_context_looks_valid(sched_context));
#ifdef JD_DEBUG
  memset(sched_context, 'c', sizeof *sched_context);
#endif
  uv__free(sched_context);
}

void sched_context_list_destroy_func (struct list_elem *e, void *aux){
  sched_context_t *sched_context = NULL;

  assert(e);
  sched_context = list_entry(e, sched_context_t, elem);
  assert(sched_context_looks_valid(sched_context));
  sched_context_destroy(sched_context);
}

static int scheduler_initialized (void)
{
  return scheduler.magic == SCHEDULER_MAGIC;
}

/* TODO DEBUGGING. */
static void dump_lcbn_tree_list_func (struct list_elem *e, void *aux)
{
  lcbn_t *lcbn = NULL;
  char buf[2048];

  assert(e);

  lcbn = tree_entry(list_entry(e, tree_node_t, tree_as_list_elem), 
                    lcbn_t, tree_node);
  assert(lcbn_looks_valid(lcbn));

  lcbn_to_string(lcbn, buf, sizeof buf);
  printf("%p: %s\n", (void *) lcbn, buf);
}

void scheduler_init (enum schedule_mode mode, char *schedule_file)
{
  FILE *f = NULL;
  char *line = NULL;
  size_t line_len;
  sched_lcbn_t *sched_lcbn = NULL;
  lcbn_t *parent = NULL;
  int found;
  struct list *filtered_nodes = NULL;

  assert(schedule_file != NULL);
  assert(!scheduler_initialized());

  scheduler.shadow_root = NULL;
  scheduler.name_to_lcbn = map_create();

  /* Allocate a line for SCHEDULE_MODE_REPLAY. */
  line_len = 2048;
  line = (char *) uv__malloc(line_len*sizeof(char));
  assert(line);
  memset(line, 0, line_len);

  scheduler.mode = mode;
  strncpy(scheduler.schedule_file, schedule_file, sizeof scheduler.schedule_file);
  scheduler.magic = SCHEDULER_MAGIC;
  scheduler.recorded_schedule = list_create();
  scheduler.desired_schedule = NULL;
  scheduler.executed_schedule = list_create();
  scheduler.n_executed = 1; /* Skip initial stack. */

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
    memset(line, 0, line_len*sizeof(char));
    while (0 < getline(&line, &line_len, f))
    {
      /* Remove trailing newline. */
      if(line[strlen(line)-1] == '\n')
        line[strlen(line)-1] = '\0';
      /* Parse line as an lcbn_t and wrap with a sched_lcbn. */
      sched_lcbn = sched_lcbn_create(lcbn_from_string(line, line_len));
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
    mylog(LOG_SCHEDULER, 1, "scheduler_init: Printing parsed nodes in registration order.\n");
    list_apply(scheduler.desired_schedule, dump_lcbn_tree_list_func, NULL);

    /* Sort by exec order so that we can efficiently handle scheduler queries. 
       Remove unexecuted nodes. */
    list_sort(scheduler.desired_schedule, lcbn_sort_by_exec_id, NULL);
    filtered_nodes = list_filter(scheduler.desired_schedule, lcbn_remove_unexecuted, NULL); 
    list_destroy(filtered_nodes);

    /* Remove more "unexecuted" nodes: internal nodes like the initial stack node are just placeholders, 
         and will never be executed through invoke_callback.
       TODO If we have more than one 'internal node', use another filter instead of this more direct approach. */
    assert(&scheduler.shadow_root->tree_node == list_entry(list_begin(scheduler.desired_schedule), tree_node_t, tree_as_list_elem));
    list_pop_front(scheduler.desired_schedule);

    mylog(LOG_SCHEDULER, 1, "scheduler_init: Printing parsed nodes in exec order.\n");
    list_apply(scheduler.desired_schedule, dump_lcbn_tree_list_func, NULL);
  }

  uv__free(line);
}

enum schedule_mode scheduler_get_mode (void)
{
  assert(scheduler_initialized());
  return scheduler.mode;
}

void scheduler_record (sched_lcbn_t *sched_lcbn)
{
  assert(scheduler_initialized());
  assert(sched_lcbn_looks_valid(sched_lcbn));

  list_push_back(scheduler.recorded_schedule, &sched_lcbn->elem);
}

void scheduler_emit (void)
{
  FILE *f = NULL;
  sched_lcbn_t *sched_lcbn = NULL;
  struct list_elem *e = NULL;
  char lcbn_str_buf[1024];

  assert(scheduler_initialized());
  if (scheduler.mode != SCHEDULE_MODE_RECORD)
    return;

  mylog(LOG_SCHEDULER, 1, "scheduler_emit: Writing schedule to %s\n", scheduler.schedule_file);

  f = fopen(scheduler.schedule_file, "w");
  assert(f);
  for (e = list_begin(scheduler.recorded_schedule); e != list_end(scheduler.recorded_schedule); e = list_next(e))
  {
    sched_lcbn = list_entry(e, sched_lcbn_t, elem);
    assert(sched_lcbn);
    lcbn_to_string(sched_lcbn->lcbn, lcbn_str_buf, sizeof lcbn_str_buf);
    assert(fprintf(f, "%s\n", lcbn_str_buf) == (int) strlen(lcbn_str_buf) + 1);
  }
  assert(fclose(f) == 0);
  fflush(NULL);
}

sched_context_t * scheduler_next_context (struct list *sched_context_list)
{
  struct list_elem *e = NULL;
  sched_context_t *next_sched_context = NULL, *sched_context = NULL;

  assert(scheduler_initialized());
  assert(sched_context_list);

  if (list_empty(sched_context_list))
    return NULL;

  next_sched_context = NULL;
  /* RECORD mode: execute the first context in the list. */
  if (scheduler.mode == SCHEDULE_MODE_RECORD)
  {
    next_sched_context = list_entry(list_begin(sched_context_list), sched_context_t, elem);
    assert(sched_context_looks_valid(next_sched_context));
  }
  /* REPLAY mode: if any context has the next_lcbn in it, return that context. */
  else if (scheduler.mode == SCHEDULE_MODE_REPLAY)
  {
    for (e = list_begin(sched_context_list); e != list_end(sched_context_list); e = list_next(e))
    {
      sched_context = list_entry(e, sched_context_t, elem);
      assert(sched_context_looks_valid(sched_context));
      if (scheduler_next_lcbn(sched_context))
      {
        next_sched_context = sched_context;
        break;
      }
    }
  }
  else
    NOT_REACHED;

  if (next_sched_context)
    assert(sched_context_looks_valid(next_sched_context));
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
  uv__ready_work_lcbns, /* UV_WORK */
  NULL, /* UV_GETADDRINFO */
  NULL  /* UV_GETNAMEINFO */
  /* UV_REQ_TYPE_PRIVATE -- empty in uv-unix.h. */
};

#define SILENT_CONTEXT 0x1
sched_lcbn_t * scheduler_next_lcbn (sched_context_t *sched_context)
{
  uv_handle_t *handle = NULL;
  uv_req_t *req = NULL;
  uv_req_type req_type;

  struct list *ready_lcbns = NULL;
  ready_lcbns_func lcbns_func;
  void *wrapper = NULL;

  struct list_elem *e = NULL;
  sched_lcbn_t *sched_lcbn = NULL, *next_sched_lcbn = NULL;

  assert(scheduler_initialized());
  assert(sched_context);
  assert(sched_context->wrapper);

  switch (sched_context->cb_context)
  {
    case CALLBACK_CONTEXT_HANDLE:
      handle = (uv_handle_t *) sched_context->wrapper;
      assert(handle->magic == UV_HANDLE_MAGIC);

      wrapper = handle;
      lcbns_func = uv__ready_handle_lcbns_wrap;
      break;
    case CALLBACK_CONTEXT_REQ:
      req = (uv_req_t *) sched_context->wrapper;
      assert(req->magic == UV_REQ_MAGIC);
      req_type = req->type;

      wrapper = req;
      lcbns_func = req_lcbn_funcs[req_type];
      break;
    case CALLBACK_CONTEXT_IO_ASYNC:
      wrapper = (uv_loop_t *) sched_context->wrapper;
      lcbns_func = uv__ready_async_event_lcbns;
      break;
    case CALLBACK_CONTEXT_IO_INOTIFY_READ:
      assert(!"scheduler_next_sched_lcbn: CALLBACK_CONTEXT_IO_INOTIFY_READ not yet handled");
      break;
    case CALLBACK_CONTEXT_IO_SIGNAL_EVENT:
      assert(!"scheduler_next_sched_lcbn: CALLBACK_CONTEXT_IO_SIGNAL_EVENT not yet handled");
      break;
    default:
      assert(!"scheduler_next_sched_lcbn: Error, unexpected cb_context");
  }

  assert(wrapper);
  assert(lcbns_func);

  /* NB This must return lcbns in the order in which they will be invoked by the handle. */
  ready_lcbns = (*lcbns_func)(wrapper, sched_context->exec_context);

  /* If SCHED_CONTEXT is schedulable but there are no LCBNs associated with it,
     then there is no (anticipated) harm in invoking it.
     Failure to invoke it means that we may not make forward progress.

     An example of possible harm is if there are two stream handles to the same
     client, and the user submits write requests along both handles.
     On one handle he submits requests with WRITE_CBs and on the other he does not.
     Executing the requests themselves can alter the behavior on REPLAY,
     but we cannot know that.

     I do not believe Node.js makes use of libuv in this fashion, though I suppose
     a 3rd-party library can do anything it wants to. 

     If it does, however, the application behavior is undefined anyway. 
     
     TODO This could be avoided by having the ready_lcbn funcs load up all possible LCBNs
     that COULD be invoked (if there's an associated CB), rather than all LCBNs that WILL be invoked,
     and changing INVOKE_CALLBACK to HANDLE_LCBN. Then we would eliminate these invisible guys, and have
     no SILENT_CONTEXTs at all.
     */
  if (list_empty(ready_lcbns))
  {
    mylog(LOG_SCHEDULER, 1, "scheduler_next_sched_lcbn: context %p has no ready lcbns, returning SILENT_CONTEXT\n", wrapper);
    next_sched_lcbn = (sched_lcbn_t *) SILENT_CONTEXT;
    goto CLEANUP;
  }

  next_sched_lcbn = NULL;
  for (e = list_begin(ready_lcbns); e != list_end(ready_lcbns); e = list_next(e))
  {
    sched_lcbn = list_entry(e, sched_lcbn_t, elem);
    assert(sched_lcbn_looks_valid(sched_lcbn));
    if (sched_lcbn_is_next(sched_lcbn))
    {
      next_sched_lcbn = sched_lcbn;
      break;
    }
  }
  if (next_sched_lcbn)
  {
    /* Make a copy so we can clean up ready_lcbns. */
    assert(sched_lcbn_looks_valid(next_sched_lcbn));
    next_sched_lcbn = sched_lcbn_create(next_sched_lcbn->lcbn);
    assert(sched_lcbn_looks_valid(next_sched_lcbn));
  }

  CLEANUP:
    /* If being called from scheduler_next_context, there may not be a match.
       Either way, clean up. */
    list_destroy_full(ready_lcbns, sched_lcbn_list_destroy_func, NULL);

    /* RECORD: next_sched_lcbn must be defined.
       REPLAY: we may not have found it. */
    assert(next_sched_lcbn || scheduler.mode == SCHEDULE_MODE_REPLAY);

    return next_sched_lcbn;
}

/* Must be called with mutex held. */
void scheduler_advance (void)
{
  lcbn_t *lcbn = NULL;
  assert(scheduler_initialized());

  if (scheduler.mode == SCHEDULE_MODE_REPLAY)
  {
    assert(!list_empty(scheduler.desired_schedule));

    lcbn = tree_entry(list_entry(list_pop_front(scheduler.desired_schedule),
                                 tree_node_t, tree_as_list_elem),
                      lcbn_t, tree_node);
    /* Make sure we're executing the right one! 
       If not, this is probably a sign that the input schedule has been
       modified incorrectly. */
    assert(lcbn_looks_valid(lcbn));
    assert(lcbn->global_exec_id == scheduler.n_executed);
    mylog(LOG_SCHEDULER, 1, "schedule_advance: discarding lcbn %p (exec_id %i type %s)\n",
      lcbn, lcbn->global_exec_id, callback_type_to_string(lcbn->cb_type));
    fflush(NULL);

    list_push_back(scheduler.executed_schedule, &sched_lcbn_create(lcbn)->elem);
  }

  scheduler.n_executed++;
}

void sched_lcbn_list_destroy_func (struct list_elem *e, void *aux)
{
  sched_lcbn_t *sched_lcbn = NULL;

  assert(e);
  sched_lcbn = list_entry(e, sched_lcbn_t, elem);
  assert(sched_lcbn_looks_valid(sched_lcbn));
  sched_lcbn_destroy(sched_lcbn);
}

struct list * uv__ready_handle_lcbns_wrap (void *wrapper, enum execution_context context)
{
  struct list *ret = NULL;
  uv_handle_t *handle = NULL;
  ready_lcbns_func func;

  assert(wrapper);
  handle = (uv_handle_t *) wrapper;
  assert(handle->magic == UV_HANDLE_MAGIC);

  func = handle_lcbn_funcs[handle->type];
  assert(func);

  ret = (*func)(handle, context);
  assert(list_looks_valid(ret));
  return ret;
}

int scheduler_already_run (void)
{
  assert(scheduler_initialized());
  return scheduler.n_executed;
}

int scheduler_remaining (void)
{
  assert(scheduler_initialized());

  if (scheduler.mode == SCHEDULE_MODE_RECORD)
    return -1;
  assert(scheduler.mode == SCHEDULE_MODE_REPLAY);
  return list_size(scheduler.desired_schedule);
}
