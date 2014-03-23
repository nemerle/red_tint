/*
** hash.c - Hash class
**
** See Copyright Notice in mruby.h
*/

#include "mruby.h"
#include "mruby/array.h"
#include "mruby/class.h"
#include "mruby/hash.h"
#include "mruby/khash.h"
#include "mruby/string.h"
#include "mruby/variable.h"

static void mrb_hash_modify(mrb_state *mrb, mrb_value hash);

static inline mrb_value mrb_hash_ht_key(mrb_state *mrb, mrb_value key)
{
    if (mrb_is_a_string(key))
        return mrb_str_dup(mrb, key);
    return key;
}

#define KEY(key) mrb_hash_ht_key(mrb, key)

void mrb_gc_mark_hash(mrb_state *mrb, RHash *hash)
{
    auto *h = hash->ht;

    if (!hash->ht)
        return;
    for (auto k = h->begin(); k != h->end(); k++) {
        if ( !h->exist(k))
            continue;
        mrb_value key = h->key(k);
        mrb_value val = h->value(k);
        mrb_gc_mark_value(mrb, key);
        mrb_gc_mark_value(mrb, val);
    }
}

size_t mrb_gc_mark_hash_size(mrb_state *mrb, RHash *hash)
{
    if (!hash->ht)
        return 0;
    return hash->ht->size()*2;
}

void mrb_gc_free_hash(mrb_state *mrb, RHash *hash)
{
    if (hash->ht)
        hash->ht->destroy();
}


mrb_value mrb_hash_new_capa(mrb_state *mrb, int capa)
{
    RHash *h = mrb->gc().obj_alloc<RHash>(mrb->hash_class);
    h->ht = RHash::kh_ht_t::init(mrb->gc());
    if (capa > 0) {
        h->ht->resize(capa);
    }
    h->iv = 0;
    return mrb_obj_value(h);
}

mrb_value mrb_hash_new(mrb_state *mrb)
{
    return mrb_hash_new_capa(mrb, 0);
}

mrb_value mrb_hash_get(mrb_value hash, mrb_value key)
{
    mrb_state *mrb = RHASH(hash)->m_vm;
    RHash::kh_ht_t *h = RHASH_TBL(hash);

    if (h) {
        auto k = h->get(key);
        if (k != h->end())
            return h->value(k);
    }

    /* not found */
    if (MRB_RHASH_PROCDEFAULT_P(hash)) {
        return mrb->funcall(RHASH_PROCDEFAULT(hash), "call", 2, hash, key);
    }
    return RHASH_IFNONE(hash);
}

mrb_value mrb_hash_fetch(mrb_state *mrb, mrb_value hash, mrb_value key, mrb_value def)
{
    RHash::kh_ht_t * h = RHASH_TBL(hash);

    if (h) {
        auto k = h->get(key);
        if (k != h->end())
            return h->value(k);
    }
    /* not found */
    return def;
}

void
mrb_hash_set(mrb_state *mrb, mrb_value hash, mrb_value key, mrb_value val) /* mrb_hash_aset */
{
    RHash::kh_ht_t *h;

    mrb_hash_modify(mrb, hash);
    h = RHASH_TBL(hash);

    if (!h)
        h = RHASH_TBL(hash) = RHash::kh_ht_t::init(mrb->gc());
    auto k = h->get(key);
    if (k == h->end()) {
        /* expand */
        int ai = mrb->gc().arena_save();
        k=h->put(KEY(key));
        mrb->gc().arena_restore(ai);
    }
    h->value(k) = val;
    mrb->gc().mrb_write_barrier((RBasic*)RHASH(hash));
    return;
}

mrb_value
mrb_hash_dup(mrb_state *mrb, mrb_value hash)
{
    RHash* ret;
    RHash::kh_ht_t *h;
    khiter_t k;

    h = RHASH_TBL(hash);
    ret = mrb->gc().obj_alloc<RHash>(mrb->hash_class);
    ret->ht = RHash::kh_ht_t::init(mrb->gc());

    if (h->size() > 0) {
        RHash::kh_ht_t *ret_h = ret->ht;

        for (k = h->begin(); k != h->end(); k++) {
            if (h->exist(k)) {
                int ai = mrb->gc().arena_save();
                auto ret_k = ret_h->put(mrb_hash_ht_key(mrb,h->key(k)));
                mrb->gc().arena_restore(ai);
                ret_h->value(ret_k) = h->value(k);
            }
        }
    }

    return mrb_obj_value(ret);
}

