#include "scheduler.h"

#include "list.h"
#include "map.h"
#include "mylog.h"

#include "unix/internal.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>

#define SCHEDULER_MAGIC 8675309 /* Jenny. */
#define SCHED_LCBN_MAGIC 19283746
#define SCHED_CONTEXT_MAGIC 55443322

/* Globals. */
struct
{
  int magic;
  /* RECORD, REPLAY modes. */
  schedule_mode_t mode;
  char schedule_file[256];

  struct list *registration_schedule; /* List of the registered sched_lcbn_t's, in registration order. */

  /* REPLAY mode. */
  lcbn_t *shadow_root; /* Root of the "shadow tree" -- the registration tree described in the input file. */
  struct map *name_to_lcbn; /* Used to map hash(name) to lcbn. Allows us to re-build the tree. */
  struct list *desired_schedule; /* A tree_as_list list (rooted at shadow_root) of sched_lcbn_t's, expressing desired execution order, first to last. We discard internal nodes (e.g. initial stack node). */
  struct list *execution_schedule; /* List of the executed sched_lcbn_t's, in order of execution. Nodes move from desired_schedule to execution_schedule as they are executed. This gives us a cheap way to implement scheduler__find_scheduled_sched_lcbn. */
  int n_executed;
  int diverged; /* 1 if the REPLAY'd schedule has diverged. */

  uv_mutex_t lock;
} scheduler;

/* Private APIs. */
/* These extract the type of handle/req H_OR_R and route to the appropriate handler. */
static struct list * uv__ready_handle_lcbns_wrap (void *wrapper, enum execution_context context);
static struct list * uv__ready_req_lcbns_wrap (void *wrapper, enum execution_context context);

/* Returns 1 if initialized, else 0. */
static int scheduler_initialized (void)
{
  int initialized = 0;
  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler_initialized: begin\n"));
  initialized = (scheduler.magic == SCHEDULER_MAGIC);
  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler_initialized: returning initialized %i\n", initialized));
  return initialized;
}

/* Set the mode of the scheduler. */
static void scheduler__set_mode (schedule_mode_t new_mode)
{
  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler__set_mode: begin: new_mode %i\n", new_mode));

  assert(scheduler_initialized());
  mylog(LOG_SCHEDULER, 3, "scheduler__set_mode: current mode <%s> new mode %s\n", 
    schedule_mode_to_string(scheduler_get_mode()), schedule_mode_to_string(new_mode));

  scheduler.mode = new_mode;
  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler__set_mode: returning\n"));
}

static void scheduler_uninitialize (void)
{
  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler_uninitialize: begin\n"));
  memset(&scheduler, 0, sizeof scheduler);
  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler_uninitialize: returning\n"));
}

static int sched_lcbn_looks_valid (sched_lcbn_t *sched_lcbn)
{
  int valid = 1;

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "sched_lcbn_looks_valid: begin: sched_lcbn %p\n", sched_lcbn));
  if (!sched_lcbn)
  {
    valid = 0;
    goto DONE;
  }

  if (sched_lcbn->magic != SCHED_LCBN_MAGIC)
  {
    valid = 0;
    goto DONE;
  }

  if (!lcbn_looks_valid(sched_lcbn->lcbn))
  {
    valid = 0;
    goto DONE;
  }

  valid = 1;
  DONE:
    ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "sched_lcbn_looks_valid: returning valid %i\n", valid));
    return valid;
}

static int sched_context_looks_valid (sched_context_t *sched_context)
{
  int valid = 1;

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "sched_context_looks_valid: begin: sched_context %p\n", sched_context));
  if (!sched_context)
  {
    valid = 0;
    goto DONE;
  }

  if (sched_context->magic != SCHED_CONTEXT_MAGIC)
  {
    valid = 0;
    goto DONE;
  }

  valid = 1;
  DONE:
    ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "sched_context_looks_valid: returning valid %i\n", valid));
    return valid;
}

/* Return the scheduled sched_lcbn corresponding to already-executed SCHED_LCBN, or NULL. 
   The returned lcbn_t is read-only. 
   LCBN must have been scheduler_advance()'d and executed already. */
static sched_lcbn_t * scheduler__find_scheduled_sched_lcbn (sched_lcbn_t *sched_lcbn)
{
  sched_lcbn_t *mate = NULL;
  struct list_elem *e = NULL;

  assert(sched_lcbn_looks_valid(sched_lcbn));
  assert(lcbn_executed(sched_lcbn->lcbn));

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler__find_scheduled_sched_lcbn: begin: sched_lcbn %p (lcbn %p)\n", sched_lcbn, sched_lcbn->lcbn));
  /* Search backwards in execution_schedule for a matching exec_id. */
  for (e = list_back(scheduler.execution_schedule); e != list_head(scheduler.execution_schedule); e = list_prev(e))
  {
    sched_lcbn_t *potential_mate = list_entry(e, sched_lcbn_t, elem); 
    assert(sched_lcbn_looks_valid(potential_mate));
    if (potential_mate->lcbn->global_exec_id == sched_lcbn->lcbn->global_exec_id)
    {
      mate = potential_mate;
      break;
    }
  }

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler__find_scheduled_sched_lcbn: returning mate %p\n", mate));
  return mate;
}

