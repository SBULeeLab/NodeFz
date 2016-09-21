#include <uv.h>

/***********************
 * Private variable and type declarations.
 ***********************/

static uv_thread_t NO_HOLDER = -1;
static int REENTRANT_MUTEX_MAGIC = 98486132;

struct reentrant_mutex_s
{
  int magic;

  uv_thread_t holder;
  int lock_depth; /* 0 means NO_HOLDER, 1 means thread has locked it x1, 2 means thread has locked it x2, etc. */
  uv_mutex_t mutex;
};

/***********************
 * Private API declarations.
 ***********************/

static int reentrant_mutex__looks_valid (reentrant_mutex_t *mutex);

/***********************
 * Public API definitions.
 ***********************/

/* Return non-zero if mutex looks valid. */

int reentrant_mutex_init (reentrant_mutex_t *mutex)
{
  assert(mutex != NULL);

  mutex->magic = REENTRANT_MUTEX_MAGIC;
  mutex->holder = NO_HOLDER;
  mutex->lock_depth = 0;

  return uv_mutex_init(&mutex);
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
    mutex->holder = NO_HOLDER;
    uv_mutex_unlock(&mutex->mutex);
  }

  assert(0 <= mutex->lock_depth);
  return;
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