mrb_value mrb_check_hash_type(mrb_state *mrb, mrb_value hash)
{
    return mrb_check_convert_type(mrb, hash, MRB_TT_HASH, "Hash", "to_hash");
}

RHash::kh_ht_t * mrb_hash_tbl(mrb_state *mrb, mrb_value hash)
{
    RHash::kh_ht_t *h = RHASH_TBL(hash);

    if (!h) {
        RHASH_TBL(hash) = RHash::kh_ht_t::init(mrb->gc());
    }
    return h;
}

static void
mrb_hash_modify(mrb_state *mrb, mrb_value hash)
{
    mrb_hash_tbl(mrb, hash);
}

/* 15.2.13.4.16 */
/*
 *  call-seq:
 *     Hash.new                          -> new_hash
 *     Hash.new(obj)                     -> new_hash
 *     Hash.new {|hash, key| block }     -> new_hash
 *
 *  Returns a new, empty hash. If this hash is subsequently accessed by
 *  a key that doesn't correspond to a hash entry, the value returned
 *  depends on the style of <code>new</code> used to create the hash. In
 *  the first form, the access returns <code>nil</code>. If
 *  <i>obj</i> is specified, this single object will be used for
 *  all <em>default values</em>. If a block is specified, it will be
 *  called with the hash object and the key, and should return the
 *  default value. It is the block's responsibility to store the value
 *  in the hash if required.
 *
 *     h = Hash.new("Go Fish")
 *     h["a"] = 100
 *     h["b"] = 200
 *     h["a"]           #=> 100
 *     h["c"]           #=> "Go Fish"
 *     # The following alters the single default object
 *     h["c"].upcase!   #=> "GO FISH"
 *     h["d"]           #=> "GO FISH"
 *     h.keys           #=> ["a", "b"]
 *
 *     # While this creates a new default object each time
 *     h = Hash.new { |hash, key| hash[key] = "Go Fish: #{key}" }
 *     h["c"]           #=> "Go Fish: c"
 *     h["c"].upcase!   #=> "GO FISH: C"
 *     h["d"]           #=> "Go Fish: d"
 *     h.keys           #=> ["c", "d"]
 *
 */

static mrb_value
mrb_hash_init_core(mrb_state *mrb, mrb_value hash)
{
    mrb_value block, ifnone;
    mrb_value *argv;
    int argc;

    mrb_get_args(mrb, "o*", &block, &argv, &argc);
    mrb_hash_modify(mrb, hash);
    if (mrb_nil_p(block)) {
        if (argc > 0) {
            if (argc != 1) mrb->mrb_raise(E_ARGUMENT_ERROR, "wrong number of arguments");
            ifnone = argv[0];
        }
        else {
            ifnone = mrb_nil_value();
        }
    }
    else {
        if (argc > 0) {
            mrb->mrb_raise(E_ARGUMENT_ERROR, "wrong number of arguments");
        }
        RHASH(hash)->flags |= MRB_HASH_PROC_DEFAULT;
        ifnone = block;
    }
  mrb_iv_set(mrb, hash, mrb_intern_lit(mrb, "ifnone"), ifnone);
    return hash;
}

static mrb_value to_hash(mrb_state *mrb, mrb_value hash)
{
    return mrb_convert_type(mrb, hash, MRB_TT_HASH, "Hash", "to_hash");
}

/* 15.2.13.4.2  */
/*
 *  call-seq:
 *     hsh[key]    ->  value
 *
 *  Element Reference---Retrieves the <i>value</i> object corresponding
 *  to the <i>key</i> object. If not found, returns the default value (see
 *  <code>Hash::new</code> for details).
 *
 *     h = { "a" => 100, "b" => 200 }
 *     h["a"]   #=> 100
 *     h["c"]   #=> nil
 *
 */
mrb_value mrb_hash_aget(mrb_state *mrb, mrb_value self)
{
    mrb_value key = mrb->get_arg<mrb_value>();
    return mrb_hash_get(self, key);
}

