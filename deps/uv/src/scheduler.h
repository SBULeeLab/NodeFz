#ifndef UV_SRC_SCHEDULER_H_
#define UV_SRC_SCHEDULER_H_

/* This file accomplishes two things:
 *   1. It defines the public interface for a scheduler. 
 *   2. It defines the scheduler function typedefs, indicating the functions each scheduler implementation must offer.
 */

#include "unified-callback-enums.h"
#include "logical-callback-node.h"

#include <uv.h>

/* Include the various schedule implementations. */
#include "scheduler_CBTree.h"
#include "scheduler_Fuzzing_Timer.h"
#include "scheduler_Fuzzing_ThreadOrder.h"

/* The Logical CallBack Nodes the scheduler works with. */
struct sched_lcbn_s
{
  int magic;
  lcbn_t *lcbn;

  struct list_elem elem; /* TODO Can the user put a sched_lcbn_t in his own lists? Also, ideally this type would be opaque. */
};
typedef struct sched_lcbn_s sched_lcbn_t;

sched_lcbn_t *sched_lcbn_create (lcbn_t *lcbn);
void sched_lcbn_destroy (sched_lcbn_t *sched_lcbn);
void sched_lcbn_list_destroy_func (struct list_elem *e, void *aux);

/* The different scheduler types we support. */
enum scheduler_type_e
{
  SCHEDULER_TYPE_CBTREE,
  SCHEDULER_TYPE_FUZZER_TIMER,
  SCHEDULER_TYPE_FUZZER_THREAD_ORDER,
};
typedef enum scheduler_type_e scheduler_type_t;

/* The mode in which the scheduler is to run. */
enum schedule_mode_e
{
  SCHEDULE_MODE_RECORD,
  SCHEDULE_MODE_REPLAY
};
typedef enum schedule_mode_e schedule_mode_t;

/* Scheduler: A scheduler has two modes: record and replay.
 *
 *  Record:
 *  Tell the scheduler before you invoke each callback.
 *  When finished, save the schedule for future replay.
 *
 *  Replay:
 *  Steer the execution of user callbacks based on a schedule recorded during a previous run.
 *  Note that this schedule need NOT be exactly the same as was recorded.
 *
 *  Given identical external inputs (e.g. gettimeofday, read(), etc.), 
 *  the requested schedule may be achievable.
 *
 *  If the input schedule is identical to the recorded schedule, it must be achievable.
 *
 *  If the input schedule is a variation of the recorded schedule (e.g. switching the
 *  order of two callbacks), one or more of the changes in the re-ordering may
 *  produce a different logical structure. 
 *  
 *  Example 1: The first callback to invoke console.log() will always register a UV_SIGNAL_CB for SIGWINCH. 
 *    If a modified schedule changes the first callback to invoke console.log(), the resulting tree 
 *    will differ. However, if the UV_SIGNAL_CB was never invoked in the original tree, this variation 
 *    will not meaningfully affect our ability to replay the schedule.
 *    If it was invoked (as a child of "the wrong" LCBN), we will notice and declare the requested schedule
 *    un-produceable.
 *
 *  Example 2: The application wishes to request three FS operations, but with no more than 2 active at a time.
 *    It initializes a JS "flag" variable to 0. The first completed FS operation sets the flag to 1 and requests 
 *    another FS operation. 
 *    If the modified schedule swaps the completion order of the first two FS operations, the "other" CB will
 *    request the third FS operation.
*/

/* Some scheduler APIs are intended for RECORD mode, others for REPLAY mode, and others for both modes. */

/* APIs for both modes. */

/* Call this prior to any other scheduler_* routines. 
 *   type: What type of scheduler to use?
 *   mode: What mode in which to use it? Not all schedulers support all modes.
 *   schedule_file: In RECORD mode, where to put the schedule we record.
 *                  In REPLAY mode, where to find the schedule we wish to replay. 
 *   args: Depends on type. Consult the header file for the scheduler implementation.
 *         Must be persistent throughout program lifetime (TODO The scheduler implementations should just make a copy).
 */
