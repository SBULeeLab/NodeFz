#include "synchronization.h"
#include <uv.h> /* uv_thread_t */
#include <assert.h>

#include <uv-common.h> /* Memory management */

/***********************
 * Private variable and type declarations.
 ***********************/

static int REENTRANT_MUTEX_MAGIC = 98486132;

struct reentrant_mutex_s
{
  int magic;

  uv_thread_t holder;
  int lock_depth; /* 0 means REENTRANT_MUTEX_NO_HOLDER, 1 means thread has locked it x1, 2 means thread has locked it x2, etc. */
  uv_mutex_t mutex;
};

/***********************
 * Private API declarations.
 ***********************/

/* Return non-zero if mutex is non-null and looks valid. */
static int reentrant_mutex__looks_valid (reentrant_mutex_t *mutex);

/* Initialize this reentrant_mutex. */
static int reentrant_mutex__init (reentrant_mutex_t *mutex);

/***********************
 * Public API definitions.
 ***********************/

reentrant_mutex_t * reentrant_mutex_create (void)
{
  reentrant_mutex_t *rm = (reentrant_mutex_t *) uv__malloc(sizeof *rm);
  if (rm == NULL)
    return NULL;

  if (reentrant_mutex__init(rm) != 0)
  {
    uv__free(rm);
    rm = NULL;
  }

  return rm;
}

void reentrant_mutex_destroy (reentrant_mutex_t *mutex)
{
  assert(reentrant_mutex__looks_valid(mutex));
  uv_mutex_destroy(&mutex->mutex);
  return;
}

void reentrant_mutex_lock (reentrant_mutex_t *mutex)
{
  assert(reentrant_mutex__looks_valid(mutex));

  if (mutex->holder != uv_thread_self())
  {
    uv_mutex_lock(&mutex->mutex);
    assert(mutex->lock_depth == 0);
    mutex->holder = uv_thread_self();
  }

  mutex->lock_depth++;

  assert(mutex->holder == uv_thread_self());
  assert(1 <= mutex->lock_depth);
  return;
}

void reentrant_mutex_unlock (reentrant_mutex_t *mutex)
{
  assert(reentrant_mutex__looks_valid(mutex));

  assert(mutex->holder == uv_thread_self());
  assert(1 <= mutex->lock_depth);

  mutex->lock_depth--;
  if (mutex->lock_depth == 0)
  {
    mutex->holder = REENTRANT_MUTEX_NO_HOLDER;
    uv_mutex_unlock(&mutex->mutex);
  }

  assert(0 <= mutex->lock_depth);
  return;
}

uv_thread_t
reentrant_mutex_holder (reentrant_mutex_t *mutex)
{
  assert(reentrant_mutex__looks_valid(mutex));
  return mutex->holder;
}


int reentrant_mutex_depth (reentrant_mutex_t *mutex)
{
  assert(reentrant_mutex__looks_valid(mutex));
  return mutex->lock_depth;
}

/***********************
 * Private API definitions.
 ***********************/

static int reentrant_mutex__looks_valid (reentrant_mutex_t *mutex)
{
  return (mutex != NULL && 
          mutex->magic == REENTRANT_MUTEX_MAGIC && 
          0 <= mutex->lock_depth);
}

static int reentrant_mutex__init (reentrant_mutex_t *mutex)
{
  assert(mutex != NULL);

  mutex->magic = REENTRANT_MUTEX_MAGIC;
  mutex->holder = REENTRANT_MUTEX_NO_HOLDER;
  mutex->lock_depth = 0;

  return uv_mutex_init(&mutex->mutex);
}
