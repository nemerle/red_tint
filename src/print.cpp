/*
** print.c - Kernel.#p
**
** See Copyright Notice in mruby.h
*/

#include "mruby.h"
#include "mruby/string.h"

static void printstr(mrb_state *mrb, const mrb_value &obj)
{
    if (obj.is_string()) {
        RString *str = obj.ptr<RString>();
        mrb->sys.print_f("%s",str->m_ptr);
    }
}
void mrb_p(mrb_state *mrb, mrb_value obj)
{    
    obj = mrb->funcall(obj, "inspect", 0);
    printstr(mrb, obj);
    mrb->sys.print_f("\n");
}
void mrb_state::print_error()
{
    mrb_print_backtrace(this);
    mrb_value s = funcall(mrb_value::wrap(m_exc), "inspect", 0);
    if (s.is_string()) {
        RString *str = s.ptr<RString>();
        sys.error_f("%s\n",str->m_ptr);
    }
}
#include "mruby/variable.h"

void mrb_show_version(mrb_state *mrb)
{
  mrb_value msg;

  msg = mrb->object_class->const_get(mrb_intern_lit(mrb, "MRUBY_DESCRIPTION"));
  printstr(mrb, msg);
  mrb->sys.print_f("\n");
}

void
mrb_show_copyright(mrb_state *mrb)
{
  mrb_value msg;

  msg = mrb->object_class->const_get(mrb_intern_lit(mrb, "MRUBY_COPYRIGHT"));
  printstr(mrb, msg);
  mrb->sys.print_f("\n");
}
