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
#include "scheduler.h"

#define MAX_THREADPOOL_SIZE 1

static uv_once_t once = UV_ONCE_INIT;
static uv_cond_t cond;
static uv_mutex_t mutex;
static unsigned int idle_threads;
static unsigned int nthreads;
static uv_thread_t* threads;
static uv_thread_t default_threads[1];
static QUEUE exit_message;
static QUEUE wq;
static volatile int initialized;

static void uv__queue_work(struct uv__work* w);
static void uv__queue_done(struct uv__work* w, int err);

static void uv__cancelled(struct uv__work* w) {
  abort();
}


/* To avoid deadlock with uv_cancel() it's crucial that the worker
 * never holds the global mutex and the loop-local mutex at the same time.
 */
static void worker(void* arg) {
  struct uv__work* w;
  uv_work_t *req;
  QUEUE* q;

  QUEUE wq_buf;
  struct list *pending_work;
  sched_context_t *sched_context;

  int tmp_queue_size = 0;

  (void) arg;

  for (;;) {
    mylog(LOG_THREADPOOL, 1, "worker: begins\n");
    uv_mutex_lock(&mutex);

    while (QUEUE_EMPTY(&wq)) {
      idle_threads += 1;
      uv_cond_wait(&cond, &mutex);
      idle_threads -= 1;
    }

    q = QUEUE_HEAD(&wq);
    if (q == &exit_message)
      uv_cond_signal(&cond);
    else
      /* Copy the work queue so we can release the mutex. */
      QUEUE_SPLIT(&wq, q, &wq_buf);
    uv_mutex_unlock(&mutex);

    if (q == &exit_message)
      break;

    /* Schedule all possible work in wq_buf. */

    /* Interpret wq_buf as list of uv__work contexts. */
    pending_work = list_create();
    QUEUE_FOREACH(q, &wq_buf) {
      w = QUEUE_DATA(q, struct uv__work, wq);
      req = container_of(w, uv_work_t, work_req);
      assert(req->magic == UV_REQ_MAGIC && req->type == UV_WORK);
      /* All threadpool users must go through uv_queue_work. */
      assert(req->work_req.work == uv__queue_work);
      sched_context = sched_context_create(EXEC_CONTEXT_THREADPOOL_WORKER, CALLBACK_CONTEXT_REQ, req);
      list_push_back(pending_work, &sched_context->elem);
    }

    /* Find, remove, and execute the work next in the schedule. */
    mylog(LOG_THREADPOOL, 5, "worker: %i ready 'work' items\n", list_size(pending_work));
    while (!list_empty(pending_work))
    {
      sched_context = scheduler_next_context(pending_work);
      if (sched_context)
      {
        list_remove(pending_work, &sched_context->elem);
        req = (uv_work_t *) sched_context->wrapper;
        assert(req->magic == UV_REQ_MAGIC && req->type == UV_WORK);
        w = &req->work_req;
        assert(w);
        sched_context_destroy(sched_context);

        /* Remove from wq_buf. */
        q = &w->wq;
        QUEUE_REMOVE(q);
        QUEUE_INIT(q); /* Signal uv_cancel() that the work req is
                           executing. */

        /* Run the work item. */
        INVOKE_CALLBACK_1(UV__WORK_WORK, w->work, (long int) w);

        /* Throw it onto the looper thread's queue. */
        uv_mutex_lock(&w->loop->wq_mutex);
        w->work = NULL;  /* Signal uv_cancel() that the work req is done
                            executing. */
        QUEUE_INSERT_TAIL(&w->loop->wq, &w->wq);
        uv_async_send(&w->loop->wq_async); /* signal a pending done CB to be executed through uv__work_done. */
        mylog(LOG_THREADPOOL, 1, "worker: signal'd a ready 'done' item (w %p w->done %p)\n", w, w->done);
        uv_mutex_unlock(&w->loop->wq_mutex);
      }
      else
        break;
    }

    /* Repair: add any work we didn't run back onto the front of wq. */
    mylog(LOG_THREADPOOL, 5, "worker: deferred %i 'work' items\n", list_size(pending_work));
    uv_mutex_lock(&mutex);
    while (!QUEUE_EMPTY(&wq_buf)) {
      q = QUEUE_HEAD(&wq_buf);
      QUEUE_REMOVE(q);
      QUEUE_INIT(q);
      QUEUE_INSERT_HEAD(&wq, q);
    }
    uv_mutex_unlock(&mutex);

#if 0
    /* TODO */
    list_destroy_full(pending_work, sched_context_list_destroy_func, NULL); 
#endif
  }
}


