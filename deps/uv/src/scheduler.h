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
struct sched_context_s
{
  enum callback_context context;
  void *handle_or_req;

  struct list_elem elem;
};
typedef struct sched_context_s sched_context_t;

sched_context_t *sched_context_create (enum callback_context context, void *handle_or_req);
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
   REPLAY: Returns NULL if none of the specified contexts has the next LCBN in the schedule. */
sched_context_t * scheduler_next_context (const struct list *sched_context_list);

/* Determine the next LCBN to invoke from those available in SCHED_CONTEXT. 
   If none of those available in SCHED_CONTEXT is up next, returns NULL. 
   This should not happen if you provide the sched_context most recently returned by scheduler_next_context. */
sched_lcbn_t * scheduler_next_lcbn (sched_context_t *sched_context);

/* Tell the scheduler that the most-recent LCBN has been executed. */
void scheduler_advance (void);

/* Each type of handle and req should declare a function of this type in internal.h
   for use in scheduler_next_context and scheduler_next_lcbn. 
     Name it like: uv__ready_*_lcbns {for * in async, check, fs_event, etc.} 
   It should return the list of LCBNs that are available on the provided handle_or_req_P. */
typedef void * handle_or_req_P;
typedef struct list * (*ready_lcbns_func)(handle_or_req_P);

#endif  /* UV_SRC_SCHEDULER_H_ */