/* 15.2.13.4.5  */
/*
 *  call-seq:
 *     hsh.default(key=nil)   -> obj
 *
 *  Returns the default value, the value that would be returned by
 *  <i>hsh</i>[<i>key</i>] if <i>key</i> did not exist in <i>hsh</i>.
 *  See also <code>Hash::new</code> and <code>Hash#default=</code>.
 *
 *     h = Hash.new                            #=> {}
 *     h.default                               #=> nil
 *     h.default(2)                            #=> nil
 *
 *     h = Hash.new("cat")                     #=> {}
 *     h.default                               #=> "cat"
 *     h.default(2)                            #=> "cat"
 *
 *     h = Hash.new {|h,k| h[k] = k.to_i*10}   #=> {}
 *     h.default                               #=> nil
 *     h.default(2)                            #=> 20
 */

static mrb_value
mrb_hash_default(mrb_state *mrb, mrb_value hash)
{
    mrb_value *argv;
    int argc;
    mrb_value key;

    mrb_get_args(mrb, "*", &argv, &argc);
    if (MRB_RHASH_PROCDEFAULT_P(hash)) {
        if (argc == 0) return mrb_nil_value();
        key = argv[0];
        return mrb->funcall(RHASH_PROCDEFAULT(hash), "call", 2, hash, key);
    }
    return RHASH_IFNONE(hash);
}

/* 15.2.13.4.6  */
/*
 *  call-seq:
 *     hsh.default = obj     -> obj
 *
 *  Sets the default value, the value returned for a key that does not
 *  exist in the hash. It is not possible to set the default to a
 *  <code>Proc</code> that will be executed on each key lookup.
 *
 *     h = { "a" => 100, "b" => 200 }
 *     h.default = "Go fish"
 *     h["a"]     #=> 100
 *     h["z"]     #=> "Go fish"
 *     # This doesn't do what you might hope...
 *     h.default = proc do |hash, key|
 *       hash[key] = key + key
 *     end
 *     h[2]       #=> #<Proc:0x401b3948@-:6>
 *     h["cat"]   #=> #<Proc:0x401b3948@-:6>
 */

static mrb_value mrb_hash_set_default(mrb_state *mrb, mrb_value hash)
{
    mrb_value ifnone = mrb->get_arg<mrb_value>();
    mrb_hash_modify(mrb, hash);
    mrb_hash_ptr(hash)->iv_set(mrb_intern_lit(mrb,"ifnone"), ifnone);
    RHASH(hash)->flags &= ~(MRB_HASH_PROC_DEFAULT);

    return ifnone;
}

/* 15.2.13.4.7  */
/*
 *  call-seq:
 *     hsh.default_proc -> anObject
 *
 *  If <code>Hash::new</code> was invoked with a block, return that
 *  block, otherwise return <code>nil</code>.
 *
 *     h = Hash.new {|h,k| h[k] = k*k }   #=> {}
 *     p = h.default_proc                 #=> #<Proc:0x401b3d08@-:1>
 *     a = []                             #=> []
 *     p.call(a, 2)
 *     a                                  #=> [nil, nil, 4]
 */
static mrb_value mrb_hash_default_proc(mrb_state *mrb, mrb_value hash)
{
    if (MRB_RHASH_PROCDEFAULT_P(hash)) {
        return RHASH_PROCDEFAULT(hash);
    }
    return mrb_nil_value();
}

/*
 *  call-seq:
 *     hsh.default_proc = proc_obj     -> proc_obj
 *
 *  Sets the default proc to be executed on each key lookup.
 *
 *     h.default_proc = proc do |hash, key|
 *       hash[key] = key + key
 *     end
 *     h[2]       #=> 4
 *     h["cat"]   #=> "catcat"
 */

static mrb_value mrb_hash_set_default_proc(mrb_state *mrb, mrb_value hash)
{
    mrb_value ifnone = mrb->get_arg<mrb_value>();
    mrb_hash_modify(mrb, hash);
    mrb_iv_set(mrb, hash, mrb->intern2("ifnone", 6), ifnone);
    RHASH(hash)->flags |= MRB_HASH_PROC_DEFAULT;

    return ifnone;
}

mrb_value mrb_hash_delete_key(mrb_value hash, mrb_value key)
{
    RHash::kh_ht_t *h = RHASH_TBL(hash);

    if (h) {
        auto k = h->get(key);
        if (k != h->end()) {
            mrb_value delVal = h->value(k);
            h->del(k);
            return delVal;
        }
    }

    /* not found */
    return mrb_nil_value();
}

