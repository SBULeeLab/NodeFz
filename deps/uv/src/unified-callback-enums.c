#include "unified-callback-enums.h"
#include <assert.h>

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

  "INITIAL STACK",
  "ANY_CALLBACK"
};

char *callback_type_to_string (enum callback_type type)
{
  assert (CALLBACK_TYPE_MIN <= type && type < CALLBACK_TYPE_MAX);
  return callback_type_strings[type];
}

static char *callback_context_strings[] = { "HANDLE", "REQ", "UNKNOWN" };

char *callback_context_to_string (enum callback_context type)
{
  return callback_context_strings[type];
}

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

static char *callback_behavior_strings[] = { "ACTION", "RESPONSE", "UNKNOWN" };

char *callback_behavior_to_string (enum callback_behavior type)
{
  return callback_behavior_strings[type];
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
      return CALLBACK_BEHAVIOR_RESPONSE;

    default:
      return CALLBACK_BEHAVIOR_UNKNOWN;
  }
  NOT_REACHED;
}
