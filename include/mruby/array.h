/*
** mruby/array.h - Array class
**
** See Copyright Notice in mruby.h
*/
#pragma once
#include <vector>
#include "mruby/value.h"
#define mrb_ary_ptr(v)    ((RArray*)((v).value.p))
#define mrb_ary_value(p)  mrb_obj_value((void*)(p))
#define RARRAY(v)  ((RArray*)((v).value.p))

#define RARRAY_LEN(a) (RARRAY(a)->m_len)
#define RARRAY_PTR(a) (RARRAY(a)->m_ptr)
enum eArrayFlags {
    MRB_ARY_SHARED = (1<<8)
};
struct mrb_state;
struct mrb_shared_array {
    int refcnt;
    mrb_int len;
    mrb_value *ptr;
};

struct RArray : public RBasic {

static const mrb_vtype ttype=MRB_TT_ARRAY;

        mrb_int m_len;
        union {
            mrb_int capa;
            mrb_shared_array *shared;
        } m_aux;
        //TODO: semantics of this field change depending on the shared flag
        // When not shared this is a pointer to base data
        // when shared it's an 'iterator'-like element over shared->ptr
        mrb_value * m_ptr;
public:
static  RArray *    create(mrb_state *mrb, size_t capa=0) {return ary_new_capa(mrb,capa);}
static  mrb_value   new_capa(mrb_state *mrb, mrb_int capa=0);
static  mrb_value   s_create(mrb_state *mrb, mrb_value self);
static  mrb_value   new_from_values(mrb_state *mrb, mrb_int size, const mrb_value *vals);
static  mrb_value   new_from_values(mrb_state *mrb, const std::vector<mrb_value> &values);
static  mrb_value   splat(mrb_state *mrb, const mrb_value &v);

        void        replace(const mrb_value &other);
        void        replace_m();
        void        reverse_bang();
        void        concat(const mrb_value &other);
        void        unshift_m();
        void        unshift(const mrb_value &item);
        mrb_value   delete_at();
        void        concat_m();
        void        clear();
        mrb_value   aset();
        mrb_value   shift();
        mrb_value   plus() const;
        void        push(const mrb_value &elem);
        void        push_m();
        mrb_value   pop();
        mrb_value   first();
        mrb_value   last();
        mrb_value   get();
        mrb_value   ref(mrb_int n) const;
        void        set(mrb_int n, const mrb_value &val);
        mrb_value   times();
        mrb_value   reverse();
        mrb_value   join_m();
        mrb_value   join(mrb_value sep);
        mrb_value   size();
        mrb_value   index_m();
        mrb_value   rindex_m();
        mrb_value   cmp() const;
        mrb_value   entry(mrb_int offset);
        mrb_value   mrb_ary_equal();
        mrb_value   mrb_ary_eql();
        mrb_value   empty_p() const;
        mrb_value   inspect();
        void        splice(mrb_int head, mrb_int len, const mrb_value &rpl);
        void mrb_ary_modify();
        mrb_value mrb_ary_ceqq();
protected:
        mrb_value * base_ptr() {
            return (flags & MRB_ARY_SHARED) ? m_aux.shared->ptr : m_ptr;
        }
static  RArray *    ary_new_capa(mrb_state *mrb, size_t capa);
        mrb_value   inspect_ary(RArray *list_arr);
        mrb_value   join_ary(const mrb_value &sep, RArray *list_arr);
        mrb_value   ary_subseq(mrb_int beg, mrb_int m_len);
        void        ary_make_shared();
        void        ary_replace(const mrb_value *argv, mrb_int m_len);
        void        ary_expand_capa(mrb_state *mrb, size_t m_len);
        void        ary_concat(const mrb_value *m_ptr, mrb_int blen);
        void        ary_shrink_capa();
        void        ary_modify();
        mrb_value   ary_elt(mrb_int offset)
                    {
                                        if ((m_len == 0) || (offset < 0 || offset >= m_len)  ) {
                            return mrb_value::nil();
                        }
                        return m_ptr[offset];
                    }
        mrb_value & unchecked_ref(mrb_int offset) { return m_ptr[offset];}
        mrb_int aget_index(mrb_value index);
};

void mrb_ary_decref(mrb_state*, mrb_shared_array*);
mrb_value mrb_check_array_type(mrb_state *mrb, const mrb_value &self);
mrb_value mrb_assoc_new(mrb_state *mrb, const mrb_value &car, const mrb_value &cdr);

