#ifndef UV_TIMESPEC_FUNCS_H_
#define UV_TIMESPEC_FUNCS_H_

#include <time.h>

/* res = stop - start. */
void timespec_sub (const struct timespec *stop, const struct timespec *start, struct timespec *res);

/* Returns ts in us. */
long timespec_us (const struct timespec *ts);

#endif /* UV_TIMESPEC_FUNCS_H_ */
