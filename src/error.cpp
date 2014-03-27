/*
** error.c - Exception class
**
** See Copyright Notice in mruby.h
*/

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include "mruby.h"
#include "mruby/array.h"
#include "mruby/irep.h"
#include "mruby/proc.h"
#include "mruby/string.h"
#include "mruby/variable.h"
#include "mruby/debug.h"
#include "mruby/error.h"
#include "mrb_throw.h"

mrb_value mrb_exc_new(RClass *c, const char *ptr, long len)
{
    return c->m_vm->funcall(mrb_value::wrap(c), "new", 1, mrb_str_new(c->m_vm, ptr, len));
}
mrb_value mrb_exc_new_str(RClass* c, RString *str)
{
    return c->m_vm->funcall(c->wrap(), "new", 1, str->wrap());
}

mrb_value mrb_exc_new_str(RClass* c, mrb_value str)
{
    str = mrb_str_to_str(c->m_vm, str);
    return c->m_vm->funcall(mrb_value::wrap(c), "new", 1, str);
}

/*
 * call-seq:
 *    Exception.new(msg = nil)   ->  exception
 *
 *  Construct a new Exception object, optionally passing in
 *  a message.
 */
static mrb_value exc_initialize(mrb_state *mrb, mrb_value exc)
{
    mrb_value mesg;

    if (mrb_get_args(mrb, "|o", &mesg) == 1) {
        mrb_iv_set(mrb, exc, mrb_intern(mrb, "mesg", 4), mesg);
    }
    return exc;
}

/*
 *  Document-method: exception
 *
 *  call-seq:
 *     exc.exception(string)  ->  an_exception or exc
 *
 *  With no argument, or if the argument is the same as the receiver,
 *  return the receiver. Otherwise, create a new
 *  exception object of the same class as the receiver, but with a
 *  message equal to <code>string.to_str</code>.
 *
 */

static mrb_value exc_exception(mrb_state *mrb, mrb_value self)
{
    mrb_value a;
    int argc = mrb_get_args(mrb, "|o", &a);
    if (argc == 0)
        return self;
    if (mrb_obj_equal(self, a))
        return self;
    mrb_value exc = mrb_obj_clone(mrb, self);
    mrb_iv_set(mrb, exc, mrb_intern(mrb, "mesg", 4), a);

    return exc;
}

/*
 * call-seq:
 *   exception.to_s   ->  string
 *
 * Returns exception's message (or the name of the exception if
 * no message is set).
 */

static mrb_value exc_to_s(mrb_state *mrb, mrb_value exc)
{
    mrb_value mesg = mrb_attr_get(exc, mrb_intern(mrb, "mesg", 4));

    if (mesg.is_nil())
        return mrb_str_new_cstr(mrb, mrb_obj_classname(mrb, exc))->wrap();
    return mesg;
}

/*
 * call-seq:
 *   exception.message   ->  string
 *
 * Returns the result of invoking <code>exception.to_s</code>.
 * Normally this returns the exception's message or name. By
 * supplying a to_str method, exceptions are agreeing to
 * be used where Strings are expected.
 */

static mrb_value exc_message(mrb_state *mrb, mrb_value exc)
{
    return mrb->funcall(exc, "to_s", 0);
}

/*
 * call-seq:
 *   exception.inspect   -> string
 *
 * Return this exception's class name an message
 */

static mrb_value exc_inspect(mrb_state *mrb, mrb_value exc)
{
    RString *str;
    mrb_value mesg = mrb_attr_get(exc, mrb->intern2("mesg", 4));
    mrb_value file = mrb_attr_get(exc, mrb->intern2("file", 4));
    mrb_value line = mrb_attr_get(exc, mrb->intern2("line", 4));
    RString *mesg_ptr = mesg.is_nil() ? nullptr : mesg.ptr<RString>();
    if (!file.is_nil() && !line.is_nil()) {
        assert(file.is_string());
        str = file.ptr<RString>()->dup();
        str->str_buf_cat(":",1);
        str->str_cat(mrb_obj_as_string(mrb,line));
        str->str_buf_cat(": ",2);
        if (mesg_ptr && mesg_ptr->len > 0) {
            str->str_cat(mesg_ptr);
            str->str_buf_cat(" (",2);
        }
        str->str_buf_cat(mrb_obj_classname(mrb, exc));
        if (mesg_ptr && mesg_ptr->len > 0) {
            str->str_buf_cat(")",1);
        }
    }
    else {
        str = RString::create(mrb,mrb_obj_classname(mrb, exc));
        str->str_buf_cat(": ",2);
        if (mesg_ptr && mesg_ptr->len > 0) {
            str->str_cat(mesg_ptr);
        } else {
            str->str_buf_cat(mrb_obj_classname(mrb, exc));
        }
    }
    return str->wrap();
}


