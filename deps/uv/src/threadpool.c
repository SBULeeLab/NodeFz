/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "uv-common.h"
#include "statistics.h"

#if !defined(_WIN32)
# include "unix/internal.h"
#else
# include "win/req-inl.h"
/* TODO(saghul): unify internal req functions */
static void uv__req_init(uv_loop_t* loop,
                         uv_req_t* req,
                         uv_req_type type) {
  uv_req_init(loop, req);
  req->type = type;
  uv__req_register(loop, req);
}
# define uv__req_init(loop, req, type) \
    uv__req_init((loop), (uv_req_t*)(req), (type))
#endif

#include <stdlib.h>
#include <unistd.h> /* usleep */

#include "scheduler.h"
#include "logical-callback-node.h"
#include "unified-callback-enums.h"

#define MAX_THREADPOOL_SIZE 128

static uv_once_t once = UV_ONCE_INIT;
static uv_cond_t cond;
static uv_mutex_t mutex;
int n_work_items = 0; /* The total number of work items retrieved from wq by a worker thread. Protected by mutex. */
static unsigned int idle_threads;
static unsigned int nthreads;
static uv_thread_t* threads;
static uv_thread_t default_threads[4];
static QUEUE exit_message;
static QUEUE wq;
static volatile int initialized;

static void uv__queue_work(struct uv__work* w);
static void uv__queue_done(struct uv__work* w, int err);

static void uv__cancelled(struct uv__work* w) {
  abort();
}

/* Final step to clean up the async handle allocated for a struct uv__work. */
static void uv__work_async_close (uv_handle_t *async) {
  uv__work_async_t *uv__work_async = NULL;
  uv__work_async = container_of(async, uv__work_async_t, async_buf);
  uv__free(uv__work_async);
}

static void post(QUEUE* q) {
  mylog(LOG_THREADPOOL, 9, "post: posting work item q %p\n", q);
  uv_mutex_lock(&mutex);
  QUEUE_INSERT_TAIL(&wq, q);
  if (idle_threads > 0)
  {
    mylog(LOG_THREADPOOL, 9, "post: Signal'ing the threadpool work cond\n");
    uv_cond_signal(&cond);
  }
  uv_mutex_unlock(&mutex);
  mylog(LOG_THREADPOOL, 9, "post: Done posting work item q %p\n", q);
}

/* Return the queue element at the specified index.
 * The returned element is still in the queue.
 * It is a fatal error if QUEUE_LEN is not large enough.
 */
static QUEUE * QUEUE_INDEX (QUEUE *head, int index)
{
  QUEUE *q = NULL;
  int i = 0;

  assert(0 <= index);
#ifdef JD_DEBUG_FULL
  {
    int queue_len = 0;
    QUEUE_LEN(len, q, head);
    assert(index < len);
  }
#endif

  q = QUEUE_HEAD(head);
  for (i = 0; i < index; i++)
    q = QUEUE_NEXT(q);
  return q;
}

/* To avoid deadlock with uv_cancel() it's crucial that the worker
 * never holds the global mutex and the loop-local mutex at the same time.
 */
