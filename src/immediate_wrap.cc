#include "async-wrap.h"
#include "async-wrap-inl.h"
#include "env.h"
#include "env-inl.h"
#include "handle_wrap.h"
#include "util.h"
#include "util-inl.h"

#include <stdint.h>

namespace node {

using v8::Context;
using v8::Function;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::HandleScope;
using v8::Integer;
using v8::Local;
using v8::Object;
using v8::Value;

/* JD: Implementation of 'immediates', an abstraction over libuv to allow
       setImmediate to work as documented.

       We use a one-shot uv_check_t because it goes off immediately after poll.
       As active check handles won't short-circuit poll, we add a dummy uv_idle_t. */

class Immediate : public HandleWrap {
 public:
  static void Initialize(Local<Object> target,
                         Local<Value> unused,
                         Local<Context> context) {
    Environment* env = Environment::GetCurrent(context);
    Local<FunctionTemplate> constructor = env->NewFunctionTemplate(New);
    constructor->InstanceTemplate()->SetInternalFieldCount(1);
    constructor->SetClassName(FIXED_ONE_BYTE_STRING(env->isolate(), "Immediate"));

    env->SetProtoMethod(constructor, "close", HandleWrap::Close);
    env->SetProtoMethod(constructor, "ref", HandleWrap::Ref);
    env->SetProtoMethod(constructor, "unref", HandleWrap::Unref);

    env->SetProtoMethod(constructor, "start", Start);
    env->SetProtoMethod(constructor, "stop", Stop);

    target->Set(FIXED_ONE_BYTE_STRING(env->isolate(), "Immediate"),
                constructor->GetFunction());
  }

  size_t self_size() const override { return sizeof(*this); }

 private:
  static void New(const FunctionCallbackInfo<Value>& args) {
    // This constructor should not be exposed to public javascript.
    // Therefore we assert that we are not trying to call this as a
    // normal function.
    CHECK(args.IsConstructCall());
    Environment* env = Environment::GetCurrent(args);
    new Immediate(env, args.This());
  }

  Immediate(Environment* env, Local<Object> object)
      : HandleWrap(env,
                   object,
                   reinterpret_cast<uv_handle_t*>(&check_handle_),
                   AsyncWrap::PROVIDER_TIMERWRAP) {
    int r = 0;
    r = uv_check_init(env->event_loop(), &check_handle_);
    CHECK_EQ(r, 0);
    r = uv_idle_init(env->event_loop(), &idle_handle_);
    CHECK_EQ(r, 0);
    active_ = 0;
  }

  static void Start(const FunctionCallbackInfo<Value>& args) {
    Immediate* wrap = Unwrap<Immediate>(args.Holder());

    CHECK(HandleWrap::IsAlive(wrap));

    int err = uv_check_start(&wrap->check_handle_, OnCallback);
    // Idle handle is needed only to stop the event loop from blocking in poll.
    uv_idle_start(&wrap->idle_handle_, IdleImmediateDummy);
    args.GetReturnValue().Set(err);

    wrap->active_ = 1;
  }

  static void Stop(const FunctionCallbackInfo<Value>& args) {
    Immediate* wrap = Unwrap<Immediate>(args.Holder());

    if (!wrap->active_)
      return;

    CHECK(HandleWrap::IsAlive(wrap));

    int err = uv_check_stop(&wrap->check_handle_);
    uv_idle_stop(&wrap->idle_handle_);
    args.GetReturnValue().Set(err);
  }

  static void OnCallback(uv_check_t* handle) {
    Immediate* wrap = static_cast<Immediate*>(handle->data);
    Environment* env = wrap->env();
    HandleScope handle_scope(env->isolate());
    Context::Scope context_scope(env->context());
    wrap->MakeCallback(env->oncheckcb_string(), 0, nullptr);

    uv_check_stop(&wrap->check_handle_);
    uv_idle_stop(&wrap->idle_handle_);
    wrap->active_ = 0;
  }

  static void IdleImmediateDummy(uv_idle_t* handle) {
    // Do nothing. Only for maintaining event loop.
  }

  int active_;
  uv_check_t check_handle_;
  uv_idle_t  idle_handle_;
};


}  // namespace node

NODE_MODULE_CONTEXT_AWARE_BUILTIN(immediate_wrap, node::Immediate::Initialize)
