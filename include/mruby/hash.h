/*
** mruby/hash.h - Hash class
**
** See Copyright Notice in mruby.h
*/

#pragma once
#include "mruby/value.h"
#include "mruby/khash.h"

struct ValueHashFunc {
    khint_t operator()(MemManager *m,mrb_value key) const {
        khint_t h = (khint_t)mrb_type(key) << 24;
        mrb_value h2;

        h2 = m->vm()->funcall(key, "hash", 0, 0);
        h ^= h2.value.i;
        return h;
    }
};
struct ValueHashEq {
    khint_t operator()(MemManager *m,mrb_value a,mrb_value b) const;
};


struct RHash : public RObject {
    typedef kh_T<mrb_value,mrb_value,ValueHashFunc,ValueHashEq> kh_ht_t;
    static const mrb_vtype ttype=MRB_TT_HASH;
    kh_ht_t *ht;
};

#define mrb_hash_ptr(v)    ((RHash*)((v).value.p))
#define mrb_hash_value(p)  mrb_obj_value((void*)(p))

mrb_value mrb_hash_new_capa(mrb_state*, int);
mrb_value mrb_hash_new(mrb_state *mrb);

void mrb_hash_set(mrb_state *mrb, mrb_value hash, mrb_value key, mrb_value val);
mrb_value mrb_hash_get(mrb_value hash, mrb_value key);
mrb_value mrb_hash_fetch(mrb_state *mrb, mrb_value hash, mrb_value key, mrb_value def);
mrb_value mrb_hash_delete_key(mrb_value hash, mrb_value key);
mrb_value mrb_hash_keys(mrb_state *mrb, mrb_value hash);
mrb_value mrb_check_hash_type(mrb_state *mrb, mrb_value hash);
mrb_value mrb_hash_empty_p(mrb_state *mrb, mrb_value self);
mrb_value mrb_hash_clear(mrb_state *mrb, mrb_value hash);

/* RHASH_TBL allocates st_table if not available. */
#define RHASH(obj)          ((RHash*)((obj).value.p))
#define RHASH_TBL(h)          (RHASH(h)->ht)
#define RHASH_IFNONE(h)       (h).mrb_iv_get(mrb->intern2("ifnone", 6))
#define RHASH_PROCDEFAULT(h)  RHASH_IFNONE(h)
RHash::kh_ht_t * mrb_hash_tbl(mrb_state *mrb, mrb_value hash);
enum eHashFlags {
    MRB_HASH_PROC_DEFAULT = (1<<8)
};
#define MRB_RHASH_PROCDEFAULT_P(h) (RHASH(h)->flags & MRB_HASH_PROC_DEFAULT)

/* GC functions */
void mrb_gc_mark_hash(mrb_state*,  RHash*);
size_t mrb_gc_mark_hash_size(mrb_state*, RHash*);
void mrb_gc_free_hash(mrb_state*, RHash*);