/* Internal scheduler lock. 
   These lock routines can be called recursively. Don't mess up. */
int lock_depth = 0; /* Mimic a recursive mutex. */
#define NO_HOLDER -1
uv_thread_t current_holder = NO_HOLDER;
static void scheduler__lock (void)
{
  int had_to_lock_mutex = 0;
  uv_thread_t lock_aspirant = uv_thread_self();

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler__lock: begin\n"));
  assert(scheduler_initialized());
  assert(uv_thread_self() != (uv_thread_t) NO_HOLDER);

  assert(0 <= lock_depth);
  if (!lock_depth)
  {
    uv_mutex_lock(&scheduler.lock);
    had_to_lock_mutex = 1;
  }
  else if (lock_aspirant != current_holder)
  {
    uv_mutex_lock(&scheduler.lock);
    had_to_lock_mutex = 1;
  }

  if (had_to_lock_mutex)
  {
    assert(lock_depth == 0 && current_holder == (uv_thread_t) NO_HOLDER);
    current_holder = uv_thread_self();
  }
  else
    assert(lock_depth && current_holder == uv_thread_self());

  lock_depth++;
  assert(current_holder == uv_thread_self());
  assert(1 <= lock_depth);

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler__lock: returning (lock_depth %i)\n", lock_depth));
}

static void scheduler__unlock (void)
{
  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler__unlock: begin\n"));
  assert(scheduler_initialized());

  assert(1 <= lock_depth && current_holder == uv_thread_self());
  lock_depth--;
  if (!lock_depth)
  {
    current_holder = NO_HOLDER;
    uv_mutex_unlock(&scheduler.lock);
  }

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler__unlock: returning (lock_depth %i)\n", lock_depth));
}

/* Public APIs. */
lcbn_t * scheduler_next_scheduled_lcbn (void)
{
  lcbn_t *next_lcbn = NULL;

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler_next_scheduled_lcbn: begin\n"));
  assert(scheduler_initialized());

  if (scheduler_get_mode() != SCHEDULE_MODE_REPLAY)
  {
    next_lcbn = NULL;
    goto DONE;
  }

  scheduler__lock();

  if (!list_empty(scheduler.desired_schedule))
  {
    next_lcbn = tree_entry(list_entry(list_begin(scheduler.desired_schedule), tree_node_t, tree_as_list_elem),
                           lcbn_t, tree_node);
    assert(lcbn_looks_valid(next_lcbn));
  }

  scheduler__unlock();

  DONE:
    ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler_next_scheduled_lcbn: returning next_lcbn %p\n", next_lcbn));
    return next_lcbn;
}

enum callback_type scheduler_next_lcbn_type (void)
{
  lcbn_t *next_lcbn = NULL;
  enum callback_type cb_type = CALLBACK_TYPE_ANY;

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler_next_lcbn_type: begin\n"));
  assert(scheduler_initialized());

  scheduler__lock();

  next_lcbn = scheduler_next_scheduled_lcbn();
  if (next_lcbn)
  {
    assert(lcbn_looks_valid(next_lcbn));
    cb_type = next_lcbn->cb_type;
  }

  scheduler__unlock();

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler_next_lcbn_type: returning type %i (%s)\n", cb_type, callback_type_to_string(cb_type)));
  return cb_type;
}

/* Return non-zero if SCHED_LCBN is next, else zero. */
int sched_lcbn_is_next (sched_lcbn_t *ready_sched_lcbn)
{
  lcbn_t *next_lcbn = NULL;
  int is_next = 0, equal = 0, verbosity = 0;

  assert(scheduler_initialized());
  assert(sched_lcbn_looks_valid(ready_sched_lcbn));

  scheduler__lock();

  /* RECORD mode: Every queried sched_lcbn is "next". */
  if (scheduler_get_mode() == SCHEDULE_MODE_RECORD)
  {
    is_next = 1;
    goto DONE;
  }

  next_lcbn = scheduler_next_scheduled_lcbn();
  /* If nothing left in the schedule, we can't run this. */
  if (!next_lcbn)
  {
    /* TODO Revisit this once divergence is working. 
       When written: "At the moment, I'm only testing replay-ability of a recorded schedule. Consequently this should only happen because we always leave the UV_ASYNC_CB for the threadpool done queue pending." */
    assert(ready_sched_lcbn->lcbn->cb_type == UV_ASYNC_CB);
    is_next = 0;
    goto DONE;
  }

  /* "Normal" REPLAY path. Test semantic equality. */
  assert(lcbn_looks_valid(next_lcbn));
  /* Optimization: marker events are just a chain, with no risk of confusion.
       Testing the cb_type is sufficient and saves time. */
  if (is_marker_event(next_lcbn->cb_type) || is_marker_event(ready_sched_lcbn->lcbn->cb_type))
    equal = (next_lcbn->cb_type == ready_sched_lcbn->lcbn->cb_type);
  else
    equal = lcbn_semantic_equals(next_lcbn, ready_sched_lcbn->lcbn);
  verbosity = equal ? 5 : 7;
  mylog(LOG_SCHEDULER, verbosity, "sched_lcbn_is_next: Next exec_id %i next_lcbn %p (type %s) ready_sched_lcbn %p (type %s) equal? %i\n", next_lcbn->global_exec_id, next_lcbn, callback_type_to_string(next_lcbn->cb_type), ready_sched_lcbn->lcbn, callback_type_to_string(ready_sched_lcbn->lcbn->cb_type), equal);
  is_next = equal;

  DONE:
    scheduler__unlock();
    ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "sched_lcbn_is_next: returning is_next %i\n", is_next));
    return is_next;
}

