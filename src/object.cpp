/*
** object.c - Object, NilClass, TrueClass, FalseClass class
**
** See Copyright Notice in mruby.h
*/

#include "mruby.h"
#include "mruby/class.h"
#include "mruby/numeric.h"
#include "mruby/string.h"

bool mrb_obj_eq(mrb_value v1, mrb_value v2)
{
    if (mrb_type(v1) != mrb_type(v2)) return false;
    switch (mrb_type(v1)) {
        case MRB_TT_TRUE:
            return true;

        case MRB_TT_FALSE:
        case MRB_TT_FIXNUM:
            return (v1.value.i == v2.value.i);
        case MRB_TT_SYMBOL:
            return (v1.value.sym == v2.value.sym);

        case MRB_TT_FLOAT:
            return (mrb_float(v1) == mrb_float(v2));

        default:
            return (v1.basic_ptr() == v2.basic_ptr());
    }
}

bool mrb_obj_equal(mrb_value v1, mrb_value v2)
{
    /* temporary definition */
    return mrb_obj_eq(v1, v2);
}

bool mrb_equal(mrb_state *mrb, mrb_value obj1, mrb_value obj2)
{

    if (mrb_obj_eq(obj1, obj2))
        return true;
    mrb_value result = mrb->funcall(obj1, "==", 1, obj2);
    return result.to_bool();
}

/*
 * Document-class: NilClass
 *
 *  The class of the singleton object <code>nil</code>.
 */

/* 15.2.4.3.4  */
/*
 * call_seq:
 *   nil.nil?               -> true
 *
 * Only the object <i>nil</i> responds <code>true</code> to <code>nil?</code>.
 */

static mrb_value mrb_true(mrb_state *mrb, mrb_value obj)
{
    return mrb_true_value();
}

/* 15.2.4.3.5  */
/*
 *  call-seq:
 *     nil.to_s    -> ""
 *
 *  Always returns the empty string.
 */

static mrb_value nil_to_s(mrb_state *mrb, mrb_value obj)
{
    return mrb_str_new(mrb, 0, 0);
}

static mrb_value nil_inspect(mrb_state *mrb, mrb_value obj)
{
    return mrb_str_new_lit(mrb, "nil")->wrap();
}

/***********************************************************************
 *  Document-class: TrueClass
 *
 *  The global value <code>true</code> is the only instance of class
 *  <code>TrueClass</code> and represents a logically true value in
 *  boolean expressions. The class provides operators allowing
 *  <code>true</code> to be used in logical expressions.
 */

/* 15.2.5.3.1  */
/*
 *  call-seq:
 *     true & obj    -> true or false
 *
 *  And---Returns <code>false</code> if <i>obj</i> is
 *  <code>nil</code> or <code>false</code>, <code>true</code> otherwise.
 */

static mrb_value true_and(mrb_state *mrb, mrb_value obj)
{
    mrb_bool obj2;

    mrb_get_args(mrb, "b", &obj2);

    return mrb_value::wrap(obj2);
}

/* 15.2.5.3.2  */
/*
 *  call-seq:
 *     true ^ obj   -> !obj
 *
 *  Exclusive Or---Returns <code>true</code> if <i>obj</i> is
 *  <code>nil</code> or <code>false</code>, <code>false</code>
 *  otherwise.
 */

static mrb_value true_xor(mrb_state *mrb, mrb_value obj)
{
    mrb_bool obj2;

    mrb_get_args(mrb, "b", &obj2);
    return mrb_value::wrap(!obj2);
}

/* 15.2.5.3.3  */
/*
 * call-seq:
 *   true.to_s   ->  "true"
 *
 * The string representation of <code>true</code> is "true".
 */

static mrb_value true_to_s(mrb_state *mrb, mrb_value obj)
{
    return mrb_str_new_lit(mrb, "true")->wrap();
}

/* 15.2.5.3.4  */
/*
 *  call-seq:
 *     true | obj   -> true
 *
 *  Or---Returns <code>true</code>. As <i>anObject</i> is an argument to
 *  a method call, it is always evaluated; there is no short-circuit
 *  evaluation in this case.
 *
 *     true |  puts("or")
 *     true || puts("logical or")
 *
 *  <em>produces:</em>
 *
 *     or
 */

static mrb_value true_or(mrb_state *mrb, mrb_value obj)
{
    return mrb_true_value();
}

