#include "mruby.h"
#include "mruby/array.h"
#include "mruby/class.h"
#include "mruby/proc.h"

#define FIBER_STACK_INIT_SIZE 64
#define FIBER_CI_INIT_SIZE 8
/* mark return from context modifying method */
#define MARK_CONTEXT_MODIFY(c) (c)->m_ci->target_class = NULL
/*
* call-seq:
* Fiber.new{...} -> obj
*
* Creates a fiber, whose execution is suspend until it is explicitly
* resumed using <code>Fiber#resume</code> method.
* The code running inside the fiber can give up control by calling
* <code>Fiber.yield</code> in which case it yields control back to caller
* (the caller of the <code>Fiber#resume</code>).
*
* Upon yielding or termination the Fiber returns the value of the last
* executed expression
*
* For instance:
*
* fiber = Fiber.new do
* Fiber.yield 1
* 2
* end
*
* puts fiber.resume
* puts fiber.resume
* puts fiber.resume
*
* <em>produces</em>
*
* 1
* 2
* resuming dead fiber (RuntimeError)
*
* The <code>Fiber#resume</code> method accepts an arbitrary number of
* parameters, if it is the first call to <code>resume</code> then they
* will be passed as block arguments. Otherwise they will be the return
* value of the call to <code>Fiber.yield</code>
*
* Example:
*
* fiber = Fiber.new do |first|
* second = Fiber.yield first + 2
* end
*
* puts fiber.resume 10
* puts fiber.resume 14
* puts fiber.resume 18
*
* <em>produces</em>
*
* 12
* 14
* resuming dead fiber (RuntimeError)
*
*/
static mrb_value fiber_init(mrb_state *mrb, mrb_value self)
{
    static const struct mrb_context mrb_context_zero = { 0 };
    RFiber *f = (RFiber*)self.value.p;
    mrb_context *c;
    RProc *p;
    mrb_callinfo *ci;
    mrb_value blk;

    mrb_get_args(mrb, "&", &blk);

    if (blk.is_nil()) {
        mrb_raise(E_ARGUMENT_ERROR, "tried to create Fiber object without a block");
    }
    p = mrb_proc_ptr(blk);
    if (MRB_PROC_CFUNC_P(p)) {
        mrb_raise(E_ARGUMENT_ERROR, "tried to create Fiber from C defined method");
    }

    f->cxt = (mrb_context*)mrb->gc()._malloc(sizeof(mrb_context));
    *f->cxt = mrb_context_zero;
    c = f->cxt;

    /* initialize VM stack */
    c->m_stbase = (mrb_value *)mrb->gc()._calloc(FIBER_STACK_INIT_SIZE, sizeof(mrb_value));
    c->stend = c->m_stbase + FIBER_STACK_INIT_SIZE;
    c->m_stack = c->m_stbase;

    /* copy receiver from a block */
    c->m_stack[0] = mrb->m_ctx->m_stack[0];

    /* initialize callinfo stack */
    c->cibase = (mrb_callinfo *)mrb->gc()._calloc(FIBER_CI_INIT_SIZE, sizeof(mrb_callinfo));
    c->ciend = c->cibase + FIBER_CI_INIT_SIZE;
    c->m_ci = c->cibase;
    c->m_ci->stackent = c->m_stack;

    /* adjust return callinfo */
    ci = c->m_ci;
    ci->target_class = p->m_target_class;
    ci->proc = p;
    ci->pc = p->body.irep->iseq;
    ci->nregs = p->body.irep->nregs;
    ci[1] = ci[0];
    c->m_ci++;                      /* push dummy callinfo */
    c->fib = f;
    c->status = MRB_FIBER_CREATED;

    return self;
}

static mrb_context* fiber_check(mrb_state *mrb, mrb_value fib)
{
    RFiber *f = (RFiber*)fib.value.p;

    if (!f->cxt) {
        mrb_raise(E_ARGUMENT_ERROR, "uninitialized Fiber");
    }
    return f->cxt;
}

static mrb_value fiber_result(mrb_state *mrb, mrb_value *a, int len)
{
    if (len == 0)
        return mrb_nil_value();
    if (len == 1)
        return a[0];
    return RArray::new_from_values(mrb, len, a);
}



/*
 *  call-seq:
 *     fiber.resume(args, ...) -> obj
 *
 *  Resumes the fiber from the point at which the last <code>Fiber.yield</code>
 *  was called, or starts running it if it is the first call to
 *  <code>resume</code>. Arguments passed to resume will be the value of
 *  the <code>Fiber.yield</code> expression or will be passed as block
 *  parameters to the fiber's block if this is the first <code>resume</code>.
 *
 *  Alternatively, when resume is called it evaluates to the arguments passed
 *  to the next <code>Fiber.yield</code> statement inside the fiber's block
 *  or to the block value if it runs to completion without any
 *  <code>Fiber.yield</code>
 */