sched_lcbn_t *sched_lcbn_create (lcbn_t *lcbn)
{
  sched_lcbn_t *sched_lcbn = (sched_lcbn_t *) uv__malloc(sizeof *sched_lcbn);

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "sched_lcbn_create: begin: lcbn %p\n", lcbn));
  assert(lcbn_looks_valid(lcbn));

  assert(sched_lcbn);
  memset(sched_lcbn, 0, sizeof *sched_lcbn);

  sched_lcbn->magic = SCHED_LCBN_MAGIC;
  sched_lcbn->lcbn = lcbn;

  assert(sched_lcbn_looks_valid(sched_lcbn));
  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "sched_lcbn_create: returning sched_lcbn %p\n", sched_lcbn));
  return sched_lcbn;
} 

void sched_lcbn_destroy (sched_lcbn_t *sched_lcbn)
{
  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "sched_lcbn_destroy: begin: sched_lcbn %p\n", sched_lcbn));
  assert(sched_lcbn_looks_valid(sched_lcbn));

#ifdef JD_DEBUG
  memset(sched_lcbn, 'c', sizeof *sched_lcbn);
#endif
  uv__free(sched_lcbn);

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "sched_lcbn_destroy: returning\n"));
}

void sched_lcbn_list_destroy_func (struct list_elem *e, void *aux)
{
  sched_lcbn_t *sched_lcbn = NULL;

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "sched_lcbn_list_destroy_func: begin: e %p aux %p\n", e, aux));

  assert(e);
  sched_lcbn = list_entry(e, sched_lcbn_t, elem);
  assert(sched_lcbn_looks_valid(sched_lcbn));
  sched_lcbn_destroy(sched_lcbn);

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "sched_lcbn_list_destroy_func: returning\n"));
}

sched_context_t *sched_context_create (enum execution_context exec_context, enum callback_context cb_context, void *wrapper)
{
  sched_context_t *sched_context = (sched_context_t *) uv__malloc(sizeof *sched_context);

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "sched_context_create: begin: exec_context %i cb_context %i wrapper %p\n", exec_context, cb_context, wrapper));
  assert(wrapper);

  assert(sched_context);
  memset(sched_context, 0, sizeof *sched_context);

  sched_context->magic = SCHED_CONTEXT_MAGIC;
  sched_context->exec_context = exec_context;
  sched_context->cb_context = cb_context;
  sched_context->wrapper = wrapper;

  assert(sched_context_looks_valid(sched_context));
  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "sched_context_create: returning sched_context %p\n", sched_context));
  return sched_context;
}

void sched_context_destroy (sched_context_t *sched_context)
{
  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "sched_context_destroy: begin: sched_context %p\n", sched_context));
  assert(sched_context_looks_valid(sched_context));

#ifdef JD_DEBUG
  memset(sched_context, 'c', sizeof *sched_context);
#endif
  uv__free(sched_context);

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "sched_context_destroy: returning\n"));
}

void sched_context_list_destroy_func (struct list_elem *e, void *aux)
{
  sched_context_t *sched_context = NULL;

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "sched_context_list_destroy_func: begin: e %p aux %p\n", e, aux));
  assert(e);
  sched_context = list_entry(e, sched_context_t, elem);
  assert(sched_context_looks_valid(sched_context));

  sched_context_destroy(sched_context);
  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "sched_context_list_destroy_func: returning\n"));
}

/* Not thread safe. */
static char dump_buf[2048];
static void dump_lcbn_tree_list_func (struct list_elem *e, void *aux)
{
  lcbn_t *lcbn = NULL;

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "dump_lcbn_tree_list_func: begin: e %p aux %p\n", e, aux));
  assert(e);

  lcbn = tree_entry(list_entry(e, tree_node_t, tree_as_list_elem), 
                    lcbn_t, tree_node);
  assert(lcbn_looks_valid(lcbn));

  memset(dump_buf, 0, sizeof dump_buf);
  lcbn_to_string(lcbn, dump_buf, sizeof dump_buf);
  mylog(LOG_SCHEDULER, 1, "%p: %s\n", (void *) lcbn, dump_buf);

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "dump_lcbn_tree_list_func: returning\n"));
}

