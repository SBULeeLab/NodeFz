#ifndef UV_SRC_SCHEDULER_H_
#define UV_SRC_SCHEDULER_H_

#include "unified-callback-enums.h"
#include "logical-callback-node.h"

#include <uv.h>

/* Scheduler: A scheduler has two modes: record and replay.

    Record:
    Tell the scheduler before you invoke each callback.
    When finished, save the schedule for future replay.

    Replay:
    Steer the execution of user callbacks based on a schedule recorded during a previous run.
    Note that this schedule need NOT be exactly the same as was recorded.

    Given identical external inputs (e.g. gettimeofday, read(), etc.), 
    the requested schedule may be achievable.

    If the input schedule is identical to the recorded schedule, it must be achievable.

    If the input schedule is a variation of the recorded schedule (e.g. switching the
    order of two callbacks), one or more of the changes in the re-ordering may
    produce a different logical structure. 
    
    Example 1: The first callback to invoke console.log() will always register a UV_SIGNAL_CB for SIGWINCH. 
      If a modified schedule changes the first callback to invoke console.log(), the resulting tree 
      will differ. However, if the UV_SIGNAL_CB was never invoked in the original tree, this variation 
      will not meaningfully affect our ability to replay the schedule.
      If it was invoked (as a child of "the wrong" LCBN), we will notice and declare the requested schedule
      un-produceable.

    Example 2: The application wishes to request three FS operations, but with no more than 2 active at a time.
      It initializes a JS "flag" variable to 0. The first completed FS operation sets the flag to 1 and requests 
      another FS operation. 
      If the modified schedule swaps the completion order of the first two FS operations, the "other" CB will
      request the third FS operation.
*/

/* Record: Indicate the LCBN whose CB we will invoke next. */
struct sched_lcbn_s
{
  lcbn_t *lcbn;

  struct list_elem elem;
};
typedef struct sched_lcbn_s sched_lcbn_t;

sched_lcbn_t *sched_lcbn_create (lcbn_t *lcbn);
void sched_lcbn_destroy (sched_lcbn_t *sched_lcbn);
void sched_lcbn_list_destroy_func (struct list_elem *e, void *aux);

/* Replay: Construct lists of "ready contexts" for the scheduler (those which have a user callback ready to invoke). */

/* Where is the sched_context coming from? The list of possibly-invoked CBs varies based on the location. */
enum execution_context
{
  EXEC_CONTEXT_UV__RUN_TIMERS,
  EXEC_CONTEXT_UV__RUN_PENDING,
  EXEC_CONTEXT_UV__RUN_IDLE,
  EXEC_CONTEXT_UV__RUN_PREPARE,
  EXEC_CONTEXT_UV__IO_POLL,
  EXEC_CONTEXT_UV__RUN_CHECK,
  EXEC_CONTEXT_UV__RUN_CLOSING_HANDLES
};

struct sched_context_s
{
  enum execution_context exec_context;

  enum callback_context cb_context;
  void *wrapper; /* uv_handle_t, uv_req_t?, uv_loop_t, struct uv__async */

  struct list_elem elem;
};
typedef struct sched_context_s sched_context_t;

sched_context_t *sched_context_create (enum execution_context exec_context, enum callback_context cb_context, void *wrapper);
void sched_context_destroy (sched_context_t *sched_context);
void sched_context_list_destroy_func (struct list_elem *e, void *aux);

/* Scheduler APIs. */
enum schedule_mode
{
  SCHEDULE_MODE_RECORD,
  SCHEDULE_MODE_REPLAY
};

/* Record mode: SCHEDULE_FILE is where to send output.
   Replay mode: SCHEDULE_FILE is where to find schedule. */
void scheduler_init (enum schedule_mode mode, char *schedule_file);

/* Record. */

/* This is the LCBN whose CB we execute next. 
   Caller should ensure mutex. Or TODO in invoke_callback, invoke LCBNs through a scheduler API and do the mutex'ing there? */
void scheduler_record (sched_lcbn_t *sched_lcbn);
/* Dump the schedule to the file specified in schedule_init. */
void scheduler_emit (void);

/* Replay. */

/* Determine the next context to invoke. 
   Input is a list of sched_context_t's. 
   REPLAY: Returns NULL if none of the specified contexts has the next LCBN in the schedule.

   TODO If there are none available in SCHED_CONTEXT (because running the context executes no CBs),
    we need to somehow indicate that you should run it to ensure forward progress. */
sched_context_t * scheduler_next_context (const struct list *sched_context_list);

/* Determine the next LCBN to invoke from those available in SCHED_CONTEXT. 
   (internal only) Returns SILENT_CONTEXT if SCHED_CONTEXT has no ready LCBNs, i.e. if
     no user CBs will be invoked if we schedule it. This is a clue to schedule it.
   If none of those available in SCHED_CONTEXT is up next, returns NULL. 
   This should not happen if you provide the sched_context most recently returned by scheduler_next_context. 

   This API is relevant as used in scheduler_next_context, but not outside of
   the scheduler internals. For the majority of use cases, invoking
   a handle will inevitably result in invoking a series/stream/cluster/sequence of related LCBNs.
   For example, invoking uv__stream_io on a handle's uv__io_t may invoke an arbitrary number
   of LCBNs, and in a specific order.
   At the moment I do not wish to violate this order, so we'll see how trustworthy the order is
   under schedule variations. My hypothesis is that, provided you acknowledge fixed sequences
   when manipulating a schedule (e.g. not inserting another LCBN in the middle of a sequence), 
   the sequences will naturally occur in the recorded order. 
    
   Call sched_lcbn_is_next in invoke_callback to confirm or reject this hypothesis. */
sched_lcbn_t * scheduler_next_lcbn (sched_context_t *sched_context);

/* (scheduler_next_context) check if SCHED_LCBN is next on the schedule, or 
   (invoke_callback) verify that SCHED_LCBN is supposed to be next on the schedule. */
int sched_lcbn_is_next (sched_lcbn_t *sched_lcbn);

/* Tell the scheduler that the most-recent LCBN has been executed. 
   This can be done prior to executing an LCBN provided that the executing
   LCBN is allowed to complete before a new (non-nested) LCBN is invoked. */
void scheduler_advance (void);

/* Each type of handle and req should declare a function of this type in internal.h
   for use in scheduler_next_context and scheduler_next_lcbn. 
     Name it like: uv__ready_*_lcbns {for * in async, check, fs_event, etc.} 
   It should return the list of sched_lcbn_t's that are available on the provided wrapper. */
typedef struct list * (*ready_lcbns_func)(void *wrapper, enum execution_context context);

#endif  /* UV_SRC_SCHEDULER_H_ */
