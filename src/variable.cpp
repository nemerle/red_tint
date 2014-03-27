/*
** variable.c - mruby variables
**
** See Copyright Notice in mruby.h
*/

#include <ctype.h>
#include "mruby.h"
#include "mruby/array.h"
#include "mruby/class.h"
#include "mruby/proc.h"
#include "mruby/string.h"
#include "InstanceVariablesTable.h"
namespace {
struct csym_arg {
    RClass *c;
    mrb_sym sym;
};

} // end of anonymous namespace

static int iv_mark_i(mrb_sym sym, mrb_value v, void *p)
{
    if (mrb_type(v) < MRB_TT_OBJECT)
        return 0;
    RBasic *obj = v.basic_ptr();
    obj->m_vm->gc().mark(v.basic_ptr());
    return 0;
}

static void mark_tbl(iv_tbl *t)
{
    if (!t)
        return;
    t->iv_foreach(iv_mark_i, 0);
}

void mrb_gc_mark_gv(mrb_state *mrb)
{
    mark_tbl(mrb->globals);
}

void mrb_gc_free_gv(mrb_state *mrb)
{
    if (mrb->globals) {
        mrb->globals->iv_free();
        mrb->globals = nullptr;
    }
}

void mrb_gc_mark_iv(RObject *obj)
{
    mark_tbl(obj->iv);
}

size_t mrb_gc_mark_iv_size(RObject *obj)
{
    return obj->iv->iv_size();
}

void mrb_gc_free_iv(RObject *obj)
{
    if (obj->iv) {
        obj->iv->iv_free();
        obj->iv = nullptr;
    }
}

mrb_value mrb_vm_special_get(mrb_state *mrb, mrb_sym i)
{
    return mrb_fixnum_value(0);
}

void mrb_vm_special_set(mrb_state *mrb, mrb_sym i, mrb_value v)
{
}

bool mrb_value::hasInstanceVariables() const
{
    switch (tt) {
    //TODO: add MRB_TT_FIBER here ?
    case MRB_TT_OBJECT:
    case MRB_TT_CLASS:
    case MRB_TT_MODULE:
    case MRB_TT_SCLASS:
    case MRB_TT_HASH:
    case MRB_TT_DATA:
        return true;
    default:
        return false;
    }
}

mrb_value RObject::iv_get(mrb_sym sym)
{
    mrb_value v;

    if (this->iv && this->iv->iv_get(sym, v))
        return v;
    return mrb_value::nil();
}

mrb_value mrb_value::mrb_iv_get(mrb_sym sym) const
{
    if (hasInstanceVariables()) {
        return object_ptr()->iv_get(sym);
    }
    return mrb_value::nil();
}

void RObject::iv_set(mrb_sym sym, const mrb_value &v)
{
    if (!iv) {
        iv = iv_tbl::iv_new(m_vm->gc());
    }
    m_vm->gc().mrb_write_barrier(this);
    iv->iv_put(sym, v);
}

void RObject::iv_ifnone(mrb_sym sym, mrb_value v)
{
    iv_tbl *t = this->iv;

    if (!t) {
        t = this->iv = iv_tbl::iv_new(m_vm->gc());
    }
    else if (t->iv_get(sym, v)) {
        return;
    }
    m_vm->gc().mrb_write_barrier(this);
    t->iv_put(sym, v);
}

void mrb_iv_set(mrb_state *mrb, mrb_value obj, mrb_sym sym, const mrb_value &v)
{
    if (obj.hasInstanceVariables()) {
        obj.object_ptr()->iv_set(sym, v);
    }
    else {
        mrb->mrb_raise(E_ARGUMENT_ERROR, "cannot set instance variable");
    }
}

bool RObject::iv_defined(mrb_sym sym)
{
    iv_tbl *t = this->iv;
    if (t) {
        return t->iv_get(sym);
    }
    return false;
}

mrb_bool mrb_iv_defined(mrb_value obj, mrb_sym sym)
{
    if (obj.hasInstanceVariables())
        return false;
    return obj.object_ptr()->iv_defined(sym);
}

void mrb_iv_copy(mrb_value dest, mrb_value src)
{
    RObject *d = dest.object_ptr();
    RObject *s = src.object_ptr();

    if (d->iv) {
        d->iv->iv_free();
        d->iv = nullptr;
    }
    if (s->iv) {
        d->iv = s->iv->iv_copy();
    }
}