/*
 *  Document-class: FalseClass
 *
 *  The global value <code>false</code> is the only instance of class
 *  <code>FalseClass</code> and represents a logically false value in
 *  boolean expressions. The class provides operators allowing
 *  <code>false</code> to participate correctly in logical expressions.
 *
 */

/* 15.2.4.3.1  */
/* 15.2.6.3.1  */
/*
 *  call-seq:
 *     false & obj   -> false
 *     nil & obj     -> false
 *
 *  And---Returns <code>false</code>. <i>obj</i> is always
 *  evaluated as it is the argument to a method call---there is no
 *  short-circuit evaluation in this case.
 */

static mrb_value false_and(mrb_state *mrb, mrb_value obj)
{
    return mrb_value::_false();
}

/* 15.2.4.3.2  */
/* 15.2.6.3.2  */
/*
 *  call-seq:
 *     false ^ obj    -> true or false
 *     nil   ^ obj    -> true or false
 *
 *  Exclusive Or---If <i>obj</i> is <code>nil</code> or
 *  <code>false</code>, returns <code>false</code>; otherwise, returns
 *  <code>true</code>.
 *
 */

static mrb_value false_xor(mrb_state *mrb, mrb_value obj)
{
    mrb_bool obj2;

    mrb_get_args(mrb, "b", &obj2);
    return mrb_value::wrap(obj2);
}

/* 15.2.4.3.3  */
/* 15.2.6.3.4  */
/*
 *  call-seq:
 *     false | obj   ->   true or false
 *     nil   | obj   ->   true or false
 *
 *  Or---Returns <code>false</code> if <i>obj</i> is
 *  <code>nil</code> or <code>false</code>; <code>true</code> otherwise.
 */

static mrb_value false_or(mrb_state *mrb, mrb_value obj)
{
    mrb_bool obj2;

    mrb_get_args(mrb, "b", &obj2);
    return mrb_value::wrap(obj2);
}

/* 15.2.6.3.3  */
/*
 * call-seq:
 *   false.to_s   ->  "false"
 *
 * 'nuf said...
 */

static mrb_value false_to_s(mrb_state *mrb, mrb_value obj)
{
    return mrb_str_new_lit(mrb, "false")->wrap();
}

void mrb_init_object(mrb_state *mrb)
{
    mrb->nil_class = &mrb->define_class("NilClass",   mrb->object_class)
            .undef_class_method("new")
            .define_method("&",    false_and,      MRB_ARGS_REQ(1))  /* 15.2.4.3.1  */
            .define_method("^",    false_xor,      MRB_ARGS_REQ(1))  /* 15.2.4.3.2  */
            .define_method("|",    false_or,       MRB_ARGS_REQ(1))  /* 15.2.4.3.3  */
            .define_method("nil?", mrb_true,       MRB_ARGS_NONE())  /* 15.2.4.3.4  */
            .define_method("to_s", nil_to_s,       MRB_ARGS_NONE())  /* 15.2.4.3.5  */
            .define_method("inspect", nil_inspect, MRB_ARGS_NONE())
            ;

    mrb->true_class = &mrb->define_class("TrueClass",  mrb->object_class)
            .undef_class_method("new")
            .define_method("&",    true_and,       MRB_ARGS_REQ(1))  /* 15.2.5.3.1  */
            .define_method("^",    true_xor,       MRB_ARGS_REQ(1))  /* 15.2.5.3.2  */
            .define_method("to_s", true_to_s,      MRB_ARGS_NONE())  /* 15.2.5.3.3  */
            .define_method("|",    true_or,        MRB_ARGS_REQ(1))  /* 15.2.5.3.4  */
            .define_method("inspect", true_to_s,   MRB_ARGS_NONE())
            ;

    mrb->false_class = &mrb->define_class("FalseClass", mrb->object_class)
            .undef_class_method("new")
            .define_method("&",    false_and,      MRB_ARGS_REQ(1))  /* 15.2.6.3.1  */
            .define_method("^",    false_xor,      MRB_ARGS_REQ(1))  /* 15.2.6.3.2  */
            .define_method("to_s", false_to_s,     MRB_ARGS_NONE())  /* 15.2.6.3.3  */
            .define_method("|",    false_or,       MRB_ARGS_REQ(1))  /* 15.2.6.3.4  */
            .define_method("inspect", false_to_s,  MRB_ARGS_NONE())
            ;
}
static RString *inspect_type(mrb_state *mrb, const mrb_value &val)
{
    if (mrb_type(val) == MRB_TT_FALSE || mrb_type(val) == MRB_TT_TRUE) {
        return mrb_inspect(mrb, val);
    }
    else {
        return mrb_str_new_cstr(mrb, mrb_obj_classname(mrb, val));
    }
}
static mrb_value convert_type(mrb_state *mrb, const mrb_value &val, const char *tname, const char *method, bool raise)
{
    mrb_sym m = mrb_intern_cstr(mrb, method);
    if (val.respond_to(mrb, m))
        return mrb_funcall_argv(mrb, val, m, 0, 0);

    if (raise)
        mrb->mrb_raisef(E_TYPE_ERROR, "can't convert %S into %S", inspect_type(mrb, val), mrb_str_new_cstr(mrb, tname));

    return mrb_value::nil();
}