/* 15.2.13.4.8  */
/*
 *  call-seq:
 *     hsh.delete(key)                   -> value
 *     hsh.delete(key) {| key | block }  -> value
 *
 *  Deletes and returns a key-value pair from <i>hsh</i> whose key is
 *  equal to <i>key</i>. If the key is not found, returns the
 *  <em>default value</em>. If the optional code block is given and the
 *  key is not found, pass in the key and return the result of
 *  <i>block</i>.
 *
 *     h = { "a" => 100, "b" => 200 }
 *     h.delete("a")                              #=> 100
 *     h.delete("z")                              #=> nil
 *     h.delete("z") { |el| "#{el} not found" }   #=> "z not found"
 *
 */
mrb_value
mrb_hash_delete(mrb_state *mrb, mrb_value self)
{
    mrb_value key = mrb->get_arg<mrb_value>();
    return mrb_hash_delete_key(self, key);
}

/* 15.2.13.4.24 */
/*
 *  call-seq:
 *     hsh.shift -> anArray or obj
 *
 *  Removes a key-value pair from <i>hsh</i> and returns it as the
 *  two-item array <code>[</code> <i>key, value</i> <code>]</code>, or
 *  the hash's default value if the hash is empty.
 *
 *     h = { 1 => "a", 2 => "b", 3 => "c" }
 *     h.shift   #=> [1, "a"]
 *     h         #=> {2=>"b", 3=>"c"}
 */

static mrb_value
mrb_hash_shift(mrb_state *mrb, mrb_value hash)
{
    RHash::kh_ht_t *h = RHASH_TBL(hash);
    mrb_value delKey, delVal;

    mrb_hash_modify(mrb, hash);
    if (h) {
        if (h->size() > 0) {
            for (auto k = h->begin(); k != h->end(); k++) {
                if (!h->exist(k))
                    continue;

                delKey = h->key(k);
                mrb_gc_protect(mrb, delKey);
                delVal = mrb_hash_delete_key(hash, delKey);
                mrb_gc_protect(mrb, delVal);

                return mrb_assoc_new(mrb, delKey, delVal);
            }
        }
    }

    if (MRB_RHASH_PROCDEFAULT_P(hash)) {
        return mrb->funcall(RHASH_PROCDEFAULT(hash), "call", 2, hash, mrb_nil_value());
    }
    else {
        return RHASH_IFNONE(hash);
    }
}

/* 15.2.13.4.4  */
/*
 *  call-seq:
 *     hsh.clear -> hsh
 *
 *  Removes all key-value pairs from <i>hsh</i>.
 *
 *     h = { "a" => 100, "b" => 200 }   #=> {"a"=>100, "b"=>200}
 *     h.clear                          #=> {}
 *
 */

mrb_value mrb_hash_clear(mrb_state *mrb, mrb_value hash)
{
    RHash::kh_ht_t *h = RHASH_TBL(hash);

    if (h)
        RHash::kh_ht_t::clear(h);
    return hash;
}

/* 15.2.13.4.3  */
/* 15.2.13.4.26 */
/*
 *  call-seq:
 *     hsh[key] = value        -> value
 *     hsh.store(key, value)   -> value
 *
 *  Element Assignment---Associates the value given by
 *  <i>value</i> with the key given by <i>key</i>.
 *  <i>key</i> should not have its value changed while it is in
 *  use as a key (a <code>String</code> passed as a key will be
 *  duplicated and frozen).
 *
 *     h = { "a" => 100, "b" => 200 }
 *     h["a"] = 9
 *     h["c"] = 4
 *     h   #=> {"a"=>9, "b"=>200, "c"=>4}
 *
 */
mrb_value
mrb_hash_aset(mrb_state *mrb, mrb_value self)
{
    mrb_value key, val;

    mrb_get_args(mrb, "oo", &key, &val);
    mrb_hash_set(mrb, self, key, val);
    return val;
}

/* 15.2.13.4.17 */
/* 15.2.13.4.23 */
/*
 *  call-seq:
 *     hsh.replace(other_hash) -> hsh
 *
 *  Replaces the contents of <i>hsh</i> with the contents of
 *  <i>other_hash</i>.
 *
 *     h = { "a" => 100, "b" => 200 }
 *     h.replace({ "c" => 300, "d" => 400 })   #=> {"c"=>300, "d"=>400}
 *
 */

