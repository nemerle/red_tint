/*
** mruby/khash.c - Hash for mruby
**
** See Copyright Notice in mruby.h
*/

#pragma once

#include "mruby.h"
#include <string.h>

typedef uint32_t khint_t;
typedef khint_t khiter_t;

#ifndef KHASH_DEFAULT_SIZE
# define KHASH_DEFAULT_SIZE 32
#endif
#define KHASH_MIN_SIZE 8

#define UPPER_BOUND(x) ((x)>>2|(x)>>1)

//extern uint8_t __m[];

/* mask for flags */
static const uint8_t __m[8] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};
static const uint8_t __m_empty[8]  = {0x02, 0x08, 0x20, 0x80};
static const uint8_t __m_del[8]    = {0x01, 0x04, 0x10, 0x40};
static const uint8_t __m_either[8] = {0x03, 0x0c, 0x30, 0xc0};


#define __ac_isempty(ed_flag, i) (ed_flag[(i)/4]&__m_empty[(i)%4])
#define __ac_isdel(ed_flag, i) (ed_flag[(i)/4]&__m_del[(i)%4])
#define __ac_iseither(ed_flag, i) (ed_flag[(i)/4]&__m_either[(i)%4])
#define khash_power2(v) do { \
  v--;\
  v |= v >> 1;\
  v |= v >> 2;\
  v |= v >> 4;\
  v |= v >> 8;\
  v |= v >> 16;\
  v++;\
} while (0)
static inline void kh_fill_flags(uint8_t *p, uint8_t c, size_t len)
{
    while (len-- > 0) {
        *p++ = c;
    }
}

//kh_is_map, __hash_func, __hash_equal
template<typename khkey_t, typename khval_t,class HashFunc,class HashEq>
struct kh_T {
    typedef khiter_t iterator;
    typedef kh_T<khkey_t,khval_t,HashFunc,HashEq> kh_t;
    khint_t n_buckets;
    khint_t m_size;
    khint_t n_occupied;
    khint_t upper_bound;
    uint8_t *ed_flags;
    khkey_t *keys;
    khval_t *vals;
    khint_t mask;
    khint_t inc;
    MemManager *m_mem;
    HashFunc __hash_func;
    HashEq __hash_equal;
    //khint_t ( *__hash_equal)(mrb_state *mrb,khkey_t,khkey_t);
    static void kh_alloc(kh_t *h)
    {
        khint_t sz = h->n_buckets;
        uint8_t *p = (uint8_t *)h->m_mem->_malloc(sizeof(uint8_t)*sz/4+(sizeof(khkey_t)+sizeof(khval_t))*sz);
        h->m_size = h->n_occupied = 0;
        h->upper_bound = UPPER_BOUND(sz);
        h->keys = (khkey_t *)p;
        h->vals = (khval_t *)(p+sizeof(khkey_t)*sz);
        h->ed_flags = (p+sizeof(khkey_t)*sz+sizeof(khval_t)*sz);
        kh_fill_flags(h->ed_flags, 0xaa, sz/4);
        h->mask = sz-1;
        h->inc = sz/2-1;
    }
    static kh_t *init_size(MemManager &mm, khint_t size) {
        kh_t *h = (kh_t*)mm._calloc(1, sizeof(kh_t));
        if (size < KHASH_MIN_SIZE)
            size = KHASH_MIN_SIZE;
        khash_power2(size);
        h->n_buckets = size;
        h->m_mem = &mm;
        kh_alloc(h);
        return h;
    }
    static kh_t * init(MemManager &mm) {
        return init_size(mm, KHASH_DEFAULT_SIZE);
    }
    void destroy()
    {
        if (this) {
            m_mem->_free(keys);
            m_mem->_free(this);
        }
    }
    static void clear(kh_t *h)
    {
        if (h && h->ed_flags) {
            kh_fill_flags(h->ed_flags, 0xaa, h->n_buckets/4);
            h->m_size = h->n_occupied = 0;
        }
    }
    iterator get(const khkey_t &key) const
    {
        khint_t k = __hash_func(this->m_mem,key) & (this->mask);
        while (!__ac_isempty(this->ed_flags, k)) {
            if (!__ac_isdel(this->ed_flags, k)) {
                if (__hash_equal(this->m_mem,this->keys[k], key)) return k;
            }
            k = (k+this->inc) & (this->mask);
        }
        return this->n_buckets;
    }
    void resize(khint_t new_n_buckets)
    {
        if (new_n_buckets < KHASH_MIN_SIZE)
            new_n_buckets = KHASH_MIN_SIZE;
        khash_power2(new_n_buckets);
        {
            uint8_t *old_ed_flags = this->ed_flags;
            khkey_t *old_keys = this->keys;
            khval_t *old_vals = this->vals;
            khint_t old_n_buckets = this->n_buckets;
            khint_t i;
            this->n_buckets = new_n_buckets;
            kh_alloc(this);
            /* relocate */
            for (i=0 ; i<old_n_buckets ; i++) {
                if (!__ac_iseither(old_ed_flags, i)) {
                    khint_t k = this->put(old_keys[i]);
                    this->value(k) = old_vals[i];
                }
            }
            this->m_mem->_free(old_keys);
        }
    }
    khint_t put(const khkey_t &key)
    {
        khint_t k;
        if (this->n_occupied >= this->upper_bound) {
            resize(this->n_buckets*2);
        }
        k = __hash_func(this->m_mem,key) & (this->mask);
        while (!__ac_iseither(this->ed_flags, k)) {
            if (__hash_equal(this->m_mem,this->keys[k], key)) break;
            k = (k+this->inc) & (this->mask);
        }
        if (__ac_isempty(this->ed_flags, k)) {
            /* put at empty */
            this->keys[k] = key;
            this->ed_flags[k/4] &= ~__m_empty[k%4];
            this->m_size++;
            this->n_occupied++;
        } else if (__ac_isdel(this->ed_flags, k)) {
            /* put at del */
            this->keys[k] = key;
            this->ed_flags[k/4] &= ~__m_del[k%4];
            this->m_size++;
        }
        return k;
    }
    void del(khint_t x)
    {
        ed_flags[x/4] |= __m_del[x%4];
        m_size--;
    }
    kh_t *copy(MemManager &mem)
    {
        kh_t *h2;
        khiter_t k, k2;

        h2 = init(mem);
        for (k = begin(); k != end(); k++) {
            if (exist(k)) {
                k2 = h2->put(key(k));
                h2->value(k2) = value(k);
            }
        }
        return h2;
    }
    bool exist(iterator x) {return (!__ac_iseither((this)->ed_flags, (x)));}
    const khkey_t &key(khiter_t x) const { return keys[x];}
    khval_t &value( khiter_t x ) { return  vals[x]; }
    const khval_t &value( khiter_t x ) const { return  vals[x]; }
    khint_t begin() const { return khint_t(0);}
    khint_t end() const { return n_buckets;}
    khint_t size() const { return m_size; }
    khint_t buckets() const { return n_buckets;}
};

struct IntHashFunc {
    khint_t operator()(MemManager *,khint_t key) const { return (khint_t)((key)^((key)<<2)^((key)>>2)); }
};
struct IntHashEq {
    khint_t operator()(MemManager *,khint_t a,khint_t b) const { return a==b; }
};