mrb_value mrb_check_to_integer(mrb_state *mrb, mrb_value val, const char *method)
{
    if (mrb_type(val) == MRB_TT_FIXNUM)
        return val;
    mrb_value v = convert_type(mrb, val, "Integer", method, false);
    if (v.is_nil() || mrb_type(v) != MRB_TT_FIXNUM)
        return mrb_value::nil();
    return v;
}

mrb_value mrb_convert_type(mrb_state *mrb, const mrb_value &val, mrb_vtype type, const char *tname, const char *method)
{
    if (mrb_type(val) == type)
        return val;
    mrb_value v = convert_type(mrb, val, tname, method, 1/*Qtrue*/);
    if (mrb_type(v) != type) {
        mrb->mrb_raisef(E_TYPE_ERROR, "%S cannot be converted to %S by #%S", val,
                        mrb_str_new_cstr(mrb, tname), mrb_str_new_cstr(mrb, method));
    }
    return v;
}

mrb_value mrb_check_convert_type(mrb_state *mrb, const mrb_value &val, mrb_vtype type, const char *tname, const char *method)
{
    if (mrb_type(val) == type && type != MRB_TT_DATA)
        return val;
    mrb_value v = convert_type(mrb, val, tname, method, false);
    if (v.is_nil() || mrb_type(v) != type)
        return mrb_value::nil();
    return v;
}

static const struct types {
    char type;
    const char *name;
} builtin_types[] = {
    //    {MRB_TT_NIL,  "nil"},
{MRB_TT_FALSE,  "false"},
{MRB_TT_TRUE,   "true"},
{MRB_TT_FIXNUM, "Fixnum"},
{MRB_TT_SYMBOL, "Symbol"},  /* :symbol */
{MRB_TT_MODULE, "Module"},
{MRB_TT_OBJECT, "Object"},
{MRB_TT_CLASS,  "Class"},
{MRB_TT_ICLASS, "iClass"},  /* internal use: mixed-in module holder */
{MRB_TT_SCLASS, "SClass"},
{MRB_TT_PROC,   "Proc"},
{MRB_TT_FLOAT,  "Float"},
{MRB_TT_ARRAY,  "Array"},
{MRB_TT_HASH,   "Hash"},
{MRB_TT_STRING, "String"},
{MRB_TT_RANGE,  "Range"},
{MRB_TT_FILE,   "File"},
{MRB_TT_DATA,   "Data"},  /* internal use: wrapped C pointers */
//    {MRB_TT_VARMAP,  "Varmap"},  /* internal use: dynamic variables */
//    {MRB_TT_NODE,  "Node"},  /* internal use: syntax tree node */
//    {MRB_TT_UNDEF,  "undef"},  /* internal use: #undef; should not happen */
{-1,  0}
};

void mrb_check_type(mrb_state *mrb, mrb_value x, mrb_vtype t)
{
    const struct types *type = builtin_types;
    RString *s;
    mrb_vtype xt = mrb_type(x);
    if ((xt == t) && (xt != MRB_TT_DATA))
        return;
    while (type->type < MRB_TT_MAXDEFINE) {
        if (type->type == t) {
            const char *etype;

            if (x.is_nil()) {
                etype = "nil";
            }
            else if (x.is_fixnum()) {
                etype = "Fixnum";
            }
            else if (x.is_symbol()) {
                etype = "Symbol";
            }
            else if (x.is_special_const()) {
                s = mrb_obj_as_string(mrb, x);
                etype = s->m_ptr;
            }
            else {
                etype = mrb_obj_classname(mrb, x);
            }
            mrb->mrb_raisef(E_TYPE_ERROR, "wrong argument type %S (expected %S)",
                            mrb_str_new_cstr(mrb, etype), mrb_str_new_cstr(mrb, type->name));
        }
        type++;
    }
    mrb->mrb_raisef(E_TYPE_ERROR, "unknown type %S (%S given)",
                    mrb_fixnum_value(t), mrb_fixnum_value(mrb_type(x)));
}

