/*
** class.c - Class class
**
** See Copyright Notice in mruby.h
*/

#include "mruby.h"
#include <stdarg.h>
#include <ctype.h>
#include "mruby/array.h"
#include "mruby/class.h"
#include "mruby/numeric.h"
#include "mruby/proc.h"
#include "mruby/string.h"
#include "mruby/variable.h"
#include "mruby/khash.h"
extern mrb_value class_instance_method_list(mrb_state*, mrb_bool, RClass*, int);
typedef kh_T<mrb_sym,RProc*,IntHashFunc,IntHashEq> kh_mt;

void RClass::mark_mt(MemManager &mm)
{
    if (!mt)
        return;
    for (khiter_t k = mt->begin(); k != mt->end(); k++) {
        if ( !mt->exist(k))
            continue;
        RProc *m = mt->value(k);
        if (m)
            mm.mark(m);
    }
}

size_t RClass::mark_mt_size() const
{
    if (!mt)
        return 0;
    return mt->size();
}

void mrb_gc_free_mt(mrb_state *mrb, RClass *c)
{
    c->mt->destroy();
}

void RClass::name_class(mrb_sym name)
{
    iv_set(m_vm->intern2("__classid__", 11), mrb_symbol_value(name));
}

#define make_metaclass(c) prepare_singleton_class((RBasic*)(c))

static void prepare_singleton_class(RBasic *o)
{
    RClass *sc, *c;

    mrb_state *mrb = o->m_vm;
    assert(o->m_vm);
    if (o->c->tt == MRB_TT_SCLASS)
        return;
    sc = mrb->gc().obj_alloc<RClass>(MRB_TT_SCLASS, mrb->class_class);
    sc->mt = 0;
    sc->iv = 0;
    if (o->tt == MRB_TT_CLASS) {
        c = (RClass*)o;
        if (!c->super) {
            sc->super = mrb->class_class;
        }
        else {
            sc->super = c->super->c;
        }
    }
    else if (o->tt == MRB_TT_SCLASS) {
        c = (RClass*)o;
        while (c->super->tt == MRB_TT_ICLASS)
            c = c->super;
        make_metaclass(c->super);
        sc->super = c->super->c;
    }
    else {
        sc->super = o->c;
    }
    o->c = sc;
    mrb->gc().mrb_field_write_barrier(o, sc);
    mrb->gc().mrb_field_write_barrier(sc, o);
    sc->iv_set(mrb->intern2("__attached__", 12), mrb_obj_value(o));
}

RClass* mrb_define_module_id(mrb_state *mrb, mrb_sym name)
{
    RClass *m = mrb_module_new(mrb);

    mrb->object_class->iv_set(name, mrb_obj_value(m));
    m->name_class(name);

    return m;
}

RClass* mrb_define_module(mrb_state *mrb, const char *name)
{
    return mrb_define_module_id(mrb, mrb_intern_cstr(mrb, name));
}

static void setup_class(mrb_state *mrb, mrb_value outer, RClass *c, mrb_sym id)
{
    c->name_class(id);
    mrb_const_set(mrb, outer, id, mrb_obj_value(c));
    c->iv_set(mrb->intern2("__outer__", 9), outer);
}

RClass* RClass::outer_module()
{
    mrb_value outer = iv_get(m_vm->intern2("__outer__", 9));
    if (mrb_nil_p(outer))
        return nullptr;
    return mrb_class_ptr(outer);
}

RClass* mrb_vm_define_module(mrb_state *mrb, const mrb_value &outer, mrb_sym id)
{
    RClass *c;

    if (mrb_const_defined(mrb, outer, id)) {
        mrb_value v = mrb->const_get(outer, id);
        c = mrb_class_ptr(v);
    }
    else {
        c = mrb_module_new(mrb);
        setup_class(mrb, outer, c, id);
    }
    return c;
}

RClass* mrb_define_class_id(mrb_state *mrb, mrb_sym name, RClass *super)
{
    RClass *c = RClass::create(mrb, super);

    mrb->object_class->iv_set(name, mrb_obj_value(c));
    c->name_class(name);

    return c;
}

RClass* mrb_define_class(mrb_state *mrb, const char *name, RClass *super)
{
    RClass *c = mrb_define_class_id(mrb, mrb_intern_cstr(mrb, name), super);
    return c;
}
RClass& mrb_state::define_class(const char *name, RClass *super)
{
    RClass *c = mrb_define_class_id(this, mrb_intern_cstr(this, name), super);
    return *c;
}

RClass* mrb_vm_define_class(mrb_state *mrb, mrb_value outer, mrb_value super, mrb_sym id)
{
    RClass *c, *s;

    if (mrb_const_defined(mrb, outer, id)) {
        mrb_value v = mrb->const_get(outer, id);

        mrb_check_type(mrb, v, MRB_TT_CLASS);
        c = mrb_class_ptr(v);
        if (!mrb_nil_p(super)) {
            if (mrb_type(super) != MRB_TT_CLASS) {
                mrb->mrb_raisef(E_TYPE_ERROR, "superclass must be a Class (%S given)", super);
            }

            if (!c->super || mrb_class_ptr(super) != mrb_class_real(c->super)) {
                mrb->mrb_raisef(E_TYPE_ERROR, "superclass mismatch for class %S", mrb_sym2str(mrb, id));
            }
        }
        return c;
    }

    if (!mrb_nil_p(super)) {
        if (mrb_type(super) != MRB_TT_CLASS) {
            mrb->mrb_raisef(E_TYPE_ERROR, "superclass must be a Class (%S given)", super);
        }
        s = mrb_class_ptr(super);
    }
    else {
        s = mrb->object_class;
    }

    c = RClass::create(mrb, s);
    setup_class(mrb, outer, c, id);
    mrb->funcall(mrb_obj_value(s), "inherited", 1, mrb_obj_value(c));

    return c;
}

bool mrb_state::class_defined(const char *name)
{
    return mrb_const_defined(this, mrb_obj_value(object_class), intern_cstr(name));
}

RClass * RClass::from_sym(mrb_sym id)
{
    mrb_value c = const_get(id);

    if (mrb_type(c) != MRB_TT_MODULE && mrb_type(c) != MRB_TT_CLASS) {
        m_vm->mrb_raisef(A_TYPE_ERROR(m_vm), "%S is not a class/module", mrb_sym2str(m_vm, id));
    }
    return mrb_class_ptr(c);
}

/*!
 * Defines a class under the namespace of \a outer.
 * \param outer  a class which contains the new class.
 * \param id     name of the new class
 * \param super  a class from which the new class will derive.
 *               NULL means \c Object class.
 * \return the created class
 * \throw TypeError if the constant name \a name is already taken but
 *                  the constant is not a \c Class.
 * \throw NameError if the class is already defined but the class can not
 *                  be reopened because its superclass is not \a super.
 * \post top-level constant named \a name refers the returned class.
 *
 * \note if a class named \a name is already defined and its superclass is
 *       \a super, the function just returns the defined class.
 */
RClass * RClass::define_class_under(const char *name, RClass *super)
{
    mrb_sym id = mrb_intern_cstr(m_vm, name);

    if (mrb_const_defined_at(this, id)) {
        RClass * c = this->from_sym(id);
        if (mrb_class_real(c->super) != super) {
            mrb_name_error(m_vm, id, "%S is already defined", mrb_sym2str(m_vm, id));
        }
        return c;
    }
    if (!super) {
        mrb_warn(m_vm,"no super class for `%S::%S', Object assumed", mrb_obj_value(this), mrb_sym2str(m_vm, id));
    }
    RClass * _cl = RClass::create(m_vm, super);
    setup_class(m_vm, mrb_obj_value(this), _cl, id);

    return _cl;
}

