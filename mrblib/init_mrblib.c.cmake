#include "mruby.h"
#include "mruby/irep.h"
#include "mruby/dump.h"
#include "mruby/string.h"
#include "mruby/proc.h"

#include "${compiled_libs}"
#include "${compiled_gems}"
void
mrb_init_mrblib(mrb_state *mrb)
{
  mrb_load_irep(mrb, mrblib_irep);
}

void
mrb_init_mrbgems_irep(mrb_state *mrb)
{
  mrb_load_irep(mrb, mrbgems_irep);
}
