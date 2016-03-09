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

  struct list *recorded_schedule; /* List of the sched_lcbn_t's we have executed so far. */
  struct list *desired_schedule; /* For REPLAY mode, list of sched_lcbn_t's expressing desired execution order, first to last. We left-shift as we execute nodes. */

  int magic;
};
typedef struct scheduler_s scheduler_t;

/* Globals. */
scheduler_t scheduler;

/* Private API declarations. */
static int scheduler_initialized(void);
static int sched_lcbn_is_next(sched_lcbn_t *sched_lcbn);

/* Return non-zero if SCHED_LCBN is next, else zero. */
static int sched_lcbn_is_next(sched_lcbn_t *sched_lcbn)
{
  sched_lcbn_t *next;

  assert(sched_lcbn);

  /* RECORD mode: Every queried sched_lcbn is "next". */
  if (scheduler.mode == SCHEDULE_MODE_RECORD)
    return 1;

  assert(scheduler.mode == SCHEDULE_MODE_REPLAY);
  assert(!list_empty(scheduler.desired_schedule));

  next = list_entry(list_begin(scheduler.desired_schedule), sched_lcbn_t, elem);
  assert(next);

  return lcbn_equals(next->lcbn, sched_lcbn->lcbn);
}

/* Public APIs. */
sched_lcbn_t *sched_lcbn_create(lcbn_t *lcbn)
{
  sched_lcbn_t *sched_lcbn;
  assert(lcbn);

  sched_lcbn = (sched_lcbn_t *) malloc(sizeof *sched_lcbn);
  assert(sched_lcbn != NULL);
  memset(sched_lcbn, 0, sizeof *sched_lcbn);

  sched_lcbn->lcbn = lcbn;

  return sched_lcbn;
} 

void sched_lcbn_destroy(sched_lcbn_t *sched_lcbn)
{
  assert(sched_lcbn);
  memset(sched_lcbn, 0, sizeof *sched_lcbn);
  free(sched_lcbn);
}

sched_context_t *sched_context_create(enum callback_context context, void *handle_or_req)
{
  sched_context_t *sched_context;
  assert(handle_or_req);

  sched_context = (sched_context_t *) malloc(sizeof *sched_context);
  assert(sched_context != NULL);
  memset(sched_context, 0, sizeof *sched_context);

  sched_context->context = context;
  sched_context->handle_or_req = handle_or_req;

  return sched_context;
}

void sched_context_destroy(sched_context_t *sched_context)
{
  assert(sched_context);
  free(sched_context);
}

void sched_context_list_destroy_func(struct list_elem *e, void *aux){
  sched_context_t *sched_context;
  assert(e);
  sched_context = list_entry(e, sched_context_t, elem);
  sched_context_destroy(sched_context);
}

static int scheduler_initialized(void)
{
  return scheduler.magic == SCHEDULER_MAGIC;
}

void scheduler_init(enum schedule_mode mode, char *schedule_file)
{
  FILE *f;
  char *line;
  size_t line_len;
  sched_lcbn_t *sched_lcbn;

  assert(schedule_file != NULL);
  assert(!scheduler_initialized());

  /* Allocate a line for SCHEDULE_MODE_REPLAY. */
  line_len = 2048;
  line = (char *) malloc(line_len*sizeof(char));
  assert(line);
  memset(line, 0, line_len);

  scheduler.mode = mode;
  strncpy(scheduler.schedule_file, schedule_file, sizeof scheduler.schedule_file);
  scheduler.magic = SCHEDULER_MAGIC;
  scheduler.recorded_schedule = list_create();
  scheduler.desired_schedule = list_create();

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
    while(0 < getline(&line, &line_len, f))
    {
      /* Remove trailing newline. */
      if(line[strlen(line)-1] == '\n')
        line[strlen(line)-1] = '\0';
      /* Parse line as an lcbn_t and add it to the desired_schedule. */
      sched_lcbn = sched_lcbn_create(lcbn_from_string(line));
      list_push_back(scheduler.desired_schedule, &sched_lcbn->elem);

      memset(line, 0, line_len);
    }
    assert(fclose(f) == 0);
  }

  free(line);
}

