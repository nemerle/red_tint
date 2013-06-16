/*
** print.c - Kernel.#p
**
** See Copyright Notice in mruby.h
*/

#include "mruby.h"
#include "mruby/string.h"

static void printstr(const mrb_state *, const mrb_value &obj)
{
#ifdef ENABLE_STDIO
    RString *str;
    if (mrb_is_a_string(obj)) {
        str = mrb_str_ptr(obj);
        fwrite(str->m_ptr, str->len, 1, stdout);
    }
#endif
}
void mrb_p(mrb_state *mrb, mrb_value obj)
{
#ifdef ENABLE_STDIO
    obj = mrb->funcall(obj, "inspect", 0);
    printstr(mrb, obj);
    putc('\n', stdout);
#endif
}
void mrb_print_error(mrb_state *mrb)
{
#ifdef ENABLE_STDIO

    mrb_print_backtrace(mrb);
    mrb_value s = mrb->funcall(mrb_obj_value(mrb->m_exc), "inspect", 0);

    if (mrb_is_a_string(s)) {
        RString *str = mrb_str_ptr(s);
        fwrite(str->m_ptr, str->len, 1, stderr);
        putc('\n', stderr);
    }
#endif
}
void mrb_show_version(mrb_state *mrb)
{
    static const char version_msg[] = "mruby - Embeddable Ruby  Copyright (c) 2010-2013 mruby developers\n";
    mrb_value msg;

    msg = mrb_str_new(mrb, version_msg, sizeof(version_msg) - 1);
    printstr(mrb, msg);
}

void mrb_show_copyright(mrb_state *mrb)
{
    static const char copyright_msg[] = "mruby - Copyright (c) 2010-2013 mruby developers\n";
    mrb_value msg;

    msg = mrb_str_new(mrb, copyright_msg, sizeof(copyright_msg) - 1);
    printstr(mrb, msg);
}