static void post(QUEUE* q) {
  uv_mutex_lock(&mutex);
  QUEUE_INSERT_TAIL(&wq, q);
  if (idle_threads > 0)
    uv_cond_signal(&cond);
  uv_mutex_unlock(&mutex);
}


#ifndef _WIN32
UV_DESTRUCTOR(static void cleanup(void)) {
  unsigned int i;

  if (initialized == 0)
    return;

  post(&exit_message);

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

  init_log();

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

  for (i = 0; i < nthreads; i++)
    if (uv_thread_create(threads + i, worker, NULL))
      abort();

  initialized = 1;
}


void uv__work_submit(uv_loop_t* loop,
                     struct uv__work* w,
                     void (*work)(struct uv__work* w),
                     void (*done)(struct uv__work* w, int status)) {
  uv_once(&once, init_once);
  w->loop = loop;
  w->work = work;
  w->done = done;

#ifdef UNIFIED_CALLBACK
  /* We are being registered. There must be an active CB registering us. */
  w->logical_parent = current_callback_node_get();
  assert (w->logical_parent != NULL);
#endif

  post(&w->wq);
}


static int uv__work_cancel(uv_loop_t* loop, uv_req_t* req, struct uv__work* w) {
  int cancelled;

  uv_mutex_lock(&mutex);
  uv_mutex_lock(&w->loop->wq_mutex);

  cancelled = !QUEUE_EMPTY(&w->wq) && w->work != NULL;
  if (cancelled)
    QUEUE_REMOVE(&w->wq);

  uv_mutex_unlock(&w->loop->wq_mutex);
  uv_mutex_unlock(&mutex);

  if (!cancelled)
    return UV_EBUSY;

  w->work = uv__cancelled;
  uv_mutex_lock(&loop->wq_mutex);
  QUEUE_INSERT_TAIL(&loop->wq, &w->wq);
  uv_async_send(&loop->wq_async);
  mylog(LOG_THREADPOOL, 1, "uv__work_cancel: signal'd a cancelled 'done' item (w %p w->done %p)\n", w, w->done);
  uv_mutex_unlock(&loop->wq_mutex);

  return 0;
}