void scheduler_record(sched_lcbn_t *sched_lcbn)
{
  assert(scheduler_initialized());
  assert(sched_lcbn != NULL);

  list_push_back(scheduler.recorded_schedule, &sched_lcbn->elem);
}

void scheduler_emit(void)
{
  FILE *f;
  sched_lcbn_t *sched_lcbn;
  struct list_elem *e;
  char lcbn_str_buf[1024];

  assert(scheduler_initialized());

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

sched_context_t * scheduler_next_context(const struct list *sched_context_list)
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

ready_lcbns_func handle_lcbn_funcs[UV_HANDLE_TYPE_MAX] = {
  NULL, /* UV_UNKNOWN_HANDLE */
  NULL, /* uv__ready_async_lcbns */
  NULL, /* uv__ready_check_lcbns */
  NULL, /* uv__ready_fs_event_lcbns */
  NULL, /* uv__ready_fs_poll_lcbns */
  NULL, /* uv__ready_handle_lcbns */
  NULL, /* uv__ready_idle_lcbns */
  NULL, /* uv__ready_named_pipe_lcbns */
  NULL, /* uv__ready_poll_lcbns */
  NULL, /* uv__ready_prepare_lcbns */
  NULL, /* uv__ready_process_lcbns */
  NULL, /* uv__ready_stream_lcbns */
  NULL, /* uv__ready_tcp_lcbns */
  uv__ready_timer_lcbns,
  NULL, /* uv__ready_tty_lcbns */
  NULL, /* uv__ready_udp_lcbns */
  NULL, /* uv__ready_signal_lcbns */
  NULL  /* UV_FILE ? */
};

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

sched_lcbn_t * scheduler_next_lcbn(sched_context_t *sched_context)
{
  uv_handle_t *handle;
  uv_handle_type handle_type;
  uv_req_t *req;
  uv_req_type req_type;

  struct list *ready_lcbns;
  struct list * (*ready_lcbns_func)(void *);
  void *handle_or_req;

  struct list_elem *e;
  sched_lcbn_t *sched_lcbn, *next_lcbn;

  assert(scheduler_initialized());
  assert(sched_context);
  assert(sched_context->handle_or_req);

  if (sched_context->context == CALLBACK_CONTEXT_HANDLE)
  {
    handle = (uv_handle_t *) sched_context->handle_or_req;
    handle_type = handle->type;

    handle_or_req = handle;
    ready_lcbns_func = handle_lcbn_funcs[handle_type];
 }
  else if (sched_context->context == CALLBACK_CONTEXT_REQ)
  {
    req = (uv_req_t *) sched_context->handle_or_req;
    req_type = req->type;

    handle_or_req = req;
    ready_lcbns_func = req_lcbn_funcs[handle_type];
  }
  else
    NOT_REACHED;

  assert(handle_or_req);
  assert(ready_lcbns_func);

  /* NB This must return lcbns in the order in which they will be invoked by the handle. */
  ready_lcbns = (*ready_lcbns_func)(handle_or_req);
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

void scheduler_advance(void){
  sched_lcbn_t *sched_lcbn;

  if (scheduler.mode != SCHEDULE_MODE_REPLAY)
    return;

  assert(!list_empty(scheduler.desired_schedule));
  sched_lcbn = list_entry(list_pop_front(scheduler.desired_schedule), sched_lcbn_t, elem);

  sched_lcbn_destroy(sched_lcbn);
}

void sched_lcbn_list_destroy_func(struct list_elem *e, void *aux){
  sched_lcbn_t *sched_lcbn;
  assert(e);
  sched_lcbn = list_entry(e, sched_lcbn_t, elem);
  sched_lcbn_destroy(sched_lcbn);
}
