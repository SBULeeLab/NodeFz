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

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "uv.h"
#include "uv-common.h"
#include "internal.h"


static void uv__getnameinfo_work(struct uv__work* w) {
  uv_getnameinfo_t* req;
  int err;
  socklen_t salen;

  req = container_of(w, uv_getnameinfo_t, work_req);

  if (req->storage.ss_family == AF_INET)
    salen = sizeof(struct sockaddr_in);
  else if (req->storage.ss_family == AF_INET6)
    salen = sizeof(struct sockaddr_in6);
  else
    abort();

  err = getnameinfo((struct sockaddr*) &req->storage,
                    salen,
                    req->host,
                    sizeof(req->host),
                    req->service,
                    sizeof(req->service),
                    req->flags);
  req->retcode = uv__getaddrinfo_translate_error(err);
}

static void uv__getnameinfo_work_wrapper (uv_work_t *req)
{
  uv_getnameinfo_t *name_req;
  name_req = (uv_getnameinfo_t *) req->data;
  invoke_callback_wrap((any_func) uv__getnameinfo_work, UV_GETNAMEINFO_WORK_CB, (long) &name_req->work_req);
}

any_func uv_uv__getnameinfo_work_wrapper_ptr (void)
{
  return (any_func) uv__getnameinfo_work_wrapper;
}

static void uv__getnameinfo_done(struct uv__work* w, int status) {
  uv_getnameinfo_t* req;
  char* host;
  char* service;

  req = container_of(w, uv_getnameinfo_t, work_req);
  uv__req_unregister(req->loop, req);
  host = service = NULL;

  if (status == -ECANCELED) {
    assert(req->retcode == 0);
    req->retcode = UV_EAI_CANCELED;
  } else if (req->retcode == 0) {
    host = req->host;
    service = req->service;
  }

  if (req->getnameinfo_cb)
  {
#ifdef UNIFIED_CALLBACK
    invoke_callback_wrap ((any_func) req->getnameinfo_cb, UV_GETNAMEINFO_CB, (long) req, (long) req->retcode, (long) host, (long) service);
#else
    req->getnameinfo_cb(req, req->retcode, host, service);
#endif
  }
}

#ifdef UNIFIED_CALLBACK
static void uv__getnameinfo_done_wrapper (uv_work_t *req, int status)
{
  uv_getnameinfo_t *name_req;
  name_req = (uv_getnameinfo_t *) req->data;

  uv__getnameinfo_done(&name_req->work_req, status);
  /* TODO Re-enable this.
    uv__free(req); */
}
#endif

/*
* Entry point for getnameinfo
* return 0 if a callback will be made
* return error code if validation fails
*/
int uv_getnameinfo(uv_loop_t* loop,
                   uv_getnameinfo_t* req,
                   uv_getnameinfo_cb getnameinfo_cb,
                   const struct sockaddr* addr,
                   int flags) {
#ifdef UNIFIED_CALLBACK
  uv_work_t *work_req;
#endif

  if (req == NULL || addr == NULL)
    return UV_EINVAL;

  if (addr->sa_family == AF_INET) {
    memcpy(&req->storage,
           addr,
           sizeof(struct sockaddr_in));
  } else if (addr->sa_family == AF_INET6) {
    memcpy(&req->storage,
           addr,
           sizeof(struct sockaddr_in6));
  } else {
    return UV_EINVAL;
  }

  uv__req_init(loop, (uv_req_t*)req, UV_GETNAMEINFO);

  req->getnameinfo_cb = getnameinfo_cb;
  req->flags = flags;
  req->type = UV_GETNAMEINFO;
  req->loop = loop;
  req->retcode = 0;

  if (getnameinfo_cb) {
    work_req = (uv_work_t *) uv__malloc(sizeof *work_req);
    assert(work_req != NULL);
    memset(work_req, 0, sizeof *work_req);
    work_req->data = req;
    uv_queue_work(loop, work_req, uv__getnameinfo_work_wrapper, uv__getnameinfo_done_wrapper);
    return 0;
  } else {
    uv__getnameinfo_work(&req->work_req);
    uv__getnameinfo_done(&req->work_req, 0);
    return req->retcode;
  }
}
