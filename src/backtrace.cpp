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
#include "mruby/proc.h"
#include "mruby/array.h"
#include "mruby/string.h"
#include "mruby/class.h"
#include "mruby/variable.h"
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

static void
get_backtrace_i(mrb_state *mrb, void *stream, int level, const char *format, ...)
{
    va_list ap;
    mrb_value ary, str;

    if (level > 0) {
        return;
    }

    int ai = mrb->gc().arena_save();
    ary = mrb_obj_value((RArray*)stream);

    va_start(ap, format);
    str = mrb_str_new(mrb, 0, vsnprintf(NULL, 0, format, ap) + 1);
    va_end(ap);

    va_start(ap, format);
    vsnprintf(RSTRING_PTR(str), RSTRING_LEN(str), format, ap);
    va_end(ap);
    mrb_str_resize(mrb, str, RSTRING_LEN(str) - 1);
    mrb_ary_ptr(ary)->push(str);
    mrb->gc().arena_restore(ai);
}
extern mrb_value mrb_mod_cv_get(mrb_state *mrb, RClass * c, mrb_sym sym);
static void
mrb_output_backtrace(mrb_state *mrb, RObject *exc, output_stream_func func, void *stream)
{
    mrb_callinfo *ci;
    mrb_int ciidx;
    const char *filename, *method, *sep;
    int i, line;

    func(mrb, stream, 1, "trace:\n");
    ciidx = mrb_fixnum(exc->iv_get(mrb->intern2("ciidx", 5)));
    if (ciidx >= mrb->m_ctx->ciend - mrb->m_ctx->cibase)
        ciidx = 10; /* ciidx is broken... */

    for (i = ciidx; i >= 0; i--) {
        ci = &mrb->m_ctx->cibase[i];
        filename = nullptr;
        line = -1;
        if (MRB_PROC_CFUNC_P(ci->proc)) {
            continue;
        }
        else {

            mrb_irep *irep = ci->proc->body.irep;
            mrb_code *pc;
            if (ci->err) {
                pc = ci->err;
            }
            else if (i+1 <= ciidx) {
                pc = mrb->m_ctx->cibase[i+1].pc - 1;
            }
            else {
                pc = (mrb_code*)mrb_cptr(exc->iv_get(mrb->intern2("lastpc", 6)));
            }
            filename = mrb_debug_get_filename(irep, pc - irep->iseq);
            line = mrb_debug_get_line(irep, pc - irep->iseq);
        }
        if (line == -1) continue;
        if (ci->target_class == ci->proc->target_class)
            sep = ".";
        else
            sep = "#";
        if (!filename) {
            filename = "(unknown)";
        }
        method = mrb_sym2name(mrb, ci->mid);
        if (method) {
            const char *cn = ci->proc->target_class->class_name();
            if (cn) {
                func(mrb, stream, 1, "\t[%d] ", i);
                func(mrb, stream, 0, "%s:%d:in %s%s%s", filename, line, cn, sep, method);
                func(mrb, stream, 1, "\n");
            }
            else {
                func(mrb, stream, 1, "\t[%d] ", i);
                func(mrb, stream, 0, "%s:%d:in %s", filename, line, method);
                func(mrb, stream, 1, "\n");            }
        }
        else {
            func(mrb, stream, 1, "\t[%d] ", i);
            func(mrb, stream, 0, "%s:%d", filename, line);
            func(mrb, stream, 1, "\n");
        }
    }
}

void mrb_print_backtrace(mrb_state *mrb)
{
#ifdef ENABLE_STDIO
    mrb_output_backtrace(mrb, mrb->m_exc, print_backtrace_i, (void *)stderr);
#endif
}
mrb_value
mrb_get_backtrace(mrb_state *mrb, mrb_value self)
{
    mrb_value ary;

    ary = mrb_ary_new(mrb);
    mrb_output_backtrace(mrb, mrb_ptr(self), get_backtrace_i, (void*)mrb_ary_ptr(ary));

    return ary;
}