/* 15.3.1.3.46 */
/*
 *  call-seq:
 *     obj.to_s    => string
 *
 *  Returns a string representing <i>obj</i>. The default
 *  <code>to_s</code> prints the object's class and an encoding of the
 *  object id. As a special case, the top-level object that is the
 *  initial execution context of Ruby programs returns ``main.''
 */

mrb_value mrb_any_to_s(mrb_state *mrb, mrb_value obj)
{
    RString * rs = RString::create(mrb, 20);
    const char *cname = mrb_obj_classname(mrb, obj);
    rs->str_buf_cat("#<", 2);
    rs->str_cat(cname,strlen(cname));
    rs->str_cat(":", 1);
    rs->str_cat(mrb_ptr_to_str(mrb, mrb_cptr(obj)));
//    mrb_value str(mrb_value::wrap(rs));
//    mrb_str_concat(mrb, str, mrb_ptr_to_str(mrb, mrb_cptr(obj)));
    rs->str_buf_cat(">", 1);
    return mrb_value::wrap(rs);
}

/*
 *  call-seq:
 *     obj.is_a?(class)       => true or false
 *     obj.kind_of?(class)    => true or false
 *
 *  Returns <code>true</code> if <i>class</i> is the class of
 *  <i>obj</i>, or if <i>class</i> is one of the superclasses of
 *  <i>obj</i> or modules included in <i>obj</i>.
 *
 *     module M;    end
 *     class A
 *       include M
 *     end
 *     class B < A; end
 *     class C < B; end
 *     b = B.new
 *     b.instance_of? A   #=> false
 *     b.instance_of? B   #=> true
 *     b.instance_of? C   #=> false
 *     b.instance_of? M   #=> false
 *     b.kind_of? A       #=> true
 *     b.kind_of? B       #=> true
 *     b.kind_of? C       #=> false
 *     b.kind_of? M       #=> true
 */

bool mrb_value::is_kind_of(mrb_state *mrb, RClass *c)
{
    RClass *cl = RClass::mrb_class(mrb, *this);

    switch (c->tt) {
        case MRB_TT_MODULE:
        case MRB_TT_CLASS:
        case MRB_TT_ICLASS:
            break;

        default:
            mrb->mrb_raise(E_TYPE_ERROR, "class or module required");
    }

    while (cl) {
        if (cl == c || cl->mt == c->mt)
            return true;
        cl = cl->super;
    }
    return false;
}

static mrb_value mrb_to_integer(mrb_state *mrb, mrb_value val, const char *method)
{

    if (val.is_fixnum())
        return val;
    mrb_value v = convert_type(mrb, val, "Integer", method, true);
    if (!v.is_kind_of(mrb, mrb->fixnum_class)) {
        mrb_value type = inspect_type(mrb, val)->wrap();
        mrb->mrb_raisef(E_TYPE_ERROR, "can't convert %S to Integer (%S#%S gives %S)",
                        type, type, mrb_str_new_cstr(mrb, method), inspect_type(mrb, v)->wrap());
    }
    return v;
}

mrb_value mrb_to_int(mrb_state *mrb, mrb_value val)
{
    return mrb_to_integer(mrb, val, "to_int");
}

