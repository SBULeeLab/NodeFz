#include "unified-callback-enums.h"

#include <assert.h>
#include <string.h>

/* Keep aligned with the declaration of enum callback_type. */
static char *callback_type_strings[] = {
  "UV_ALLOC_CB", "UV_READ_CB", "UV_WRITE_CB", "UV_CONNECT_CB", "UV_SHUTDOWN_CB", 
  "UV_CONNECTION_CB", "UV_CLOSE_CB", "UV_POLL_CB", "UV_TIMER_CB", "UV_ASYNC_CB", 
  "UV_PREPARE_CB", "UV_CHECK_CB", "UV_IDLE_CB", "UV_EXIT_CB", "UV_WALK_CB", 
  "UV_FS_WORK_CB", "UV_FS_CB", 
  "UV_WORK_CB", "UV_AFTER_WORK_CB", 
  "UV_GETADDRINFO_WORK_CB", "UV_GETADDRINFO_CB", 
  "UV_GETNAMEINFO_WORK_CB", "UV_GETNAMEINFO_CB", 
  "UV_FS_EVENT_CB", "UV_FS_POLL_CB", "UV_SIGNAL_CB", "UV_UDP_SEND_CB", "UV_UDP_RECV_CB", 
  "UV_THREAD_CB", 

  /* include/uv-unix.h */
  "UV__IO_CB", "UV__ASYNC_CB", 

  /* include/uv-threadpool.h */
  "UV__WORK_WORK", "UV__WORK_DONE", 

  "INITIAL_STACK",
  "EXIT",

  /* Internal libuv loop events. */
  "MARKER_UV_RUN_BEGIN",
  "MARKER_UV_RUN_END",
  "MARKER_RUN_TIMERS_1_BEGIN",
  "MARKER_RUN_TIMERS_1_END",
  "MARKER_RUN_PENDING_BEGIN",
  "MARKER_RUN_PENDING_END",
  "MARKER_RUN_IDLE_BEGIN",
  "MARKER_RUN_IDLE_END",
  "MARKER_RUN_PREPARE_BEGIN",
  "MARKER_RUN_PREPARE_END",
  "MARKER_IO_POLL_BEGIN",
  "MARKER_IO_POLL_END",
  "MARKER_RUN_CHECK_BEGIN",
  "MARKER_RUN_CHECK_END",
  "MARKER_RUN_CLOSING_BEGIN",
  "MARKER_RUN_CLOSING_END",
  "MARKER_RUN_TIMERS_2_BEGIN",
  "MARKER_RUN_TIMERS_2_END",

  "ANY_CALLBACK"
};

/* Keep aligned with the declaration of enum callback_context. */
static char *callback_context_strings[] = { "HANDLE", "REQ", "IO_ASYNC", "IO_INOTIFY_READ", "IO_SIGNAL_EVENT", "UNKNOWN" };

/* Keep aligned with the declaration of enum callback_behavior. */
static char *callback_behavior_strings[] = { "ACTION", "RESPONSE", "UNKNOWN" };

enum callback_context callback_type_to_context (enum callback_type cb_type)
{
  switch (cb_type)
  {
    case UV_FS_POLL_CB:  
    case UV_CLOSE_CB:    
    case UV_FS_EVENT_CB:
    case UV_CHECK_CB:
    case UV_IDLE_CB:
    case UV_PREPARE_CB:
    case UV_POLL_CB:
    case UV_EXIT_CB:
    case UV_SIGNAL_CB:
    case UV_TIMER_CB:
    case UV_UDP_RECV_CB:
    case UV_CONNECTION_CB:
    case UV_ALLOC_CB:
    case UV_READ_CB:
    case UV_ASYNC_CB:
      return CALLBACK_CONTEXT_HANDLE;

    case UV_WORK_CB:
    case UV_AFTER_WORK_CB:
    case UV_FS_WORK_CB:
    case UV_FS_CB:
    case UV_GETADDRINFO_WORK_CB:
    case UV_GETADDRINFO_CB:
    case UV_GETNAMEINFO_WORK_CB:
    case UV_GETNAMEINFO_CB:
    case UV_CONNECT_CB:
    case UV_SHUTDOWN_CB:
    case UV_WRITE_CB:
    case UV_UDP_SEND_CB:
      return CALLBACK_CONTEXT_REQ;

    default:
      return CALLBACK_CONTEXT_UNKNOWN;
  }
  NOT_REACHED;
}

