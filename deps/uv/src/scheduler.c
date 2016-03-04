#include "scheduler.h"

#include "list.h"
#include "map.h"
#include "mylog.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#define SCHEDULER_MAGIC 8675309 /* Jenny. */

struct scheduler_s
{
  enum schedule_mode mode;
  char *schedule_file;

  struct list *recorded_schedule; /* List of the sched_lcbn_t's we have executed so far. */
  struct list *desired_schedule; /* For REPLAY mode, list of sched_lcbn_t's expressing desired execution order. */

  int magic;
};
typedef struct scheduler_s scheduler_t;

/* Globals. */
scheduler_t scheduler;

/* Private API declarations. */
static int scheduler_initialized(void);

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

static int scheduler_initialized(void)
{
  return scheduler.magic == SCHEDULER_MAGIC;
}

void scheduler_init(enum schedule_mode mode, char *schedule_file)
{
  FILE *f;
  char *line;

  assert(schedule_file != NULL);
  assert(!scheduler_initialized());

  scheduler.mode = mode;
  scheduler.schedule_file = schedule_file;
  scheduler.magic = SCHEDULER_MAGIC;
  scheduler.recorded_schedule = list_create();
  scheduler.desired_schedule = list_create();

  if (scheduler.mode == SCHEDULE_MODE_RECORD)
  {
    /* Verify that we can open and close the file. */ 
    f = fopen(schedule_file, "w");
    assert(f);
    assert(fclose(f) == 0);
  }
  else if (scheduler.mode == SCHEDULE_MODE_REPLAY)
  {
    f = fopen(schedule_file, "r");
    assert(f);
    while(getline(&line, NULL, f))
    {
      /* TODO On REPLAY, parse each line as an LCBN and store in scheduler.desired_schedule. */
      free(line);
    }
    assert(fclose(f) == 0);
  }
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
  assert(scheduler_initialized());
  assert(sched_context_list);

  if (list_empty(sched_context_list))
    return NULL;

  /* RECORD mode: execute the first context in the list. */
  if (scheduler.mode == SCHEDULE_MODE_RECORD)
    return list_entry(list_begin(sched_context_list), sched_context_t, elem);
  else if (scheduler.mode == SCHEDULE_MODE_REPLAY)
    /* TODO. Traverse the list. Identify whether any of the possible CBs associated with any of the contexts is next in the desired_schedule. */
    return NULL;
  else
    NOT_REACHED;
}

sched_lcbn_t * scheduler_next_lcbn(sched_context_t *sched_context)
{
  assert(scheduler_initialized());
  assert(sched_context);

  /* TODO. */
  /* RECORD mode: execute the first possible LCBN of the context. */
  if (scheduler.mode == SCHEDULE_MODE_RECORD)
    return NULL;
  else if (scheduler.mode == SCHEDULE_MODE_REPLAY)
    /* TODO. Traverse the list of ready LCBNs. Identify the next one in desired_schedule. */
    return NULL;
  else
    NOT_REACHED;

  return NULL;
}
