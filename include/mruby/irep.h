/*
** mruby/irep.h - mrb_irep structure
**
** See Copyright Notice in mruby.h
*/
#pragma once

#define MRB_ISEQ_NO_FREE 1

struct mrb_irep {
    uint16_t idx; //FIXME: this overflows when large number of closures is created.
    uint16_t nlocals;
    uint16_t nregs;
    uint8_t flags;

    mrb_code *iseq;
    mrb_value *m_pool;
    mrb_sym *syms;

    /* debug info */
    const char *filename;
    uint16_t *lines;

    size_t ilen, plen, slen;
};

mrb_irep *mrb_add_irep(mrb_state *mrb);
mrb_value mrb_load_irep(mrb_state*, const uint8_t*);