RClass * RClass::define_module_under(const char *name)
{
    mrb_sym id = m_vm->intern_cstr(name);

    if (mrb_const_defined_at(this, id)) {
        return this->from_sym(id);
    }
    RClass * c = mrb_module_new(m_vm);
    setup_class(m_vm, mrb_obj_value(this), c, id);

    return c;
}


static mrb_value check_type(mrb_state *mrb, mrb_value val, enum mrb_vtype t, const char *c, const char *m)
{
    mrb_value tmp = mrb_check_convert_type(mrb, val, t, c, m);
    if (mrb_nil_p(tmp)) {
        mrb->mrb_raisef(E_TYPE_ERROR, "expected %S", mrb_str_new_cstr(mrb, c));
    }
    return tmp;
}

static mrb_value to_str(mrb_state *mrb, mrb_value val)
{
    return check_type(mrb, val, MRB_TT_STRING, "String", "to_str");
}

static mrb_value to_ary(mrb_state *mrb, mrb_value val)
{
    return check_type(mrb, val, MRB_TT_ARRAY, "Array", "to_ary");
}

static mrb_value to_hash(mrb_state *mrb, mrb_value val)
{
    return check_type(mrb, val, MRB_TT_HASH, "Hash", "to_hash");
}

/*
  retrieve arguments from mrb_state.

  mrb_get_args(mrb, format, ...)

  returns number of arguments parsed.

  format specifiers:

    string  mruby type     C type                 note
    ----------------------------------------------------------------------------------------------
    o:      Object         [mrb_value]
    C:      class/module   [mrb_value]
    S:      String         [mrb_value]
    A:      Array          [mrb_value]
    H:      Hash           [mrb_value]
    s:      String         [char*,int]            Receive two arguments.
    z:      String         [char*]                NUL terminated string.
    a:      Array          [mrb_value*,mrb_int]   Receive two arguments.
    f:      Float          [mrb_float]
    i:      Integer        [mrb_int]
    b:      Boolean        [mrb_bool]
    n:      Symbol         [mrb_sym]
    &:      Block          [mrb_value]
    *:      rest argument  [mrb_value*,int]       Receive the rest of the arguments as an array.
    |:      optional                              Next argument of '|' and later are optional.
 */
int mrb_get_args(mrb_state *mrb, const char *format, ...)
{
    char c;
    int i = 0;
    mrb_value *sp = mrb->m_ctx->m_stack + 1;
    va_list ap;
    int argc = mrb->m_ctx->m_ci->argc;
    int opt = 0;

    va_start(ap, format);
    if (argc < 0) {
        struct RArray *a = mrb_ary_ptr(mrb->m_ctx->m_stack[1]);

        argc = a->m_len;
        sp = a->m_ptr;
    }
    while ((c = *format++)) {
        switch (c) {
            case '|': case '*': case '&':
                break;
            default:
                if (argc <= i && !opt) {
                    mrb_raise(E_ARGUMENT_ERROR, "wrong number of arguments");
                }
                break;
        }

        switch (c) {
            case 'o':
            {
                mrb_value *p;

                p = va_arg(ap, mrb_value*);
                if (i < argc) {
                    *p = *sp++;
                    i++;
                }
            }
                break;
            case 'C':
            {
                mrb_value *p;

                p = va_arg(ap, mrb_value*);
                if (i < argc) {
                    mrb_value ss;

                    ss = *sp++;
                    switch (mrb_type(ss)) {
                        case MRB_TT_CLASS:
                        case MRB_TT_MODULE:
                        case MRB_TT_SCLASS:
                            break;
                        default:
                            mrb->mrb_raisef(E_TYPE_ERROR, "%S is not class/module", ss);
                            break;
                    }
                    *p = ss;
                    i++;
                }
            }
                break;
            case 'S':
            {
                mrb_value *p;

                p = va_arg(ap, mrb_value*);
                if (i < argc) {
                    *p = to_str(mrb, *sp++);
                    i++;
                }
            }
                break;
            case 'A':
            {
                mrb_value *p;

                p = va_arg(ap, mrb_value*);
                if (i < argc) {
                    *p = to_ary(mrb, *sp++);
                    i++;
                }
            }
                break;
            case 'H':
            {
                mrb_value *p;

                p = va_arg(ap, mrb_value*);
                if (i < argc) {
                    *p = to_hash(mrb, *sp++);
                    i++;
                }
            }
                break;
            case 's':
            {
                mrb_value ss;
                struct RString *s;
                char **ps = 0;
                int *pl = 0;

                ps = va_arg(ap, char**);
                pl = va_arg(ap, int*);
                if (i < argc) {
                    ss = to_str(mrb, *sp++);
                    s = mrb_str_ptr(ss);
                    *ps = s->m_ptr;
                    *pl = s->len;
                    i++;
                }
            }
                break;
            case 'z':
            {
                mrb_value ss;
                struct RString *s;
                char **ps;
                mrb_int len;

                ps = va_arg(ap, char**);
                if (i < argc) {
                    ss = to_str(mrb, *sp++);
                    s = mrb_str_ptr(ss);
                    len = (mrb_int)strlen(s->m_ptr);
                    if (len <  s->len) {
                        mrb_raise(E_ARGUMENT_ERROR, "String contains NUL");
                    } else if (len > s->len) {
                        s->str_modify();
                    }
                    *ps = s->m_ptr;
                    i++;
                }
            }
                break;
            case 'a':
            {
                mrb_value **pb = va_arg(ap, mrb_value**);
                mrb_int *pl = va_arg(ap, mrb_int*);
                if (i < argc) {
                    mrb_value aa = to_ary(mrb, *sp++);
                    RArray *a = mrb_ary_ptr(aa);
                    *pb = a->m_ptr;
                    *pl = a->m_len;
                    i++;
                }
            }
                break;
            case 'f':
            {
                mrb_float *p;

                p = va_arg(ap, mrb_float*);
                if (i < argc) {
                    switch (mrb_type(*sp)) {
                        case MRB_TT_FLOAT:
                            *p = mrb_float(*sp);
                            break;
                        case MRB_TT_FIXNUM:
                            *p = (mrb_float)mrb_fixnum(*sp);
                            break;
                        case MRB_TT_STRING:
                            mrb_raise(E_TYPE_ERROR, "String can't be coerced into Float");
                            break;
                        default:
                        {
                            mrb_value tmp;

                            tmp = mrb_convert_type(mrb, *sp, MRB_TT_FLOAT, "Float", "to_f");
                            *p = mrb_float(tmp);
                        }
                            break;
                    }
                    sp++;
                    i++;
                }
            }
                break;
            case 'i':
            {
                mrb_int *p;

                p = va_arg(ap, mrb_int*);
                if (i < argc) {
                    switch (mrb_type(*sp)) {
                        case MRB_TT_FIXNUM:
                            *p = mrb_fixnum(*sp);
                            break;
                        case MRB_TT_FLOAT:
                        {
                            mrb_float f = mrb_float(*sp);

                            if (!FIXABLE(f)) {
                                mrb_raise(E_RANGE_ERROR, "float too big for int");
                            }
                            *p = (mrb_int)f;
                        }
                            break;
                        default:
                            *p = mrb_fixnum(mrb_Integer(mrb, *sp));
                            break;
                    }
                    sp++;
                    i++;
                }
            }
                break;
            case 'b':
            {
                mrb_bool *boolp = va_arg(ap, mrb_bool*);

                if (i < argc) {
                    mrb_value b = *sp++;
                    *boolp = mrb_test(b);
                    i++;
                }
            }
                break;
            case 'n':
            {
                mrb_sym *symp;

                symp = va_arg(ap, mrb_sym*);
                if (i < argc) {
                    mrb_value ss;

                    ss = *sp++;
                    if (mrb_type(ss) == MRB_TT_SYMBOL) {
                        *symp = mrb_symbol(ss);
                    }
                    else if (mrb_is_a_string(ss)) {
                        *symp = mrb_intern_str(mrb, to_str(mrb, ss));
                    }
                    else {
                        mrb_value obj = mrb->funcall(ss, "inspect", 0);
                        mrb->mrb_raisef(E_TYPE_ERROR, "%S is not a symbol", obj);
                    }
                    i++;
                }
            }
                break;

            case '&':
            {
                mrb_value *p, *bp;

                p = va_arg(ap, mrb_value*);
                if (mrb->m_ctx->m_ci->argc < 0) {
                    bp = mrb->m_ctx->m_stack + 2;
                }
                else {
                    bp = mrb->m_ctx->m_stack + mrb->m_ctx->m_ci->argc + 1;
                }
                *p = *bp;
            }
                break;
            case '|': opt = 1; break;

            case '*':
            {
                mrb_value **var = va_arg(ap, mrb_value**);
                int *pl = va_arg(ap, int*);
                if (i < argc) {
                    *pl = argc-i;
                    if (*pl > 0) {
                        *var = sp;
                    }
                    i = argc;
                    sp += *pl;
                }
                else {
                    *pl = 0;
                    *var = nullptr;
                }
            }
                break;
            default:
                mrb->mrb_raisef(E_ARGUMENT_ERROR, "invalid argument specifier %S", mrb_str_new(mrb, &c, 1));
                break;
        }
    }
    if (!c && argc > i) {
        mrb_raise(E_ARGUMENT_ERROR, "wrong number of arguments");
    }
    va_end(ap);
    return i;
}