static mrb_value mrb_hash_replace(mrb_state *mrb, mrb_value hash)
{
    mrb_value ifnone;

    mrb_value hash2 = mrb->get_arg<mrb_value>();
    hash2 = to_hash(mrb, hash2);
    if (mrb_obj_equal(hash, hash2))
        return hash;
    mrb_hash_clear(mrb, hash);
    auto h2 = RHASH_TBL(hash2);
    if (h2) {
        for (auto k = h2->begin(); k != h2->end(); k++) {
            if (h2->exist(k))
                mrb_hash_set(mrb, hash, h2->key(k), h2->value(k));
        }
    }

    if (MRB_RHASH_PROCDEFAULT_P(hash2)) {
        RHASH(hash)->flags |= MRB_HASH_PROC_DEFAULT;
        ifnone = RHASH_PROCDEFAULT(hash2);
    }
    else {
        ifnone = RHASH_IFNONE(hash2);
    }
    mrb_hash_ptr(hash)->iv_set(mrb->intern2("ifnone", 6), ifnone);
    return hash;
}

/* 15.2.13.4.20 */
/* 15.2.13.4.25 */
/*
 *  call-seq:
 *     hsh.length    ->  fixnum
 *     hsh.size      ->  fixnum
 *
 *  Returns the number of key-value pairs in the hash.
 *
 *     h = { "d" => 100, "a" => 200, "v" => 300, "e" => 400 }
 *     h.length        #=> 4
 *     h.delete("a")   #=> 200
 *     h.length        #=> 3
 */
static mrb_value
mrb_hash_size_m(mrb_state *mrb, mrb_value self)
{
    auto *h = RHASH_TBL(self);

    if (!h)
        return mrb_fixnum_value(0);
    return mrb_fixnum_value(h->size());
}

/* 15.2.13.4.12 */
/*
 *  call-seq:
 *     hsh.empty?    -> true or false
 *
 *  Returns <code>true</code> if <i>hsh</i> contains no key-value pairs.
 *
 *     {}.empty?   #=> true
 *
 */
mrb_value mrb_hash_empty_p(mrb_state *mrb, mrb_value self)
{
    auto h = RHASH_TBL(self);
    if (h)
        return mrb_bool_value(h->size() == 0);
    return mrb_true_value();
}

static mrb_value inspect_hash(mrb_value hash, bool recur)
{
    mrb_value str, str2;
    RHash *hsh = mrb_hash_ptr(hash);
    RHash::kh_ht_t *h = hsh->ht;

    if (recur)
        return mrb_str_new_lit(hsh->m_vm, "{...}");

    str = mrb_str_new_lit(hsh->m_vm, "{");
    if (h && h->size() > 0) {
        for (auto k = h->begin(); k != h->end(); k++) {
            int ai;

            if (!h->exist(k))
                continue;

            ai = hsh->m_vm->gc().arena_save();

            if (RSTRING_LEN(str) > 1)
                mrb_str_cat_lit(hsh->m_vm, str, ", ");

            str2 = mrb_inspect(hsh->m_vm, h->key(k));
            mrb_str_append(hsh->m_vm, str, str2);
            mrb_str_buf_cat(str, "=>", 2);
            str2 = mrb_inspect(hsh->m_vm, h->value(k));
            mrb_str_append(hsh->m_vm, str, str2);

            hsh->m_vm->gc().arena_restore(ai);
        }
    }
    mrb_str_buf_cat(str, "}", 1);

    return str;
}

/* 15.2.13.4.30 (x)*/
/*
 * call-seq:
 *   hsh.to_s     -> string
 *   hsh.inspect  -> string
 *
 * Return the contents of this hash as a string.
 *
 *     h = { "c" => 300, "a" => 100, "d" => 400, "c" => 300  }
 *     h.to_s   #=> "{\"c\"=>300, \"a\"=>100, \"d\"=>400}"
 */

static mrb_value mrb_hash_inspect(mrb_state *mrb, mrb_value hash)
{
    RHash::kh_ht_t *h = RHASH_TBL(hash);

    if (!h || h->size() == 0)
        return mrb_str_new_lit(mrb, "{}");
    return inspect_hash(hash, 0);
}