static int inspect_i(mrb_sym sym, mrb_value v, void *p)
{
    RString *p_str = (RString *)p;

    /* need not to show internal data */
    if (p_str->m_ptr[0] == '-') { /* first element */
        p_str->m_ptr[0] = '#';
        p_str->str_buf_cat(" ",1);
    }
    else {
        p_str->str_buf_cat(", ",2);
    }
    size_t len;
    const char *s = mrb_sym2name_len(p_str->m_vm, sym, len);
    p_str->str_cat(s,len);
    p_str->str_buf_cat("=",1);
    RString * ins;
    if (mrb_type(v) == MRB_TT_OBJECT) {
        ins = mrb_any_to_s(p_str->m_vm, v).ptr<RString>();
    }
    else {
        ins = mrb_inspect(p_str->m_vm, v);
    }
    p_str->str_cat(ins);
    return 0;
}

mrb_value RObject::iv_inspect()
{
    iv_tbl *t = this->iv;
    size_t len = t->iv_size();
    mrb_value wrapped_self = mrb_value::wrap(this);
    if (len > 0) {
        const char *cn = mrb_obj_classname(m_vm, wrapped_self);
        RString *res = RString::create(m_vm,30);
        res->str_buf_cat("-<", 2);
        res->str_buf_cat(cn);
        res->str_buf_cat(":",1);
        res->str_cat(mrb_ptr_to_str(m_vm, this));

        t->iv_foreach(inspect_i, res);
        res->str_buf_cat(">",1);
        return res->wrap();
    }
    return mrb_any_to_s(m_vm, wrapped_self);
}

mrb_value mrb_iv_remove(mrb_value obj, mrb_sym sym)
{
    if (!obj.hasInstanceVariables())
        return mrb_value::undef();
    iv_tbl *t = obj.object_ptr()->iv;
    mrb_value val;

    if (t && t->iv_del(sym, &val)) {
        return val;
    }
    return mrb_value::undef();

}

mrb_value mrb_state::vm_iv_get(mrb_sym sym)
{
    /* get self */
    return m_ctx->m_stack[0].mrb_iv_get(sym);
}

void mrb_state::vm_iv_set(mrb_sym sym, const mrb_value &v)
{
    /* get self */
    mrb_value &obj(m_ctx->m_stack[0]);
    if (obj.hasInstanceVariables()) {
        obj.object_ptr()->iv_set(sym, v);
    }
    else {
        mrb_raise(I_ARGUMENT_ERROR, "cannot set instance variable");
    }
}

static int iv_i(mrb_sym sym, mrb_value v, void *p)
{
    const char* s;
    size_t len;

    RArray *tgt_array = (RArray *)p;
    s = mrb_sym2name_len(tgt_array->m_vm, sym, len);
    if (len > 1 && s[0] == '@' && s[1] != '@') {
        tgt_array->push(mrb_symbol_value(sym));
    }
    return 0;
}

/* 15.3.1.3.23 */
/*
 *  call-seq:
 *     obj.instance_variables    -> array
 *
 *  Returns an array of instance variable names for the receiver. Note
 *  that simply defining an accessor does not create the corresponding
 *  instance variable.
 *
 *     class Fred
 *       attr_accessor :a1
 *       def initialize
 *         @iv = 3
 *       end
 *     end
 *     Fred.new.instance_variables   #=> [:@iv]
 */
mrb_value mrb_obj_instance_variables(mrb_state *mrb, mrb_value self)
{
    RArray *ary = RArray::create(mrb);
    if (self.hasInstanceVariables() && self.object_ptr()->iv) {
        self.object_ptr()->iv->iv_foreach(iv_i, ary);
    }
    return mrb_value::wrap(ary);
}

static int cv_i(mrb_sym sym, mrb_value /*v*/, void *p)
{
    const char* s;
    size_t len;

    RArray *arr = (RArray *)p;
    s = mrb_sym2name_len(arr->m_vm, sym, len);
    if (len > 2 && s[0] == '@' && s[1] == '@') {
        arr->push(mrb_symbol_value(sym));
    }
    return 0;
}

/* 15.2.2.4.19 */
/*
 *  call-seq:
 *     mod.class_variables   -> array
 *
 *  Returns an array of the names of class variables in <i>mod</i>.
 *
 *     class One
 *       @@var1 = 1
 *     end
 *     class Two < One
 *       @@var2 = 2
 *     end
 *     One.class_variables   #=> [:@@var1]
 *     Two.class_variables   #=> [:@@var2,:@@var1]
 */
mrb_value mrb_mod_class_variables(mrb_state *mrb, mrb_value mod)
{
    RArray *arr = RArray::create(mrb);
    RClass *c = mod.ptr<RClass>();
    while (c) {
        if (c->iv) {
            c->iv->iv_foreach(cv_i, arr);
        }
        c = c->super;
    }
    return mrb_value::wrap(arr);
}

