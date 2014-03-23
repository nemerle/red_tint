/*
** mruby/proc.h - Proc class
**
** See Copyright Notice in mruby.h
*/
#pragma once
#include "mruby/value.h"

#include "mruby/irep.h"
/* aspec access */
#define MRB_ASPEC_REQ(a) (((a) >> 18) & 0x1f)
#define MRB_ASPEC_OPT(a) (((a) >> 13) & 0x1f)
#define MRB_ASPEC_REST(a) ((a) & (1<<12))
#define MRB_ASPEC_POST(a) (((a) >> 7) & 0x1f)
#define MRB_ASPEC_KEY(a) (((a) >> 2) & 0x1f)
#define MRB_ASPEC_KDICT(a) ((a) & (1<<1))
#define MRB_ASPEC_BLOCK(a) ((a) & 1)

#define MRB_PROC_CFUNC (1<<7)
#define MRB_PROC_CFUNC_P(p) ((p)->flags & MRB_PROC_CFUNC)
#define MRB_PROC_STRICT (1<<8)
#define MRB_PROC_STRICT_P(p) ((p)->flags & MRB_PROC_STRICT)

struct REnv;
struct RProc : public RBasic {
    static const mrb_vtype ttype=MRB_TT_PROC;
    union {
        mrb_irep *irep;
        mrb_func_t func;
    } body;
//protected:
    RClass *m_target_class;
public:
    RClass *target_class() {return m_target_class;}
    REnv *env;
    void copy_from(RProc *src) {
        flags = src->flags;
        body  = src->body;
        if (!(this->flags & MRB_PROC_CFUNC)) {
            body.irep->refcnt++;
        };
        m_target_class = src->m_target_class;
        env   = src->env;
    }
    static RProc *alloc(mrb_state *mrb);
    static RProc *copy_construct(mrb_state *mrb,RProc *from) {
        RProc * r = alloc(mrb);
        r->copy_from(from);
        return r;
    }
};
struct REnv : public RBasic {
    mrb_value *stack;
    mrb_sym mid;
    int cioff;
    static REnv *alloc(mrb_state *mrb);
};


#define mrb_proc_ptr(v)    ((RProc*)((v).value.p))

RProc *mrb_proc_new(mrb_state*, mrb_irep*);
RProc *mrb_proc_new_cfunc(mrb_state*, mrb_func_t);
RProc *mrb_closure_new(mrb_state*, mrb_irep*);
RProc *mrb_closure_new_cfunc(mrb_state *mrb, mrb_func_t func, int nlocals);
//void mrb_proc_copy(RProc *a, RProc *b);
/* implementation of #send method */
mrb_value mrb_f_send(mrb_state *mrb, mrb_value self);