void uv__work_done(uv_async_t* handle) {
  struct uv__work* w = NULL;
  uv_loop_t* loop = NULL;
  uv_work_t *req = NULL;
  QUEUE* q = NULL;
  QUEUE wq;
  int err;

  struct list *pending_done = NULL;
  sched_context_t *sched_context = NULL;

  loop = container_of(handle, uv_loop_t, wq_async);
  QUEUE_INIT(&wq);

  /* Go once through the loop every time.
     If we get to the end of the loop and the next LCBN is a (not-yet-present) done item,
     spin. If we don't spin, by returning we introduce an unexpected UV_ASYNC_CB into the schedule. */
  do
  {
    mylog(LOG_THREADPOOL, 5, "uv__work_done: Checking for done LCBNs\n");
    uv_mutex_lock(&loop->wq_mutex);
    if (!QUEUE_EMPTY(&loop->wq)) {
      q = QUEUE_HEAD(&loop->wq);
      QUEUE_SPLIT(&loop->wq, q, &wq);
    }
    uv_mutex_unlock(&loop->wq_mutex);

    /* Interpret wq as list of uv__work contexts. */
    pending_done = list_create();
    QUEUE_FOREACH(q, &wq) {
      w = QUEUE_DATA(q, struct uv__work, wq);
      req = container_of(w, uv_work_t, work_req);
      assert(req->magic == UV_REQ_MAGIC && req->type == UV_WORK);
      /* All threadpool users must go through uv_queue_work. */
      assert(req->work_req.done == uv__queue_done);
      sched_context = sched_context_create(EXEC_CONTEXT_THREADPOOL_DONE, CALLBACK_CONTEXT_REQ, req);
      list_push_back(pending_done, &sched_context->elem);
    }

    /* Find, remove, and execute the work next in the schedule. */
    mylog(LOG_THREADPOOL, 3, "uv__work_done: %i pending 'done' items\n", list_size(pending_done));
    while (!list_empty(pending_done))
    {
      mylog(LOG_THREADPOOL, 5, "uv__work_done: %i pending 'done' items\n", list_size(pending_done));
      sched_context = scheduler_next_context(pending_done);
      if (sched_context)
      {
        list_remove(pending_done, &sched_context->elem);
        req = (uv_work_t *) sched_context->wrapper;
        assert(req->magic == UV_REQ_MAGIC && req->type == UV_WORK);
        w = &req->work_req;
        assert(w);
        sched_context_destroy(sched_context);

        /* Remove from wq. */
        q = &w->wq;
        QUEUE_REMOVE(q);
        QUEUE_INIT(q);

        /* Run the done item. */
        err = (w->work == uv__cancelled) ? UV_ECANCELED : 0;
        mylog(LOG_THREADPOOL, 5, "uv__work_done: Next work item: w %p w->done %p\n", w, w->done);
        INVOKE_CALLBACK_2(UV__WORK_DONE, w->done, (long int) w, (long int) err);
      }
      else
        break;
    }

    /* Repair: add any work we didn't run back onto the front of loop's wq. */
    mylog(LOG_THREADPOOL, 3, "uv__work_done: deferred %i 'done' items\n", list_size(pending_done));
    uv_mutex_lock(&loop->wq_mutex);
    while (!QUEUE_EMPTY(&wq)) {
      q = QUEUE_HEAD(&wq);
      QUEUE_REMOVE(q);
      QUEUE_INIT(q);
      QUEUE_INSERT_HEAD(&loop->wq, q);

      /* These may be the last items ever put in loop->wq, so
         async_send to ensure we come back through this loop. */
      w = QUEUE_DATA(q, struct uv__work, wq);
      uv_async_send(&w->loop->wq_async);
      mylog(LOG_THREADPOOL, 1, "uv__work_done: signal'd a ready 'done' item (w %p w->done %p)\n", w, w->done);
    }
    uv_mutex_unlock(&loop->wq_mutex);

#if 0
    /* TODO */
    list_destroy_full(pending_done, sched_context_list_destroy_func, NULL); 
#endif
    /* TODO DEBUG */
    mylog(LOG_THREADPOOL, 5, "uv__work_done: Next type is UV_AFTER_WORK_CB? %i\n", (scheduler_next_lcbn_type() == UV_AFTER_WORK_CB));
  } while (scheduler_next_lcbn_type() == UV_AFTER_WORK_CB);

}

static void uv__queue_work(struct uv__work* w) {
  uv_work_t* req = container_of(w, uv_work_t, work_req);

#ifdef UNIFIED_CALLBACK
  INVOKE_CALLBACK_1(UV_WORK_CB, req->work_cb, (long) req);
#else
  req->work_cb(req);
#endif
}

void * uv_uv__queue_work_ptr (void)
{
  return (void *) uv__queue_work;
}

static void uv__queue_done(struct uv__work* w, int err) {
  uv_work_t* req;

  req = container_of(w, uv_work_t, work_req);
  uv__req_unregister(req->loop, req);

  if (req->after_work_cb == NULL)
    return;

#ifdef UNIFIED_CALLBACK
  INVOKE_CALLBACK_2(UV_AFTER_WORK_CB, req->after_work_cb, (long int) req, (long int) err);
#else
  req->after_work_cb(req, err);
#endif
}

void * uv_uv__queue_done_ptr (void)
{
  return (void *) uv__queue_done;
}

/* Add extra LCBN dependencies if REQ is of one of the 'wrapped' paths
    (fs, getaddrinfo, getnameinfo). */ 