static RClass* boot_defclass(mrb_state *mrb, RClass *super)
{
    RClass *c  = mrb->gc().obj_alloc<RClass>(mrb->class_class);
    c->super = super ? super : mrb->object_class;
    mrb->gc().mrb_field_write_barrier(c, super);
    c->mt = kh_mt::init(mrb->gc());
    return c;
}

static mrb_value mrb_mod_append_features(mrb_state *mrb, mrb_value mod)
{

    mrb_check_type(mrb, mod, MRB_TT_MODULE);
    RClass *cls = mrb->get_arg<RClass *>();
    cls->include_module(mrb_class_ptr(mod));
    return mod;
}

static mrb_value mrb_mod_include(mrb_state *mrb, mrb_value klass)
{
    mrb_value *argv;
    int argc, i;

    mrb_get_args(mrb, "*", &argv, &argc);
    for (i=0; i<argc; i++) {
        mrb_check_type(mrb, argv[i], MRB_TT_MODULE);
    }
    while (argc--) {
        mrb->funcall(argv[argc], "append_features", 1, klass);
        mrb->funcall(argv[argc], "included", 1, klass);
    }

    return klass;
}

/* 15.2.2.4.28 */
/*
 *  call-seq:
 *     mod.include?(module)    -> true or false
 *
 *  Returns <code>true</code> if <i>module</i> is included in
 *  <i>mod</i> or one of <i>mod</i>'s ancestors.
 *
 *     module A
 *     end
 *     class B
 *       include A
 *     end
 *     class C < B
 *     end
 *     B.include?(A)   #=> true
 *     C.include?(A)   #=> true
 *     A.include?(A)   #=> false
 */
static mrb_value mrb_mod_include_p(mrb_state *mrb, mrb_value mod)
{
    // = mrb->get_arg<mrb_value>()
    mrb_value mod2;
    mrb_get_args(mrb,"C",&mod2);
    mrb_check_type(mrb, mod2, MRB_TT_MODULE);

    RClass *c = mrb_class_ptr(mod);

    while (c) {
        if (c->tt == MRB_TT_ICLASS) {
            if (c->c == mrb_class_ptr(mod2))
                return mrb_true_value();
        }
        c = c->super;
    }
    return mrb_false_value();
}

static mrb_value mrb_mod_ancestors(mrb_state *mrb, mrb_value self)
{
    RClass *c = mrb_class_ptr(self);
    RArray *res = RArray::create(mrb, 0);

    res->push(mrb_obj_value(c));
    c = c->super;
    while (c) {
        if (c->tt == MRB_TT_ICLASS) {
            res->push(mrb_obj_value(c->c));
        }
        else if (c->tt != MRB_TT_SCLASS) {
            res->push(mrb_obj_value(c));
        }
        c = c->super;
    }

    return mrb_obj_value(res);
}

static mrb_value mrb_mod_extend_object(mrb_state *mrb, mrb_value mod)
{

    mrb_check_type(mrb, mod, MRB_TT_MODULE);
    mrb_value obj = mrb->get_arg<mrb_value>();
    mrb_class_ptr(mrb_singleton_class(mrb, obj))->include_module(mrb_class_ptr(mod));
    return mod;
}

static mrb_value mrb_mod_included_modules(mrb_state *mrb, mrb_value self)
{
    RClass *c = mrb_class_ptr(self);
    RArray *res = RArray::create(mrb);
    while (c) {
        if (c->tt == MRB_TT_ICLASS) {
            res->push(mrb_obj_value(c->c));
        }
        c = c->super;
    }

    return mrb_obj_value(res);
}


/* 15.2.2.4.33 */
/*
 *  call-seq:
 *     mod.instance_methods(include_super=true)   -> array
 *
 *  Returns an array containing the names of the public and protected instance
 *  methods in the receiver. For a module, these are the public and protected methods;
 *  for a class, they are the instance (not singleton) methods. With no
 *  argument, or with an argument that is <code>false</code>, the
 *  instance methods in <i>mod</i> are returned, otherwise the methods
 *  in <i>mod</i> and <i>mod</i>'s superclasses are returned.
 *
 *     module A
 *       def method1()  end
 *     end
 *     class B
 *       def method2()  end
 *     end
 *     class C < B
 *       def method3()  end
 *     end
 *
 *     A.instance_methods                #=> [:method1]
 *     B.instance_methods(false)         #=> [:method2]
 *     C.instance_methods(false)         #=> [:method3]
 *     C.instance_methods(true).length   #=> 43
 */

static mrb_value mrb_mod_instance_methods(mrb_state *mrb, mrb_value mod)
{
    RClass *c = mrb_class_ptr(mod);
    mrb_bool recur=true;
    mrb_get_args(mrb, "|b", &recur);
    return class_instance_method_list(mrb, recur, c, 0);
}

mrb_value mrb_yield_internal(mrb_state *mrb, mrb_value b, int argc, mrb_value *argv, mrb_value self, RClass *c);

/* 15.2.2.4.35 */
/*
 *  call-seq:
 *     mod.class_eval {| | block }  -> obj
 *     mod.module_eval {| | block } -> obj
 *
 *  Evaluates block in the context of _mod_. This can
 *  be used to add methods to a class. <code>module_eval</code> returns
 *  the result of evaluating its argument.
 */