static void worker(void* arg) {
  struct uv__work* w;
  QUEUE *q;
  int work_item_number = -1; /* Holds value of n_work_items when worker gets each work item. */

  /* Scheduler supplies. */
  spd_wants_work_t spd_wants_work;
  spd_getting_work_t spd_getting_work;
  spd_got_work_t spd_got_work;
  spd_before_put_done_t spd_before_put_done;
  spd_after_put_done_t spd_after_put_done;

  (void) arg;

  scheduler_register_thread(THREAD_TYPE_THREADPOOL);
  mylog(LOG_THREADPOOL, 1, "worker %lli: begins\n", uv_thread_self());

  for (;;) {
    mylog(LOG_THREADPOOL, 1, "worker: top of loop\n");

    memset(&spd_wants_work, 0, sizeof spd_wants_work);
    spd_wants_work_init(&spd_wants_work);
    assert(clock_gettime(CLOCK_MONOTONIC_RAW, &spd_wants_work.start_time) == 0);
    spd_wants_work.wq = &wq;

   GET_WORK: /* worker holds no locks. */
    uv_mutex_lock(&mutex);
    while (QUEUE_EMPTY(&wq)) {
      mylog(LOG_THREADPOOL, 1, "worker: No work, waiting. %i LCBNs already executed.\n", scheduler_n_executed());
      idle_threads += 1;
      uv_cond_wait(&cond, &mutex);
      idle_threads -= 1;
      /* Reset the spd_wants_work clock. */
      assert(clock_gettime(CLOCK_MONOTONIC_RAW, &spd_wants_work.start_time) == 0);
    }

    /* Now we know there is at least one work item in the queue. 
     * Ask the scheduler if we can get a work item yet.
     */
    scheduler_thread_yield(SCHEDULE_POINT_TP_WANTS_WORK, &spd_wants_work);
    if (!spd_wants_work.should_get_work)
    {
      mylog(LOG_THREADPOOL, 5, "worker: scheduler says I can't get a work item yet\n");
      uv_mutex_unlock(&mutex);
      
      /* Let looper thread proceed. */
      assert(uv_thread_yield() == 0);
      usleep(10); /* Seems to be more effective at yielding on Ubuntu than just uv_thread_yield. */

      goto GET_WORK;
    }
    mylog(LOG_THREADPOOL, 5, "worker: getting work item\n");

    /* Get advice from scheduler about which work item to grab. 
     * In the case of a TP with "degrees of freedom" to simulate more threads,
     * we may be advised to use an index other than 0.
     */
    spd_getting_work_init(&spd_getting_work);
    spd_getting_work.wq = &wq;
    scheduler_thread_yield(SCHEDULE_POINT_TP_GETTING_WORK, &spd_getting_work);

    mylog(LOG_THREADPOOL, 7, "worker: getting item at index %i\n", spd_getting_work.index);
    q = QUEUE_INDEX(&wq, spd_getting_work.index);

    /* Tell the scheduler how long the TP queue is at the point when we are getting work. */
    {
      int len;
      QUEUE *q2;
      QUEUE_LEN(len, q2, &wq);
      statistics_record(STATISTIC_TP_SIMULTANEOUS_WORK, len);
    }

    if (q == &exit_message)
      uv_cond_signal(&cond);
    else {
      QUEUE_REMOVE(q);
      QUEUE_INIT(q);  /* Signal uv_cancel() that the work req is
                             executing. */
      work_item_number = n_work_items;
      n_work_items++;
    }

    uv_mutex_unlock(&mutex);

    if (q == &exit_message)
      break;

    mylog(LOG_THREADPOOL, 9, "worker: Got work item %i\n", work_item_number);
    w = QUEUE_DATA(q, struct uv__work, wq);

    /* Yield to scheduler. */
    spd_got_work_init(&spd_got_work);
    spd_got_work.work_item = w;
    spd_got_work.work_item_num = work_item_number;
    scheduler_thread_yield(SCHEDULE_POINT_TP_GOT_WORK, &spd_got_work);

    /* Do work. */
    mylog(LOG_THREADPOOL, 9, "worker: Doing work item %i w %p\n", work_item_number, w);
#if UNIFIED_CALLBACK
    invoke_callback_wrap((any_func) w->work, UV__WORK_WORK, (long int) w);
#else
    w->work(w);
#endif
    mylog(LOG_THREADPOOL, 9, "worker: Done doing work item %i w %p\n", work_item_number, w);

    /* Yield to the scheduler before and after adding the "done" item. 
     * This allows the scheduler to perturb the order in which "done" items are queued.
     */
    spd_before_put_done_init(&spd_before_put_done);
    spd_before_put_done.work_item = w;
    spd_before_put_done.work_item_num = work_item_number;
    scheduler_thread_yield(SCHEDULE_POINT_TP_BEFORE_PUT_DONE, &spd_before_put_done);

    w->work = NULL;  /* Signal uv_cancel() that the work req is done
                        executing. */
    /* Signal the looper that this item is done. */
    mylog(LOG_THREADPOOL, 9, "worker: Signaling that work item %i w %p (async handle %p) is done\n", work_item_number, w, w->ptr_and_async->async_buf);
    uv_async_send((uv_async_t *) w->ptr_and_async->async_buf);

    spd_after_put_done_init(&spd_after_put_done);
    spd_after_put_done.work_item = w;
    spd_after_put_done.work_item_num = work_item_number;
    scheduler_thread_yield(SCHEDULE_POINT_TP_AFTER_PUT_DONE, &spd_after_put_done);

    statistics_record(STATISTIC_TP_WORK_EXECUTED, 1);
  }
}


