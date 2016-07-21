#ifndef UV_TIMESPEC_FUNCS_H_
#define UV_TIMESPEC_FUNCS_H_

#include <time.h>

void timespec_sub (const struct timespec *stop, const struct timespec *start, struct timespec *res);

#endif /* UV_TIMESPEC_FUNCS_H_ */