mrb_value mrb_mod_module_eval(mrb_state *mrb, mrb_value mod)
{
    mrb_value a, b;
    RClass *c;

    if (mrb_get_args(mrb, "|S&", &a, &b) == 1) {
        mrb_raise(E_NOTIMP_ERROR, "module_eval/class_eval with string not implemented");
    }
    c = mrb_class_ptr(mod);
    return mrb_yield_internal(mrb, b, 0, 0, mod, c);
}
mrb_value
mrb_mod_dummy_visibility(mrb_state *mrb, mrb_value mod) {
    return mod;
}
mrb_value mrb_singleton_class(mrb_state *mrb, mrb_value v)
{

    switch (mrb_type(v)) {
        case MRB_TT_FALSE:
            if (mrb_nil_p(v))
                return mrb_obj_value(mrb->nil_class);
            return mrb_obj_value(mrb->false_class);
        case MRB_TT_TRUE:
            return mrb_obj_value(mrb->true_class);
        case MRB_TT_CPTR:
            return mrb_obj_value(mrb->object_class);
        case MRB_TT_SYMBOL:
        case MRB_TT_FIXNUM:
        case MRB_TT_FLOAT:
            mrb_raise(E_TYPE_ERROR, "can't define singleton");
            return mrb_nil_value();    /* not reached */
        default:
            break;
    }
    RBasic *obj = mrb_basic_ptr(v);
    prepare_singleton_class(obj);
    return mrb_obj_value(obj->c);
}

void RObject::define_singleton_method(const char *name, mrb_func_t func, mrb_aspec aspec)
{
    assert(m_vm);
    prepare_singleton_class(this);
    c->define_method_id(m_vm->intern_cstr(name), func, aspec);
}

void RClass::define_module_function(const char *name, mrb_func_t func, mrb_aspec aspec)
{
    define_class_method(name, func, aspec);
    define_method(name, func, aspec);
}
RClass &RClass::include_module(RClass *m)
{
    RClass *ins_pos = this;

    while (m) {
        RClass *p = this, *ic;
        bool superclass_seen = false;

        if (this->mt == m->mt) {
            mrb_raise(A_ARGUMENT_ERROR(m_vm), "cyclic include detected");
        }

        while(p) {
            if (this != p && p->tt == MRB_TT_CLASS) {
                superclass_seen = true;
            }
            else if (p->mt == m->mt){
                if (p->tt == MRB_TT_ICLASS && !superclass_seen) {
                    ins_pos = p;
                }
                goto skip;
            }
            p = p->super;
        }
        ic = m_vm->gc().obj_alloc<RClass>(MRB_TT_ICLASS, m_vm->class_class);
        if (m->tt == MRB_TT_ICLASS) {
            ic->c = m->c;
        }
        else {
            ic->c = m;
        }
        ic->mt = m->mt;
        ic->iv = m->iv;
        ic->super = ins_pos->super;
        ins_pos->super = ic;
        m_vm->gc().mrb_field_write_barrier(ins_pos, ic);
        ins_pos = ic;
skip:
        m = m->super;
    }
    return *this;
}

RProc* RClass::method_search_vm(RClass **cp, mrb_sym mid)//(mrb_state *mrb, RClass **cp, mrb_sym mid)
{
    khiter_t k;
    RProc *m;
    RClass *c = *cp;

    while (c) {
        kh_mt *h = c->mt;

        if (h) {
            k = h->get(mid);
            if (k != h->end()) {
                m = h->value(k);
                if (!m)
                    break;
                *cp = c;
                return m;
            }
        }
        c = c->super;
    }
    return nullptr;                  /* no method */
}

RProc * mrb_method_search(mrb_state *mrb, RClass* c, mrb_sym mid)
{
    RProc *m = RClass::method_search_vm(&c, mid);
    if (m)
        return m;

    mrb_value inspect = mrb->funcall(mrb_obj_value(c), "inspect", 0);
    if (RSTRING_LEN(inspect) > 64) {
        inspect = mrb_any_to_s(mrb, mrb_obj_value(c));
    }
    mrb_name_error(mrb,mid, "undefined method '%S' for class %S", mrb_sym2str(mrb, mid), inspect);
    return m;
}
mrb_value
mrb_instance_alloc(mrb_state *mrb, mrb_value cv)
{
    RClass *c = mrb_class_ptr(cv);
    enum mrb_vtype ttype = MRB_INSTANCE_TT(c);

    if (c->tt == MRB_TT_SCLASS)
        mrb_raise(E_TYPE_ERROR, "can't create instance of singleton class");

    if (ttype == 0)
        ttype = MRB_TT_OBJECT;
    RObject *o = mrb->gc().obj_alloc<RObject>(ttype,c);
    return mrb_obj_value(o);
}

mrb_value RClass::mrb_instance_alloc()
{
    enum mrb_vtype ttype = MRB_INSTANCE_TT(this);

    if (tt == MRB_TT_SCLASS)
        mrb_raise(A_TYPE_ERROR(m_vm), "can't create instance of singleton class");

    if (ttype == 0)
        ttype = MRB_TT_OBJECT;
    RObject *o = m_vm->gc().obj_alloc<RObject>(ttype,this);
    return mrb_obj_value(o);
}
/*
 *  call-seq:
 *     class.new(args, ...)    ->  obj
 *
 *  Calls <code>allocate</code> to create a new object of
 *  <i>class</i>'s class, then invokes that object's
 *  <code>initialize</code> method, passing it <i>args</i>.
 *  This is the method that ends up getting called whenever
 *  an object is constructed using .new.
 *
 */
mrb_value RClass::new_instance(int argc, mrb_value *argv)
{
    mrb_value obj = mrb_instance_alloc();
    mrb_funcall_argv(m_vm, obj, m_vm->intern2("initialize",10), argc, argv);
    return obj;
}

//mrb_value RClass::new_instance(int argc, mrb_value *argv)
//{
//    RClass * c = m_vm->gc().obj_alloc<RClass>(this->tt, this);
//    c->super = this;
//    mrb_value obj = mrb_obj_value(c);
//    mrb_obj_call_init(m_vm, obj, argc, argv);
//    return obj;
//}

//mrb_value mrb_class_new_instance_m(mrb_state *mrb, mrb_value klass)
//{
//    mrb_value *argv;
//    mrb_value blk;
//    RClass *k = mrb_class_ptr(klass);
//    int argc;
//    mrb_value obj;

//    mrb_get_args(mrb, "*&", &argv, &argc, &blk);
//    RClass *c = mrb->gc().obj_alloc<RClass>(k->tt, k);
//    c->super = k;
//    obj = mrb_obj_value(c);
//    mrb_funcall_with_block(mrb, obj, mrb->init_sym, argc, argv, blk);

//    return obj;
//}