/* Not thread safe. */
void scheduler_init (schedule_mode_t mode, char *schedule_file)
{
  FILE *f = NULL;
  sched_lcbn_t *sched_lcbn = NULL;
  lcbn_t *parent_lcbn = NULL;
  struct list *filtered_nodes = NULL;
  char *line = NULL;

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler_init: begin: mode %i schedule_file %p (%s)\n", mode, schedule_file, schedule_file));
  assert(schedule_file);
  assert(!scheduler_initialized());
  memset(&scheduler, 0, sizeof scheduler);

  scheduler.magic = SCHEDULER_MAGIC;
  scheduler.mode = mode;
  strncpy(scheduler.schedule_file, schedule_file, sizeof scheduler.schedule_file);
  scheduler.registration_schedule = list_create();
  scheduler.execution_schedule = list_create();
  scheduler.shadow_root = NULL;
  scheduler.name_to_lcbn = map_create();
  scheduler.desired_schedule = NULL;
  scheduler.n_executed = 0;
  scheduler.diverged = 0;
  uv_mutex_init(&scheduler.lock);

  if (mode == SCHEDULE_MODE_RECORD)
  {
    /* Verify that we can open and close the file; truncate it. */
    f = fopen(scheduler.schedule_file, "w");
    assert(f);
    assert(!fclose(f));
    mylog(LOG_SCHEDULER, 5, "scheduler_init: RECORD: file %s looks OK\n", scheduler.schedule_file);
  }
  else if (mode == SCHEDULE_MODE_REPLAY)
  {
    int found = 0;
    size_t dummy = 0;
    struct stat sb;
    size_t schedule_size = 0;

    f = fopen(scheduler.schedule_file, "r");
    assert(f);
    assert(!fstat(fileno(f), &sb));
    assert(0 < sb.st_size);
    mylog(LOG_SCHEDULER, 5, "scheduler_init: REPLAY: file %s looks OK\n", scheduler.schedule_file);

    line = NULL;
    while (0 < getline(&line, &dummy, f))
    {
      mylog(LOG_SCHEDULER, 5, "scheduler_init: lcbn %u, got line <%s>\n", schedule_size, line);
      /* Remove trailing newline. */
      if(line[strlen(line)-1] == '\n')
        line[strlen(line)-1] = '\0';

      /* Parse line_buf as an lcbn_t and wrap in a sched_lcbn. */
      sched_lcbn = sched_lcbn_create(lcbn_from_string(line, strlen(line)));

      /* Add the new lcbn to the name map. */
      mylog(LOG_SCHEDULER, 5, "scheduler_init: Adding new lcbn (name %s)\n", sched_lcbn->lcbn->name);
      map_insert(scheduler.name_to_lcbn, map_hash(sched_lcbn->lcbn->name, strlen(sched_lcbn->lcbn->name)), sched_lcbn->lcbn);

      if (!scheduler.shadow_root)
      {
        /* First is the root. */
        assert(sched_lcbn->lcbn->cb_type == INITIAL_STACK);
        scheduler.shadow_root = sched_lcbn->lcbn;
      }
      else
      {
        /* Locate the parent_lcbn by name; update the tree. 
           This requires that the input schedule be in REGISTRATION ORDER,
           since otherwise the parent may not be in the tree yet. */
        mylog(LOG_SCHEDULER, 5, "scheduler_init: looking up parent_lcbn (name %s)\n", sched_lcbn->lcbn->parent_name);
        parent_lcbn = map_lookup(scheduler.name_to_lcbn, map_hash(sched_lcbn->lcbn->parent_name, strlen(sched_lcbn->lcbn->parent_name)), &found);
        mylog(LOG_SCHEDULER, 5, "scheduler_init: found %i; parent_lcbn %p\n", found, parent_lcbn);
        assert(found && lcbn_looks_valid(parent_lcbn));
        lcbn_add_child(parent_lcbn, sched_lcbn->lcbn);
      }

      free(line);
      line = NULL;
      schedule_size++;
    }
    assert(errno != EINVAL); /* getline failure */

    assert(!fclose(f));
    assert(scheduler.shadow_root);

    /* Extract the execution schedule from the tree. */
    scheduler.desired_schedule = tree_as_list(&scheduler.shadow_root->tree_node);

    /* Sort by registration order and print out what we've parsed. */
    list_sort(scheduler.desired_schedule, lcbn_sort_by_reg_id, NULL);
    mylog(LOG_SCHEDULER, 1, "scheduler_init: Printing all %u parsed nodes in registration order.\n", list_size(scheduler.desired_schedule));
    list_apply(scheduler.desired_schedule, dump_lcbn_tree_list_func, NULL);

    /* Calculate desired_schedule based on the execution order of the tree we've parsed
        - remove unexecuted nodes from the list (but leave them in the tree for divergence testing).
        - sort by exec order so that we can efficiently handle scheduler queries
    */ 
    mylog(LOG_SCHEDULER, 5, "scheduler_init: calculating desired schedule\n");

    filtered_nodes = list_filter(scheduler.desired_schedule, lcbn_remove_unexecuted, NULL);  /* unexecuted */
    list_destroy(filtered_nodes);

    list_sort(scheduler.desired_schedule, lcbn_sort_by_exec_id, NULL);

    mylog(LOG_SCHEDULER, 1, "scheduler_init: Printing all %u executed nodes in exec order.\n", list_size(scheduler.desired_schedule));
    list_apply(scheduler.desired_schedule, dump_lcbn_tree_list_func, NULL);
  } /* REPLAY mode. */

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler_init: returning\n"));
}

