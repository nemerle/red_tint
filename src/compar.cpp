/*
** compar.c - Comparable module
**
** See Copyright Notice in mruby.h
*/

#include "mruby.h"

void mrb_init_comparable(mrb_state *mrb)
{
    mrb->define_module("Comparable");
}