mrb_value mrb_instance_new(mrb_state *mrb, mrb_value cv)
{
    mrb_value blk;
    mrb_value *argv;
    int argc;
    mrb_value obj = mrb_instance_alloc(mrb, cv);
    mrb_get_args(mrb, "*&", &argv, &argc, &blk);
    mrb_funcall_with_block(mrb, obj, mrb->intern2("initialize",10), argc, argv, blk);

    //    RClass *c = mrb_class_ptr(cv);
    //    mrb_vtype ttype = MRB_INSTANCE_TT(c);
    //    mrb_value obj, blk;
    //    mrb_value *argv;
    //    int argc;
    //    if (c->tt == MRB_TT_SCLASS)
    //        mrb_raise(E_TYPE_ERROR, "can't create instance of singleton class");

    //    if (ttype == 0)
    //        ttype = MRB_TT_OBJECT;
    //    RObject *o = mrb->gc().obj_alloc<RObject>(ttype, c);
    //    obj = mrb_obj_value(o);
    //    mrb_get_args(mrb, "*&", &argv, &argc, &blk);
    //    mrb_funcall_with_block(mrb, obj, mrb->init_sym, argc, argv, blk);

    return obj;
}
static mrb_value mrb_class_new_class(mrb_state *mrb, mrb_value cv)
{
    mrb_value super,blk;
    RClass *new_class;

    if (mrb_get_args(mrb, "|C&", &super,&blk) == 0) {
        super = mrb_obj_value(mrb->object_class);
    }
    new_class = RClass::create(mrb,mrb_class_ptr(super));
    mrb_value res(mrb_obj_value(new_class));
    if (!mrb_nil_p(blk)) {
        mrb_funcall_with_block(mrb, res, mrb_intern_cstr(mrb, "class_eval"), 0, NULL, blk);
    }
    mrb->funcall(super,"inherited",1,res);
    return res;
}

mrb_value mrb_obj_new(mrb_state *mrb, RClass *c, int argc, mrb_value *argv)
{
    return c->new_instance(argc,argv);
}

mrb_value mrb_class_superclass(mrb_state *mrb, mrb_value klass)
{
    RClass *c = mrb_class_ptr(klass);
    c = c->super;
    while (c && c->tt == MRB_TT_ICLASS) {
        c = c->super;
    }
    if (!c)
        return mrb_nil_value();
    return mrb_obj_value(c);
}

static mrb_value mrb_bob_init(mrb_state *mrb, mrb_value cv)
{
    return mrb_nil_value();
}

static mrb_value mrb_bob_not(mrb_state *mrb, mrb_value cv)
{
    return mrb_bool_value(!mrb_test(cv));
}

/* 15.3.1.3.30 */
/*
 *  call-seq:
 *     obj.method_missing(symbol [, *args] )   -> result
 *
 *  Invoked by Ruby when <i>obj</i> is sent a message it cannot handle.
 *  <i>symbol</i> is the symbol for the method called, and <i>args</i>
 *  are any arguments that were passed to it. By default, the interpreter
 *  raises an error when this method is called. However, it is possible
 *  to override the method to provide more dynamic behavior.
 *  If it is decided that a particular method should not be handled, then
 *  <i>super</i> should be called, so that ancestors can pick up the
 *  missing method.
 *  The example below creates
 *  a class <code>Roman</code>, which responds to methods with names
 *  consisting of roman numerals, returning the corresponding integer
 *  values.
 *
 *     class Roman
 *       def romanToInt(str)
 *         # ...
 *       end
 *       def method_missing(methId)
 *         str = methId.id2name
 *         romanToInt(str)
 *       end
 *     end
 *
 *     r = Roman.new
 *     r.iv      #=> 4
 *     r.xxiii   #=> 23
 *     r.mm      #=> 2000
 */
static mrb_value mrb_bob_missing(mrb_state *mrb, mrb_value mod)
{
    mrb_sym name;
    mrb_value *a;
    int alen;
    mrb_value inspect;

    mrb_get_args(mrb, "n*", &name, &a, &alen);

    if (mrb_respond_to(mrb,mod,mrb_intern2(mrb,"inspect",7))){
        inspect = mrb->funcall(mod, "inspect", 0);
        if (RSTRING_LEN(inspect) > 64) {
            inspect = mrb_any_to_s(mrb, mod);
        }
    }
    else {
        inspect = mrb_any_to_s(mrb, mod);
    }

    mrb->mrb_raisef(E_NOMETHOD_ERROR, "undefined method '%S' for %S",
                    mrb_sym2str(mrb, name), inspect);
    /* not reached */
    return mrb_nil_value();
}

mrb_bool RClass::respond_to(mrb_sym mid) const
{
    const RClass * c = this;
    while (c) {
        const kh_mt *h = c->mt;

        if (h) {
            khiter_t k = h->get(mid);
            if (k != h->end()) {
                if (h->value(k)) {
                    return true;  /* method exists */
                }
                return false; /* undefined method */
            }
        }
        c = c->super;
    }
    return false;         /* no method */
}

int mrb_respond_to(mrb_state *mrb, const mrb_value &obj, mrb_sym mid)
{
    return RClass::mrb_class(mrb, obj)->respond_to(mid);
}

mrb_value RClass::class_path()
{
    mrb_value path;
    mrb_sym classpath = m_vm->intern2("__classpath__", 13);

    path = iv_get(classpath);

    if (!mrb_nil_p(path))
        return path;

    size_t len;
    RClass *outer = outer_module();
    mrb_sym sym = mrb_class_sym(m_vm, this, outer);
    if (sym == 0) {
        return mrb_nil_value();
    }
    const char *name = mrb_sym2name_len(m_vm, sym, len);
    if (outer && outer != m_vm->object_class) {
        mrb_value base = outer->class_path();
        path = mrb_str_plus(m_vm, base, mrb_str_new(m_vm, "::", 2));
        mrb_str_concat(m_vm, path, mrb_str_new(m_vm, name, len));
    }
    else {
        path = mrb_str_new(m_vm, name, len);
    }
    iv_set(classpath, path);
    return path;
}

RClass * mrb_class_real(RClass* cl)
{
    while ((cl->tt == MRB_TT_SCLASS) || (cl->tt == MRB_TT_ICLASS)) {
        cl = cl->super;
    }
    return cl;
}

const char* RClass::class_name()
{
    mrb_value path = class_path();
    if (mrb_nil_p(path)) {
        path = mrb_str_new(m_vm, "#<Class:", 8);
        mrb_str_concat(m_vm, path, mrb_ptr_to_str(m_vm, this ));
        mrb_str_cat(m_vm, path, ">", 1);
    }
    return mrb_str_ptr(path)->m_ptr;
}

const char* mrb_obj_classname(mrb_state *mrb, mrb_value obj)
{
    return mrb_obj_class(mrb, obj)->class_name();
}
RClass *RClass::mrb_class(mrb_state *mrb, mrb_value &v) {
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
        case MRB_TT_CPTR:
            return mrb->object_class;
        default:
            return mrb_ptr(v)->c;
    }
}

RClass &RClass::define_method_raw(mrb_sym mid, RProc *p) {

    if (!mt)
        mt = kh_mt::init(m_vm->gc());
    khiter_t k = mt->put(mid);
    mt->value(k) = p;
    if (p) {
        m_vm->gc().mrb_field_write_barrier(this, p);
    }
    return *this;
}

void RClass::define_method_vm(mrb_sym name, mrb_value body) {
    kh_mt *h = this->mt;
    khiter_t k;
    RProc *p;
    assert(m_vm);
    if (!h)
        this->mt = kh_mt::init(m_vm->gc());
    k = this->mt->put(name);
    p = mrb_proc_ptr(body);
    this->mt->value(k) = p;
    if (p) {
        m_vm->gc().mrb_field_write_barrier(this, p);
    }
}

