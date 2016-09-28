#ifndef UV_SRC_UV_RANDOM_H_
#define UV_SRC_UV_RANDOM_H_

/* Handy set of random functions. */

#include <stddef.h> /* size_t */

/* 0 <= X < n */
int rand_int (int n);

/* Shuffle items of len nitems, each of size item_size, using Fisher-Yates. */
void random_shuffle (void *items, int nitems, size_t item_size);

#endif  /* UV_SRC_UV_RANDOM_H_ */
