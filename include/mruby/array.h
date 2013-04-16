/*
** mruby/array.h - Array class
**
** See Copyright Notice in mruby.h
*/
#pragma once
#include "mruby/value.h"
#define mrb_ary_ptr(v)    ((RArray*)((v).value.p))
#define mrb_ary_value(p)  mrb_obj_value((void*)(p))
#define RARRAY(v)  ((RArray*)((v).value.p))

#define RARRAY_LEN(a) (RARRAY(a)->m_len)
#define RARRAY_PTR(a) (RARRAY(a)->m_ptr)
#define MRB_ARY_SHARED      256

struct mrb_state;
struct mrb_shared_array {
    int refcnt;
    mrb_value *ptr;
    mrb_int len;
};

struct RArray : public RBasic {
    static const mrb_vtype ttype=MRB_TT_ARRAY;
    mrb_int m_len;
    union {
        mrb_int capa;
        mrb_shared_array *shared;
    } m_aux;
    mrb_value *m_ptr;
#define FORWARD_TO_INSTANCE(name)\
    static mrb_value name(mrb_state *mrb, mrb_value self) {\
    return mrb_ary_ptr(self)->name(mrb);\
}
public:
    static mrb_value new_capa(mrb_state *mrb, mrb_int capa);
    static mrb_value s_create(mrb_state *mrb, mrb_value self);
    static mrb_value new_from_values(mrb_state *mrb, mrb_int size, const mrb_value *vals);
static  void        concat(mrb_state *mrb, const mrb_value &self, const mrb_value &other);
    static mrb_value concat_m(mrb_state *mrb, mrb_value self);
    static mrb_value plus(mrb_state *mrb, mrb_value self);
static  void        replace(mrb_state *mrb, const mrb_value &self, const mrb_value &other);
    static mrb_value reverse_bang(mrb_state *mrb, mrb_value self);
static  mrb_value   unshift(mrb_state *mrb, const mrb_value &self, const mrb_value &item);
    static mrb_value unshift_m(mrb_state *mrb, mrb_value self);
static  mrb_value   splice(mrb_state *mrb, const mrb_value &ary, mrb_int head, mrb_int m_len, mrb_value rpl);
    static mrb_value aset(mrb_state *mrb, mrb_value self);
    static mrb_value delete_at(mrb_state *mrb, mrb_value self);
    static mrb_value clear(mrb_state *mrb, mrb_value self);
    static mrb_value push_m(mrb_state *mrb, mrb_value self);
    static mrb_value splat(mrb_state *mrb, mrb_value v);
    static mrb_value replace_m(mrb_state *mrb, mrb_value self);
    static mrb_value shift(mrb_state *mrb, mrb_value self);
    FORWARD_TO_INSTANCE(first)
    FORWARD_TO_INSTANCE(last)
    FORWARD_TO_INSTANCE(pop)
    FORWARD_TO_INSTANCE(empty_p)
    FORWARD_TO_INSTANCE(get)
    FORWARD_TO_INSTANCE(size)
    FORWARD_TO_INSTANCE(times)
    FORWARD_TO_INSTANCE(reverse)
    FORWARD_TO_INSTANCE(inspect)
    FORWARD_TO_INSTANCE(join_m)
    FORWARD_TO_INSTANCE(index_m)
    FORWARD_TO_INSTANCE(rindex_m)
    FORWARD_TO_INSTANCE(cmp)
    static void set(mrb_state *mrb, mrb_value ary, mrb_int n, mrb_value val) {
        return mrb_ary_ptr(ary)->set(mrb,n,val);
    }
    static void push(mrb_state *mrb, mrb_value ary, mrb_value elem) {
        return mrb_ary_ptr(ary)->push(mrb,elem);
    }
    static mrb_value ref(mrb_state *mrb, mrb_value self,mrb_int n) {
        return mrb_ary_ptr(self)->ref(mrb,n);
    }
    static mrb_value join(mrb_state *mrb, mrb_value self, mrb_value sep) {
        return mrb_ary_ptr(self)->join(mrb,sep);
    }
    static mrb_value entry(mrb_value self, mrb_int offset) {
        return mrb_ary_ptr(self)->entry(offset);
    }

    void        push(mrb_state *mrb, mrb_value elem);
    mrb_value   pop(mrb_state *mrb);
    mrb_value   first(mrb_state *mrb);
    mrb_value   last(mrb_state *mrb);
    mrb_value   get(mrb_state *mrb);
    mrb_value   ref(mrb_state *mrb, mrb_int n) const;
        void        set(mrb_state *mrb, mrb_int n, const mrb_value &val);
    mrb_value   times(mrb_state *mrb);
    mrb_value   reverse(mrb_state *mrb);
    mrb_value   join_m(mrb_state *mrb);
    mrb_value   join(mrb_state *mrb, mrb_value sep);
    mrb_value   size(mrb_state *mrb);
    mrb_value   index_m(mrb_state *mrb);
    mrb_value   rindex_m(mrb_state *mrb);
        mrb_value   cmp(mrb_state *mrb) const;
    mrb_value   entry(mrb_int offset);
    static mrb_value mrb_ary_equal(mrb_state *mrb, mrb_value ary1);
    static mrb_value mrb_ary_eql(mrb_state *mrb, mrb_value ary1);
    mrb_value   empty_p(mrb_state *mrb);
    mrb_value   inspect(mrb_state *mrb);
    mrb_int     size() {return m_len;}
protected:
//    RArray(mrb_int _capa) : len(0) {
//        aux.capa=_capa;
//    }
static  RArray *    ary_new_capa(mrb_state *mrb, size_t capa);
    mrb_value   inspect_ary(mrb_state *mrb, RArray *list_arr);
    mrb_value   join_ary(mrb_state *mrb, mrb_value sep, RArray *list_arr);
    mrb_value   ary_subseq(mrb_state *mrb, mrb_int beg, mrb_int m_len);
    void ary_make_shared(mrb_state *mrb);
        void        ary_replace(mrb_state *mrb, const mrb_value *argv, mrb_int m_len);
        void        ary_expand_capa(mrb_state *mrb, size_t m_len);
    void ary_concat(mrb_state *mrb, const mrb_value *m_ptr, mrb_int blen);
    void ary_shrink_capa(mrb_state *mrb);
    void ary_modify(mrb_state *mrb);
    mrb_value ary_elt(mrb_int offset)
    {
        if (m_len == 0)
            return mrb_nil_value();
        if (offset < 0 || m_len <= offset) {
            return mrb_nil_value();
        }
        return m_ptr[offset];
    }
#undef FORWARD_TO_INSTANCE
};

void mrb_ary_decref(mrb_state*, mrb_shared_array*);
mrb_value mrb_ary_new(mrb_state *mrb);
mrb_value mrb_check_array_type(mrb_state *mrb, mrb_value self);
mrb_value mrb_assoc_new(mrb_state *mrb, const mrb_value &car, const mrb_value &cdr);