RProc *RClass::method_search(mrb_sym mid) {
    RProc *m;
    RClass* found_in = this;
    m = method_search_vm(&found_in, mid);
    if (!m) {
        mrb_value inspect = m_vm->funcall(mrb_obj_value(found_in), "inspect", 0);
        if (RSTRING_LEN(inspect) > 64) {
            inspect = mrb_any_to_s(m_vm, mrb_obj_value(found_in));
        }
        mrb_name_error(m_vm, mid, "undefined method '%S' for class %S",
                       mrb_sym2str(m_vm, mid), inspect);
    }
    return m;
}

RClass &RClass::define_method(const char *name, mrb_func_t func, mrb_aspec aspec) {
    define_method_id(m_vm->intern_cstr(name),func, aspec);
    return *this;
}

RClass &RClass::include_module(const char *name) {
    return include_module(m_vm->class_get(name));
}
RClass &RClass::define_alias(const char *name1, const char *name2) {
    alias_method(m_vm->intern_cstr(name1), m_vm->intern_cstr(name2));
    return *this;
}

/*!
 * Ensures a class can be derived from super.
 *
 * \param super a reference to an object.
 * \exception TypeError if \a super is not a Class or \a super is a singleton class.
 */
void mrb_check_inheritable(mrb_state *mrb, RClass *super)
{
    if (super->tt != MRB_TT_CLASS) {
        mrb->mrb_raisef(E_TYPE_ERROR, "superclass must be a Class (%S given)", mrb_obj_value(super));
    }
    if (super->tt == MRB_TT_SCLASS) {
        mrb_raise(E_TYPE_ERROR, "can't make subclass of singleton class");
    }
    if (super == mrb->class_class) {
        mrb_raise(E_TYPE_ERROR, "can't make subclass of Class");
    }
}

/*!
 * Creates a new class.
 * \param super     a class from which the new class derives.
 * \exception TypeError \a super is not inheritable.
 * \exception TypeError \a super is the Class class.
 */
RClass * RClass::create(mrb_state *mrb, RClass *super)
{

    if (super) {
        mrb_check_inheritable(mrb, super);
    }
    RClass *c = boot_defclass(mrb, super);
    if (super){
        MRB_SET_INSTANCE_TT(c, MRB_INSTANCE_TT(super));
    }
    make_metaclass(c);

    return c;
}

/*!
 * Creates a new module.
 */
RClass * mrb_module_new(mrb_state *mrb)
{
    RClass *m = mrb->gc().obj_alloc<RClass>(MRB_TT_MODULE, mrb->module_class);
    m->mt = kh_mt::init(mrb->gc());

    return m;
}

/*!
 *  call-seq:
 *     obj.class    => class
 *
 *  Returns the class of <i>obj</i>, now preferred over
 *  <code>Object#type</code>, as an object's type in Ruby is only
 *  loosely tied to that object's class. This method must always be
 *  called with an explicit receiver, as <code>class</code> is also a
 *  reserved word in Ruby.
 *
 *     1.class      #=> Fixnum
 *     self.class   #=> Object
 */

RClass* mrb_obj_class(mrb_state *mrb, mrb_value obj)
{
    return mrb_class_real(RClass::mrb_class(mrb, obj));
}

/*!
 * call-seq:
 *   mod.to_s   -> string
 *
 * Return a string representing this module or class. For basic
 * classes and modules, this is the name. For singletons, we
 * show information on the thing we're attached to as well.
 */

static mrb_value mrb_mod_to_s(mrb_state *mrb, mrb_value klass)
{
    mrb_value str;

    if (mrb_type(klass) == MRB_TT_SCLASS) {
        mrb_value v = mrb_ptr(klass)->iv_get(mrb->intern2("__attached__", 12));

        str = mrb_str_new(mrb, "#<Class:", 8);

        switch (mrb_type(v)) {
            case MRB_TT_CLASS:
            case MRB_TT_MODULE:
            case MRB_TT_SCLASS:
                mrb_str_append(mrb, str, mrb_inspect(mrb, v));
                break;
            default:
                mrb_str_append(mrb, str, mrb_any_to_s(mrb, v));
                break;
        }
        mrb_str_cat(mrb, str, ">", 1);
        return str;
    }
    else {

        str = mrb_str_buf_new(mrb, 32);
        RClass *c = mrb_class_ptr(klass);
        mrb_value path = c->class_path();

        if (mrb_nil_p(path)) {
            switch (mrb_type(klass)) {
                case MRB_TT_CLASS:
                    mrb_str_cat(mrb, str, "#<Class:", 8);
                    break;

                case MRB_TT_MODULE:
                    mrb_str_cat(mrb, str, "#<Module:", 9);
                    break;

                default:
                    /* Shouldn't be happened? */
                    mrb_str_cat(mrb, str, "#<??????:", 9);
                    break;
            }
            mrb_str_concat(mrb, str, mrb_ptr_to_str(mrb, c));
            mrb_str_cat(mrb, str, ">", 1);
        }
        else {
            str = path;
        }
    }

    return str;
}

mrb_value mrb_mod_alias(mrb_state *mrb, mrb_value mod)
{
    RClass *c = mrb_class_ptr(mod);
    mrb_sym new_name, old_name;

    mrb_get_args(mrb, "nn", &new_name, &old_name);
    c->alias_method(new_name, old_name);
    return mrb_nil_value();
}


RClass& RClass::undef_method(mrb_sym a)
{
    static const mrb_value m = {{0},MRB_TT_PROC};
    if(!respond_to(a)) {
        mrb_name_error(m_vm, a, "undefined method '%S' for class '%S'", mrb_sym2str(m_vm, a), mrb_obj_value(c));
    }
    else {
        define_method_vm(a, m);
    }
    return *this;
}

RClass& RClass::undef_method(const char *name)
{
    return undef_method(m_vm->intern_cstr(name));
}

RClass& RClass::undef_class_method(const char *name)
{
    prepare_singleton_class(this);
    c->undef_method(name);
    //mrb_class_ptr(mrb_singleton_class(m_vm, mrb_obj_value(this)))->undef_method(name);
    return *this;
}
void RClass::define_method_id(mrb_sym mid, mrb_func_t func, mrb_aspec aspec)
{
    int ai = m_vm->gc().arena_save();

    RProc *p = mrb_proc_new_cfunc(m_vm, func);
    //p->target_class = c;
    define_method_raw(mid, p);
    m_vm->gc().arena_restore(ai);
}

mrb_value mrb_mod_undef(mrb_state *mrb, mrb_value mod)
{
    RClass *c = mrb_class_ptr(mod);
    int argc;
    mrb_value *argv;

    mrb_get_args(mrb, "*", &argv, &argc);
    while (argc--) {
        c->undef_method(mrb_symbol(*argv));
        argv++;
    }
    return mrb_nil_value();
}

static mrb_value mod_define_method(mrb_state *mrb, mrb_value self)
{
    RClass *c = mrb_class_ptr(self);
    RProc *p;
    mrb_sym mid;
    mrb_value blk;

    mrb_get_args(mrb, "n&", &mid, &blk);
    if (mrb_nil_p(blk)) {
        mrb_raise(E_ARGUMENT_ERROR, "no block given");
    }
    p = RProc::copy_construct(mrb,mrb_proc_ptr(blk));
    p->flags |= MRB_PROC_STRICT;
    c->define_method_raw(mid, p);
    return mrb_symbol_value(mid);
}

static void check_cv_name(mrb_state *mrb, mrb_sym id)
{
    size_t len;

    const char *s = mrb_sym2name_len(mrb, id, len);
    if (len < 3 || !(s[0] == '@' && s[1] == '@')) {
        mrb_name_error(mrb, id, "`%S' is not allowed as a class variable name", mrb_sym2str(mrb, id));
    }
}

