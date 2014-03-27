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
static constexpr const mrb_vtype ttype=MRB_TT_HASH;

        kh_ht_t *   ht;

public:
        mrb_value   delete_key(mrb_value key);
        mrb_value   shift();
        void        modify();
        mrb_value   set(mrb_value key, mrb_value val);
        mrb_value   set_default(mrb_value ifnone);
        mrb_value   default_proc();
        mrb_value   set_default_proc(mrb_value ifnone);
        RHash *init_core(mrb_value block, int argc, mrb_value *argv);
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
static  RHash *     new_capa(mrb_state *mrb, int capa);

protected:
        void        init_ht();
};

/* GC functions */
void mrb_gc_mark_hash(mrb_state*,  RHash*);
size_t mrb_gc_mark_hash_size(mrb_state*, RHash*);
void mrb_gc_free_hash(mrb_state*, RHash*);