/* 15.2.13.4.29 (x)*/
/*
 * call-seq:
 *    hsh.to_hash   => hsh
 *
 * Returns +self+.
 */

static mrb_value mrb_hash_to_hash(mrb_state *mrb, mrb_value hash)
{
    return hash;
}

/* 15.2.13.4.19 */
/*
 *  call-seq:
 *     hsh.keys    -> array
 *
 *  Returns a new array populated with the keys from this hash. See also
 *  <code>Hash#values</code>.
 *
 *     h = { "a" => 100, "b" => 200, "c" => 300, "d" => 400 }
 *     h.keys   #=> ["a", "b", "c", "d"]
 *
 */

mrb_value
mrb_hash_keys(mrb_state *mrb, mrb_value hash)
{
    RHash::kh_ht_t *h = RHASH_TBL(hash);
    if (!h)
        return mrb_ary_new(mrb);
    RArray *p_ary = RArray::create(mrb,h->size());
    for (auto k = h->begin(); k != h->end(); k++) {
        if (h->exist(k)) {
            p_ary->push(h->key(k));
        }
    }
    return mrb_obj_value(p_ary);
}

/* 15.2.13.4.28 */
/*
 *  call-seq:
 *     hsh.values    -> array
 *
 *  Returns a new array populated with the values from <i>hsh</i>. See
 *  also <code>Hash#keys</code>.
 *
 *     h = { "a" => 100, "b" => 200, "c" => 300 }
 *     h.values   #=> [100, 200, 300]
 *
 */

static mrb_value mrb_hash_values(mrb_state *mrb, mrb_value hash)
{
    RHash::kh_ht_t *h = RHASH_TBL(hash);
    if (!h)
        return mrb_obj_value(RArray::create(mrb,0));

    RArray *arr = RArray::create(mrb,h->size());
    for (auto k = h->begin(); k != h->end(); k++) {
        if (h->exist(k)){
            mrb_value v = h->value(k);
            arr->push(v);
        }
    }
    return mrb_obj_value(arr);
}

static mrb_value mrb_hash_has_keyWithKey(mrb_value hash, mrb_value key)
{
    RHash::kh_ht_t *h = RHASH_TBL(hash);
    RHash::kh_ht_t::iterator k;

    if (h) {
        k = h->get(key);
        return mrb_bool_value(k != h->end());
    }
    return mrb_false_value();
}

/* 15.2.13.4.13 */
/* 15.2.13.4.15 */
/* 15.2.13.4.18 */
/* 15.2.13.4.21 */
/*
 *  call-seq:
 *     hsh.has_key?(key)    -> true or false
 *     hsh.include?(key)    -> true or false
 *     hsh.key?(key)        -> true or false
 *     hsh.member?(key)     -> true or false
 *
 *  Returns <code>true</code> if the given key is present in <i>hsh</i>.
 *
 *     h = { "a" => 100, "b" => 200 }
 *     h.has_key?("a")   #=> true
 *     h.has_key?("z")   #=> false
 *
 */

static mrb_value mrb_hash_has_key(mrb_state *mrb, mrb_value hash)
{
    mrb_value key = mrb->get_arg<mrb_value>();
    return mrb_hash_has_keyWithKey(hash, key);
}

static mrb_value
mrb_hash_has_valueWithvalue(mrb_state *mrb, mrb_value hash, mrb_value value)
{
    RHash::kh_ht_t *h = RHASH_TBL(hash);
    if (h) {
        for (auto k = h->begin(); k != h->end(); k++) {
            if (!h->exist(k))
                continue;

            if (mrb_equal(mrb, h->value(k), value)) {
                return mrb_true_value();
            }
        }
    }

    return mrb_false_value();
}

/* 15.2.13.4.14 */
/* 15.2.13.4.27 */
/*
 *  call-seq:
 *     hsh.has_value?(value)    -> true or false
 *     hsh.value?(value)        -> true or false
 *
 *  Returns <code>true</code> if the given value is present for some key
 *  in <i>hsh</i>.
 *
 *     h = { "a" => 100, "b" => 200 }
 *     h.has_value?(100)   #=> true
 *     h.has_value?(999)   #=> false
 */