void scheduler_init (scheduler_type_t type, schedule_mode_t mode, char *schedule_file, void *args);

/* Register LCBN for potential scheduler_execute()'d later. 
 * Caller must ensure mutex for deterministic replay.
 */
void scheduler_register_lcbn (lcbn_t *lcbn);

/* Execute this lcbn, which must previously have been scheduler_register_lcbn()'d.
 *   RECORD mode: duh
 *   REPLAY mode: we must wait until it's our turn
 */
void scheduler_execute_lcbn (lcbn_t *lcbn);

/* Dump the schedule in registration order to the schedule_file specified in schedule_init. 
 *   RECORD mode: duh
 *   REPLAY mode: we don't want to overwrite the input schedule, so we emit to sprintf("%s-replay", schedule_file). */
void scheduler_emit (void);

schedule_mode_t scheduler_get_schedule_mode (void);

/* APIs for RECORD mode only. */

/* How many LCBNs have already been scheduler_execute()'d? */
int scheduler_lcbns_already_executed (void);

/* APIs for REPLAY mode only. */

/* How many LCBNs remain to be scheduler_execute()'d before we are done? */
int scheduler_lcbns_remaining (void);

/* Non-zero if schedule has diverged, else 0. */
int scheduler_schedule_has_diverged (void);

/* TODO I AM HERE REFACTORING */

/* Returns the next scheduled LCBN.
 * If nothing left to schedule, returns NULL.
 */
const lcbn_t * scheduler_next_scheduled_lcbn (void);

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
  EXEC_CONTEXT_UV__RUN_CLOSING_HANDLES,

  EXEC_CONTEXT_THREADPOOL_WORKER,
  EXEC_CONTEXT_THREADPOOL_DONE
};

struct sched_context_s
{
  int magic;

  enum execution_context exec_context;
  enum callback_context cb_context;
  void *wrapper; /* uv_handle_t, uv_req_t?, uv_loop_t, struct uv__async */

  struct list_elem elem;
};
typedef struct sched_context_s sched_context_t;

sched_context_t *sched_context_create (enum execution_context exec_context, enum callback_context cb_context, void *wrapper);
void sched_context_destroy (sched_context_t *sched_context);
void sched_context_list_destroy_func (struct list_elem *e, void *aux);

/* Record mode: SCHEDULE_FILE is where to send output.
   Replay mode: SCHEDULE_FILE is where to find schedule. 
    SCHEDULE_FILE must be in registration order. 
    The exec_id of each LCBN should indicate the order in which execution occurred. 

    min_n_executed_before_divergence_allowed: exactly what it says.

    The schedule recorded in replay mode is saved to 'SCHEDULE_FILE-replay'. */
