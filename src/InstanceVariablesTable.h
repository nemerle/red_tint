#pragma once

#include "mruby/khash.h"

#ifndef MRB_IVHASH_INIT_SIZE
#define MRB_IVHASH_INIT_SIZE 8
#endif

/* Instance variable table structure */
struct iv_tbl {
protected:
    typedef kh_T<mrb_sym, mrb_value,IntHashFunc,IntHashEq> hashtab;
    hashtab *h;
    //kh_iv h;
public:
    typedef int (iv_foreach_func)(mrb_sym,mrb_value,void*);


    iv_tbl() {
    }
    /**
     * Set the value for the symbol in the instance variable table.
     *
     *   \arg mrb
     *   \arg t     the instance variable table to be set in.
     *   \arg sym   the symbol to be used as the key.
     *   \arg val   the value to be set.
     */
    void iv_put(mrb_sym sym, const mrb_value &val)
    {
        khiter_t k = h->put(sym);
        h->value(k) = val;
    }
    /**
     * Get a value for a symbol from the instance the variable table.
     *
     * Parameters
     *   \arg mrb
     *   \arg t     the variable table to be searched.
     *   \arg sym   the symbol to be used as the key.
     *   \arg vp    the value pointer. Recieves the value if the specified symbol contains
     *         in the instance variable table.
     * \returns true if the specfiyed symbol contains in the instance variable table.
     */
    bool iv_get(mrb_sym sym, mrb_value &vp) const
    {
        khiter_t k = h->get(sym);
        if (k != h->end() ) {
            vp = h->value(k);
            return true;
        }
        return false;
    }
    bool iv_get(mrb_sym sym) const
    {
        khiter_t k = h->get(sym);
        if (k != h->end() ) {
            return true;
        }
        return false;
    }
    /**
     * Deletes the value for the symbol from the instance variable table.
     *
     * Parameters
     *   \arg t    the variable table to be searched.
     *   \arg sym  the symbol to be used as the key.
     *   \arg vp   the value pointer. Recieves the value if the specified symbol contains
     *        in the instance varible table.
     * \returns true if the specfied symbol contains in the instance variable table.
     */
    bool iv_del(mrb_sym sym, mrb_value *vp)
    {
        if(!h)
            return false;
        khiter_t k = h->get(sym);
        if (k == h->end() )
            return false;
        if (vp)
            *vp = h->value(k);
        h->del(k);
        return true;
    }
    size_t iv_size()
    {
        if(!this || !this->h)
            return 0;
        return h->size();
    }
    mrb_bool iv_foreach(iv_foreach_func *func, void *p)
    {

        if (!h)
            return true;
        for (khiter_t k = h->begin(); k != h->end(); k++) {
            if (!h->exist(k))
                continue;
            int n = (*func)(h->key(k), h->value(k), p);
            if (n > 0)
                return false;
            if (n < 0) {
                h->del(k);
            }
        }
        return true;
    }
    /**
     * Creates the instance variable table.
     *
     * Parameters
     *   mrb
     * \returns the instance variable table.
     */
    static iv_tbl* iv_new(MemManager &gc)
    {
        iv_tbl * res = new(gc._malloc(sizeof(iv_tbl))) iv_tbl;
        res->h = hashtab::init_size(gc,MRB_IVHASH_INIT_SIZE);
        return res;
    }
    iv_tbl* iv_copy()
    {
        iv_tbl * res = new(h->m_mem->_malloc(sizeof(iv_tbl))) iv_tbl;
        res->h = h->copy(*h->m_mem);
        return res;
    }

    void iv_free()
    {
        h->destroy();
        this->~iv_tbl();
    }
};

