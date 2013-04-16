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


#define __ac_isempty(e_flag, d_flag, i) (e_flag[(i)/8]&__m[(i)%8])
#define __ac_isdel(e_flag, d_flag, i) (d_flag[(i)/8]&__m[(i)%8])
#define __ac_iseither(e_flag, d_flag, i) (__ac_isempty(e_flag,d_flag,i)||__ac_isdel(e_flag,d_flag,i))
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
    uint8_t *e_flags;
    uint8_t *d_flags;
    khkey_t *keys;
    khval_t *vals;
    khint_t mask;
    khint_t inc;
    mrb_state *mrb;
    HashFunc __hash_func;
    HashEq __hash_equal;
    //khint_t ( *__hash_equal)(mrb_state *mrb,khkey_t,khkey_t);
    static void kh_alloc(kh_t *h)
    {
        khint_t sz = h->n_buckets;
        h->m_size = h->n_occupied = 0;
        h->upper_bound = UPPER_BOUND(sz);
        h->e_flags = (uint8_t *)h->mrb->gc()._malloc(sizeof(uint8_t)*sz/4);
        h->d_flags = h->e_flags + sz/8;
        kh_fill_flags(h->e_flags, 0xff, sz/8);
        kh_fill_flags(h->d_flags, 0x00, sz/8);
        h->keys = (khkey_t *)h->mrb->gc()._malloc(sizeof(khkey_t)*sz);
        h->vals = (khval_t *)h->mrb->gc()._malloc(sizeof(khval_t)*sz);
        h->mask = sz-1;
        h->inc = sz/2-1;
    }
    static kh_t *init_size(mrb_state *mrb, khint_t size) {
        kh_t *h = (kh_t*)mrb->gc()._calloc(1, sizeof(kh_t));
        if (size < KHASH_MIN_SIZE)
            size = KHASH_MIN_SIZE;
        khash_power2(size);
        h->n_buckets = size;
        h->mrb = mrb;
        kh_alloc(h);
        return h;
    }
    static kh_t * init(mrb_state *mrb) {
        return init_size(mrb, KHASH_DEFAULT_SIZE);
    }
    void destroy()
    {
        if (this) {
            mrb->gc()._free(keys);
            mrb->gc()._free(vals);
            mrb->gc()._free(e_flags);
            mrb->gc()._free(this);
        }
    }
    static void clear(kh_t *h)
    {
        if (h && h->e_flags) {
            kh_fill_flags(h->e_flags, 0xff, h->n_buckets/8);
            kh_fill_flags(h->d_flags, 0x00, h->n_buckets/8);
            h->m_size = h->n_occupied = 0;
        }
    }
    iterator get(const khkey_t &key)
    {
        khint_t k = __hash_func(this->mrb,key) & (this->mask);
        while (!__ac_isempty(this->e_flags, this->d_flags, k)) {
            if (!__ac_isdel(this->e_flags, this->d_flags, k)) {
                if (__hash_equal(this->mrb,this->keys[k], key)) return k;
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
            uint8_t *old_e_flags = this->e_flags;
            khkey_t *old_keys = this->keys;
            khval_t *old_vals = this->vals;
            khint_t old_n_buckets = this->n_buckets;
            khint_t i;
            this->n_buckets = new_n_buckets;
            kh_alloc(this);
            /* relocate */
            for (i=0 ; i<old_n_buckets ; i++) {
                if (!__ac_isempty(old_e_flags, old_d_flags, i)) {
                    khint_t k = this->put(old_keys[i]);
                    this->value(k) = old_vals[i];
                }
            }
            this->mrb->gc()._free(old_e_flags);
            this->mrb->gc()._free(old_keys);
            this->mrb->gc()._free(old_vals);
        }
    }
    khint_t put(const khkey_t &key)
    {
        khint_t k;
        if (this->n_occupied >= this->upper_bound) {
            resize(this->n_buckets*2);
        }
        k = __hash_func(this->mrb,key) & (this->mask);
        while (!__ac_iseither(this->e_flags, this->d_flags, k)) {
            if (__hash_equal(this->mrb,this->keys[k], key)) break;
            k = (k+this->inc) & (this->mask);
        }
        if (__ac_isempty(this->e_flags, this->d_flags, k)) {
            /* put at empty */
            this->keys[k] = key;
            this->e_flags[k/8] &= ~__m[k%8];
            this->m_size++;
            this->n_occupied++;
        } else if (__ac_isdel(this->e_flags, this->d_flags, k)) {
            /* put at del */
            this->keys[k] = key;
            this->d_flags[k/8] &= ~__m[k%8];
            this->m_size++;
        }
        return k;
    }
    void del(khint_t x)
    {
        d_flags[x/8] |= __m[x%8];
        m_size--;
    }
    kh_t *copy(mrb_state *mrb)
    {
        kh_t *h2;
        khiter_t k, k2;

        h2 = init(mrb);
        for (k = begin(); k != end(); k++) {
            if (exist(k)) {
                k2 = h2->put(key(k));
                h2->value(k2) = value(k);
            }
        }
        return h2;
    }
    bool exist(iterator x) {return (!__ac_iseither((this)->e_flags, (this)->d_flags, (x)));}
    khkey_t key(int x) { return keys[x];}
    khval_t val(int x) { return vals[x];}
    khval_t &value( khiter_t x ) { return  vals[x]; }
    khint_t begin() const { return khint_t(0);}
    khint_t end() const { return n_buckets;}
    khint_t size() const { return m_size; }
    khint_t buckets() const { return n_buckets;}
};