void scheduler_init (schedule_mode_t mode, char *schedule_file, 

/* Return the mode of the scheduler. */
schedule_mode_t scheduler_get_mode (void);
const char * schedule_mode_to_string (schedule_mode_t);

/* Record. */

/* TODO The caller sets the global_exec_id for the LCBNs in invoke_callback.
     This puts the burden of tracking exec IDs on the caller instead of on the scheduler,
     which seems a bit odd. 
   Anyway, make sure you set lcbn->global_exec_id under a mutex! */


/* Replay. */

/* Determine the next context to invoke. 
   Input is a list of sched_context_t's. 
   REPLAY: Returns NULL if none of the specified contexts has the next LCBN in the schedule.

   TODO If there are none available in SCHED_CONTEXT (because running the context executes no CBs),
    we need to somehow indicate that you should run it to ensure forward progress. */
sched_context_t * scheduler_next_context (struct list *sched_context_list);

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

/* Returns the callback_type of the next scheduled LCBN. */
enum callback_type scheduler_next_lcbn_type (void);

/* Block until SCHED_LCBN is next up.
   This allows competing threads to finish whatever they are doing.
   This is necessary if you call scheduler_advance prior to actually
   invoking a callback. */
void scheduler_block_until_next (sched_lcbn_t *sched_lcbn);

/* (scheduler_next_context) check if SCHED_LCBN is next on the schedule, or 
   (invoke_callback) verify that SCHED_LCBN is supposed to be next on the schedule. 
   
   REPLAY mode: If we go long enough without scheduler_advance'ing, calls to this function 
                may trigger a switch to RECORD mode (presuming a more subtle schedule 
                divergence than scheduler_advance detects).
                You can call with NULL to check for timeout. */
int sched_lcbn_is_next (sched_lcbn_t *sched_lcbn);

/* Tell the scheduler that the most-recent LCBN has been executed. 
   This can be done prior to executing an LCBN provided that the executing
   LCBN is allowed to complete before a new (non-nested) LCBN is invoked. 
   
   RECORD mode: Does some bookkeeping.
   REPLAY mode: Does bookeeping, checks for divergence, etc. */
void scheduler_advance (void);

/* For REPLAY mode.
   LCBN is a just-finished node. Check if it has diverged from the schedule.

   Divergence: The schedule has diverged if the children of LCBN are not exactly 
      (number, order, and type) as indicated in the input schedule.

   If divergence is detected, we can no longer REPLAY the input schedule because
     we are no longer seeing the input schedule.
   We respond by "diverging" (switching back into RECORD mode if acceptable based on min_n_executed_before_divergence_allowed.

   A divergent schedule can occur in one of two ways:
    - REPLAYing a RECORDed application, encountering non-determinism in some fashion
      e.g. branches that rely on wall clock time, random numbers, change in inputs.
    - REPLAYing a rescheduled application -- we hoped the schedule would remain the
      same after changing the order of observed events, but it didn't.

   Returns the schedule mode in place at the end of the function. 
   Test that or scheduler_has_diverged() for divergence.
*/
schedule_mode_t scheduler_check_lcbn_for_divergence (lcbn_t *lcbn);

/* For REPLAY mode.
   cbt is the callback type of the next marker node, which we are trying to emit.
   Check against the schedule to see if we've diverged.

   Divergence example: We might have entered the loop and be presenting MARKER_RUN_TIMERS_1_BEGIN 
     instead of the expected MARKER_UV_RUN_END. I can't think of another case that wouldn't
     have been caught by the divergence timeout code instead.

   Same idea as scheduler_check_lcbn_for_divergence.
   Test the returned schedule_mode_t or scheduler_has_diverged() for divergence.
*/
schedule_mode_t scheduler_check_marker_for_divergence (enum callback_type cbt);


/* Each type of handle and req should declare a function of this type in internal.h
   for use in scheduler_next_context and scheduler_next_lcbn. 
     Name it like: uv__ready_*_lcbns {for * in async, check, fs_event, etc.} 
   It should return the list of sched_lcbn_t's that are available on the provided wrapper. */
typedef struct list * (*ready_lcbns_func)(void *wrapper, enum execution_context context);

void scheduler_UT (void);

/********************************
 * Each scheduler implementation must define these APIs.
 ********************************/

/* Initialize the scheduler implementation.
 *   args: Define this in your header file
 *   details: Hide this in your C file. We'll supply it to each of your other APIs
 */
typedef void (*schedulerImpl_init) (schedule_mode_t mode, void *args, void *details);
typedef void (*schedulerImpl_register_lcbn) (lcbn_t *lcbn, void *details);
typedef void (*schedulerImpl_execute_lcbn) (lcbn_t *lcbn, void *details);
typedef void (*schedulerImpl_emit) (void *details);
typedef int  (*schedulerImpl_schedule_has_diverged) (void *details);
typedef int  (*schedulerImpl_lcbns_remaining) (void *details);
/* TODO Add more as needed. */

#endif  /* UV_SRC_SCHEDULER_H_ */
