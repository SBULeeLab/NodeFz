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
enum callback_type
{
  /* include/uv.h; see also the libuv documentation. */
  CALLBACK_TYPE_MIN = 0,
  UV_ALLOC_CB = CALLBACK_TYPE_MIN,
  UV_READ_CB,
  UV_WRITE_CB,
  UV_CONNECT_CB,
  UV_SHUTDOWN_CB,
  UV_CONNECTION_CB,
  UV_CLOSE_CB,
  UV_POLL_CB,
  UV_TIMER_CB,
  UV_ASYNC_CB,
  UV_PREPARE_CB,
  UV_CHECK_CB,
  UV_IDLE_CB,
  UV_EXIT_CB,
  UV_WALK_CB,
  UV_FS_WORK_CB,
  UV_FS_CB,
  UV_WORK_CB,
  UV_AFTER_WORK_CB,
  UV_GETADDRINFO_WORK_CB,
  UV_GETADDRINFO_CB,
  UV_GETNAMEINFO_WORK_CB,
  UV_GETNAMEINFO_CB,
  UV_FS_EVENT_CB,
  UV_FS_POLL_CB,
  UV_SIGNAL_CB,
  UV_UDP_SEND_CB,
  UV_UDP_RECV_CB,
  UV_THREAD_CB,

  /* include/uv-unix.h */
  UV__IO_CB,
  UV__ASYNC_CB,

  /* include/uv-threadpool.h */
  UV__WORK_WORK,
  UV__WORK_DONE,

  CALLBACK_TYPE_INITIAL_STACK,

  CALLBACK_TYPE_ANY,
  CALLBACK_TYPE_MAX
};

enum callback_context
{
  CALLBACK_CONTEXT_HANDLE,
  CALLBACK_CONTEXT_REQ,
  CALLBACK_CONTEXT_UNKNOWN
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
  CALLBACK_BEHAVIOR_ACTION,
  CALLBACK_BEHAVIOR_RESPONSE,
  CALLBACK_BEHAVIOR_UNKNOWN
};

enum callback_context callback_type_to_context (enum callback_type cb_type);
enum callback_behavior callback_type_to_behavior (enum callback_type cb_type);

char * callback_type_to_string (enum callback_type);
char *callback_context_to_string (enum callback_context type);
char *callback_behavior_to_string (enum callback_behavior type);

#endif /* UV_UNIFIED_CALLBACK_ENUMS_H_ */
