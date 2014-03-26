#include "mruby.h"
#include "mruby/variable.h"

void
mrb_init_version(mrb_state* mrb)
{
  mrb->define_global_const("RUBY_VERSION", mrb_str_new_lit(mrb, MRUBY_RUBY_VERSION));
  mrb->define_global_const("RUBY_ENGINE", mrb_str_new_lit(mrb, MRUBY_RUBY_ENGINE));
  mrb->define_global_const("MRUBY_VERSION", mrb_str_new_lit(mrb, MRUBY_VERSION));
  mrb->define_global_const("MRUBY_RELEASE_DATE", mrb_str_new_lit(mrb, MRUBY_RELEASE_DATE));
  mrb->define_global_const("MRUBY_DESCRIPTION", mrb_str_new_lit(mrb, MRUBY_DESCRIPTION));
  mrb->define_global_const("MRUBY_COPYRIGHT", mrb_str_new_lit(mrb, MRUBY_COPYRIGHT));
}
