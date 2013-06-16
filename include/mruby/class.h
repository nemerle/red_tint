/*
** mruby/class.h - Class class
**
** See Copyright Notice in mruby.h
*/

#pragma once
#include <cassert>
#include "mrbconf.h"
#include "mruby/value.h"
#include "mruby/proc.h"
#include "mruby/string.h"
#include "mruby/mem_manager.h"
#include "mruby/khash.h"
#define mrb_class_ptr(v)    ((struct RClass*)((v).value.p))
#define RCLASS_SUPER(v)     (((struct RClass*)((v).value.p))->super)
#define RCLASS_IV_TBL(v)    (((struct RClass*)((v).value.p))->iv)
#define RCLASS_M_TBL(v)     (((struct RClass*)((v).value.p))->mt)
#define MRB_SET_INSTANCE_TT(c, tt) c->flags = ((c->flags & ~0xff) | (char)tt)
#define MRB_INSTANCE_TT(c) (enum mrb_vtype)(c->flags & 0xff)

struct RProc;
struct RClass : public RObject {
    typedef kh_T<mrb_sym,RProc*,IntHashFunc,IntHashEq> kh_mt;
    static const mrb_vtype ttype=MRB_TT_CLASS;

    kh_mt *mt;
    RClass *super;
public:
    RClass & instance_tt(int tt) { flags = ((flags & ~0xff) | (char)tt); return *this;}
    //static RProc *method_search_vm(mrb_state *, RClass**, mrb_sym);
    static RProc* method_search_vm(RClass ** cp, mrb_sym mid);
    static RClass *create(mrb_state *mrb, RClass *super);
    static RClass * mrb_class(mrb_state *mrb, mrb_value &v);
    static const RClass * mrb_class(mrb_state *mrb, const mrb_value &v) {
        return const_cast<const RClass *>(mrb_class(mrb,const_cast<mrb_value &>(v)));
        }
    RClass &define_class_method(const char *name, mrb_func_t func, mrb_aspec aspec) {
        define_singleton_method(name, func, aspec);
        return *this;
    }
    RClass &define_method(const char *name, mrb_func_t func, mrb_aspec aspec);
    void define_method_vm(mrb_sym name, mrb_value body);
    void define_method_id(mrb_sym mid, mrb_func_t func, mrb_aspec aspec);
    RClass &define_method_raw(mrb_sym mid, RProc *p);
    RClass &include_module(const char *name);
    RClass &include_module(RClass *m);
    /*!
     * Defines an alias of a method.
     * \param name1  a new name for the method
     * \param name2  the original name of the method
     */
    RClass &define_alias(const char *name1, const char *name2);
    void alias_method(mrb_sym a, mrb_sym b)
    {
        RProc *m = method_search(b);
        define_method_vm(a, mrb_obj_value(m));
    }
    RProc* method_search(mrb_sym mid);
    void fin() const {} // used as a marker in all define_method/define_x sets.

    RClass& undef_method(const char *name);
    RClass& undef_method(mrb_sym a);
    RClass& undef_class_method(const char *name);
    mrb_value const_get(mrb_sym sym);
    void mark_mt(MemManager &mm);
    size_t mark_mt_size() const;
    mrb_bool respond_to(mrb_sym mid) const;
    mrb_value new_instance(int argc, mrb_value *argv);
    void name_class(mrb_sym name);
    void define_const(const char *name, mrb_value v);
    RClass *define_module_under(const char *name);
    RClass *define_class_under(const char *name, RClass *super);
    RClass *outer_module();
    mrb_value class_path();
    const char *class_name();
    void define_module_function(const char *name, mrb_func_t func, mrb_aspec aspec);
    RClass *from_sym(mrb_sym id);
};


RClass* mrb_define_class_id(mrb_state*, mrb_sym, RClass*);
RClass* mrb_define_module_id(mrb_state*, mrb_sym);
RClass *mrb_vm_define_class(mrb_state*, mrb_value, mrb_value, mrb_sym);
RClass *mrb_vm_define_module(mrb_state*, const mrb_value &, mrb_sym);

RClass *mrb_class_outer_module(mrb_state*, RClass *);
RProc *mrb_method_search_vm(mrb_state*, RClass**, mrb_sym);
RProc *mrb_method_search(mrb_state*, RClass*, mrb_sym);

RClass* mrb_class_real(RClass* cl);

void mrb_obj_call_init(mrb_state *mrb, mrb_value obj, int argc, mrb_value *argv);

void mrb_gc_free_mt(mrb_state*, struct RClass*);


