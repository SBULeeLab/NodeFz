#ifndef UV_TIMESPEC_FUNCS_H_
#define UV_TIMESPEC_FUNCS_H_

#include <time.h>

/* res = stop - start. */
void timespec_sub (const struct timespec *stop, const struct timespec *start, struct timespec *res);

/* Returns ts in us. */
long timespec_us (const struct timespec *ts);

/* Returns -1 if a < b, 0 if a == b, 1 if a > b. 
 * Here < means "happened before", etc. */
int timespec_cmp (const struct timespec *a, const struct timespec *b);

#endif /* UV_TIMESPEC_FUNCS_H_ */