schedule_mode_t scheduler_get_mode (void)
{
  schedule_mode_t mode = scheduler.mode;

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler_get_mode: begin\n"));
  assert(scheduler_initialized());

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler_get_mode: returning mode %i (%s)\n", mode, schedule_mode_to_string(mode)));
  return mode;
}

char *schedule_mode_strings[SCHEDULE_MODE_MAX - SCHEDULE_MODE_MIN] = {
  "SCHEDULE_MODE_RECORD",
  "SCHEDULE_MODE_REPLAY"
};

const char * schedule_mode_to_string (schedule_mode_t mode)
{
  char *str = NULL;
  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler_mode_to_string: begin: mode %i\n", mode));

  assert(SCHEDULE_MODE_MIN <= mode && mode < SCHEDULE_MODE_MAX);
  str = schedule_mode_strings[mode - SCHEDULE_MODE_MIN];

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler_mode_to_string: returning string %s\n", str));
  return str;
}

void scheduler_register_lcbn (sched_lcbn_t *sched_lcbn)
{
  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler_register_lcbn: begin: sched_lcbn %p\n", sched_lcbn));
  assert(scheduler_initialized());
  assert(sched_lcbn_looks_valid(sched_lcbn));

  scheduler__lock();
  list_push_back(scheduler.registration_schedule, &sched_lcbn->elem);
  scheduler__unlock();

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler_register_lcbn: returning\n"));
}

/* Not thread safe. */
static char lcbn_str_buf[2048];
void scheduler_emit (void)
{
  int fd = -1;
  ssize_t len = 0;
  sched_lcbn_t *sched_lcbn = NULL;
  struct list_elem *e = NULL;

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler_emit: begin\n"));
  assert(scheduler_initialized());

  if (scheduler_get_mode() == SCHEDULE_MODE_REPLAY)
    strncat(scheduler.schedule_file, "-replay", sizeof scheduler.schedule_file);
  else if (scheduler_has_diverged())
    strncat(scheduler.schedule_file, "-diverged", sizeof scheduler.schedule_file);

  mylog(LOG_SCHEDULER, 1, "scheduler_emit: Writing schedule to %s\n", scheduler.schedule_file);

  fd = open(scheduler.schedule_file, O_CREAT|O_TRUNC|O_WRONLY, 0777);
  assert(0 <= fd);

  scheduler__lock();

  for (e = list_begin(scheduler.registration_schedule); e != list_end(scheduler.registration_schedule); e = list_next(e))
  {
    memset(lcbn_str_buf, 0, sizeof lcbn_str_buf);
    sched_lcbn = list_entry(e, sched_lcbn_t, elem);
    assert(sched_lcbn_looks_valid(sched_lcbn));
    lcbn_to_string(sched_lcbn->lcbn, lcbn_str_buf, sizeof lcbn_str_buf);
    len = strlen(lcbn_str_buf);
    lcbn_str_buf[len] = '\n';
    lcbn_str_buf[len+1] = '\0';
    assert(write(fd, lcbn_str_buf, len+1) == len+1);
  }
  assert(!fsync(fd));
  assert(!close(fd));

  scheduler__unlock();

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler_emit: returning\n"));
}

