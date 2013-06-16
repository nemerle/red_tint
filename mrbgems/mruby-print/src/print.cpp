#include <cstdio>
#include "mruby.h"
#include "mruby/string.h"
#include "mruby/class.h"

static void printstr(mrb_state *mrb, mrb_value obj)
{

    if (mrb_is_a_string(obj)) {
        RString *str = mrb_str_ptr(obj);
        char *s = str->m_ptr;
        int len = str->len;
        fwrite(s, len, 1, stdout);
    }
}

/* 15.3.1.2.9  */
/* 15.3.1.3.34 */
mrb_value mrb_printstr(mrb_state *mrb, mrb_value self)
{
    mrb_value argv;

    mrb_get_args(mrb, "o", &argv);
    printstr(mrb, argv);

    return argv;
}

void mrb_mruby_print_gem_init(mrb_state* mrb)
{
    RClass *krn = mrb->kernel_module;
    krn->define_method("__printstr__", mrb_printstr, MRB_ARGS_REQ(1));
}

void mrb_mruby_print_gem_final(mrb_state* mrb)
{
}