void uv__add_other_dependencies(uv_work_t *req)
{
  enum callback_type wrapped_work_type, wrapped_done_type;
  lcbn_t *work_lcbn;
  uv_req_t *wrapped_req;

  assert(req != NULL);

  /* Identify the type. */
  work_lcbn = lcbn_get(req->cb_type_to_lcbn, UV_WORK_CB);
  if (work_lcbn->cb == uv_uv__fs_work_wrapper_ptr())
  {
    wrapped_work_type = UV_FS_WORK_CB;
    wrapped_done_type = UV_FS_CB;
  }
  else if (work_lcbn->cb == uv_uv__getaddrinfo_work_wrapper_ptr())
  {
    wrapped_work_type = UV_GETADDRINFO_WORK_CB;
    wrapped_done_type = UV_GETADDRINFO_CB;
  }
  else if (work_lcbn->cb == uv_uv__getnameinfo_work_wrapper_ptr())
  {
    wrapped_work_type = UV_GETNAMEINFO_WORK_CB;
    wrapped_done_type = UV_GETNAMEINFO_CB;
  }
  else
    /* Not a wrapped request, nothing to do. */
    return;

  wrapped_req = (uv_req_t *) req->data;

  /* WORK -> wrapped_work_type */
  lcbn_add_dependency(lcbn_get(req->cb_type_to_lcbn, UV_WORK_CB),
                      lcbn_get(wrapped_req->cb_type_to_lcbn, wrapped_work_type));
  /* wrapped_work_type -> AFTER_WORK */
  lcbn_add_dependency(lcbn_get(wrapped_req->cb_type_to_lcbn, wrapped_work_type),
                      lcbn_get(req->cb_type_to_lcbn, UV_AFTER_WORK_CB));
  /* AFTER_WORK -> wrapped_done_type */
  lcbn_add_dependency(lcbn_get(req->cb_type_to_lcbn, UV_AFTER_WORK_CB),
                      lcbn_get(wrapped_req->cb_type_to_lcbn, wrapped_done_type));
  return;
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

#ifdef UNIFIED_CALLBACK
  uv__register_callback(req, work_cb, UV_WORK_CB);
  uv__register_callback(req, after_work_cb, UV_AFTER_WORK_CB);
  /* WORK -> AFTER_WORK. */
  lcbn_add_dependency(lcbn_get(req->cb_type_to_lcbn, UV_WORK_CB),
                      lcbn_get(req->cb_type_to_lcbn, UV_AFTER_WORK_CB));
  uv__add_other_dependencies(req);
#endif

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

struct list * uv__ready_work_lcbns (void *wrapper, enum execution_context exec_context)
{
  struct list *ready_work_lcbns;
  uv_work_t *req;
  lcbn_t *lcbn;

  assert(wrapper);
  req = (uv_work_t *) wrapper;
  assert(req->magic == UV_REQ_MAGIC && req->type == UV_WORK);

  ready_work_lcbns = list_create();
  switch (exec_context)
  {
    case EXEC_CONTEXT_THREADPOOL_WORKER:
      /* uv__queue_work
         (All work goes through uv_queue_work) */
      lcbn = lcbn_get(req->cb_type_to_lcbn, UV_WORK_CB);
      assert(lcbn && lcbn->cb == req->work_cb);
      assert(lcbn->cb);
      list_push_back(ready_work_lcbns, &sched_lcbn_create(lcbn)->elem);
      break;
    case EXEC_CONTEXT_THREADPOOL_DONE:
      /* uv__queue_done 
         (All work goes through uv_queue_work) */
      lcbn = lcbn_get(req->cb_type_to_lcbn, UV_AFTER_WORK_CB);
      assert(lcbn && lcbn->cb == req->after_work_cb);
      if (lcbn->cb)
        list_push_back(ready_work_lcbns, &sched_lcbn_create(lcbn)->elem);
      break;
    default:
      assert(!"uv__ready_work_lcbns: Error, unexpected execution_context");
  }
  return ready_work_lcbns;
}