static mrb_value exc_equal(mrb_state *mrb, mrb_value exc)
{
    mrb_value mesg;
    mrb_bool equal_p;
    mrb_sym id_mesg = mrb_intern(mrb, "mesg", 4);
    mrb_value obj = mrb->get_arg<mrb_value>();
    if (mrb_obj_equal(exc, obj)) {
        equal_p = 1;
    }
    else {
        if (mrb_obj_class(mrb, exc) != mrb_obj_class(mrb, obj)) {
            if (obj.respond_to(mrb, mrb->intern2("message", 7))) {
                mesg = mrb->funcall(obj, "message", 0);
            }
            else
                return mrb_value::_false();
        }
        else {
            mesg = mrb_attr_get(obj, id_mesg);
        }

        equal_p = mrb_equal(mrb, mrb_attr_get(exc, id_mesg), mesg);
    }

    return mrb_value::wrap(equal_p);
}

static void exc_debug_info(mrb_state *mrb, RObject *exc)
{
    mrb_callinfo *ci = mrb->m_ctx->m_ci;
    mrb_code *pc = ci->pc;

    exc->iv_set(mrb_intern(mrb, "ciidx", 5), mrb_fixnum_value(ci - mrb->m_ctx->cibase));

    while (ci >= mrb->m_ctx->cibase) {
        mrb_code *err = ci->err;

        if (!err && pc) err = pc - 1;
        if (err && ci->proc && !ci->proc->is_cfunc()) {
            mrb_irep *irep = ci->proc->ireps();
            int32_t const line = mrb_debug_get_line(irep, err - irep->iseq);
            char const* file = mrb_debug_get_filename(irep, err - irep->iseq);
            if (line != -1 && file) {
                exc->iv_set(mrb->intern2("file", 4), mrb_str_new_cstr(mrb, file)->wrap());
                exc->iv_set(mrb->intern2("line", 4), mrb_fixnum_value(line));
                return;
            }
        }
        pc = ci->pc;
        ci--;
    }
}

void mrb_exc_raise(mrb_state *mrb, mrb_value exc)
{
    mrb->m_exc = exc.object_ptr();
    exc_debug_info(mrb, mrb->m_exc);
    if (!mrb->jmp) {
        mrb_p(mrb, exc);
        abort();
    }
    MRB_THROW(mrb->jmp);
}

void mrb_raise(RClass *c, const char *msg)
{
    auto mesg = mrb_str_new_cstr(c->m_vm, msg);
    mrb_exc_raise(c->m_vm, mrb_exc_new_str(c, mesg));
}

void mrb_state::mrb_raise( RClass *c, const char *msg)
{
    auto mesg = mrb_str_new_cstr(this, msg);
    mrb_exc_raise(this, mrb_exc_new_str(c, mesg));
}
RString *mrb_vformat(mrb_state *mrb, const char *format, va_list ap)
{
    const char *p = format;
    const char *b = p;
    ptrdiff_t size;
    RArray *p_ary = RArray::create(mrb,4);

    while (*p) {
        const char c = *p++;

        if (c == '%') {
            if (*p == 'S') {
                size = p - b - 1;
                p_ary->push(mrb_str_new(mrb, b, size));
                p_ary->push(va_arg(ap, mrb_value));
                b = p + 1;
            }
        }
        else if (c == '\\') {
            if (*p) {
                size = p - b - 1;
                p_ary->push(mrb_str_new(mrb, b, size));
                p_ary->push(mrb_str_new(mrb, p, 1));
                b = ++p;
            }
            else {
                break;
            }
        }
    }
    if (b == format) {
        return mrb_str_new_cstr(mrb, format);
    }
    else {
        size = p - b;
        p_ary->push(mrb_str_new(mrb, b, size));
        return p_ary->join(mrb_str_new(mrb,NULL,0));
    }
}

RString *mrb_format(mrb_state *mrb, const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    auto str = mrb_vformat(mrb, format, ap);
    va_end(ap);

    return str;
}

void mrb_state::mrb_raisef(RClass *c, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    auto mesg = mrb_vformat(this, fmt, args);
    va_end(args);
    mrb_exc_raise(this, mrb_exc_new_str(c, mesg));
}

void mrb_name_error(mrb_state *mrb, mrb_sym id, const char *fmt, ...)
{
    mrb_value exc;
    mrb_value argv[2];
    va_list args;

    va_start(args, fmt);
    argv[0] = mrb_vformat(mrb, fmt, args)->wrap();
    va_end(args);
    argv[1] = mrb_symbol_value(id);
    exc = E_NAME_ERROR->new_instance(2, argv);
    mrb_exc_raise(mrb, exc);
}

