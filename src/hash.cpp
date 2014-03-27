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

static inline mrb_value mrb_hash_ht_key(mrb_state *mrb, mrb_value key)
{
    if (key.is_string())
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


RHash *RHash::new_capa(mrb_state *mrb, int capa)
{
    RHash *h = mrb->gc().obj_alloc<RHash>(mrb->hash_class);
    h->ht = RHash::kh_ht_t::init(mrb->gc());
    if (capa > 0) {
        h->ht->resize(capa);
    }
    h->iv = 0;
    return h;
}

mrb_value RHash::get(mrb_value key)
{
    mrb_state *mrb = m_vm;
    RHash::kh_ht_t *h = ht;

    if (h) {
        auto k = h->get(key);
        if (k != h->end())
            return h->value(k);
    }

    /* not found */
    if (flags & MRB_HASH_PROC_DEFAULT) {
        return mrb->funcall(iv_get(mrb->intern2("ifnone", 6)), "call", 2, mrb_value::wrap(this), key);
    }
    return iv_get(mrb->intern2("ifnone", 6));
}

mrb_value RHash::fetch(mrb_value key, mrb_value def)
{
    if (ht) {
        auto k = ht->get(key);
        if (k != ht->end())
            return ht->value(k);
    }
    /* not found */
    return def;
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
mrb_value RHash::set(mrb_value key, mrb_value val) /* mrb_hash_aset */
{
    RHash::kh_ht_t *h;

    modify();
    h = this->ht;

    if (!h)
        h = this->ht = RHash::kh_ht_t::init(m_vm->gc());
    auto k = h->get(key);
    if (k == h->end()) {
        /* expand */
        int ai = m_vm->gc().arena_save();
        k=h->put(mrb_hash_ht_key(m_vm,key));
        m_vm->gc().arena_restore(ai);
    }
    h->value(k) = val;
    m_vm->gc().mrb_write_barrier(this);
    return val;
}

RHash *RHash::dup() const
{
    RHash* ret;
    RHash::kh_ht_t *h;
    khiter_t k;

    h = ht;
    ret = m_vm->gc().obj_alloc<RHash>(m_vm->hash_class);
    ret->ht = RHash::kh_ht_t::init(m_vm->gc());

    if (h->size() > 0) {
        RHash::kh_ht_t *ret_h = ret->ht;

        for (k = h->begin(); k != h->end(); k++) {
            if (h->exist(k)) {
                int ai = m_vm->gc().arena_save();
                auto ret_k = ret_h->put(mrb_hash_ht_key(m_vm,h->key(k)));
                m_vm->gc().arena_restore(ai);
                ret_h->value(ret_k) = h->value(k);
            }
        }
    }

    return ret;
}
void RHash::init_ht()
{
    if (!ht) {
        ht = RHash::kh_ht_t::init(m_vm->gc());
    }
}

void RHash::modify()
{
    init_ht();
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
RHash *RHash::init_core(mrb_value block,int argc, mrb_value *argv) {
    mrb_value ifnone;

    mrb_get_args(m_vm, "o*", &block, &argv, &argc);
    modify();
    if (block.is_nil()) {
        if (argc > 0) {
            if (argc != 1) m_vm->mrb_raise(A_ARGUMENT_ERROR(m_vm), "wrong number of arguments");
            ifnone = argv[0];
        }
        else {
            ifnone = mrb_value::nil();
        }
    }
    else {
        if (argc > 0) {
            m_vm->mrb_raise(A_ARGUMENT_ERROR(m_vm), "wrong number of arguments");
        }
        flags |= MRB_HASH_PROC_DEFAULT;
        ifnone = block;
    }
    iv_set(mrb_intern_lit(m_vm, "ifnone"), ifnone);
    return this;
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
mrb_value RHash::aget(mrb_value key)
{
    return get(key);
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

mrb_value RHash::default_val()
{
    mrb_value key = mrb_value::nil();

    mrb_get_args(m_vm, "|o", &key);
    if (flags & MRB_HASH_PROC_DEFAULT) {
        if (key.is_nil())
            return mrb_value::nil();
        return m_vm->funcall(iv_get(m_vm->intern2("ifnone", 6)), "call", 2, mrb_value::wrap(this), key);
    }
    return iv_get(m_vm->intern2("ifnone", 6));
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

mrb_value RHash::set_default(mrb_value ifnone)
{
    modify();
    iv_set(mrb_intern_lit(m_vm,"ifnone"), ifnone);
    flags &= ~(MRB_HASH_PROC_DEFAULT);
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
mrb_value RHash::default_proc()
{
    if(flags & MRB_HASH_PROC_DEFAULT)
        return iv_get(m_vm->intern2("ifnone", 6));
    return mrb_value::nil();
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

mrb_value RHash::set_default_proc(mrb_value ifnone)
{
    modify();
    iv_set(m_vm->intern2("ifnone", 6), ifnone);
    flags |= MRB_HASH_PROC_DEFAULT;
    return ifnone;
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
mrb_value RHash::delete_key(mrb_value key)
{
    RHash::kh_ht_t *h = this->ht;

    if (h) {
        auto k = h->get(key);
        if (k != h->end()) {
            mrb_value delVal = h->value(k);
            h->del(k);
            return delVal;
        }
    }

    /* not found */
    return mrb_value::nil();
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

mrb_value RHash::shift()
{
    RHash::kh_ht_t *h = this->ht;
    mrb_value delKey, delVal;

    modify();
    if (h) {
        if (h->size() > 0) {
            for (auto k = h->begin(); k != h->end(); k++) {
                if (!h->exist(k))
                    continue;

                delKey = h->key(k);
                mrb_gc_protect(m_vm, delKey);
                delVal = delete_key(delKey);
                mrb_gc_protect(m_vm, delVal);

                return mrb_value::wrap(mrb_assoc_new(m_vm, delKey, delVal));
            }
        }
    }

    if (flags & MRB_HASH_PROC_DEFAULT) {
        return m_vm->funcall(iv_get(m_vm->intern2("ifnone", 6)), "call", 2, mrb_value::wrap(this), mrb_value::nil());
    }
    else {
        return iv_get(m_vm->intern2("ifnone", 6));
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

void RHash::clear()
{
    RHash::kh_ht_t::clear(ht);
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

mrb_value RHash::replace(mrb_value hash2)
{
    mrb_value ifnone;
    mrb_value self = mrb_value::wrap(this);
    hash2 = ::to_hash(m_vm, hash2);
    if (mrb_obj_equal(self, hash2))
        return self;
    RHash *other_ptr = hash2.ptr<RHash>();
    clear();
    auto h2 = other_ptr->ht;
    if (h2) {
        for (auto k = h2->begin(); k != h2->end(); k++) {
            if (h2->exist(k))
                set(h2->key(k), h2->value(k));
        }
    }

    ifnone =  other_ptr->iv_get(m_vm->intern2("ifnone", 6));
    if (other_ptr->flags & MRB_HASH_PROC_DEFAULT) {
        flags |= MRB_HASH_PROC_DEFAULT;
    }
    iv_set(m_vm->intern2("ifnone", 6), ifnone);
    return self;
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
mrb_int RHash::size() const
{
    return ht? ht->size() : 0;
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
bool RHash::empty() const
{
    return ht ? (ht->size() == 0) : true;
}

static RString *inspect_hash(RHash *hsh, bool recur)
{
    RHash::kh_ht_t *h = hsh->ht;
    auto vm = hsh->m_vm;
    if (recur)
        return mrb_str_new_lit(vm, "{...}");

    RString *str = mrb_str_new_lit(vm, "{");
    if (h && h->size() > 0) {
        for (auto k = h->begin(); k != h->end(); k++) {
            int ai;

            if (!h->exist(k))
                continue;

            ai = hsh->m_vm->gc().arena_save();

            if (str->len > 1)
                str->str_buf_cat(", ",2);

            str->str_cat(mrb_inspect(vm, h->key(k)));
            str->str_cat("=>", 2);
            str->str_cat(mrb_inspect(vm, h->value(k)));
            vm->gc().arena_restore(ai);
        }
    }
    str->str_buf_cat("}", 1);

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
    RHash *h = hash.ptr<RHash>();

    if (h->empty())
        return mrb_str_new_lit(mrb, "{}")->wrap();
    return inspect_hash(h, 0)->wrap();
}

/* 15.2.13.4.29 (x)*/
/*
 * call-seq:
 *    hsh.to_hash   => hsh
 *
 * Returns +self+.
 */

RHash *RHash::toHash()
{
    return this;
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

RArray *RHash::keys()
{
    RHash::kh_ht_t *h = ht;
    size_t sz = h ? h->size() : 0;
    RArray *p_ary = RArray::create(m_vm,sz);
    if (h) {
        for (auto k = h->begin(); k != h->end(); k++) {
            if (h->exist(k)) {
                p_ary->push(h->key(k));
            }
        }
    }
    return p_ary;
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

RArray *RHash::values()
{
    RHash::kh_ht_t *h = ht;
    size_t sz = ht ? ht->size() : 0;
    RArray *arr = RArray::create(m_vm,sz);
    if (h) {
        for (auto k = h->begin(); k != h->end(); k++) {
            if (h->exist(k)){
                mrb_value v = h->value(k);
                arr->push(v);
            }
        }
    }
    return arr;
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
bool RHash::hasKey(mrb_value key)
{
    return ht ? (ht->get(key) != ht->end()) : false;
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
bool RHash::hasValue(mrb_value value)
{
    RHash::kh_ht_t *h = ht;
    if (h) {
        for (auto k = h->begin(); k != h->end(); k++) {
            if (!h->exist(k))
                continue;

            if (mrb_equal(m_vm, h->value(k), value)) {
                return true;
            }
        }
    }

    return false;
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
/* 15.2.13.4.32 (x)*/
/*
 *  call-seq:
 *     hash.eql?(other)  -> true or false
 *
 *  Returns <code>true</code> if <i>hash</i> and <i>other</i> are
 *  both hashes with the same content.
 */

bool RHash::hash_equal(mrb_value other_hash, bool eql) // eql == 1 perform eql? op
{
    RHash::kh_ht_t *h1,*h2;
    mrb_value self = mrb_value::wrap(this);
    if(other_hash.tt == MRB_TT_HASH && other_hash.ptr<RHash>()==this)
        return true;
    if (!other_hash.is_hash()) {
        if (!other_hash.respond_to(m_vm, m_vm->intern2("to_hash", 7))) {
            return false;
        }
        if (eql)
            return mrb_eql(m_vm, other_hash, self);
        return mrb_equal(m_vm, other_hash, self);
    }
    RHash *other_ptr = other_hash.ptr<RHash>();
    h1 = ht;
    h2 = other_ptr->ht;
    if (!h1) {
        return !h2;
    }
    if (!h2)
        return false;
    if (h1->size() != h2->size())
        return false;

    RHash::kh_ht_t::iterator k1, k2;
    mrb_value key;
    if(eql)
    {
        for (k1 = h1->begin(); k1 != h1->end(); k1++) {
            if (!h1->exist(k1))
                continue;
            key = h1->key(k1);
            k2 = h2->get(key);
            if (k2 != h2->end()) {
                if (mrb_eql(m_vm, h1->value(k1), h2->value(k2))) {
                    continue; /* next key */
                }
            }
            return false;
        }
    }
    else {
        for (k1 = h1->begin(); k1 != h1->end(); k1++) {
            if (!h1->exist(k1))
                continue;
            key = h1->key(k1);
            k2 = h2->get(key);
            if (k2 != h2->end()) {
                if (mrb_equal(m_vm, h1->value(k1), h2->value(k2))) {
                    continue; /* next key */
                }
            }
            return false;
        }
    }
    return true;
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
namespace {
#define NOWRAP(x) x
#define WRAP(x) mrb_value::wrap(x)

#define FORWARD_TO_INSTANCE(name)\
    static mrb_value name(mrb_state *mrb, mrb_value self) {\
    return mrb_value::wrap(self.ptr<RHash>()->name());\
}
#define FORWARD_TO_INSTANCE_O(name,opt_wrap)\
    static mrb_value name(mrb_state *mrb, mrb_value self) {\
    return opt_wrap(self.ptr<RHash>()->name());\
}
#define FORWARD_TO_INSTANCE_ARG1(name)\
    static mrb_value name(mrb_state *mrb, mrb_value self) {\
    mrb_value arg0 = mrb->get_arg<mrb_value>();\
    return mrb_value::wrap(self.ptr<RHash>()->name(arg0));\
}
#define FORWARD_TO_INSTANCE_ARG2(name)\
    static mrb_value name(mrb_state *mrb, mrb_value self) {\
    mrb_value arg0,arg1;\
    mrb_get_args(mrb, "oo", &arg0,&arg1);\
    return self.ptr<RHash>()->name(arg0,arg1);\
}
#define FORWARD_TO_INSTANCE_RET_SELF(name)\
    static mrb_value name(mrb_state *mrb, mrb_value self) {\
    self.ptr<RHash>()->name();\
    return self;\
}
FORWARD_TO_INSTANCE(shift)
FORWARD_TO_INSTANCE(default_val)
FORWARD_TO_INSTANCE(keys)
FORWARD_TO_INSTANCE(values)
FORWARD_TO_INSTANCE(empty)
FORWARD_TO_INSTANCE(dup)
FORWARD_TO_INSTANCE(size)
FORWARD_TO_INSTANCE(default_proc)
FORWARD_TO_INSTANCE_ARG1(aget)
FORWARD_TO_INSTANCE_ARG1(delete_key)
FORWARD_TO_INSTANCE_ARG1(set_default)
FORWARD_TO_INSTANCE_ARG1(set_default_proc)
FORWARD_TO_INSTANCE_ARG1(hasKey)
FORWARD_TO_INSTANCE_ARG1(hasValue)
FORWARD_TO_INSTANCE_ARG2(set)
FORWARD_TO_INSTANCE_RET_SELF(toHash)
FORWARD_TO_INSTANCE_RET_SELF(clear)
FORWARD_TO_INSTANCE_ARG1(replace)

#undef FORWARD_TO_INSTANCE
#undef FORWARD_TO_INSTANCE_RET_SELF
#undef FORWARD_TO_INSTANCE_ARG1
#undef FORWARD_TO_INSTANCE_ARG2
#undef WRAP
#undef NOWRAP
static mrb_value mrb_hash_init_core(mrb_state *mrb, mrb_value hash)
{
    mrb_value block;
    mrb_value *argv;
    int argc;

    mrb_get_args(mrb, "o*", &block, &argv, &argc);
    return mrb_value::wrap(hash.ptr<RHash>()->init_core(block,argc,argv));
}
mrb_value hash_equal(mrb_state *mrb, mrb_value hash1)
{
    return mrb_value::wrap(hash1.ptr<RHash>()->hash_equal(mrb->get_arg<mrb_value>(),false));
}


static mrb_value hash_eql(mrb_state *mrb, mrb_value hash1)
{
    return mrb_value::wrap(hash1.ptr<RHash>()->hash_equal(mrb->get_arg<mrb_value>(),true));
}

}

void
mrb_init_hash(mrb_state *mrb)
{
    mrb->hash_class = &mrb->define_class("Hash", mrb->object_class)
            .instance_tt(MRB_TT_HASH)
            .define_method("==",              hash_equal,           MRB_ARGS_REQ(1)) /* 15.2.13.4.1  */
            .define_method("[]",              aget,                 MRB_ARGS_REQ(1)) /* 15.2.13.4.2  */
            .define_method("[]=",             set,                  MRB_ARGS_REQ(2)) /* 15.2.13.4.3  */
            .define_method("clear",           clear,                MRB_ARGS_NONE()) /* 15.2.13.4.4  */
            .define_method("default",         default_val,          MRB_ARGS_OPT(1))  /* 15.2.13.4.5  */
            .define_method("default=",        set_default,          MRB_ARGS_REQ(1)) /* 15.2.13.4.6  */
            .define_method("default_proc",    default_proc,         MRB_ARGS_NONE()) /* 15.2.13.4.7  */
            .define_method("default_proc=",   set_default_proc,     MRB_ARGS_REQ(1)) /* 15.2.13.4.7  */
            .define_method("__delete",        delete_key,           MRB_ARGS_REQ(1)) /* core of 15.2.13.4.8  */
            .define_method("empty?",          empty,                MRB_ARGS_NONE()) /* 15.2.13.4.12 */
            .define_method("has_key?",        hasKey,               MRB_ARGS_REQ(1)) /* 15.2.13.4.13 */
            .define_method("has_value?",      hasValue,             MRB_ARGS_REQ(1)) /* 15.2.13.4.14 */
            .define_method("include?",        hasKey,               MRB_ARGS_REQ(1)) /* 15.2.13.4.15 */
            .define_method("__init_core",     mrb_hash_init_core,   MRB_ARGS_ANY())  /* core of 15.2.13.4.16 */
            .define_method("initialize_copy", replace,              MRB_ARGS_REQ(1)) /* 15.2.13.4.17 */
            .define_method("key?",            hasKey,               MRB_ARGS_REQ(1)) /* 15.2.13.4.18 */
            .define_method("keys",            keys,                 MRB_ARGS_NONE()) /* 15.2.13.4.19 */
            .define_method("length",          size,                 MRB_ARGS_NONE()) /* 15.2.13.4.20 */
            .define_method("member?",         hasKey,               MRB_ARGS_REQ(1)) /* 15.2.13.4.21 */
            .define_method("replace",         replace,              MRB_ARGS_REQ(1)) /* 15.2.13.4.23 */
            .define_method("shift",           shift,                MRB_ARGS_NONE()) /* 15.2.13.4.24 */
            .define_method("dup",             dup,                  MRB_ARGS_NONE())
            .define_method("size",            size,                 MRB_ARGS_NONE()) /* 15.2.13.4.25 */
            .define_method("store",           set,                  MRB_ARGS_REQ(2)) /* 15.2.13.4.26 */
            .define_method("value?",          hasValue,             MRB_ARGS_REQ(1)) /* 15.2.13.4.27 */
            .define_method("values",          values,               MRB_ARGS_NONE()) /* 15.2.13.4.28 */
            .define_method("to_hash",         toHash,     MRB_ARGS_NONE()) /* 15.2.13.4.29 (x)*/
            .define_method("inspect",         mrb_hash_inspect,     MRB_ARGS_NONE()) /* 15.2.13.4.30 (x)*/
            .define_alias("to_s",            "inspect")                         /* 15.2.13.4.31 (x)*/
            .define_method("eql?",            hash_eql,         MRB_ARGS_REQ(1)) /* 15.2.13.4.32 (x)*/
            ;
}


khint_t ValueHashEq::operator()(MemManager *m, mrb_value a, mrb_value b) const {
    return mrb_eql(m->vm(), a, b);
}