/* 15.2.2.4.16 */
/*
 *  call-seq:
 *     obj.class_variable_defined?(symbol)    -> true or false
 *
 *  Returns <code>true</code> if the given class variable is defined
 *  in <i>obj</i>.
 *
 *     class Fred
 *       @@foo = 99
 *     end
 *     Fred.class_variable_defined?(:@@foo)    #=> true
 *     Fred.class_variable_defined?(:@@bar)    #=> false
 */

static mrb_value mrb_mod_cvar_defined(mrb_state *mrb, mrb_value mod)
{
    mrb_sym id = mrb->get_arg<mrb_sym>();

    check_cv_name(mrb, id);
    mrb_bool defined_p = mrb_cv_defined(mrb, mod, id);
    return mrb_bool_value(defined_p);
}

/* 15.2.2.4.17 */
/*
 *  call-seq:
 *     mod.class_variable_get(symbol)    -> obj
 *
 *  Returns the value of the given class variable (or throws a
 *  <code>NameError</code> exception). The <code>@@</code> part of the
 *  variable name should be included for regular class variables
 *
 *     class Fred
 *       @@foo = 99
 *     end
 *     Fred.class_variable_get(:@@foo)     #=> 99
 */

static mrb_value mrb_mod_cvar_get(mrb_state *mrb, mrb_value mod)
{
    mrb_sym id=mrb->get_arg<mrb_sym>();
    check_cv_name(mrb, id);
    return mrb_cv_get(mrb, mod, id);
}

/* 15.2.2.4.18 */
/*
 *  call-seq:
 *     obj.class_variable_set(symbol, obj)    -> obj
 *
 *  Sets the class variable names by <i>symbol</i> to
 *  <i>object</i>.
 *
 *     class Fred
 *       @@foo = 99
 *       def foo
 *         @@foo
 *       end
 *     end
 *     Fred.class_variable_set(:@@foo, 101)     #=> 101
 *     Fred.new.foo                             #=> 101
 */

static mrb_value
mrb_mod_cvar_set(mrb_state *mrb, mrb_value mod)
{
    mrb_value value;
    mrb_sym id;

    mrb_get_args(mrb, "no", &id, &value);
    check_cv_name(mrb, id);
    mrb_cv_set(mrb, mod, id, value);
    return value;
}

/* 15.2.2.4.39 */
/*
 *  call-seq:
 *     remove_class_variable(sym)    -> obj
 *
 *  Removes the definition of the <i>sym</i>, returning that
 *  constant's value.
 *
 *     class Dummy
 *       @@var = 99
 *       puts @@var
 *       p class_variables
 *       remove_class_variable(:@@var)
 *       p class_variables
 *     end
 *
 *  <em>produces:</em>
 *
 *     99
 *     [:@@var]
 *     []
 */

mrb_value mrb_mod_remove_cvar(mrb_state *mrb, mrb_value mod)
{
    mrb_value val;
    mrb_sym id = mrb->get_arg<mrb_sym>();
    check_cv_name(mrb, id);

    val = mrb_iv_remove(mod, id);
    if (!mrb_undef_p(val))
        return val;

    if (mrb_cv_defined(mrb, mod, id)) {
        mrb_name_error(mrb, id, "cannot remove %S for %S", mrb_sym2str(mrb, id), mod);
    }

    mrb_name_error(mrb, id, "class variable %S not defined for %S",mrb_sym2str(mrb, id), mod);

    /* not reached */
    return mrb_nil_value();
}

/* 15.2.2.4.34 */
/*
 *  call-seq:
 *     mod.method_defined?(symbol)    -> true or false
 *
 *  Returns +true+ if the named method is defined by
 *  _mod_ (or its included modules and, if _mod_ is a class,
 *  its ancestors). Public and protected methods are matched.
 *
 *     module A
 *       def method1()  end
 *     end
 *     class B
 *       def method2()  end
 *     end
 *     class C < B
 *       include A
 *       def method3()  end
 *     end
 *
 *     A.method_defined? :method1    #=> true
 *     C.method_defined? "method1"   #=> true
 *     C.method_defined? "method2"   #=> true
 *     C.method_defined? "method3"   #=> true
 *     C.method_defined? "method4"   #=> false
 */

static mrb_value mrb_mod_method_defined(mrb_state *mrb, mrb_value mod)
{
    mrb_sym id = mrb->get_arg<mrb_sym>();
    mrb_bool method_defined_p = mrb_class_ptr(mod)->respond_to(id);
    return mrb_bool_value(method_defined_p);
}

static void remove_method(mrb_state *mrb, mrb_value mod, mrb_sym mid)
{
    RClass *c = mrb_class_ptr(mod);
    kh_mt *h = c->mt;

    if (h) {
        khiter_t k = h->get(mid);
        if (k != h->end()) {
            h->del(k);
            return;
        }
    }

    mrb_name_error(mrb, mid, "method `%S' not defined in %S",mrb_sym2str(mrb, mid), mod);
}

/* 15.2.2.4.41 */
/*
 *  call-seq:
 *     remove_method(symbol)   -> self
 *
 *  Removes the method identified by _symbol_ from the current
 *  class. For an example, see <code>Module.undef_method</code>.
 */

mrb_value mrb_mod_remove_method(mrb_state *mrb, mrb_value mod)
{
    int argc;
    mrb_value *argv;

    mrb_get_args(mrb, "*", &argv, &argc);
    while (argc--) {
        remove_method(mrb, mod, mrb_symbol(*argv));
        argv++;
    }
    return mod;
}

static void check_const_name(mrb_state *mrb, mrb_sym id)
{
    const char *s;
    size_t len;

    s = mrb_sym2name_len(mrb, id, len);
    if (len < 1 || !ISUPPER(*s)) {
        mrb_name_error(mrb, id, "wrong constant name %S", mrb_sym2str(mrb, id));
    }
}

mrb_value mrb_mod_const_defined(mrb_state *mrb, mrb_value mod)
{
    mrb_sym id=mrb->get_arg<mrb_sym>();
    mrb_bool const_defined_p;

    check_const_name(mrb, id);
    const_defined_p = mrb_const_defined(mrb, mod, id);

    return mrb_bool_value(const_defined_p);
}

mrb_value mrb_mod_const_get(mrb_state *mrb, mrb_value mod)
{
    mrb_sym id=mrb->get_arg<mrb_sym>();
    check_const_name(mrb, id);
    return mrb->const_get(mod, id);
}

mrb_value mrb_mod_const_set(mrb_state *mrb, mrb_value mod)
{
    mrb_sym id;
    mrb_value value;

    mrb_get_args(mrb, "no", &id, &value);
    check_const_name(mrb, id);
    mrb_const_set(mrb, mod, id, value);
    return value;
}

mrb_value mrb_mod_remove_const(mrb_state *mrb, mrb_value mod)
{
    mrb_value val;

    mrb_sym id = mrb->get_arg<mrb_sym>();
    check_const_name(mrb, id);
    val = mrb_iv_remove(mod, id);
    if (mrb_undef_p(val)) {
        mrb_name_error(mrb, id, "constant %S not defined", mrb_sym2str(mrb, id));
    }
    return val;
}
mrb_value
mrb_mod_const_missing(mrb_state *mrb, mrb_value mod)
{
    mrb_sym sym;

    mrb_get_args(mrb, "n", &sym);
    mrb_name_error(mrb, sym, "uninitialized constant %S",
                   mrb_sym2str(mrb, sym));
    /* not reached */
    return mrb_nil_value();
}

