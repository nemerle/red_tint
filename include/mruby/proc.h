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
enum eProcFlags {
    MRB_PROC_CFUNC  = (1<<7),
    MRB_PROC_STRICT = (1<<8)
};
#define MRB_PROC_STRICT_P(p) ((p)->flags & MRB_PROC_STRICT)

struct REnv;
struct RProc : public RBasic {
    static const mrb_vtype ttype=MRB_TT_PROC;
protected:
    union {
        mrb_irep *irep;
        mrb_func_t func;
    } body;
//protected:
public:
    RClass *m_target_class;
    RClass *target_class() {return m_target_class;}
    REnv *env;
    mrb_irep *ireps() {assert(!is_cfunc()); return body.irep;}
    inline bool is_cfunc() const { return (flags & MRB_PROC_CFUNC)!=0;}
    void copy_from(RProc *src) {
        flags = src->flags;
        body  = src->body;
        if (!is_cfunc())
            body.irep->refcnt++;
        m_target_class = src->m_target_class;
        env   = src->env;
    }
    static RProc *alloc(mrb_state *mrb);
    static RProc *copy_construct(mrb_state *mrb,RProc *from) {
        RProc * r = alloc(mrb);
        r->copy_from(from);
        return r;
    }
static  RProc *     create(mrb_state *mrb, mrb_irep *irep);
static  RProc *     create(mrb_state *mrb, mrb_func_t func);
static  RProc *     new_closure(mrb_state *mrb, mrb_irep *irep);
static  RProc *     new_closure(mrb_state *mrb, mrb_func_t func, int nlocals);
        mrb_value   call_cfunc(mrb_value self);
        bool        isWrappedCfunc(mrb_func_t v) const {return body.func==v;}
};
struct REnv : public RBasic {
    static const mrb_vtype ttype=MRB_TT_ENV;
    mrb_value *stack;
    mrb_sym mid;
    int cioff;
    constexpr inline int stackSize() const { return this->flags; }
    static REnv *alloc(mrb_state *mrb);
};
/* implementation of #send method */
mrb_value mrb_f_send(mrb_state *mrb, mrb_value self);
