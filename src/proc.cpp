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

RProc * mrb_proc_new(mrb_state *mrb, mrb_irep *irep)
{
    RProc *p = RProc::alloc(mrb);
    p->target_class = (mrb->m_ci) ? mrb->m_ci->target_class : nullptr;
    p->body.irep = irep;
    p->env = nullptr;

    return p;
}

static inline void
closure_setup(mrb_state *mrb, RProc *p, int nlocals)
{

    if (!mrb->m_ci->env) {
        REnv * e = REnv::alloc(mrb);
        e->flags= (unsigned int)nlocals;
        e->mid = mrb->m_ci->mid;
        e->cioff = mrb->m_ci - mrb->cibase;
        e->stack = mrb->m_stack;
        mrb->m_ci->env = e;
    }
    p->env = mrb->m_ci->env;
}

RProc *
mrb_closure_new(mrb_state *mrb, mrb_irep *irep)
{
    RProc *p = mrb_proc_new(mrb, irep);

    closure_setup(mrb, p, mrb->m_ci->proc->body.irep->nlocals);
    return p;
}

RProc *
mrb_proc_new_cfunc(mrb_state *mrb, mrb_func_t func)
{
    RProc *p = RProc::alloc(mrb);
    p->body.func = func;
    p->flags |= MRB_PROC_CFUNC;

    return p;
}

RProc *
mrb_closure_new_cfunc(mrb_state *mrb, mrb_func_t func, int nlocals)
{
    RProc *p = mrb_proc_new_cfunc(mrb, func);

    closure_setup(mrb, p, nlocals);
    return p;
}

static mrb_value
mrb_proc_initialize(mrb_state *mrb, mrb_value self)
{
    mrb_value blk;

    mrb_get_args(mrb, "&", &blk);
    if (mrb_nil_p(blk)) {
        /* Calling Proc.new without a block is not implemented yet */
        mrb_raise(mrb, E_ARGUMENT_ERROR, "tried to create Proc object without a block");
    }
    else {
        mrb_proc_ptr(self)->copy_from(mrb_proc_ptr(blk));
    }
    return self;
}

static mrb_value
mrb_proc_init_copy(mrb_state *mrb, mrb_value self)
{
    mrb_value proc;

    mrb_get_args(mrb, "o", &proc);
    if (mrb_type(proc) != MRB_TT_PROC) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "not a proc");
    }
    mrb_proc_ptr(self)->copy_from(mrb_proc_ptr(proc));
    return self;
}

int
mrb_proc_cfunc_p(RProc *p)
{
    return MRB_PROC_CFUNC_P(p);
}

mrb_value
mrb_proc_call_cfunc(mrb_state *mrb, RProc *p, mrb_value self)
{
    return (p->body.func)(mrb, self);
}

mrb_code*
mrb_proc_iseq(mrb_state *mrb, RProc *p)
{
    return p->body.irep->iseq;
}

/* 15.2.17.4.2 */
static mrb_value mrb_proc_arity(mrb_state *mrb, mrb_value self)
{
  RProc *p = mrb_proc_ptr(self);
  mrb_code *iseq = mrb_proc_iseq(mrb, p);
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
static mrb_value
proc_lambda(mrb_state *mrb, mrb_value self)
{
    mrb_value blk;
    RProc *p;

    mrb_get_args(mrb, "&", &blk);
    if (mrb_nil_p(blk)) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "tried to create Proc object without a block");
    }
    p = mrb_proc_ptr(blk);
    if (!MRB_PROC_STRICT_P(p)) {
        RProc *p2 = mrb->gc().obj_alloc<RProc>(p->c);
        p2->copy_from(p);
        p2->flags |= MRB_PROC_STRICT;
        return mrb_obj_value(p2);
    }
    return blk;
}

void
mrb_init_proc(mrb_state *mrb)
{
    RProc *m;
    mrb_irep *call_irep = (mrb_irep *)mrb->gc().mrb_alloca(sizeof(mrb_irep));
    static constexpr mrb_irep mrb_irep_zero = { 0 };

    if ( call_iseq == nullptr || call_irep == nullptr )
        return;

    *call_irep = mrb_irep_zero;
    call_irep->flags = MRB_ISEQ_NO_FREE;
    call_irep->idx = -1;
    call_irep->iseq = call_iseq;
    call_irep->ilen = 1;

    mrb->proc_class = mrb_define_class(mrb, "Proc", mrb->object_class);
    MRB_SET_INSTANCE_TT(mrb->proc_class, MRB_TT_PROC);
    m = mrb_proc_new(mrb, call_irep);
    mrb->proc_class->define_method(mrb, "initialize", mrb_proc_initialize, MRB_ARGS_NONE())
            .define_method(mrb, "initialize_copy", mrb_proc_init_copy, MRB_ARGS_REQ(1))
            .define_method(mrb, "arity", mrb_proc_arity, MRB_ARGS_NONE())
            .define_method_raw(mrb, mrb_intern(mrb, "call"), m)
            .define_method_raw(mrb, mrb_intern(mrb, "[]"), m);

    mrb->kernel_module->define_class_method(mrb, "lambda", proc_lambda, MRB_ARGS_NONE())        /* 15.3.1.2.6  */
            .define_method(mrb, "lambda", proc_lambda, MRB_ARGS_NONE());   /* 15.3.1.3.27 */
}
