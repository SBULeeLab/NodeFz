#include "uv-random.h"

#include "uv-common.h" /* Allocators */

#include <uv.h>

#include <string.h> /* memcpy */
#include <assert.h>
#include <stdlib.h> /* rand */
#include <stddef.h> /* size_t */

int rand_int (int n)
{
  /* Assumes n << RAND_MAX, else the distribution will be off. */
  assert(0 < n);
  return (rand() % n);
}

void random_shuffle (void *items, int nitems, size_t item_size)
{
  void *tmp = uv__malloc(item_size);
  size_t i, j;
  void *a = NULL, *b = NULL;

  assert(tmp != NULL);

  /* https://en.wikipedia.org/wiki/Fisher%E2%80%93Yates_shuffle#The_modern_algorithm
   *
   * For each slot, starting from the final slot in the array: 
   *   choose one of the items in the not-yet-handled slots
   *   place it in the current slot
   * The result is equivalent to "3 choices for slot 1, 2 choices for slot 2, 1 choice for slot 3" -- 3! possible results.
   */
  for (i = nitems-1; i > 0; i--)
  {
    j = rand_int(i+1);
    a = (char *) items + i*item_size;
    b = (char *) items + j*item_size;

    memcpy(tmp, a, item_size); /* tmp = a_orig */
    memcpy(a, b, item_size);   /* a = b */
    memcpy(b, tmp, item_size); /* b = tmp (== a_orig) */
  }

  uv__free(tmp);
}
