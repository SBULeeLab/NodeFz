#ifndef UV_UNIFIED_CALLBACK_H_
#define UV_UNIFIED_CALLBACK_H_

#include "list.h"
#include "map.h"
#include "mylog.h"
#include "logical-callback-node.h"
#include "unified-callback-enums.h"
#include <sys/types.h> /* struct sockaddr_storage */
#include <sys/socket.h>

/* Unified callback queue. */
#define UNIFIED_CALLBACK 1
#define GRAPHVIZ 1

#ifndef NOT_REACHED
#define NOT_REACHED assert (0 == 1);
#endif

struct callback_info_s;
typedef struct callback_info_s callback_info_t;

struct callback_origin
{
  enum callback_origin_type origin;
  enum callback_type type;
  any_func cb;
};

/* Description of an instance of a callback. */
struct callback_info_s
{
  enum callback_type type;
  enum callback_origin_type origin;
  any_func cb;
  long args[MAX_CALLBACK_NARGS]; /* Must be wide enough for the widest arg type. Seems to be 8 bytes. */
};

void dump_and_exit_sighandler (int signum);

void invoke_callback_wrap (any_func cb, enum callback_type type, ...);
void invoke_callback (callback_info_t *);

time_t get_relative_time (void);

/* Register and retrieve an LCBN in its context (handle or req). */
void lcbn_register (struct map *cb_type_to_lcbn, enum callback_type cb_type, lcbn_t *lcbn);
lcbn_t * lcbn_get (struct map *cb_type_to_lcbn, enum callback_type cb_type);

/* Set and get the current LCBN for this thread. */
void lcbn_current_set (lcbn_t *lcbn);
lcbn_t * lcbn_current_get (void);

/* Return the internal ID for the current pthread. */
int pthread_self_internal (void);

#endif /* UV_UNIFIED_CALLBACK_H_ */
