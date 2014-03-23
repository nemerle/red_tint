/*
** print.c - Kernel.#p
**
** See Copyright Notice in mruby.h
*/

#include "mruby.h"
#include "mruby/string.h"

static void printstr(mrb_state *mrb, const mrb_value &obj)
{

    RString *str;
    if (mrb_is_a_string(obj)) {
        str = mrb_str_ptr(obj);
        mrb->sys.print_f("%s",str->m_ptr);
        //fwrite(str->m_ptr, str->len, 1, stdout);
    }
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
#include "mruby/variable.h"
extern mrb_value mrb_const_get(mrb_state *mrb, mrb_value mod, mrb_sym sym);
void mrb_show_version(mrb_state *mrb)
{
  mrb_value msg;

  msg = mrb_const_get(mrb, mrb_obj_value(mrb->object_class), mrb_intern_lit(mrb, "MRUBY_DESCRIPTION"));
  printstr(mrb, msg);
  printstr(mrb, mrb_str_new_lit(mrb, "\n"));
}

void
mrb_show_copyright(mrb_state *mrb)
{
  mrb_value msg;

  msg = mrb_const_get(mrb, mrb_obj_value(mrb->object_class), mrb_intern_lit(mrb, "MRUBY_COPYRIGHT"));
  printstr(mrb, msg);
  printstr(mrb, mrb_str_new_lit(mrb, "\n"));
}
