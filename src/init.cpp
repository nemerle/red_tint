/*
** init.c - initialize mruby core
**
** See Copyright Notice in mruby.h
*/
#include <stdarg.h>

#include "mruby.h"

extern "C" {
}
void mrb_init_mrbgems(mrb_state*);
void mrb_final_mrbgems(mrb_state*);

void mrb_init_mrblib(mrb_state*);
void mrb_init_object(mrb_state*);
void mrb_init_string(mrb_state*);
void mrb_init_array(mrb_state*);
void mrb_init_hash(mrb_state*);
void mrb_init_numeric(mrb_state*);
void mrb_init_range(mrb_state*);
void mrb_init_gc(mrb_state*);
void mrb_init_math(mrb_state*);
void mrb_init_class(mrb_state*);
void mrb_init_enumerable(mrb_state*);
void mrb_init_exception(mrb_state*);
void mrb_init_kernel(mrb_state*);
void mrb_init_symbol(mrb_state*);
void mrb_init_symtbl(mrb_state*);
void mrb_init_proc(mrb_state*);
void mrb_init_comparable(mrb_state*);

#define DONE mrb->gc().arena_restore(0);
void
mrb_core_init(mrb_state *mrb)
{
  mrb_init_symtbl(mrb); DONE;

  mrb_init_class(mrb); DONE;
  mrb_init_object(mrb); DONE;
  mrb_init_kernel(mrb); DONE;
  mrb_init_comparable(mrb); DONE;
  mrb_init_enumerable(mrb); DONE;

  mrb_init_symbol(mrb); DONE;
  mrb_init_exception(mrb); DONE;
  mrb_init_proc(mrb); DONE;
  mrb_init_string(mrb); DONE;
  mrb_init_array(mrb); DONE;
  mrb_init_hash(mrb); DONE;
  mrb_init_numeric(mrb); DONE;
  mrb_init_range(mrb); DONE;
  mrb_init_gc(mrb); DONE;
  mrb_init_mrblib(mrb); DONE;
#ifndef DISABLE_GEMS
  mrb_init_mrbgems(mrb); DONE;
#endif
}

void
mrb_core_final(mrb_state *mrb)
{
#ifndef DISABLE_GEMS
  mrb_final_mrbgems(mrb); DONE;
#endif
}
void SysInterface::print_f(const char *fmt,...) {
    va_list argptr;
    va_start(argptr, fmt);
    vprintf(fmt, argptr);
    va_end(argptr);
}
void SysInterface::error_f(const char *fmt,...) {
    va_list argptr;
    va_start(argptr, fmt);
    vfprintf(stderr,fmt, argptr);
    va_end(argptr);
}