#if defined(__cplusplus)
extern "C" {
#endif

/* declare struct kh_xxx and kh_xxx_funcs

   name: hash name
   khkey_t: key data type
   khval_t: value data type
   kh_is_map: (not implemented / not used in RiteVM)
*/
#define KHASH_DECLARE(name, khkey_t, khval_t, kh_is_map)                \
  typedef struct kh_##name {                                            \
    khint_t n_buckets;                                                  \
    khint_t size;                                                       \
    khint_t n_occupied;                                                 \
    khint_t upper_bound;                                                \
    uint8_t *e_flags;                                                   \
    uint8_t *d_flags;                                                   \
    khkey_t *keys;                                                      \
    khval_t *vals;                                                      \
    khint_t mask;                                                       \
    khint_t inc;                                                        \
    mrb_state *mrb;                                                     \
  } kh_##name##_t;                                                      \
  void kh_alloc_##name(kh_##name##_t *h);                               \
  kh_##name##_t *kh_init_##name##_size(mrb_state *mrb, khint_t size);   \
  kh_##name##_t *kh_init_##name(mrb_state *mrb);                        \
  void kh_destroy_##name(kh_##name##_t *h);                             \
  void kh_clear_##name(kh_##name##_t *h);                               \
  khint_t kh_get_##name(kh_##name##_t *h, khkey_t key);                 \
  khint_t kh_put_##name(kh_##name##_t *h, khkey_t key);                 \
  void kh_resize_##name(kh_##name##_t *h, khint_t new_n_buckets);       \
  void kh_del_##name(kh_##name##_t *h, khint_t x);                      \
  kh_##name##_t *kh_copy_##name(mrb_state *mrb, kh_##name##_t *h);