static mrb_value mrb_mod_s_constants(mrb_state *mrb, mrb_value mod)
{
    mrb->mrb_raise(E_NOTIMP_ERROR, "Module.constants not implemented");
    return mrb_nil_value();
}

static mrb_value mrb_mod_eqq(mrb_state *mrb, mrb_value mod)
{
    mrb_value obj = mrb->get_arg<mrb_value>();
    mrb_bool eqq = mrb_obj_is_kind_of(mrb, obj, mrb_class_ptr(mod));

    return mrb_bool_value(eqq);
}

void mrb_init_class(mrb_state *mrb)
{
    /* boot class hierarchy */
    RClass *bob = boot_defclass(mrb, 0);        /* BasicObject */
    RClass *obj = boot_defclass(mrb, bob);      /* Object */
    RClass *mod = boot_defclass(mrb, obj);      /* Module */
    RClass *cls = boot_defclass(mrb, mod);      /* Class */
    //struct RClass *krn;    /* Kernel */

    mrb->object_class = obj;
    mrb->module_class = mod;/* obj -> mod */
    mrb->class_class  = cls; /* obj -> cls */
    /* fix-up loose ends */
    bob->c = obj->c = mod->c = cls->c = cls;
    make_metaclass(bob);
    make_metaclass(obj);
    make_metaclass(mod);
    make_metaclass(cls);

    /* name basic classes */
    bob->define_const("BasicObject", mrb_obj_value(bob));
    obj->define_const("BasicObject", mrb_obj_value(bob));
    obj->define_const("Object",      mrb_obj_value(obj));
    obj->define_const("Module",      mrb_obj_value(mod));
    obj->define_const("Class",       mrb_obj_value(cls));

    /* name each classes */
    bob->name_class(mrb->intern2("BasicObject",11));
    obj->name_class(mrb->intern2("Object",6));
    mod->name_class(mrb->intern2("Module",6));
    cls->name_class(mrb->intern2("Class",5));

    MRB_SET_INSTANCE_TT(cls, MRB_TT_CLASS);
    bob->define_method("initialize",           mrb_bob_init,             MRB_ARGS_NONE())
            .define_method("!",                    mrb_bob_not,              MRB_ARGS_NONE())
            .define_method("method_missing",       mrb_bob_missing,          MRB_ARGS_ANY());  /* 15.3.1.3.30 */

    cls->define_class_method("new",                 mrb_class_new_class,       MRB_ARGS_NONE())
            .define_method("superclass",            mrb_class_superclass,     MRB_ARGS_NONE()) /* 15.2.3.3.4 */
            .define_method("new",                   mrb_instance_new,         MRB_ARGS_ANY())  /* 15.2.3.3.3 */
            .define_method("inherited",             mrb_bob_init,             MRB_ARGS_REQ(1))
            .fin();
    mod->instance_tt(MRB_TT_MODULE)
            .define_method("class_variable_defined?", mrb_mod_cvar_defined,     MRB_ARGS_REQ(1))  /* 15.2.2.4.16 */
            .define_method("class_variable_get",      mrb_mod_cvar_get,         MRB_ARGS_REQ(1))  /* 15.2.2.4.17 */
            .define_method("class_variable_set",      mrb_mod_cvar_set,         MRB_ARGS_REQ(2)) /* 15.2.2.4.18 */
            .define_method("extend_object",           mrb_mod_extend_object,    MRB_ARGS_REQ(1)) /* 15.2.2.4.25 */
            .define_method("extended",                mrb_bob_init,             MRB_ARGS_REQ(1)) /* 15.2.2.4.26 */
            .define_method("include",                 mrb_mod_include,          MRB_ARGS_ANY())  /* 15.2.2.4.27 */
            .define_method("include?",                mrb_mod_include_p,        MRB_ARGS_REQ(1)) /* 15.2.2.4.28 */
            .define_method("append_features",         mrb_mod_append_features,  MRB_ARGS_REQ(1)) /* 15.2.2.4.10 */
            .define_method("class_eval",              mrb_mod_module_eval,      MRB_ARGS_ANY())  /* 15.2.2.4.15 */
            .define_method("included",                mrb_bob_init,             MRB_ARGS_REQ(1)) /* 15.2.2.4.29 */
            .define_method("included_modules",        mrb_mod_included_modules, MRB_ARGS_NONE()) /* 15.2.2.4.30 */
            .define_method("instance_methods",        mrb_mod_instance_methods, MRB_ARGS_OPT(1))  /* 15.2.2.4.33 */
            .define_method("method_defined?",         mrb_mod_method_defined,   MRB_ARGS_REQ(1)) /* 15.2.2.4.34 */
            .define_method("module_eval",             mrb_mod_module_eval,      MRB_ARGS_ANY())  /* 15.2.2.4.35 */
            .define_method("remove_class_variable",   mrb_mod_remove_cvar,      MRB_ARGS_REQ(1)) /* 15.2.2.4.39 */
            .define_method("private",                 mrb_mod_dummy_visibility, MRB_ARGS_ANY())  /* 15.2.2.4.36 */
            .define_method("protected",               mrb_mod_dummy_visibility, MRB_ARGS_ANY())  /* 15.2.2.4.37 */
            .define_method("public",                  mrb_mod_dummy_visibility, MRB_ARGS_ANY())  /* 15.2.2.4.38 */
            .define_method("remove_method",           mrb_mod_remove_method,    MRB_ARGS_ANY())  /* 15.2.2.4.41 */
            .define_method("to_s",                    mrb_mod_to_s,             MRB_ARGS_NONE())
            .define_method("inspect",                 mrb_mod_to_s,             MRB_ARGS_NONE())
            .define_method("alias_method",            mrb_mod_alias,            MRB_ARGS_ANY())  /* 15.2.2.4.8 */
            .define_method("ancestors",               mrb_mod_ancestors,        MRB_ARGS_NONE()) /* 15.2.2.4.9 */
            .define_method("undef_method",            mrb_mod_undef,            MRB_ARGS_ANY())  /* 15.2.2.4.41 */
            .define_method("const_defined?",          mrb_mod_const_defined,    MRB_ARGS_REQ(1)) /* 15.2.2.4.20 */
            .define_method("const_get",               mrb_mod_const_get,        MRB_ARGS_REQ(1)) /* 15.2.2.4.21 */
            .define_method("const_set",               mrb_mod_const_set,        MRB_ARGS_REQ(2)) /* 15.2.2.4.23 */
            .define_method("constants",               mrb_mod_constants,        MRB_ARGS_NONE()) /* 15.2.2.4.24 */
            .define_method("remove_const",            mrb_mod_remove_const,     MRB_ARGS_REQ(1)) /* 15.2.2.4.40 */
            .define_method("const_missing",           mrb_mod_const_missing,    MRB_ARGS_REQ(1))
            .define_method("define_method",           mod_define_method,        MRB_ARGS_REQ(1))
            .define_method("class_variables",         mrb_mod_class_variables,  MRB_ARGS_NONE()) /* 15.2.2.4.19 */
            .define_method("===",                     mrb_mod_eqq,              MRB_ARGS_REQ(1))
            .define_class_method("constants",         mrb_mod_s_constants,      MRB_ARGS_ANY())  /* 15.2.2.3.1 */
            .fin();
    cls->undef_method("append_features")
            .undef_method("extend_object");

}
