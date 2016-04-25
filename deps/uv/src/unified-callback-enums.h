#ifndef UV_UNIFIED_CALLBACK_ENUMS_H_
#define UV_UNIFIED_CALLBACK_ENUMS_H_

#ifndef NOT_REACHED
#define NOT_REACHED assert (0 == 1);
#endif

/* Special values for uv__callback_origin. */
enum internal_callback_wrappers
{
  WAS_UV__FS_WORK = 1, /* NULL < 1, so we can test the return of uv__callback_origin != NULL. */
  WAS_UV__FS_DONE,
  WAS_UV__STREAM_IO,
  WAS_UV__ASYNC_IO,
  WAS_UV__ASYNC_EVENT,
  WAS_UV__SERVER_IO,
  WAS_UV__SIGNAL_EVENT,
  WAS_UV__GETADDRINFO_WORK,
  WAS_UV__GETADDRINFO_DONE,
  WAS_UV__QUEUE_WORK,
  WAS_UV__QUEUE_DONE,
  WAS_UV__WORK_DONE,
  INTERNAL_CALLBACK_WRAPPERS_MAX = WAS_UV__WORK_DONE
};

enum callback_tree_type
{
  CALLBACK_TREE_PHYSICAL,
  CALLBACK_TREE_LOGICAL
};

enum callback_origin_type
{
  /* Callbacks can be registered by one of these sources. 
     Note that we assume that libuv never registers callbacks on its own.
     This is my understanding of the libuv documentation. */
  NODEJS_INTERNAL,
  LIBUV_INTERNAL,
  USER
};

#define MAX_CALLBACK_NARGS 5
/* Keep aligned with unified-callback-enums.c: callback_type_strings[]. */
enum callback_type
{
  /* include/uv.h; see also the libuv documentation. */
  UV_ALLOC_CB = 0,
  CALLBACK_TYPE_MIN = UV_ALLOC_CB,
  UV_READ_CB,
  UV_WRITE_CB,
  UV_CONNECT_CB,
  UV_SHUTDOWN_CB,
  UV_CONNECTION_CB, /* 5 */
  UV_CLOSE_CB,
  UV_POLL_CB,
  UV_TIMER_CB,
  UV_ASYNC_CB,
  UV_PREPARE_CB, /* 10 */
  UV_CHECK_CB,
  UV_IDLE_CB,
  UV_EXIT_CB,
  UV_WALK_CB,
  UV_FS_WORK_CB, /* 15 */
  UV_FS_CB,
  UV_WORK_CB,
  UV_AFTER_WORK_CB,
  UV_GETADDRINFO_WORK_CB,
  UV_GETADDRINFO_CB, /* 20 */
  UV_GETNAMEINFO_WORK_CB,
  UV_GETNAMEINFO_CB,
  UV_FS_EVENT_CB,
  UV_FS_POLL_CB,
  UV_SIGNAL_CB, /* 25 */
  UV_UDP_SEND_CB,
  UV_UDP_RECV_CB,
  UV_THREAD_CB,

  /* include/uv-unix.h */
  UV__IO_CB,
  UV__ASYNC_CB, /* 30 */

  /* include/uv-threadpool.h */
  UV__WORK_WORK,
  UV__WORK_DONE,

  INITIAL_STACK,

  /* Internal libuv loop events. TODO The scheduler should probably work on events, not callbacks. And there are 'callback' events and 'internal marker' events. */
  MARKER_UV_RUN_BEGIN,
  MARKER_EVENTS_BEGIN = MARKER_UV_RUN_BEGIN, /* For iteration. */
  MARKER_UV_RUN_END,
  MARKER_RUN_TIMERS_1_BEGIN,
  MARKER_RUN_TIMERS_1_END,
  MARKER_RUN_PENDING_BEGIN,
  MARKER_RUN_PENDING_END,
  MARKER_RUN_IDLE_BEGIN,
  MARKER_RUN_IDLE_END,
  MARKER_RUN_PREPARE_BEGIN,
  MARKER_RUN_PREPARE_END,
  MARKER_IO_POLL_BEGIN,
  MARKER_IO_POLL_END,
  MARKER_RUN_CHECK_BEGIN,
  MARKER_RUN_CHECK_END,
  MARKER_RUN_CLOSING_BEGIN,
  MARKER_RUN_CLOSING_END,
  MARKER_RUN_TIMERS_2_BEGIN,
  MARKER_RUN_TIMERS_2_END,
  MARKER_EVENTS_END = MARKER_RUN_TIMERS_2_END, /* For iteration. */

  CALLBACK_TYPE_ANY,
  CALLBACK_TYPE_MAX
};

enum callback_context
{
  CALLBACK_CONTEXT_HANDLE = 0,
  CALLBACK_CONTEXT_MIN = CALLBACK_CONTEXT_HANDLE,
  CALLBACK_CONTEXT_REQ,
  CALLBACK_CONTEXT_IO_ASYNC, /* struct uv__async.io_watcher */
  CALLBACK_CONTEXT_IO_INOTIFY_READ, /* loop */
  CALLBACK_CONTEXT_IO_SIGNAL_EVENT, /* loop */
  /* TODO ? CALLBACK_CONTEXT_MARKER, */
  CALLBACK_CONTEXT_UNKNOWN,
  CALLBACK_CONTEXT_MAX
};

/* Whether a callback is part of an action or part of a response. 
   Some callbacks are part of a request for an action, while others indicate
   the response to take based on some condition (typically external input).

   Action callbacks ultimately result in an action and the subsequent invocation of the callback.
   Response callbacks indicate the response to take if the event ever occurs, aka a 'listener'. 
   
   Action callbacks will be called at most once, and are expected to be called once.
   Response callbacks will be called 0 or more times. There is no guarantee that they will
    be called. */
enum callback_behavior
{
  CALLBACK_BEHAVIOR_ACTION = 0,
  CALLBACK_BEHAVIOR_MIN = CALLBACK_BEHAVIOR_ACTION,
  CALLBACK_BEHAVIOR_RESPONSE,
  CALLBACK_BEHAVIOR_UNKNOWN,
  CALLBACK_BEHAVIOR_MAX
};

enum callback_context callback_type_to_context (enum callback_type cb_type);
enum callback_behavior callback_type_to_behavior (enum callback_type cb_type);

char * callback_type_to_string (enum callback_type);
enum callback_type callback_type_from_string (char *str);

char * callback_context_to_string (enum callback_context type);
enum callback_context callback_context_from_string (char *str);

char * callback_behavior_to_string (enum callback_behavior type);
enum callback_behavior callback_behavior_from_string (char *str);

/* Whether the specified cb_type could be invoked by the associated call in uv_run. */
int is_threadpool_cb (enum callback_type cb_type);
int is_run_timers_cb (enum callback_type cb_type);
int is_io_poll_cb (enum callback_type cb_type);
int is_run_check_cb (enum callback_type cb_type);
int is_run_idle_cb (enum callback_type cb_type); 
int is_run_pending_cb (enum callback_type cb_type); 
int is_marker_event (enum callback_type cb_type);

#endif /* UV_UNIFIED_CALLBACK_ENUMS_H_ */
