/*
** mruby/class.h - Class class
**
** See Copyright Notice in mruby.h
*/

#pragma once
#include <cassert>
#include "mruby.h"
#include "mruby/value.h"
#include "mruby/proc.h"
#include "mruby/string.h"

#define mrb_class_ptr(v)    ((struct RClass*)((v).value.p))
#define RCLASS_SUPER(v)     (((struct RClass*)((v).value.p))->super)
#define RCLASS_IV_TBL(v)    (((struct RClass*)((v).value.p))->iv)
#define RCLASS_M_TBL(v)     (((struct RClass*)((v).value.p))->mt)
#define MRB_SET_INSTANCE_TT(c, tt) c->flags = ((c->flags & ~0xff) | (char)tt)
#define MRB_INSTANCE_TT(c) (enum mrb_vtype)(c->flags & 0xff)

struct RProc;
struct RClass : public RObject {
    static const mrb_vtype ttype=MRB_TT_CLASS;

    struct kh_mt *mt;
    RClass *super;
#define FORWARD_TO_INSTANCE(name)\
    static mrb_value name(mrb_state *mrb, mrb_value self) {\
        return mrb_class_ptr(self)->name(mrb);\
    }
public:
    RClass & instance_tt(int tt) { flags = ((flags & ~0xff) | (char)tt); return *this;}
    //static RProc *method_search_vm(mrb_state *, RClass**, mrb_sym);
    static RProc* method_search_vm(mrb_state *mrb, RClass ** cp, mrb_sym mid);
    static RClass * mrb_class(mrb_state *mrb, mrb_value v) {
        switch (mrb_type(v)) {
            case MRB_TT_FALSE:
                if (v.value.i)
                    return mrb->false_class;
                return mrb->nil_class;
            case MRB_TT_TRUE:
                return mrb->true_class;
            case MRB_TT_SYMBOL:
                return mrb->symbol_class;
            case MRB_TT_FIXNUM:
                return mrb->fixnum_class;
            case MRB_TT_FLOAT:
                return mrb->float_class;
            default:
                return mrb_obj_ptr(v)->c;
        }
    }
    RClass &define_class_method(mrb_state *mrb, const char *name, mrb_func_t func, mrb_aspec aspec) {
        mrb_define_singleton_method(mrb, this, name, func, aspec);
        return *this;
    }
    RClass &define_method(mrb_state *mrb, const char *name, mrb_func_t func, mrb_aspec aspec) {
        define_method_id(mrb, mrb_intern(mrb, name), func, aspec);
        return *this;
    }
    void define_method_vm(mrb_state *mrb, mrb_sym name, mrb_value body)
    {
        khash_t(mt) *h = this->mt;
        khiter_t k;
        RProc *p;

        if (!h) h = this->mt = kh_init(mt, mrb);
        k = kh_put(mt, h, name);
        p = mrb_proc_ptr(body);
        kh_value(h, k) = p;
        if (p) {
            mrb->gc().mrb_field_write_barrier(this, p);
        }
    }
    void define_method_id(mrb_state *mrb, mrb_sym mid, mrb_func_t func, mrb_aspec aspec);
    RClass &define_method_raw(mrb_state *mrb, mrb_sym mid, RProc *p)
    {
        khash_t(mt) *h = this->mt;
        khiter_t k;

        if (!h) h = this->mt = kh_init(mt, mrb);
        k = kh_put(mt, h, mid);
        kh_value(h, k) = p;
        if (p) {
            mrb->gc().mrb_field_write_barrier(this, p);
        }
        return *this;
    }
    RClass &include_module(mrb_state *mrb, const char *name) {
        return include_module(mrb, mrb->class_get("Enumerable"));
    }
    RClass &include_module(mrb_state *mrb, RClass *m);
    RClass &define_alias(mrb_state *mrb, const char *name1, const char *name2)
    {
        alias_method(mrb, mrb_intern(mrb, name1), mrb_intern(mrb, name2));
        return *this;
    }
    void alias_method(mrb_state *mrb, mrb_sym a, mrb_sym b)
    {
        RProc *m = method_search(mrb, b);

        define_method_vm(mrb, a, mrb_obj_value(m));
    }
    RProc* method_search(mrb_state *mrb, mrb_sym mid)
    {
        RProc *m;
        RClass* found_in = this;
        m = method_search_vm(mrb, &found_in, mid);
        if (!m) {
            mrb_value inspect = mrb_funcall(mrb, mrb_obj_value(found_in), "inspect", 0);
            if (RSTRING_LEN(inspect) > 64) {
                inspect = mrb_any_to_s(mrb, mrb_obj_value(found_in));
            }
            mrb_name_error(mrb, mid, "undefined method '%S' for class %S",
                       mrb_sym2str(mrb, mid), inspect);
        }
        return m;
    }
    void fin() const {} // used as a marker in all define_method/define_x sets.

#undef FORWARD_TO_INSTANCE
    RClass& undef_method(mrb_state *mrb, const char *name);
    RClass& undef_method(mrb_state *mrb, mrb_sym a);
    RClass& undef_class_method(mrb_state *mrb, const char *name);
    mrb_value const_get(mrb_state * mrb, mrb_sym sym);
};


#if defined(__cplusplus)
extern "C" {
#endif
RClass* mrb_define_class_id(mrb_state*, mrb_sym, RClass*);
RClass* mrb_define_module_id(mrb_state*, mrb_sym);
RClass *mrb_vm_define_class(mrb_state*, mrb_value, mrb_value, mrb_sym);
RClass *mrb_vm_define_module(mrb_state*, mrb_value, mrb_sym);

RClass *mrb_class_outer_module(mrb_state*, RClass *);
RProc *mrb_method_search_vm(mrb_state*, RClass**, mrb_sym);
RProc *mrb_method_search(mrb_state*, RClass*, mrb_sym);

RClass* mrb_class_real(RClass* cl);

void mrb_obj_call_init(mrb_state *mrb, mrb_value obj, int argc, mrb_value *argv);

void mrb_gc_mark_mt(mrb_state*, struct RClass*);
size_t mrb_gc_mark_mt_size(mrb_state*, struct RClass*);
void mrb_gc_free_mt(mrb_state*, struct RClass*);

#if defined(__cplusplus)
}  /* extern "C" { */
#endif