mrb_value RClass::mrb_mod_cv_get(mrb_sym sym)
{
    RClass * cls = this;

    while (cls) {
        if (cls->iv) {
            iv_tbl *t = cls->iv;
            mrb_value v;

            if (t->iv_get(sym, v))
                return v;
        }
        cls = cls->super;
    }
    mrb_name_error(m_vm, sym, "uninitialized class variable %S in %S", mrb_sym2str(m_vm, sym), mrb_value::wrap(this));
    /* not reached */
    return mrb_value::nil();
}

void RClass::mrb_mod_cv_set(mrb_sym sym, const mrb_value &v)
{
    for(RClass * cls = this; cls; cls=cls->super) {
        if (!cls->iv)
            continue;
        iv_tbl *t = cls->iv;

        if (t->iv_get(sym)) {
            m_vm->gc().mrb_write_barrier(cls);
            t->iv_put(sym, v);
            return;
        }
    }

    if (!this->iv) {
        this->iv = iv_tbl::iv_new(m_vm->gc());
    }

    m_vm->gc().mrb_write_barrier(this);
    this->iv->iv_put(sym, v);
}

bool RClass::mrb_mod_cv_defined(mrb_sym sym)
{
    RClass *c = this;
    while (c) {
        if (c->iv) {
            iv_tbl *t = c->iv;
            if (t->iv_get(sym))
                return true;
        }
        c = c->super;
    }
    return false;
}

mrb_value mrb_state::vm_cv_get(mrb_sym sym)
{
    RClass *c = m_ctx->m_ci->proc->m_target_class;

    if (!c)
        c = m_ctx->m_ci->target_class;
    return c->mrb_mod_cv_get(sym);
}

void mrb_state::vm_cv_set(mrb_sym sym, const mrb_value &v)
{
    RClass *c = m_ctx->m_ci->proc->m_target_class;

    if (!c)
        c = m_ctx->m_ci->target_class;
    c->mrb_mod_cv_set(sym, v);
}

bool mrb_const_defined(const mrb_value &mod, mrb_sym sym)
{
    const RClass *m = mod.ptr<RClass>();
    const iv_tbl *t = m->iv;

    return t ? t->iv_get(sym) : false;
}

static void mod_const_check(mrb_state *mrb, const mrb_value &mod)
{
    switch (mrb_type(mod)) {
    case MRB_TT_CLASS:
    case MRB_TT_MODULE:
    case MRB_TT_SCLASS:
        break;
    default:
        mrb->mrb_raise(E_TYPE_ERROR, "constant look-up for non class/module");
        break;
    }
}

mrb_value RClass::const_get(mrb_sym sym)
{
    RClass *c = this;
    mrb_value v;
    iv_tbl *t;
    mrb_bool retry = 0;
    mrb_value name;

L_RETRY:
    while (c) {
        if (c->iv) {
            t = c->iv;
            if (t->iv_get(sym, v))
                return v;
        }
        c = c->super;
    }
    if (!retry && this && this->tt == MRB_TT_MODULE) {
        c = m_vm->object_class;
        retry = 1;
        goto L_RETRY;
    }
    name = mrb_symbol_value(sym);
    return mrb_funcall_argv(m_vm, mrb_value::wrap(this), m_vm->intern2("const_missing", 13), 1, &name);
}

mrb_value mrb_state::const_get(const mrb_value &mod, mrb_sym sym)
{
    mod_const_check(this, mod);
    return mod.ptr<RClass>()->const_get(sym);
}
void mrb_state::const_set(mrb_value mod, mrb_sym sym, const mrb_value &v)
{
    mod_const_check(this, mod);
    mrb_iv_set(this, mod, sym, v);
}

mrb_value mrb_state::mrb_vm_const_get(mrb_sym sym)
{
    RClass *c = m_ctx->m_ci->proc->m_target_class;

    if (!c)
        c = m_ctx->m_ci->target_class;
    if (c) {
        mrb_value v;

        if (c->iv && c->iv->iv_get(sym, v)) {
            return v;
        }
        for (RClass *c2 = c->outer_module(); c2; c2 = c2->outer_module()) {
            if (c2->iv && c2->iv->iv_get(sym, v)) {
                return v;
            }
        }
    }
    if(c)
        return c->const_get(sym);
    return mrb_value::nil();
}


void mrb_vm_const_set(mrb_state *mrb, mrb_sym sym, mrb_value v)
{
    RClass *c = mrb->m_ctx->m_ci->proc->m_target_class;

    if (!c)
        c = mrb->m_ctx->m_ci->target_class;
    c->iv_set(sym, v);
}

//void mrb_const_remove(mrb_state *mrb, mrb_value mod, mrb_sym sym)
//{
//    mod_const_check(mrb, mod);
//    mrb_iv_remove(mod, sym);
//}