sched_context_t * scheduler_next_context (struct list *sched_context_list)
{
  struct list_elem *e = NULL;
  sched_context_t *next_sched_context = NULL, *sched_context = NULL;

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler_next_context: begin: sched_context_list %p\n", sched_context_list));
  assert(scheduler_initialized());
  assert(list_looks_valid(sched_context_list));

  scheduler__lock();

  if (list_empty(sched_context_list))
    goto DONE;

  next_sched_context = NULL;
  /* RECORD mode: execute the first context in the list. */
  if (scheduler_get_mode() == SCHEDULE_MODE_RECORD)
  {
    next_sched_context = list_entry(list_begin(sched_context_list), sched_context_t, elem);
    assert(sched_context_looks_valid(next_sched_context));
  }
  /* REPLAY mode: if any context has the next_lcbn in it, return that context. */
  else if (scheduler_get_mode() == SCHEDULE_MODE_REPLAY)
  {
    for (e = list_begin(sched_context_list); e != list_end(sched_context_list); e = list_next(e))
    {
      sched_context = list_entry(e, sched_context_t, elem);
      assert(sched_context_looks_valid(sched_context));
      mylog(LOG_SCHEDULER, 3, "scheduler_next_context: examining context %p (exec_context %i cb_context %i wrapper %p)\n", sched_context, sched_context->exec_context, sched_context->cb_context, sched_context->wrapper);
      if (scheduler_next_lcbn(sched_context))
      {
        next_sched_context = sched_context;
        break;
      }
    }
  }
  else
    assert(!"scheduler_next_context: Error, unexpected scheduler mode");

  if (next_sched_context)
  {
    assert(sched_context_looks_valid(next_sched_context));
    mylog(LOG_SCHEDULER, 3, "scheduler_next_context: sched_context %p is next\n", next_sched_context);
  }
  else
    mylog(LOG_SCHEDULER, 3, "scheduler_next_context: None of the %u sched_contexts were next (next type: %s; exec_id %i)\n", list_size(sched_context_list), callback_type_to_string(scheduler_next_lcbn_type()), scheduler_already_run());

  DONE:
    scheduler__unlock();
    ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler_next_context: returning next_sched_context %p\n", next_sched_context));
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

  struct list *ready_lcbns = NULL;
  ready_lcbns_func lcbns_func = NULL;
  void *wrapper = NULL; /* TODO Use a union. This function is silly. */

  struct list_elem *e = NULL;
  sched_lcbn_t *sched_lcbn = NULL, *next_sched_lcbn = NULL;

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler_get_mode: begin: sched_context %p\n", sched_context));
  assert(scheduler_initialized());
  assert(sched_context_looks_valid(sched_context));
  assert(sched_context->wrapper);

  scheduler__lock();

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

      wrapper = req;
      lcbns_func = uv__ready_req_lcbns_wrap;
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
  assert(list_looks_valid(ready_lcbns));

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
  mylog(LOG_SCHEDULER, 3, "scheduler_next_sched_lcbn: sched_context %p (wrapper %p): %u ready LCBNs\n", sched_context, wrapper, list_size(ready_lcbns));
  if (list_empty(ready_lcbns))
  {
    mylog(LOG_SCHEDULER, 1, "scheduler_next_sched_lcbn: no ready LCBNs, returning SILENT_CONTEXT\n", sched_context, wrapper);
    next_sched_lcbn = (sched_lcbn_t *) SILENT_CONTEXT;
    goto DONE;
  }

  next_sched_lcbn = NULL;
  for (e = list_begin(ready_lcbns); e != list_end(ready_lcbns); e = list_next(e))
  {
    sched_lcbn = list_entry(e, sched_lcbn_t, elem);
    assert(sched_lcbn_looks_valid(sched_lcbn));
    mylog(LOG_SCHEDULER, 3, "scheduler_next_sched_lcbn: examining sched_lcbn %p (lcbn %p type %s)\n", sched_lcbn, sched_lcbn->lcbn, callback_type_to_string(sched_lcbn->lcbn->cb_type));
    if (sched_lcbn_is_next(sched_lcbn))
    {
      next_sched_lcbn = sched_lcbn;
      break;
    }
  }

  if (next_sched_lcbn)
  {
    /* Make a copy so we can easily clean up ready_lcbns. */
    assert(sched_lcbn_looks_valid(next_sched_lcbn));
    next_sched_lcbn = sched_lcbn_create(next_sched_lcbn->lcbn);
    assert(sched_lcbn_looks_valid(next_sched_lcbn));
    mylog(LOG_SCHEDULER, 3, "scheduler_next_sched_lcbn: next_sched_lcbn %p (lcbn %p type %s) is next\n", next_sched_lcbn, sched_lcbn->lcbn, callback_type_to_string(sched_lcbn->lcbn->cb_type));
  }
  else
    mylog(LOG_SCHEDULER, 3, "scheduler_next_sched_lcbn: None of the %u sched_lcbns were next\n", list_size(ready_lcbns));

  DONE:
    scheduler__unlock();
    /* If being called from scheduler_next_context, there may not be a match.
       Either way, clean up. */
    list_destroy_full(ready_lcbns, sched_lcbn_list_destroy_func, NULL);

    /* RECORD: next_sched_lcbn must be defined.
       REPLAY: we may not have found it. */
    assert(next_sched_lcbn || scheduler_get_mode() == SCHEDULE_MODE_REPLAY);

    ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler_next_lcbn: returning next_sched_lcbn %p\n", next_sched_lcbn));

    return next_sched_lcbn;
}

void scheduler_advance (void)
{
  lcbn_t *lcbn = NULL;

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler_advance: begin\n"));
  assert(scheduler_initialized());

  scheduler__lock();

  if (scheduler_get_mode() == SCHEDULE_MODE_REPLAY)
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
    mylog(LOG_SCHEDULER, 1, "scheduler_advance: Advancing past lcbn %p (exec_id %i type %s)\n",
      lcbn, lcbn->global_exec_id, callback_type_to_string(lcbn->cb_type));

    list_push_back(scheduler.execution_schedule, &sched_lcbn_create(lcbn)->elem);

    /* Preview of next candidate. */
    if (list_empty(scheduler.desired_schedule))
    {
      mylog(LOG_SCHEDULER, 1, "scheduler_advance: Next up: No LCBNs left in the schedule\n",
        lcbn, lcbn->global_exec_id, callback_type_to_string(lcbn->cb_type));
    }
    else
    {
      lcbn = tree_entry(list_entry(list_front(scheduler.desired_schedule),
                                   tree_node_t, tree_as_list_elem),
                        lcbn_t, tree_node);
      mylog(LOG_SCHEDULER, 1, "scheduler_advance: Next up: lcbn %p (exec_id %i type %s)\n",
        lcbn, lcbn->global_exec_id, callback_type_to_string(lcbn->cb_type));
    }
  }
  scheduler.n_executed++;

  scheduler__unlock();
  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler_advance: returning\n"));
}

