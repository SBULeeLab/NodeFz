#ifndef UV_SRC_SYNCHRONIZATION_H_
#define UV_SRC_SYNCHRONIZATION_H_

/* Author: Jamie Davis <davisjam@vt.edu> 
 * Description: Defines synchronization primitives not provided by thread.c.
 */

/* reentrant_mutex_t is a re-entrant mutex.
 * If you always pair lock-unlock calls, it will work fine.
 * Otherwise it may crash your program.
 *
 * The usual warnings and questions apply:
 *   - Why are you using a re-entrant mutex instead of a regular one?
 *   - Are you sure this is a good idea?
 *   - There may be a better way to write your code.
 */
struct reentrant_mutex_s;
typedef struct reentrant_mutex_s reentrant_mutex_t;

/* Returns NULL on error. */
reentrant_mutex_t * reentrant_mutex_create (void);
void reentrant_mutex_destroy (reentrant_mutex_t *mutex);

void reentrant_mutex_lock (reentrant_mutex_t *mutex);
void reentrant_mutex_unlock (reentrant_mutex_t *mutex);

#endif  /* UV_SRC_SYNCHRONIZATION_H_ */