static mrb_value mrb_hash_has_value(mrb_state *mrb, mrb_value hash)
{
    mrb_value val = mrb->get_arg<mrb_value>();
    return mrb_hash_has_valueWithvalue(mrb, hash, val);
}

static mrb_value
hash_equal(mrb_state *mrb, mrb_value hash1, mrb_value hash2, int eql)
{
    RHash::kh_ht_t *h1,*h2;
    //khash_t(ht) *h1, *h2;

    if (mrb_obj_equal(hash1, hash2))
        return mrb_true_value();
    if (!mrb_hash_p(hash2)) {
        if (!mrb_respond_to(mrb, hash2, mrb->intern2("to_hash", 7))) {
            return mrb_false_value();
        }
        if (eql)
            return mrb_bool_value(mrb_eql(mrb, hash2, hash1));
        return mrb_bool_value(mrb_equal(mrb, hash2, hash1));
    }
    h1 = RHASH_TBL(hash1);
    h2 = RHASH_TBL(hash2);
    if (!h1) {
        return mrb_bool_value(!h2);
    }
    if (!h2)
        return mrb_false_value();
    if (h1->size() != h2->size())
        return mrb_false_value();

    RHash::kh_ht_t::iterator k1, k2;
    mrb_value key;
    bool eq;
    if(eql)
    {
        for (k1 = h1->begin(); k1 != h1->end(); k1++) {
            if (!h1->exist(k1))
                continue;
            key = h1->key(k1);
            k2 = h2->get(key);
            if (k2 != h2->end()) {
                if (mrb_eql(mrb, h1->value(k1), h2->value(k2))) {
                    continue; /* next key */
                }
            }
            return mrb_false_value();
        }
    }
    else {
        for (k1 = h1->begin(); k1 != h1->end(); k1++) {
            if (!h1->exist(k1))
                continue;
            key = h1->key(k1);
            k2 = h2->get(key);
            if (k2 != h2->end()) {
                if (mrb_equal(mrb, h1->value(k1), h2->value(k2))) {
                    continue; /* next key */
                }
            }
            return mrb_false_value();
        }
    }
    return mrb_true_value();
}

/* 15.2.13.4.1  */
/*
 *  call-seq:
 *     hsh == other_hash    -> true or false
 *
 *  Equality---Two hashes are equal if they each contain the same number
 *  of keys and if each key-value pair is equal to (according to
 *  <code>Object#==</code>) the corresponding elements in the other
 *  hash.
 *
 *     h1 = { "a" => 1, "c" => 2 }
 *     h2 = { 7 => 35, "c" => 2, "a" => 1 }
 *     h3 = { "a" => 1, "c" => 2, 7 => 35 }
 *     h4 = { "a" => 1, "d" => 2, "f" => 35 }
 *     h1 == h2   #=> false
 *     h2 == h3   #=> true
 *     h3 == h4   #=> false
 *
 */

static mrb_value
mrb_hash_equal(mrb_state *mrb, mrb_value hash1)
{
    mrb_value hash2 = mrb->get_arg<mrb_value>();
    return hash_equal(mrb, hash1, hash2, false);
}

/* 15.2.13.4.32 (x)*/
/*
 *  call-seq:
 *     hash.eql?(other)  -> true or false
 *
 *  Returns <code>true</code> if <i>hash</i> and <i>other</i> are
 *  both hashes with the same content.
 */

static mrb_value mrb_hash_eql(mrb_state *mrb, mrb_value hash1)
{
    mrb_value hash2 = mrb->get_arg<mrb_value>();
    return hash_equal(mrb, hash1, hash2, true);
}
/*
 *  A <code>Hash</code> is a collection of key-value pairs. It is
 *  similar to an <code>Array</code>, except that indexing is done via
 *  arbitrary keys of any object type, not an integer index. Hashes enumerate
 *  their values in the order that the corresponding keys were inserted.
 *
 *  Hashes have a <em>default value</em> that is returned when accessing
 *  keys that do not exist in the hash. By default, that value is
 *  <code>nil</code>.
 *
 */