static mrb_value fiber_resume(mrb_state *mrb, mrb_value self)
{
    mrb_context *c = fiber_check(mrb, self);
    mrb_value *a;
    int len;
    mrb_callinfo *ci;

    for (ci = c->m_ci; ci >= c->cibase; ci--) {
        if (ci->acc < 0) {
            mrb->mrb_raise(E_ARGUMENT_ERROR, "can't cross C function boundary");
        }
    }
    if (c->status == MRB_FIBER_RESUMED) {
        mrb->mrb_raise(E_RUNTIME_ERROR, "double resume");
    }
    if (c->status == MRB_FIBER_TERMINATED) {
        mrb->mrb_raise(E_RUNTIME_ERROR, "resuming dead fiber");
    }
    mrb_get_args(mrb, "*", &a, &len);
    mrb->m_ctx->status = MRB_FIBER_RESUMED;

    if (c->status == MRB_FIBER_CREATED) {
        mrb_value *b = c->m_stack+1;
        mrb_value *e = b + len;

        while (b<e) {
            *b++ = *a++;
        }
        c->cibase->argc = len;
        c->prev = mrb->m_ctx;
        if (c->prev->fib)
            mrb->gc().mrb_field_write_barrier(c->fib, c->prev->fib);
        mrb->gc().mrb_write_barrier(c->fib);
        c->status = MRB_FIBER_RUNNING;
        mrb->m_ctx = c;
        MARK_CONTEXT_MODIFY(c);
        return c->m_ci->proc->env->stack[0];
    }
    MARK_CONTEXT_MODIFY(c);
    c->prev = mrb->m_ctx;
    if(c->prev->fib)
        mrb->gc().mrb_field_write_barrier(c->fib, c->prev->fib);
    mrb->gc().mrb_write_barrier(c->fib);
    c->status = MRB_FIBER_RUNNING;
    mrb->m_ctx = c;
    return fiber_result(mrb, a, len);
}

/*
 *  call-seq:
 *     fiber.alive? -> true or false
 *
 *  Returns true if the fiber can still be resumed. After finishing
 *  execution of the fiber block this method will always return false.
 */
static mrb_value fiber_alive_p(mrb_state *mrb, mrb_value self)
{
    struct mrb_context *c = fiber_check(mrb, self);
    return mrb_bool_value(c->status != MRB_FIBER_TERMINATED);
}

/*
 *  call-seq:
 *     Fiber.yield(args, ...) -> obj
 *
 *  Yields control back to the context that resumed the fiber, passing
 *  along any arguments that were passed to it. The fiber will resume
 *  processing at this point when <code>resume</code> is called next.
 *  Any arguments passed to the next <code>resume</code> will be the
 *  value that this <code>Fiber.yield</code> expression evaluates to.
 */
static mrb_value fiber_yield(mrb_state *mrb, mrb_value self)
{
    struct mrb_context *c = mrb->m_ctx;
    mrb_callinfo *ci;
    mrb_value *a;
    int len;
    for (ci = c->m_ci; ci >= c->cibase; ci--) {
        if (ci->acc < 0) {
            mrb->mrb_raise(E_ARGUMENT_ERROR, "can't cross C function boundary");
        }
    }
    if (!c->prev) {
        mrb->mrb_raise(E_ARGUMENT_ERROR, "can't yield from root fiber");
    }
    mrb_get_args(mrb, "*", &a, &len);
    c->prev->status = MRB_FIBER_RUNNING;
    mrb->m_ctx = c->prev;
    c->prev = NULL;
    MARK_CONTEXT_MODIFY(mrb->m_ctx);
    return fiber_result(mrb, a, len);
}
/*
 *  call-seq:
 *     Fiber.current() -> fiber
 *
 *  Returns the current fiber. You need to <code>require 'fiber'</code>
 *  before using this method. If you are not running in the context of
 *  a fiber this method will return the root fiber.
 */
static mrb_value
fiber_current(mrb_state *mrb, mrb_value self)
{
    if (!mrb->m_ctx->fib) {
        RFiber *f = mrb->gc().obj_alloc<RFiber>(MRB_TT_FIBER, mrb_class_ptr(self));

        f->cxt = mrb->m_ctx;
        mrb->m_ctx->fib = f;
    }
    return mrb_obj_value(mrb->m_ctx->fib);
}
void
mrb_mruby_fiber_gem_init(mrb_state* mrb)
{
    RClass &c = mrb->define_class("Fiber", mrb->object_class)
            .define_method("initialize", fiber_init, MRB_ARGS_NONE())
            .define_method("resume", fiber_resume, MRB_ARGS_ANY())
            .define_method("alive?", fiber_alive_p, MRB_ARGS_NONE())
            .define_class_method("yield", fiber_yield, MRB_ARGS_ANY())
            .define_class_method("current", fiber_current, MRB_ARGS_NONE())
            ;
    MRB_SET_INSTANCE_TT((&c), MRB_TT_FIBER);
}

void
mrb_mruby_fiber_gem_final(mrb_state* mrb)
{
}
