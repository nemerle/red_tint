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

enum eHashFlags {
    MRB_HASH_PROC_DEFAULT = (1<<8)
};

struct RHash : public RObject {
    typedef kh_T<mrb_value,mrb_value,ValueHashFunc,ValueHashEq> kh_ht_t;
    static const mrb_vtype ttype=MRB_TT_HASH;
    kh_ht_t *ht;

public:
    mrb_value   delete_key(mrb_value key);
    mrb_value   shift();
    void        modify();
    mrb_value   set(mrb_value key, mrb_value val);
    mrb_value   set_default(mrb_value ifnone);
    mrb_value   default_proc();
    mrb_value   set_default_proc(mrb_value ifnone);
    mrb_value   init_core(mrb_value block, int argc, mrb_value *argv);
    mrb_value   replace(mrb_value hash2);
    void        clear();
    bool        hash_equal(mrb_value other_hash, bool eql);
    bool        hasKey(mrb_value key);
    bool        hasValue(mrb_value value);
    mrb_value   fetch(mrb_value key, mrb_value def);
    RArray *    keys();
    RArray *    values();
    bool        empty() const;
    RHash *     dup() const;
    mrb_int     size() const;
    mrb_value   default_val();
    RHash *     toHash();
    mrb_value   aget(mrb_value key);
    mrb_value   get(mrb_value key);
protected:
    void init_ht();
};

mrb_value mrb_hash_new_capa(mrb_state*, int);
mrb_value mrb_hash_new(mrb_state *mrb);
mrb_value mrb_hash_keys(mrb_state *mrb, mrb_value hash);
//mrb_value mrb_check_hash_type(mrb_state *mrb, mrb_value hash);
mrb_value mrb_hash_empty_p(mrb_state *mrb, mrb_value self);

/* RHASH_TBL allocates st_table if not available. */
#define RHASH_TBL(h)          ((h).ptr<RHash>()->ht)
#define RHASH_IFNONE(h)       (h).mrb_iv_get(mrb->intern2("ifnone", 6))
#define RHASH_PROCDEFAULT(h)  RHASH_IFNONE(h)
RHash::kh_ht_t * mrb_hash_tbl(mrb_state *mrb, mrb_value hash);
#define MRB_RHASH_PROCDEFAULT_P(h) ((h).ptr<RHash>()->flags & MRB_HASH_PROC_DEFAULT)

/* GC functions */
void mrb_gc_mark_hash(mrb_state*,  RHash*);
size_t mrb_gc_mark_hash_size(mrb_state*, RHash*);
void mrb_gc_free_hash(mrb_state*, RHash*);