void
mrb_init_hash(mrb_state *mrb)
{
    mrb->hash_class = &mrb->define_class("Hash", mrb->object_class)
            .instance_tt(MRB_TT_HASH)
            .define_method("==",              mrb_hash_equal,       MRB_ARGS_REQ(1)) /* 15.2.13.4.1  */
            .define_method("[]",              mrb_hash_aget,        MRB_ARGS_REQ(1)) /* 15.2.13.4.2  */
            .define_method("[]=",             mrb_hash_aset,        MRB_ARGS_REQ(2)) /* 15.2.13.4.3  */
            .define_method("clear",           mrb_hash_clear,       MRB_ARGS_NONE()) /* 15.2.13.4.4  */
            .define_method("default",         mrb_hash_default,     MRB_ARGS_ANY())  /* 15.2.13.4.5  */
            .define_method("default=",        mrb_hash_set_default, MRB_ARGS_REQ(1)) /* 15.2.13.4.6  */
            .define_method("default_proc",    mrb_hash_default_proc,MRB_ARGS_NONE()) /* 15.2.13.4.7  */
            .define_method("default_proc=",   mrb_hash_set_default_proc,MRB_ARGS_REQ(1)) /* 15.2.13.4.7  */
            .define_method("__delete",        mrb_hash_delete,      MRB_ARGS_REQ(1)) /* core of 15.2.13.4.8  */
            // "each"                                                               15.2.13.4.9  move to mrblib/hash.rb
            // "each_key"                                                           15.2.13.4.10 move to mrblib/hash.rb
            // "each_value"                                                         15.2.13.4.11 move to mrblib/hash.rb
            .define_method("empty?",          mrb_hash_empty_p,     MRB_ARGS_NONE()) /* 15.2.13.4.12 */
            .define_method("has_key?",        mrb_hash_has_key,     MRB_ARGS_REQ(1)) /* 15.2.13.4.13 */
            .define_method("has_value?",      mrb_hash_has_value,   MRB_ARGS_REQ(1)) /* 15.2.13.4.14 */
            .define_method("include?",        mrb_hash_has_key,     MRB_ARGS_REQ(1)) /* 15.2.13.4.15 */
            .define_method("__init_core",     mrb_hash_init_core,   MRB_ARGS_ANY())  /* core of 15.2.13.4.16 */
            .define_method("initialize_copy", mrb_hash_replace,     MRB_ARGS_REQ(1)) /* 15.2.13.4.17 */
            .define_method("key?",            mrb_hash_has_key,     MRB_ARGS_REQ(1)) /* 15.2.13.4.18 */
            .define_method("keys",            mrb_hash_keys,        MRB_ARGS_NONE()) /* 15.2.13.4.19 */
            .define_method("length",          mrb_hash_size_m,      MRB_ARGS_NONE()) /* 15.2.13.4.20 */
            .define_method("member?",         mrb_hash_has_key,     MRB_ARGS_REQ(1)) /* 15.2.13.4.21 */
            // "merge"                                                                            15.2.13.4.22 move to mrblib/hash.rb
            .define_method("replace",         mrb_hash_replace,     MRB_ARGS_REQ(1)) /* 15.2.13.4.23 */
            .define_method("shift",           mrb_hash_shift,       MRB_ARGS_NONE()) /* 15.2.13.4.24 */
            .define_method("dup",             mrb_hash_dup,         MRB_ARGS_NONE())
            .define_method("size",            mrb_hash_size_m,      MRB_ARGS_NONE()) /* 15.2.13.4.25 */
            .define_method("store",           mrb_hash_aset,        MRB_ARGS_REQ(2)) /* 15.2.13.4.26 */
            .define_method("value?",          mrb_hash_has_value,   MRB_ARGS_REQ(1)) /* 15.2.13.4.27 */
            .define_method("values",          mrb_hash_values,      MRB_ARGS_NONE()) /* 15.2.13.4.28 */

            .define_method("to_hash",         mrb_hash_to_hash,     MRB_ARGS_NONE()) /* 15.2.13.4.29 (x)*/
            .define_method("inspect",         mrb_hash_inspect,     MRB_ARGS_NONE()) /* 15.2.13.4.30 (x)*/
            .define_alias("to_s",            "inspect")                         /* 15.2.13.4.31 (x)*/
            .define_method("eql?",            mrb_hash_eql,         MRB_ARGS_REQ(1)) /* 15.2.13.4.32 (x)*/
            ;
}


khint_t ValueHashEq::operator()(MemManager *m, mrb_value a, mrb_value b) const {
    return mrb_eql(m->vm(), a, b);
}