enum callback_behavior callback_type_to_behavior (enum callback_type cb_type)
{
  switch (cb_type)
  {
    case UV_CLOSE_CB:       /* Moves handle to "closing" state, where it is finished in uv_run by a call to uv__run_closing_handles. The handle might have other callbacks pending as well. */

    /* Requests. Assume no simultaneous request reuse. */
    case UV_WORK_CB:
    case UV_AFTER_WORK_CB:
    case UV_FS_WORK_CB:
    case UV_FS_CB:
    case UV_GETADDRINFO_WORK_CB:
    case UV_GETADDRINFO_CB:
    case UV_GETNAMEINFO_WORK_CB:
    case UV_GETNAMEINFO_CB:
    case UV_CONNECT_CB:
    case UV_SHUTDOWN_CB:
    case UV_WRITE_CB:
    case UV_UDP_SEND_CB:
      return CALLBACK_BEHAVIOR_ACTION;

    /* loop-watchers. One active CB per handle. loop-watcher.c returns if the handle is active. */
    case UV_CHECK_CB:
    case UV_IDLE_CB:
    case UV_PREPARE_CB:

    case UV_TIMER_CB:       /* A timer handle can be re-used. If there was already a timer active on it, it would be removed from the heap and the CB replaced. I don't think Node actually does this, and it would also depend on lib/timers.js. However, this means that there is one CB per handle. */
    case UV_FS_EVENT_CB:    /* One active CB per handle. uv_fs_poll_start returns if the handle is active. */
    case UV_FS_POLL_CB:     /* One active CB per handle. uv_fs_poll_start returns if the handle is active. */
    case UV_POLL_CB:        /* One active CB per handle. uv_poll_start stops the handle before proceeding. */
    case UV_EXIT_CB:        /* One active CB per process handle. */
    case UV_SIGNAL_CB:      /* One active CB per handle. */
    case UV_UDP_RECV_CB:    /* One active CB per handle. */
    case UV_CONNECTION_CB:  /* uv_tcp_listen, uv_pipe_listen: One active CB per handle. */
    case UV_ALLOC_CB:       /* One active CB per handle. */
    case UV_READ_CB:        /* One active CB per handle. */
    case UV_ASYNC_CB:       /* One active CB per handle. */
      return CALLBACK_BEHAVIOR_RESPONSE;

    default:
      return CALLBACK_BEHAVIOR_UNKNOWN;
  }
  assert(!"callback_type_to_behavior: Error, how did I get here");
}

char * callback_type_to_string (enum callback_type type)
{
  assert (CALLBACK_TYPE_MIN <= type && type < CALLBACK_TYPE_MAX);
  return callback_type_strings[type];
}

enum callback_type callback_type_from_string (char *str)
{
  enum callback_type i;
  assert(str != NULL);

  for (i = CALLBACK_TYPE_MIN; i < CALLBACK_TYPE_MAX; i++)
  {
    if (strcmp(str, callback_type_strings[i]) == 0)
      return i;
  }

  assert(!"callback_type_from_string: Error, invalid string");
  return 0;
}

char * callback_context_to_string (enum callback_context type)
{
  assert (CALLBACK_CONTEXT_MIN <= type && type < CALLBACK_CONTEXT_MAX);
  return callback_context_strings[type];
}

enum callback_context callback_context_from_string (char *str)
{
  enum callback_context i;
  assert(str != NULL);

  for (i = CALLBACK_CONTEXT_MIN; i < CALLBACK_CONTEXT_MAX; i++)
  {
    if (strcmp(str, callback_context_strings[i]) == 0)
      return i;
  }

  assert(!"callback_context_from_string: Error, invalid string");
  return 0;
}

char *callback_behavior_to_string (enum callback_behavior type)
{
  assert (CALLBACK_BEHAVIOR_MIN <= type && type < CALLBACK_BEHAVIOR_MAX);
  return callback_behavior_strings[type];
}

enum callback_behavior callback_behavior_from_string (char *str)
{
  enum callback_behavior i;
  assert(str != NULL);

  for (i = CALLBACK_BEHAVIOR_MIN; i < CALLBACK_BEHAVIOR_MAX; i++)
  {
    if (strcmp(str, callback_behavior_strings[i]) == 0)
      return i;
  }

  assert(!"callback_behavior_from_string: Error, invalid string");
  return 0;
}

int is_threadpool_cb (enum callback_type cbt)
{
  assert(CALLBACK_TYPE_MIN <= cbt && cbt < CALLBACK_TYPE_MAX);
  return (cbt == UV__WORK_WORK || cbt == UV_WORK_CB || /* Obvious ones. */
          cbt == UV_FS_WORK_CB || cbt == UV_GETADDRINFO_WORK_CB || cbt == UV_GETNAMEINFO_WORK_CB); /* Internal threadpool users. */
}

int is_run_timers_cb (enum callback_type cbt)
{
  assert(CALLBACK_TYPE_MIN <= cbt && cbt < CALLBACK_TYPE_MAX);
  return (cbt == UV_TIMER_CB);
}

int is_io_poll_cb (enum callback_type cbt)
{
  assert(CALLBACK_TYPE_MIN <= cbt && cbt < CALLBACK_TYPE_MAX);
  /* return (cbt == UV_ASYNC_CB || cbt == UV_ALLOC_CB || cb == ...); */
  return (cbt != MARKER_IO_POLL_END); /* TODO Hack. */
}

int is_run_check_cb (enum callback_type cbt)
{
  assert(CALLBACK_TYPE_MIN <= cbt && cbt < CALLBACK_TYPE_MAX);
  return (cbt == UV_CHECK_CB);
}

int is_run_idle_cb (enum callback_type cbt)
{
  assert(CALLBACK_TYPE_MIN <= cbt && cbt < CALLBACK_TYPE_MAX);
  return (cbt == UV_IDLE_CB);
}

int is_run_pending_cb (enum callback_type cbt)
{
  assert(CALLBACK_TYPE_MIN <= cbt && cbt < CALLBACK_TYPE_MAX);
  /* TODO Is this accurate? */
  return (cbt == UV_WRITE_CB);
}

int is_marker_event (enum callback_type cbt)
{
  assert(CALLBACK_TYPE_MIN <= cbt && cbt < CALLBACK_TYPE_MAX);
  return (MARKER_EVENTS_BEGIN <= cbt && cbt <= MARKER_EVENTS_END);
}

int is_internal_event (enum callback_type cbt)
{
  return (INTERNAL_BEGIN <= cbt && cbt <= INTERNAL_END);
}