#ifndef _WIN32
UV_DESTRUCTOR(static void cleanup(void)) {
  unsigned int i;

  if (initialized == 0)
    return;

  post(&exit_message);
  while (scheduler_current_cb_thread() == uv_thread_self())
  {
    /* We came here through process.exit() in the middle of executing a CB, and won't ever return from the CB.
     * Tell the scheduler we're done (with the entire CB stack) so that other waiters (the TP worker thread(s)) can make progress.
     */
    spd_after_exec_cb_t spd_after_exec_cb;

    mylog(LOG_THREADPOOL, 1, "cleanup: unwinding CB stack\n");
    spd_after_exec_cb_init(&spd_after_exec_cb);
    spd_after_exec_cb.lcbn = NULL;
    scheduler_thread_yield(SCHEDULE_POINT_AFTER_EXEC_CB, &spd_after_exec_cb);
  }

  for (i = 0; i < nthreads; i++)
    if (uv_thread_join(threads + i))
      abort();

  if (threads != default_threads)
    uv__free(threads);

  uv_mutex_destroy(&mutex);
  uv_cond_destroy(&cond);

  threads = NULL;
  nthreads = 0;
  initialized = 0;
}
#endif


static void init_once(void) {
  unsigned int i;
  const char* val;
  
  initialize_fuzzy_libuv();

  nthreads = ARRAY_SIZE(default_threads);
  val = getenv("UV_THREADPOOL_SIZE");
  if (val != NULL)
    nthreads = atoi(val);
  if (nthreads == 0)
    nthreads = 1;
  if (nthreads > MAX_THREADPOOL_SIZE)
    nthreads = MAX_THREADPOOL_SIZE;

  threads = default_threads;
  if (nthreads > ARRAY_SIZE(default_threads)) {
    threads = uv__malloc(nthreads * sizeof(threads[0]));
    if (threads == NULL) {
      nthreads = ARRAY_SIZE(default_threads);
      threads = default_threads;
    }
  }

  if (uv_cond_init(&cond))
    abort();

  if (uv_mutex_init(&mutex))
    abort();

  QUEUE_INIT(&wq);

  mylog(LOG_THREADPOOL, 1, "init_once: %i threads\n", nthreads);
  for (i = 0; i < nthreads; i++)
    if (uv_thread_create(threads + i, worker, NULL))
      abort();

  initialized = 1;
}

void uv__work_done(uv_async_t *handle) {
  struct uv__work *w = NULL;
  uv__work_async_t *uv__work_async = NULL;
  int err = 0;

  assert(handle != NULL && handle->type == UV_ASYNC);

  uv__work_async = container_of(handle, uv__work_async_t, async_buf);
  w = uv__work_async->uv__work;
  mylog(LOG_THREADPOOL, 1, "uv__work_done: handle %p w %p\n", handle, w);

  err = (w->work == uv__cancelled) ? UV_ECANCELED : 0;
#ifdef UNIFIED_CALLBACK
  invoke_callback_wrap((any_func) w->done, UV__WORK_DONE, (long int) w, (long int) err);
#else
  w->done(w, err);
#endif

  /* We're done with handle. */
  mylog(LOG_THREADPOOL, 1, "uv__work_done: calling uv_close on handle %p w %p\n", handle, w);
  uv_close((uv_handle_t *) handle, uv__work_async_close);
}

