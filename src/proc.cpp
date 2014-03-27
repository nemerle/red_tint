/*
** proc.c - Proc class
**
** See Copyright Notice in mruby.h
*/

#include "mruby.h"
#include "mruby/class.h"
#include "mruby/proc.h"
#include "opcode.h"

static mrb_code call_iseq[] = {
    MKOP_A(OP_CALL, 0),
};

RProc * RProc::create(mrb_state *mrb, mrb_irep *irep)
{
    RProc *p = RProc::alloc(mrb);
    mrb_callinfo *ci = mrb->m_ctx->m_ci;
    p->m_target_class = nullptr;
    if (ci) {
        if (ci->proc)
            p->m_target_class = ci->proc->m_target_class;
        if (!p->m_target_class)
            p->m_target_class = ci->target_class;
    }
    p->body.irep = irep;
    p->env = nullptr;
    mrb_irep_incref(mrb, irep);

    return p;
}

static inline void closure_setup(RProc *p, int nlocals)
{
    mrb_context *ctx = p->m_vm->m_ctx;
    if (!ctx->m_ci->env) {
        REnv * e = REnv::alloc(p->m_vm);
        e->flags = (unsigned int)nlocals;
        e->mid   = ctx->m_ci->mid;
        e->cioff = ctx->m_ci - ctx->cibase;
        e->stack = ctx->m_stack;
        ctx->m_ci->env = e;
    }
    p->env = ctx->m_ci->env;
}

RProc *RProc::new_closure(mrb_state *mrb, mrb_irep *irep)
{
    RProc *p = RProc::create(mrb, irep);

    closure_setup(p, mrb->m_ctx->m_ci->proc->body.irep->nlocals);
    return p;
}

RProc *RProc::create(mrb_state *mrb, mrb_func_t func)
{
    RProc *p = RProc::alloc(mrb);
    p->body.func = func;
    p->flags |= MRB_PROC_CFUNC;
    p->env = nullptr;
    return p;
}

RProc * RProc::new_closure(mrb_state *mrb, mrb_func_t func, int nlocals)
{
    RProc *p = RProc::create(mrb, func);

    closure_setup(p, nlocals);
    return p;
}

static mrb_value mrb_proc_initialize(mrb_state *mrb, mrb_value self)
{
    mrb_value blk;

    mrb_get_args(mrb, "&", &blk);
    if (blk.is_nil()) {
        /* Calling Proc.new without a block is not implemented yet */
        mrb->mrb_raise(E_ARGUMENT_ERROR, "tried to create Proc object without a block");
    }
    else {
        self.ptr<RProc>()->copy_from(blk.ptr<RProc>());
    }
    return self;
}

static mrb_value mrb_proc_init_copy(mrb_state *mrb, mrb_value self)
{
    mrb_value proc = mrb->get_arg<mrb_value>();
    if (mrb_type(proc) != MRB_TT_PROC) {
        mrb->mrb_raise(E_ARGUMENT_ERROR, "not a proc");
    }
    self.ptr<RProc>()->copy_from(proc.ptr<RProc>());
    return self;
}

mrb_value RProc::call_cfunc(mrb_value self)
{
    return (body.func)(m_vm, self);
}

/* 15.2.17.4.2 */
static mrb_value mrb_proc_arity(mrb_state *mrb, mrb_value self)
{
    RProc *p = self.ptr<RProc>();
    mrb_code *iseq = p->ireps()->iseq;
    mrb_aspec aspec = GETARG_Ax(*iseq);
    int ma, ra, pa, arity;

    ma = MRB_ASPEC_REQ(aspec);
    ra = MRB_ASPEC_REST(aspec);
    pa = MRB_ASPEC_POST(aspec);
    arity = ra ? -(ma + pa + 1) : ma + pa;

    return mrb_fixnum_value(arity);
}

/* 15.3.1.2.6  */
/* 15.3.1.3.27 */
/*
 * call-seq:
 *   lambda { |...| block }  -> a_proc
 *
 * Equivalent to <code>Proc.new</code>, except the resulting Proc objects
 * check the number of parameters passed when called.
 */
static mrb_value proc_lambda(mrb_state *mrb, mrb_value self)
{
    mrb_value blk;

    mrb_get_args(mrb, "&", &blk);
    if (blk.is_nil()) {
        mrb->mrb_raise(E_ARGUMENT_ERROR, "tried to create Proc object without a block");
    }
    RProc *p = blk.ptr<RProc>();
    if (!MRB_PROC_STRICT_P(p)) {
        RProc *p2 = mrb->gc().obj_alloc<RProc>(p->c);
        p2->copy_from(p);
        p2->flags |= MRB_PROC_STRICT;
        return mrb_value::wrap(p2);
    }
    return blk;
}

void mrb_init_proc(mrb_state *mrb)
{
    RProc *m;
    mrb_irep *call_irep = (mrb_irep *)mrb->gc()._malloc(sizeof(mrb_irep));
    static constexpr mrb_irep mrb_irep_zero = { 0 };

    if ( call_irep == nullptr )
        return;

    *call_irep = mrb_irep_zero;
    call_irep->flags = MRB_ISEQ_NO_FREE;
    call_irep->iseq = call_iseq;
    call_irep->ilen = 1;

    mrb->proc_class = &mrb->define_class("Proc", mrb->object_class);
    MRB_SET_INSTANCE_TT(mrb->proc_class, MRB_TT_PROC);
    m = RProc::create(mrb, call_irep);
    mrb->proc_class->define_method("initialize", mrb_proc_initialize, MRB_ARGS_NONE())
            .define_method("initialize_copy", mrb_proc_init_copy, MRB_ARGS_REQ(1))
            .define_method("arity", mrb_proc_arity, MRB_ARGS_NONE())
            .define_method_raw(mrb->intern2("call",4), m)
            .define_method_raw(mrb->intern2("[]",2), m);

    mrb->kernel_module->define_class_method("lambda", proc_lambda, MRB_ARGS_NONE())        /* 15.3.1.2.6  */
            .define_method("lambda", proc_lambda, MRB_ARGS_NONE());   /* 15.3.1.3.27 */
}

RProc *RProc::alloc(mrb_state *mrb) {
    return mrb->gc().obj_alloc<RProc>(mrb->proc_class);
}

REnv *REnv::alloc(mrb_state *mrb) {
    return mrb->gc().obj_alloc<REnv>((RClass*)mrb->m_ctx->m_ci->proc->env);
}
