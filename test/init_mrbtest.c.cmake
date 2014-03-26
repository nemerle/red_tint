#include <stdlib.h>
#include "mruby.h"
#include "mruby/irep.h"
#include "mruby/dump.h"
#include "mruby/string.h"
#include "mruby/proc.h"

#include "${compiled_tests}"
#include "${compiled_gem_tests}"

void mrbgemtest_init(mrb_state* mrb);

void
mrb_init_mrbtest(mrb_state *mrb)
{
  mrb_load_irep(mrb, mrbtest_irep);
#ifndef DISABLE_GEMS
  mrbgemtest_init(mrb);
#endif
  if (mrb->exc) {
    mrb_p(mrb, mrb_value::wrap(mrb->exc));
    exit(0);
  }
}