void uv__work_submit(uv_loop_t* loop,
                     struct uv__work* w,
                     void (*work)(struct uv__work* w),
                     void (*done)(struct uv__work* w, int status)) {
  uv_once(&once, init_once);
  w->loop = loop;
  w->work = work;
  w->done = done;

  /* Prep the uv__work_async_t inside uv__work. */
  w->ptr_and_async = (uv__work_async_t *) uv__malloc(sizeof(uv__work_async_t) + sizeof(uv_async_t));
  w->ptr_and_async->uv__work = w; /* This pointer is safe until we call UV__WORK_DONE in uv__work_done, at which point w may be free'd. */

  mylog(LOG_THREADPOOL, 1, "uv__work_submit: uv_async_init'ing: w %p async %p\n", w, w->ptr_and_async->async_buf);
  uv_async_init(loop, (uv_async_t *) w->ptr_and_async->async_buf, uv__work_done);

  mylog(LOG_THREADPOOL, 1, "uv__work_submit: post'ing w %p (async %p)\n", w, w->ptr_and_async->async_buf);
  post(&w->wq);
}

static int uv__work_cancel(uv_loop_t* loop, uv_req_t* req, struct uv__work* w) {
  int can_cancel;

  uv_mutex_lock(&mutex);
  uv_mutex_lock(&w->loop->wq_mutex);

  /* Is it still on the wq? */
  can_cancel = !QUEUE_EMPTY(&w->wq) && w->work != NULL;
  if (can_cancel)
    QUEUE_REMOVE(&w->wq);

  uv_mutex_unlock(&w->loop->wq_mutex);
  uv_mutex_unlock(&mutex);

  if (!can_cancel)
    return UV_EBUSY;

  w->work = uv__cancelled;
  uv_async_send((uv_async_t *) w->ptr_and_async->async_buf);
  mylog(LOG_THREADPOOL, 1, "uv__work_cancel: signal'd a cancelled 'work' item (w %p)\n", w);

  return 0;
}

static void uv__queue_work(struct uv__work* w) {
  uv_work_t* req = container_of(w, uv_work_t, work_req);

#ifdef UNIFIED_CALLBACK
  invoke_callback_wrap((any_func) req->work_cb, UV_WORK_CB, (long) req);
#else
  req->work_cb(req);
#endif
}

any_func uv_uv__queue_work_ptr (void)
{
  return (any_func) uv__queue_work;
}

static void uv__queue_done(struct uv__work* w, int err) {
  uv_work_t* req;

  req = container_of(w, uv_work_t, work_req);
  uv__req_unregister(req->loop, req);

  if (req->after_work_cb == NULL)
    return;

#ifdef UNIFIED_CALLBACK
  invoke_callback_wrap((any_func) req->after_work_cb, UV_AFTER_WORK_CB, (long int) req, (long int) err);
#else
  req->after_work_cb(req, err);
#endif
}

any_func uv_uv__queue_done_ptr (void)
{
  return (any_func) uv__queue_done;
}

int uv_queue_work(uv_loop_t* loop,
                  uv_work_t* req,
                  uv_work_cb work_cb,
                  uv_after_work_cb after_work_cb) {
  if (work_cb == NULL)
    return UV_EINVAL;

  uv__req_init(loop, req, UV_WORK);
  req->loop = loop;
  req->work_cb = work_cb;
  req->after_work_cb = after_work_cb;

  uv__work_submit(loop, &req->work_req, uv__queue_work, uv__queue_done);
  return 0;
}


int uv_cancel(uv_req_t* req) {
  struct uv__work* wreq;
  uv_loop_t* loop;

  switch (req->type) {
  case UV_FS:
    loop =  ((uv_fs_t*) req)->loop;
    wreq = &((uv_fs_t*) req)->work_req;
    break;
  case UV_GETADDRINFO:
    loop =  ((uv_getaddrinfo_t*) req)->loop;
    wreq = &((uv_getaddrinfo_t*) req)->work_req;
    break;
  case UV_GETNAMEINFO:
    loop = ((uv_getnameinfo_t*) req)->loop;
    wreq = &((uv_getnameinfo_t*) req)->work_req;
    break;
  case UV_WORK:
    loop =  ((uv_work_t*) req)->loop;
    wreq = &((uv_work_t*) req)->work_req;
    break;
  default:
    return UV_EINVAL;
  }

  return uv__work_cancel(loop, req, wreq);
}