struct list * uv__ready_handle_lcbns_wrap (void *wrapper, enum execution_context context)
{
  struct list *ret = NULL;
  uv_handle_t *handle = NULL;
  ready_lcbns_func func = NULL;

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "uv__ready_handle_lcbns_wrap: begin: wrapper %p context %i\n", wrapper, context));
  assert(wrapper);
  handle = (uv_handle_t *) wrapper;
  assert(handle->magic == UV_HANDLE_MAGIC);

  mylog(LOG_SCHEDULER, 5, "uv__ready_handle_lcbns_wrap: handle %p type %i\n", handle, handle->type);
  func = handle_lcbn_funcs[handle->type];
  assert(func);

  ret = (*func)(handle, context);
  assert(list_looks_valid(ret));

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "uv__ready_handle_lcbns_wrap: returning ret %p\n", ret));
  return ret;
}

static struct list * uv__ready_req_lcbns_wrap (void *wrapper, enum execution_context context)
{
  struct list *ret = NULL;
  uv_req_t *req = NULL;
  ready_lcbns_func func = NULL;

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "uv__ready_req_lcbns_wrap: begin: wrapper %p context %i\n", wrapper, context));
  assert(wrapper);
  req = (uv_req_t *) wrapper;
  assert(req->magic == UV_REQ_MAGIC);

  func = req_lcbn_funcs[req->type];
  assert(func);

  ret = (*func)(req, context);
  assert(list_looks_valid(ret));
  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "uv__ready_req_lcbns_wrap: returning ret %p\n", ret));
  return ret;
}

schedule_mode_t scheduler_check_for_divergence (lcbn_t *lcbn)
{
  int is_diverged = 0;
  schedule_mode_t schedule_mode = scheduler_get_mode();
  sched_lcbn_t *executed_sched_lcbn = NULL, *scheduled_sched_lcbn = NULL;
  unsigned executed_n_children = 0, scheduled_n_children = 0, i = 0;
  struct list_elem *executed_child_elem = NULL, *scheduled_child_elem = NULL;

  assert(scheduler_initialized());
  assert(lcbn);

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler_check_for_divergence: begin: lcbn %p (mode %s)\n", lcbn, schedule_mode));

  if (schedule_mode == SCHEDULE_MODE_RECORD)
  {
    /* Can't diverge if we're recording. */
    ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler_check_for_divergence: returning mode %s\n", schedule_mode));
    return schedule_mode;
  }
  
  /* Find the REPLAY record corresponding to this observed LCBN. */
  executed_sched_lcbn = sched_lcbn_create(lcbn);
  scheduled_sched_lcbn = scheduler__find_scheduled_sched_lcbn(executed_sched_lcbn);
  assert(scheduled_sched_lcbn);

  /* Same number of children? */
  executed_n_children  = list_size(executed_sched_lcbn->lcbn->tree_node.children);
  scheduled_n_children = list_size(scheduled_sched_lcbn->lcbn->tree_node.children);
  if (executed_n_children != scheduled_n_children)
  {
    mylog(LOG_SCHEDULER, 1, "scheduler_check_for_divergence: schedule has diverged: executed_n_children %u != scheduled_n_children %u\n", executed_n_children, scheduled_n_children);
    is_diverged = 1;
    goto MAYBE_DIVERGED;
  }

  /* Children have matching types? */
  executed_child_elem  = list_front(executed_sched_lcbn->lcbn->tree_node.children);
  scheduled_child_elem = list_front(scheduled_sched_lcbn->lcbn->tree_node.children);
  for (i = 0; i < executed_n_children; i++)
  {
    lcbn_t *executed_child_lcbn = tree_entry(list_entry(executed_child_elem, tree_node_t, parent_child_list_elem),
                                             lcbn_t, tree_node);
    lcbn_t *scheduled_child_lcbn = tree_entry(list_entry(scheduled_child_elem, tree_node_t, parent_child_list_elem),
                                             lcbn_t, tree_node);
    assert(lcbn_looks_valid(executed_child_lcbn));
    assert(lcbn_looks_valid(scheduled_child_lcbn));

    if (executed_child_lcbn->cb_type != scheduled_child_lcbn->cb_type)
    {
      mylog(LOG_SCHEDULER, 1, "scheduler_check_for_divergence: schedule has diverged: child %u: executed child type %s != scheduled child %u type %s\n", i, callback_type_to_string(executed_child_lcbn->cb_type), callback_type_to_string(scheduled_child_lcbn->cb_type));
      is_diverged = 1;
      goto MAYBE_DIVERGED;
    }
  }

MAYBE_DIVERGED:
  if (is_diverged)
  {
    /* Schedule has diverged. 
       We're breaking new ground; switch back to record mode. */
    scheduler.diverged = 1;
    mylog(LOG_SCHEDULER, 1, "scheduler_check_for_divergence: Schedule has diverged. Changing mode to SCHEDULE_MODE_RECORD\n");
    schedule_mode = SCHEDULE_MODE_RECORD;
    scheduler__set_mode(schedule_mode);
  }
  else
  {
    mylog(LOG_SCHEDULER, 9, "scheduler_check_for_divergence: Schedule has not diverged.\n");
  }

  sched_lcbn_destroy(executed_sched_lcbn);

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler_check_for_divergence: returning mode %s\n", schedule_mode));
  return schedule_mode;
}