/* define kh_xxx_funcs

   name: hash name
   khkey_t: key data type
   khval_t: value data type
   kh_is_map: (not implemented / not used in RiteVM)
   __hash_func: hash function
   __hash_equal: hash comparation function
*/
#define KHASH_DEFINE(name, khkey_t, khval_t, kh_is_map, __hash_func, __hash_equal) \
  void kh_alloc_##name(kh_##name##_t *h)                                \
  {                                                                     \
    khint_t sz = h->n_buckets;                                          \
    h->size = h->n_occupied = 0;                                        \
    h->upper_bound = UPPER_BOUND(sz);                                   \
    h->e_flags = (uint8_t *)h->mrb->gc()._malloc(sizeof(uint8_t)*sz/4);   \
    h->d_flags = h->e_flags + sz/8;                                     \
    kh_fill_flags(h->e_flags, 0xff, sz/8);                              \
    kh_fill_flags(h->d_flags, 0x00, sz/8);                              \
    h->keys = (khkey_t *)h->mrb->gc()._malloc(sizeof(khkey_t)*sz);        \
    h->vals = (khval_t *)h->mrb->gc()._malloc(sizeof(khval_t)*sz);        \
    h->mask = sz-1;                                                     \
    h->inc = sz/2-1;                                                    \
  }                                                                     \
  kh_##name##_t *kh_init_##name##_size(mrb_state *mrb, khint_t size) {  \
    kh_##name##_t *h = (kh_##name##_t*)mrb->gc()._calloc(1, sizeof(kh_##name##_t)); \
    if (size < KHASH_MIN_SIZE)                                          \
      size = KHASH_MIN_SIZE;                                            \
    khash_power2(size);                                                 \
    h->n_buckets = size;                                                \
    h->mrb = mrb;                                                       \
    kh_alloc_##name(h);                                                 \
    return h;                                                           \
  }                                                                     \
  kh_##name##_t *kh_init_##name(mrb_state *mrb){                        \
    return kh_init_##name##_size(mrb, KHASH_DEFAULT_SIZE);              \
  }                                                                     \
  void kh_destroy_##name(kh_##name##_t *h)                              \
  {                                                                     \
    if (h) {                                                            \
      h->mrb->gc()._free(h->keys);                                        \
      h->mrb->gc()._free(h->vals);                                        \
      h->mrb->gc()._free(h->e_flags);                                     \
      h->mrb->gc()._free(h);                                              \
    }                                                                   \
  }                                                                     \
  void kh_clear_##name(kh_##name##_t *h)                                \
  {                                                                     \
    if (h && h->e_flags) {                                              \
      kh_fill_flags(h->e_flags, 0xff, h->n_buckets/8);                  \
      kh_fill_flags(h->d_flags, 0x00, h->n_buckets/8);                  \
      h->size = h->n_occupied = 0;                                      \
    }                                                                   \
  }                                                                     \
  khint_t kh_get_##name(kh_##name##_t *h, khkey_t key)                  \
  {                                                                     \
    khint_t k = __hash_func(h->mrb,key) & (h->mask);                    \
    while (!__ac_isempty(h->e_flags, h->d_flags, k)) {                  \
      if (!__ac_isdel(h->e_flags, h->d_flags, k)) {                     \
        if (__hash_equal(h->mrb,h->keys[k], key)) return k;             \
      }                                                                 \
      k = (k+h->inc) & (h->mask);                                       \
    }                                                                   \
    return h->n_buckets;                                                \
  }                                                                     \
  void kh_resize_##name(kh_##name##_t *h, khint_t new_n_buckets)        \
  {                                                                     \
    if (new_n_buckets < KHASH_MIN_SIZE)                                 \
      new_n_buckets = KHASH_MIN_SIZE;                                   \
    khash_power2(new_n_buckets);                                        \
    {                                                                   \
      uint8_t *old_e_flags = h->e_flags;                                \
      khkey_t *old_keys = h->keys;                                      \
      khval_t *old_vals = h->vals;                                      \
      khint_t old_n_buckets = h->n_buckets;                             \
      khint_t i;                                                        \
      h->n_buckets = new_n_buckets;                                     \
      kh_alloc_##name(h);                                               \
      /* relocate */                                                    \
      for (i=0 ; i<old_n_buckets ; i++) {                               \
        if (!__ac_isempty(old_e_flags, old_d_flags, i)) {               \
          khint_t k = kh_put_##name(h, old_keys[i]);                    \
          kh_value(h,k) = old_vals[i];                                  \
        }                                                               \
      }                                                                 \
      h->mrb->gc()._free(old_e_flags);                                  \
      h->mrb->gc()._free(old_keys);                                     \
      h->mrb->gc()._free(old_vals);                                     \
    }                                                                   \
  }                                                                     \
  khint_t kh_put_##name(kh_##name##_t *h, khkey_t key)                  \
  {                                                                     \
    khint_t k;                                                          \
    if (h->n_occupied >= h->upper_bound) {                              \
      kh_resize_##name(h, h->n_buckets*2);                              \
    }                                                                   \
    k = __hash_func(h->mrb,key) & (h->mask);                            \
    while (!__ac_iseither(h->e_flags, h->d_flags, k)) {                 \
      if (__hash_equal(h->mrb,h->keys[k], key)) break;                  \
      k = (k+h->inc) & (h->mask);                                       \
    }                                                                   \
    if (__ac_isempty(h->e_flags, h->d_flags, k)) {                      \
      /* put at empty */                                                \
      h->keys[k] = key;                                                 \
      h->e_flags[k/8] &= ~__m[k%8];                                     \
      h->size++;                                                        \
      h->n_occupied++;                                                  \
    } else if (__ac_isdel(h->e_flags, h->d_flags, k)) {                 \
      /* put at del */                                                  \
      h->keys[k] = key;                                                 \
      h->d_flags[k/8] &= ~__m[k%8];                                     \
      h->size++;                                                        \
    }                                                                   \
    return k;                                                           \
  }                                                                     \
  void kh_del_##name(kh_##name##_t *h, khint_t x)                       \
  {                                                                     \
    h->d_flags[x/8] |= __m[x%8];                                        \
    h->size--;                                                          \
  }                                                                     \
  kh_##name##_t *kh_copy_##name(mrb_state *mrb, kh_##name##_t *h)       \
  {                                                                     \
    kh_##name##_t *h2;                                                  \
    khiter_t k, k2;                                                     \
                                                                        \
    h2 = kh_init_##name(mrb);                                           \
    for (k = kh_begin(h); k != kh_end(h); k++) {                        \
      if (kh_exist(h, k)) {                                             \
        k2 = kh_put_##name(h2, kh_key(h, k));                           \
        kh_value(h2, k2) = kh_value(h, k);                              \
      }                                                                 \
    }                                                                   \
    return h2;                                                          \
  }