static mrb_int mrb_convert_to_integer(mrb_state *mrb, mrb_value val, int base)
{
    mrb_value tmp;

    if (val.is_nil()) {
        if (base != 0)
            goto arg_error;
        mrb->mrb_raise(E_TYPE_ERROR, "can't convert nil into Integer");
    }
    switch (mrb_type(val)) {
        case MRB_TT_FLOAT:
            if (base != 0)
                goto arg_error;
            if (FIXABLE(mrb_float(val))) {
                break;
            }
            return mrb_flo_to_fixnum(mrb, val);

        case MRB_TT_FIXNUM:
            if (base != 0)
                goto arg_error;
            return val.value.i;

        default:
            break;
    }
    if (base != 0) {
        tmp = mrb_check_string_type(mrb, val);
        if (!tmp.is_nil())
            return val.ptr<RString>()->mrb_str_to_inum(base, true);
arg_error:
        mrb->mrb_raise(E_ARGUMENT_ERROR, "base specified for non string value");
    }
    tmp = convert_type(mrb, val, "Integer", "to_int", false);
    if (tmp.is_nil()) {
        return mrb_to_integer(mrb, val, "to_i").value.i;
    }
    return tmp.value.i;
}

mrb_value mrb_Integer(mrb_state *mrb, mrb_value val)
{
    return mrb_value::wrap(mrb_convert_to_integer(mrb, val, 0));
}

mrb_value mrb_Float(mrb_state *mrb, mrb_value val)
{
    if (val.is_nil()) {
        mrb->mrb_raise(E_TYPE_ERROR, "can't convert nil into Float");
    }
    switch (mrb_type(val)) {
        case MRB_TT_FIXNUM:
            return mrb_float_value((mrb_float)mrb_fixnum(val));

        case MRB_TT_FLOAT:
            return val;

        case MRB_TT_STRING:
            return mrb_float_value(val.ptr<RString>()->to_dbl(true));

        default:
            return mrb_convert_type(mrb, val, MRB_TT_FLOAT, "Float", "to_f");
    }
}

RString *mrb_inspect(mrb_state *mrb, mrb_value obj)
{
    return mrb_obj_as_string(mrb, mrb->funcall(obj, "inspect", 0));
}

bool mrb_eql(mrb_state *mrb, mrb_value obj1, mrb_value obj2)
{
    if (mrb_obj_eq(obj1, obj2))
        return true;
    return mrb->funcall(obj1, "eql?", 1, obj2).to_bool();
}
mrb_value mrb_value::check_type(mrb_state *mrb, mrb_vtype t, const char *c, const char *m) const {

    mrb_value tmp = mrb_check_convert_type(mrb, *this, t, c, m);
    if (tmp.is_nil()) {
        mrb->mrb_raisef(E_TYPE_ERROR, "expected %S", mrb_str_new_cstr(mrb, c));
    }
    return tmp;
}

bool mrb_value::respond_to(mrb_state *mrb, mrb_sym msg) const
{
    return RClass::mrb_class(mrb, *this)->respond_to(msg);
}
bool mrb_value::is_instance_of(mrb_state *mrb, RClass* c) const
{
    return RClass::mrb_class(mrb, *this)->class_real() == c;
}

void mrb_state::get_arg(const mrb_value &arg,mrb_int &tgt) {
    switch (mrb_type(arg)) {
        case MRB_TT_FIXNUM:
            tgt = mrb_fixnum(arg);
            break;
        case MRB_TT_FLOAT:
        {
            mrb_float f = mrb_float(arg);

            if (!FIXABLE(f)) {
                mrb_raise(I_RANGE_ERROR, "float too big for int");
            }
            tgt = (mrb_int)f;
        }
            break;
        case MRB_TT_FALSE:
            tgt = 0;
            break;
        default:
        {
            mrb_value tmp;
            tmp = mrb_convert_type(this, arg, MRB_TT_FIXNUM, "Integer", "to_int");
            tgt = mrb_fixnum(tmp);
        }
            break;
    }
}
void mrb_state::get_arg(const mrb_value &arg, RClass *&tgt) {
    switch (mrb_type(arg)) {
        case MRB_TT_CLASS:
        case MRB_TT_MODULE:
        case MRB_TT_SCLASS:
            break;
        default:
            mrb_raisef(I_TYPE_ERROR, "%S is not class/module", arg);
            break;
    }
    tgt = mrb_class_ptr(arg);
}
void mrb_state::get_arg(const mrb_value &arg, mrb_sym &tgt) {

    if (mrb_type(arg) == MRB_TT_SYMBOL) {
        tgt = mrb_symbol(arg);
    }
    else if (arg.is_string()) {
        tgt = mrb_intern_str(this, arg.to_str(this));
    }
    else {
        mrb_raisef(I_TYPE_ERROR, "%S is not a symbol", funcall(arg, "inspect", 0));
    }
}
