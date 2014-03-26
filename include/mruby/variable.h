/*
** mruby/variable.h - mruby variables
**
** See Copyright Notice in mruby.h
*/

#ifndef MRUBY_VARIABLE_H
#define MRUBY_VARIABLE_H
struct RObject;
typedef struct global_variable {
    int   counter;
    mrb_value *data;
    mrb_value (*getter)(void);
    void  (*setter)(void);
    //void  (*marker)();
    //int block_trace;
    //struct trace_var *trace;
} global_variable;

struct global_entry {
    global_variable *var;
    mrb_sym id;
};

mrb_value mrb_vm_special_get(mrb_state*, mrb_sym);
void mrb_vm_special_set(mrb_state*, mrb_sym, mrb_value);

void mrb_vm_const_set(mrb_state*, mrb_sym, mrb_value);
bool mrb_const_defined(const mrb_value &, mrb_sym);
//void mrb_const_remove(mrb_state*, mrb_value, mrb_sym);

void mrb_iv_set(mrb_state *mrb, mrb_value obj, mrb_sym sym, const mrb_value &v);
mrb_bool mrb_iv_defined(mrb_value, mrb_sym);
mrb_value mrb_iv_remove(mrb_value obj, mrb_sym sym);
void mrb_iv_copy(mrb_value dst, mrb_value src);
mrb_value mrb_mod_constants(mrb_state *mrb, mrb_value mod);
mrb_value mrb_f_global_variables(mrb_state *mrb, mrb_value self);
mrb_value mrb_obj_instance_variables(mrb_state*, mrb_value);
mrb_sym mrb_class_sym(mrb_state *mrb, struct RClass *c, struct RClass *outer);
mrb_value mrb_mod_class_variables(mrb_state*, mrb_value);

/* GC functions */
void mrb_gc_mark_gv(mrb_state*);
void mrb_gc_free_gv(mrb_state*);
void mrb_gc_mark_iv(RObject*);
size_t mrb_gc_mark_iv_size(RObject*);
void mrb_gc_free_iv(RObject*);

#endif  /* MRUBY_VARIABLE_H */