void mrb_warn(mrb_state *mrb, const char *fmt, ...)
{
#ifdef ENABLE_STDIO
    va_list ap;

    va_start(ap, fmt);
    auto str = mrb_vformat(mrb, fmt, ap);
    fputs("warning: ", stderr);
    fwrite(str->m_ptr, str->len, 1, stderr);
    va_end(ap);
#endif
}

void mrb_bug(mrb_state *mrb, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    auto str = mrb_vformat(mrb, fmt, ap);
    fputs("bug: ", stderr);
    mrb->sys.error_f("%s",str->m_ptr);
    //fwrite(RSTRING_PTR(str), RSTRING_LEN(str), 1, stderr);
    va_end(ap);
    exit(EXIT_FAILURE);
}

int sysexit_status(mrb_state *mrb, mrb_value err)
{
    mrb_value st = err.mrb_iv_get(mrb_intern(mrb, "status", 6));
    return mrb_fixnum(st);
}

static void set_backtrace(mrb_state *mrb, mrb_value info, mrb_value bt)
{
    mrb->funcall(info, "set_backtrace", 1, bt);
}

mrb_value make_exception(mrb_state *mrb, int argc, mrb_value *argv, int isstr)
{
    mrb_value mesg;
    int n;

    mesg = mrb_value::nil();
    switch (argc) {
        case 0:
            break;
        case 1:
            if (argv[0].is_nil())
                break;
            if (isstr) {
                mesg = mrb_check_string_type(mrb, argv[0]);
                if (!mesg.is_nil()) {
                    mesg = mrb_exc_new_str(E_RUNTIME_ERROR, mesg);
                    break;
                }
            }
            n = 0;
            goto exception_call;

        case 2:
        case 3:
            n = 1;
exception_call:
        {
            mrb_sym exc = mrb_intern(mrb, "exception", 9);
            if (argv[0].respond_to(mrb, exc)) {
                mesg = mrb_funcall_argv(mrb, argv[0], exc, n, argv+1);
            }
            else {
                /* undef */
                mrb->mrb_raise(E_TYPE_ERROR, "exception class/object expected");
            }
        }

            break;
        default:
            mrb->mrb_raisef(E_ARGUMENT_ERROR, "wrong number of arguments (%S for 0..3)", mrb_fixnum_value(argc));
            break;
    }
    if (argc > 0) {
        if (!mesg.is_kind_of(mrb, mrb->eException_class))
            mrb->mrb_raise(E_TYPE_ERROR, "exception object expected");
        if (argc > 2)
            set_backtrace(mrb, mesg, argv[2]);
    }

    return mesg;
}

mrb_value mrb_make_exception(mrb_state *mrb, int argc, mrb_value *argv)
{
    return make_exception(mrb, argc, argv, true);
}

void mrb_sys_fail(mrb_state *mrb, const char *mesg)
{
    mrb_int no;

    no = (mrb_int)errno;
    if (mrb->class_defined("SystemCallError")) {
        RClass *sce = mrb->class_get("SystemCallError");
        if (mesg != NULL) {
            mrb->funcall(mrb_value::wrap(sce), "_sys_fail", 2, mrb_fixnum_value(no), mrb_str_new_cstr(mrb, mesg));
        } else {
            mrb->funcall(mrb_value::wrap(sce), "_sys_fail", 1, mrb_fixnum_value(no));
        }
    } else {
        mrb->mrb_raise(E_RUNTIME_ERROR, mesg);
    }
}
#ifdef MRB_ENABLE_CXX_EXCEPTION
mrb_int mrb_jmpbuf::jmpbuf_id = 0;
#endif
void mrb_init_exception(mrb_state *mrb)
{
    RClass *e;

    e = mrb->eException_class = &mrb->define_class("Exception", mrb->object_class);        /* 15.2.22 */
    e->define_class_method("exception", mrb_instance_new, MRB_ARGS_ANY())
            .define_method("exception", exc_exception, MRB_ARGS_ANY())
            .define_method("initialize", exc_initialize, MRB_ARGS_ANY())
            .define_method("==", exc_equal, MRB_ARGS_REQ(1))
            .define_method("to_s", exc_to_s, MRB_ARGS_NONE())
            .define_method("message", exc_message, MRB_ARGS_NONE())
            .define_method("inspect", exc_inspect, MRB_ARGS_NONE())
            .define_method("backtrace", mrb_exc_backtrace, MRB_ARGS_NONE())
            .fin();

    mrb->eStandardError_class = &mrb->define_class("StandardError",       mrb->eException_class);       /* 15.2.23 */
    mrb->define_class("RuntimeError", mrb->eStandardError_class);                                       /* 15.2.28 */
    e = &mrb->define_class("ScriptError",  mrb->eException_class);                                    /* 15.2.37 */
    mrb->define_class("SyntaxError",  e);                                                           /* 15.2.38 */
}
