/*
** kernel.c - Kernel module suppliment
**
** See Copyright Notice in mruby.h
*/

#include "mruby.h"
#include "mruby/class.h"

mrb_value mrb_f_sprintf(mrb_state *mrb, mrb_value obj); /* in sprintf.c */

void
mrb_mruby_sprintf_gem_init(mrb_state* mrb)
{
    RClass *krn;

    if (mrb->kernel_module == NULL) {
        mrb->kernel_module = mrb_define_module(mrb, "Kernel"); /* Might be PARANOID. */
    }
    krn = mrb->kernel_module;

    krn->define_method("sprintf", mrb_f_sprintf, MRB_ARGS_ANY())
            .define_method("format",  mrb_f_sprintf, MRB_ARGS_ANY());
}

void
mrb_mruby_sprintf_gem_final(mrb_state* mrb)
{
    /* nothing to do. */
}