int scheduler_already_run (void)
{
  int n_already_run = 0;

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler_already_run: begin\n"));
  assert(scheduler_initialized());

  scheduler__lock();

  n_already_run = scheduler.n_executed;

  scheduler__unlock();

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler_already_run: returning n_already_run %i\n", n_already_run));
  return n_already_run;
}

int scheduler_remaining (void)
{
  int n_remaining = 0;

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler_remaining: begin\n"));
  assert(scheduler_initialized());

  scheduler__lock();

  if (scheduler_get_mode() == SCHEDULE_MODE_RECORD)
    n_remaining = -1;
  else if (scheduler_get_mode() == SCHEDULE_MODE_REPLAY)
    n_remaining = list_size(scheduler.desired_schedule);
  else
    NOT_REACHED;

  scheduler__unlock();

  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler_remaining: returning n_remaining %i\n", n_remaining));
  return n_remaining;
}

int scheduler_has_diverged (void)
{
  return scheduler.diverged;
}

/* This leaks memory but NBD. */
void scheduler_UT (void)
{
  lcbn_t *lcbns[100];
  sched_lcbn_t *sched_lcbns[100];
  int i = 0, j = 0, n_items = 100;
  int n_real_items = 99; /* Initial stack LCBN is considered already run. */
  enum callback_type cb_type = CALLBACK_TYPE_ANY;

  mylog(LOG_SCHEDULER, 5, "scheduler_UT: begin\n"); 

  /* Record mode: record a tree. */
  mylog(LOG_SCHEDULER, 5, "scheduler_UT: beginning RECORD mode\n"); 
  scheduler_init(SCHEDULE_MODE_RECORD, "/tmp/scheduler_UT");

  /* Create an LCBN tree. */
  for (i = 0; i < n_items; i++)
  {
    if (i == 0)
      cb_type = INITIAL_STACK;
    else
      cb_type = (UV_ALLOC_CB + i) % (UV_THREAD_CB - UV_ALLOC_CB);

    lcbns[i] = lcbn_create(NULL, scheduler_UT, cb_type);
    assert(lcbn_looks_valid(lcbns[i]));

    /* lcbn 10, 20, ... are children of 0 */
    if (i && i % 10 == 0)
      lcbn_add_child(lcbns[0], lcbns[i]);
    /* the rest are children of 0, 10, 20, ... */
    else if (i && i % 10 != 0)
      lcbn_add_child(lcbns[i/10], lcbns[i]);

    /* Add some hidden dependencies. */
    if (i && i % 5 == 0)
      lcbn_add_dependency(lcbns[i-1], lcbns[i]);
  }

  /* Add sched_lcbn's for the scheduler. */
  for (i = 0; i < n_items; i++)
  {
    sched_lcbns[i] = sched_lcbn_create(lcbns[i]);
    assert(sched_lcbn_looks_valid(sched_lcbns[i]));
    scheduler_register_lcbn(sched_lcbns[i]);
  }

  /* Simulate execution of the lcbns -- same as registration order. */
  lcbns[0]->global_exec_id = 0;
  for (i = 1; i < n_items; i++)
  {
    assert(scheduler_already_run() == i);
    lcbns[i]->global_exec_id = i;
    scheduler_advance();
  }
  assert(scheduler_already_run() == n_items);

  /* Execution finished. */
  scheduler_emit();
  scheduler_uninitialize();
  mylog(LOG_SCHEDULER, 5, "scheduler_UT: RECORD mode finished\n"); 

  /* Replay mode: Try to replay. */
  mylog(LOG_SCHEDULER, 5, "scheduler_UT: beginning REPLAY mode\n"); 
  scheduler_init(SCHEDULE_MODE_REPLAY, "/tmp/scheduler_UT");

  /* The initial stack LCBN is considered already run. */
  assert(scheduler_remaining() == n_real_items);
  assert(scheduler_already_run() == 1);
  for (i = 1; i < n_items; i++)
  {
    assert(sched_lcbn_is_next(sched_lcbns[i]));

    for (j = i+1; j < n_items; j++)
      assert(!sched_lcbn_is_next(sched_lcbns[j]));

    /* Internal statistics look OK? */
    assert(scheduler_already_run() == i);
    assert(scheduler_remaining() == n_items-i);
    assert(scheduler_already_run() + scheduler_remaining() == n_items);

    /* Execute this LCBN. */
    assert(sched_lcbn_is_next(sched_lcbns[i]));
    scheduler_advance();
  }
  assert(scheduler_remaining() == 0);
  assert(scheduler_already_run() == n_items);

  mylog(LOG_SCHEDULER, 5, "scheduler_UT: passed\n"); 
  scheduler_uninitialize();
}

void scheduler_block_until_next (sched_lcbn_t *sched_lcbn)
{
  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler_block_until_next: begin: sched_lcbn %p\n", sched_lcbn));
  assert(sched_lcbn_looks_valid(sched_lcbn));
  while (!sched_lcbn_is_next(sched_lcbn))
    uv_thread_yield();
  ENTRY_EXIT_LOG((LOG_SCHEDULER, 9, "scheduler_block_until_next: returning\n"));
}