RClass& RClass::define_const(const char *name, mrb_value v)
{
    iv_set(m_vm->intern_cstr(name), v);
    return *this;
}
void mrb_state::define_global_const(const char *name, RBasic *val)
{
    object_class->define_const(name, val->wrap());
}

void mrb_state::define_global_const(const char *name, mrb_value val)
{
    object_class->define_const(name, val);
}
static int const_i(mrb_sym sym, mrb_value v, void *p)
{
    size_t len;

    RArray *arr = (RArray *)p;
    const char* s = mrb_sym2name_len(arr->m_vm, sym, len);
    if (len >= 1 && ISUPPER(s[0])) {
        arr->push(mrb_symbol_value(sym));
    }
    return 0;
}

/* 15.2.2.4.24 */
/*
 *  call-seq:
 *     mod.constants    -> array
 *
 *  Returns an array of all names of contants defined in the receiver.
 */
mrb_value mrb_mod_constants(mrb_state *mrb, mrb_value mod)
{
    RArray *arr = RArray::create(mrb);
    RClass *c = mod.ptr<RClass>();
    while (c) {
        if (c->iv) {
            c->iv->iv_foreach(const_i, arr);
        }
        c = c->super;
        if (c == mrb->object_class)
            break;
    }
    return mrb_value::wrap(arr);
}
mrb_value mrb_state::mrb_gv_get(mrb_sym sym)
{
    mrb_value v;

    if (!globals) {
        return mrb_value::nil();
    }
    if (globals->iv_get(sym, v))
        return v;
    return mrb_value::nil();
}

void mrb_state::gv_set(mrb_sym sym, mrb_value v)
{
    if (!globals)
        globals = iv_tbl::iv_new(gc());
    globals->iv_put(sym, v);
}
void mrb_state::gv_remove(mrb_sym sym)
{
    if (!globals) {
        return;
    }
    globals->iv_del(sym,NULL);
}

static int gv_i_arr(mrb_sym sym, mrb_value v, void *p)
{
    RArray* ary = (RArray*)p;
    ary->push(mrb_symbol_value(sym));
    return 0;
}

/* 15.3.1.2.4  */
/* 15.3.1.3.14 */
/*
 *  call-seq:
 *     global_variables    -> array
 *
 *  Returns an array of the names of global variables.
 *
 *     global_variables.grep /std/   #=> [:$stdin, :$stdout, :$stderr]
 */
mrb_value mrb_f_global_variables(mrb_state *mrb, mrb_value self)
{
    iv_tbl *t = mrb->globals;
    RArray *arr = RArray::create(mrb);
    size_t i;
    char buf[3];

    if (t) {
        t->iv_foreach(gv_i_arr, arr);
    }
    buf[0] = '$';
    buf[2] = 0;
    for (i = 1; i <= 9; ++i) {
        buf[1] = (char)(i + '0');
        arr->push(mrb_symbol_value(mrb_intern(mrb, buf, 2)));
    }
    return mrb_value::wrap(arr);
}

bool RClass::const_defined_at(mrb_sym id)
{
    //return mrb_const_defined_0(klass, id, true, false);
    RClass *klass = this;
    constexpr bool exclude = true;
    constexpr bool recurse = false;

    const RClass *  obj_class   = m_vm->object_class;
    const RClass *  tmp         = this;
    mrb_bool mod_retry = 0;

    retry:
    while (tmp) {
        if (tmp->iv && tmp->iv->iv_get(id)) {
            return true;
        }
        if (!recurse && (klass != obj_class)) break;
        tmp = tmp->super;
    }
    if (!exclude && !mod_retry && (klass->tt == MRB_TT_MODULE)) {
        mod_retry = 1;
        tmp = obj_class;
        goto retry;
    }
    return false;
}

mrb_value mrb_attr_get(const mrb_value &obj, mrb_sym id)
{
    return obj.mrb_iv_get(id);
}

static int csym_i(mrb_sym sym, mrb_value v, void *p)
{
    csym_arg *a = (csym_arg*)p;
    RClass *c = a->c;

    if (mrb_type(v) == c->tt && v.ptr<RClass>() == c) {
        a->sym = sym;
        return 1;     /* stop iteration */
    }
    return 0;
}

mrb_sym mrb_class_sym(mrb_state *mrb, RClass *c, RClass *outer)
{
    mrb_value name = c->iv_get(mrb->intern2("__classid__", 11));
    if (!name.is_nil())
        return mrb_symbol(name);
    if (!outer)
        return 0;
    csym_arg arg {c,0};
    outer->iv->iv_foreach(csym_i, &arg);
    return arg.sym;
}
