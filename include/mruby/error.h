/*
** error.h - Exception class
**
** See Copyright Notice in mruby.h
*/

#pragma once
struct RObject;
struct RClass;
struct RString;
void mrb_sys_fail(mrb_state *mrb, const char *mesg);
int sysexit_status(mrb_state *mrb, mrb_value err);
mrb_value mrb_exc_new_str(struct RClass* c, mrb_value str);
mrb_value mrb_exc_new_str(RClass* c, RString *str);
#define mrb_exc_new_str_lit(mrb, c, lit) mrb_exc_new_str(mrb, c, mrb_str_new_lit(mrb, (lit)))
mrb_value make_exception(mrb_state *mrb, int argc, mrb_value *argv, int isstr);
mrb_value mrb_make_exception(mrb_state *mrb, int argc, mrb_value *argv);
RString *mrb_format(mrb_state *mrb, const char *format, ...);
void mrb_exc_print(mrb_state *mrb, RObject *exc);
void mrb_print_backtrace(mrb_state *mrb);
mrb_value mrb_exc_backtrace(mrb_state *mrb, mrb_value exc);
void mrb_get_backtrace_at(mrb_state *mrb, mrb_callinfo *ci, mrb_code *pc0);

