/*
** backtrace.c -
**
** See Copyright Notice in mruby.h
*/

#include <stdarg.h>
#ifdef ENABLE_STDIO
#include <stdio.h>
#endif
#include "mruby.h"
#include "mruby/variable.h"
#include "mruby/proc.h"
#include "mruby/array.h"
#include "mruby/string.h"
#include "mruby/class.h"
#include "mruby/debug.h"

typedef void (*output_stream_func)(mrb_state*, void*, int, const char*, ...);
#ifdef ENABLE_STDIO
static void
print_backtrace_i(mrb_state *mrb, void *stream, int level, const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    vfprintf((FILE*)stream, format, ap);
    va_end(ap);
}
#endif

#define MIN_BUFSIZE 127

static void get_backtrace_i(mrb_state *mrb, void *stream, int level, const char *format, ...)
{
    va_list ap;

    if (level > 0) {
        return;
    }
    RArray *ar_stream = (RArray*)stream;
    int ai = mrb->gc().arena_save();

    va_start(ap, format);
    RString *str_ptr = RString::create(mrb,0,vsnprintf(NULL, 0, format, ap) + 1);
    va_end(ap);

    va_start(ap, format);
    vsnprintf(str_ptr->m_ptr, str_ptr->len, format, ap);
    va_end(ap);
    str_ptr->resize(str_ptr->len - 1);
    ar_stream->push(mrb_value::wrap(str_ptr));
    mrb->gc().arena_restore(ai);
}

static void
output_backtrace(mrb_state *mrb, mrb_int ciidx, mrb_code *pc0, output_stream_func func, void *stream)
{
    mrb_callinfo *ci;
    const char *filename, *method, *sep;
    int i, lineno, tracehead = 1;;

    if (ciidx >= mrb->m_ctx->ciend - mrb->m_ctx->cibase)
        ciidx = 10; /* ciidx is broken... */

    for (i = ciidx; i >= 0; i--) {
        ci = &mrb->m_ctx->cibase[i];
        filename = nullptr;
        lineno = -1;
        if (!ci->proc || ci->proc->is_cfunc())
            continue;
        {
            //assert(mrb_type(*ci->proc)==MRB_TT_PROC);
            mrb_irep *irep = ci->proc->body.irep;
            mrb_code *pc;
            if (ci->err) {
                pc = ci->err;
            }
            else if (i+1 <= ciidx) {
                pc = mrb->m_ctx->cibase[i+1].pc - 1;
            }
            else {
                pc = pc0;
            }
            filename = mrb_debug_get_filename(irep, pc - irep->iseq);
            lineno = mrb_debug_get_line(irep, pc - irep->iseq);
        }
        if (lineno == -1) continue;
        if (ci->target_class == ci->proc->target_class())
            sep = ".";
        else
            sep = "#";
        if (!filename) {
            filename = "(unknown)";
        }
        if (tracehead) {
            func(mrb, stream, 1, "trace:\n");
            tracehead = 0;
        }
        method = mrb_sym2name(mrb, ci->mid);
        if (method) {
            const char *cn = ci->proc->target_class()->class_name();
            if (cn) {
                func(mrb, stream, 1, "\t[%d] ", i);
                func(mrb, stream, 0, "%s:%d:in %s%s%s", filename, lineno, cn, sep, method);
                func(mrb, stream, 1, "\n");
            }
            else {
                func(mrb, stream, 1, "\t[%d] ", i);
                func(mrb, stream, 0, "%s:%d:in %s", filename, lineno, method);
                func(mrb, stream, 1, "\n");            }
        }
        else {
            func(mrb, stream, 1, "\t[%d] ", i);
            func(mrb, stream, 0, "%s:%d", filename, lineno);
            func(mrb, stream, 1, "\n");
        }
    }
}
static void
exc_output_backtrace(mrb_state *mrb, RObject *exc, output_stream_func func, void *stream)
{
    output_backtrace(mrb, mrb_fixnum(exc->iv_get(mrb_intern_lit(mrb, "ciidx"))),
                     (mrb_code*)mrb_cptr(exc->iv_get(mrb_intern_lit(mrb, "lastpc"))),
                     func, stream);
}
/* mrb_print_backtrace/mrb_get_backtrace:

   function to retrieve backtrace information from the exception.
   note that if you call method after the exception, call stack will be
   overwritten.  So invoke these functions just after detecting exceptions.
*/
void mrb_print_backtrace(mrb_state *mrb)
{
#ifdef ENABLE_STDIO
    exc_output_backtrace(mrb, mrb->m_exc, print_backtrace_i, (void*)stderr);
#endif
}
mrb_value
mrb_exc_backtrace(mrb_state *mrb, mrb_value self)
{
    RArray *ary = RArray::create(mrb);
    exc_output_backtrace(mrb, self.object_ptr(), get_backtrace_i, ary);
    return mrb_value::wrap(ary);
}

mrb_value mrb_get_backtrace(mrb_state *mrb)
{
    mrb_callinfo *ci = mrb->m_ctx->m_ci;
    mrb_code *pc = ci->pc;
    mrb_int ciidx = ci - mrb->m_ctx->cibase - 1;

    if (ciidx < 0)
        ciidx = 0;
    RArray *arr = RArray::create(mrb);
    output_backtrace(mrb, ciidx, pc, get_backtrace_i, arr);

    return mrb_value::wrap(arr);
}
