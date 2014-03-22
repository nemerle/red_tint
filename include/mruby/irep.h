/*
** mruby/irep.h - mrb_irep structure
**
** See Copyright Notice in mruby.h
*/
#pragma once

#define MRB_ISEQ_NO_FREE 1

struct mrb_irep {
    uint16_t idx; //FIXME: this overflows when large number of closures is created.
    uint16_t nlocals; /* Number of local variables */
    uint16_t nregs;/* Number of register variables */
    uint8_t flags;

    mrb_code *iseq;
    struct irep_pool_string {
        mrb_int len;
        char buf[1];
    };
    struct irep_pool {
        union {
            mrb_float f;
            irep_pool_string *s;
            mrb_int i;
        } value;
        enum mrb_vtype type;
    } *m_pool;
    mrb_sym *syms;
    struct mrb_irep **reps;

    /* debug info */
    const char *filename;
    uint16_t *lines;
    struct mrb_irep_debug_info* debug_info;
    size_t ilen, plen, slen, rlen, refcnt;
};

mrb_irep *mrb_add_irep(mrb_state *mrb);
mrb_value mrb_load_irep(mrb_state*, const uint8_t*);
void mrb_irep_free(mrb_state*, struct mrb_irep*);
void mrb_irep_incref(mrb_state*, struct mrb_irep*);
void mrb_irep_decref(mrb_state*, struct mrb_irep*);