#define khash_t(name) kh_##name##_t

#define kh_init_size(name,mrb,size) kh_init_##name##_size(mrb,size)
#define kh_init(name,mrb) kh_init_##name(mrb)
#define kh_destroy(name, h) kh_destroy_##name(h)
#define kh_clear(name, h) kh_clear_##name(h)
#define kh_resize(name, h, s) kh_resize_##name(h, s)
#define kh_put(name, h, k) kh_put_##name(h, k)
#define kh_get(name, h, k) kh_get_##name(h, k)
#define kh_del(name, h, k) kh_del_##name(h, k)
#define kh_copy(name, mrb, h) kh_copy_##name(mrb, h)

#define kh_exist(h, x) (!__ac_iseither((h)->e_flags, (h)->d_flags, (x)))
#define kh_key(h, x) ((h)->keys[x])
#define kh_val(h, x) ((h)->vals[x])
#define kh_value(h, x) ((h)->vals[x])
#define kh_begin(h) (khint_t)(0)
#define kh_end(h) ((h)->n_buckets)
#define kh_size(h) ((h)->size)
#define kh_n_buckets(h) ((h)->n_buckets)

#define kh_int_hash_func(mrb,key) (khint_t)((key)^((key)<<2)^((key)>>2))
#define kh_int_hash_equal(mrb,a, b) (a == b)
struct IntHashFunc {
    khint_t operator()(mrb_state *,khint_t key) { return (khint_t)((key)^((key)<<2)^((key)>>2)); }
};
struct IntHashEq {
    khint_t operator()(mrb_state *,khint_t a,khint_t b) { return a==b; }
};
static inline khint_t __ac_X31_hash_string(const char *s)
{
    khint_t h = *s;
    if (h) for (++s ; *s; ++s) h = (h << 5) - h + *s;
    return h;
}
#define kh_str_hash_func(mrb,key) __ac_X31_hash_string(key)
#define kh_str_hash_equal(mrb,a, b) (strcmp(a, b) == 0)

typedef const char *kh_cstr_t;

#if defined(__cplusplus)
}  /* extern "C" { */

#endif  /* KHASH_H */
