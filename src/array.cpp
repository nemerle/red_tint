/*
** array.c - Array class
**
** See Copyright Notice in mruby.h
*/
#include <vector>
#include <limits.h>
#include "mruby.h"
#include "mruby/array.h"
#include "mruby/class.h"
#include "mruby/string.h"
#include "mruby/range.h"
#include "value_array.h"
#include "allocator.h"

#define ARY_DEFAULT_LEN   4
#define ARY_SHRINK_RATIO  5 /* must be larger than 2 */
#define ARY_C_MAX_SIZE (SIZE_MAX / sizeof(mrb_value))
#define ARY_MAX_SIZE ((ARY_C_MAX_SIZE < (size_t)MRB_INT_MAX) ? (mrb_int)ARY_C_MAX_SIZE : MRB_INT_MAX-1)
#define ARY_SHARED_P(a) ((a)->flags & MRB_ARY_SHARED)
#define ARY_SET_SHARED_FLAG(a) ((a)->flags |= MRB_ARY_SHARED)
#define ARY_UNSET_SHARED_FLAG(a) ((a)->flags &= ~MRB_ARY_SHARED)
#define ARY_SHIFT_SHARED_MIN 10
size_t gAllocatedSize=0;
RArray* RArray::ary_new_capa(mrb_state *mrb, size_t capa)
{
    Allocator<mrb_value> allocz(&mrb->gc());
    std::vector<mrb_value,Allocator<mrb_value> > vec(allocz);
    std::allocator_traits<Allocator<mrb_value> >::rebind_alloc<RArray> az(allocz);

    size_t blen = capa * sizeof(mrb_value) ;

    // check for size_t wrap-around blen < capa -> wrap around occured.
    if ( (capa > ARY_MAX_SIZE) || (blen < capa) ) {
        mrb_raise(E_ARGUMENT_ERROR, "array size too big");
    }

    RArray *a = mrb->gc().obj_alloc<RArray>(mrb->array_class);
    a->m_ptr = (mrb_value *)mrb->gc()._malloc(blen);
    a->m_aux.capa = capa;
    a->m_len = 0;

    return a;
}

/*
 * to copy array, use this instead of memcpy because of portability
 * * gcc on ARM may fail optimization of memcpy
 *   http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.faqs/ka3934.html
 * * gcc on MIPS also fail
 *   http://gcc.gnu.org/bugzilla/show_bug.cgi?id=39755
 * * memcpy doesn't exist on freestanding environment
 *
 * If you optimize for binary size, use memcpy instead of this at your own risk
 * of above portability issue.
 *
 * see also http://togetter.com/li/462898
 *
 */
static inline void array_copy(mrb_value *dst, const mrb_value *src, size_t size)
{
    assert(dst>(src+size) || (dst+size)<= src); // cannot overlap, otherwise we'd need to do memmove here
    memcpy(dst,src,size*sizeof(mrb_value));
}

RArray *mrb_assoc_new(mrb_state *mrb, const mrb_value &car, const mrb_value &cdr)
{
    mrb_value arv[2] = {car,cdr};
    return RArray::new_from_values(mrb, 2, arv);
}

static void ary_fill_with_nil(mrb_value *ptr, mrb_int size)
{
    mrb_value nil = mrb_value::nil();

    while((int)(size--)) {
        *ptr++ = nil;
    }
}

void RArray::ary_modify()
{
    if (!ARY_SHARED_P(this))
        return;
    mrb_shared_array *shared = m_aux.shared;

    if (shared->refcnt == 1 && m_ptr == shared->ptr) {
        // single reference, we can use already allocated buffer
        m_ptr = shared->ptr;
        m_aux.capa = m_len; // overwrites m_aux.shared
        m_vm->gc()._free(shared);
    }
    else {
        // multiple references, we have to create a copy
        mrb_value * _ptr = (mrb_value *)m_vm->gc()._malloc(m_len * sizeof(mrb_value));
        if (m_ptr) {
            array_copy(_ptr, m_ptr, m_len);
        }
        m_ptr = _ptr;
        m_aux.capa = m_len;
        mrb_ary_decref(m_vm, shared);
    }
    ARY_UNSET_SHARED_FLAG(this); // the array contents are no longer shared
}
void RArray::mrb_ary_modify()
{
    m_vm->gc().mrb_write_barrier(this);
    ary_modify();
}

void RArray::ary_make_shared()
{
    if (ARY_SHARED_P(this))
        return;
    mrb_shared_array *shared = (mrb_shared_array *)m_vm->gc()._malloc(sizeof(mrb_shared_array));

    shared->refcnt = 1;
    if (m_aux.capa > m_len) {
        m_ptr = (mrb_value *)m_vm->gc()._realloc(m_ptr, sizeof(mrb_value)*m_len+1);
    }
    shared->ptr = m_ptr;
    shared->len = m_len;
    m_aux.shared = shared;
    ARY_SET_SHARED_FLAG(this);
}

void RArray::ary_expand_capa(mrb_state *mrb, size_t _len)
{
    assert(!ARY_SHARED_P(this)); // will not work if in shared mode
    mrb_int capa = m_aux.capa;

    if (_len > ARY_MAX_SIZE) {
        mrb_raise(E_ARGUMENT_ERROR, "array size too big");
    }

    if (capa == 0) {
        capa = ARY_DEFAULT_LEN;
    }
    while(capa < _len) {
        capa *= 2;
    }

    if (capa > ARY_MAX_SIZE)
        capa = ARY_MAX_SIZE; /* len <= capa <= ARY_MAX_SIZE */

    if (capa > m_aux.capa) {
        mrb_value *expanded_ptr = (mrb_value *)mrb->gc()._realloc(m_ptr, sizeof(mrb_value)*capa);

        if(!expanded_ptr) {
            mrb_raise(E_RUNTIME_ERROR, "out of memory");
        }

        m_aux.capa = capa;
        m_ptr = expanded_ptr;
    }
}

void RArray::ary_shrink_capa()
{
    assert(!ARY_SHARED_P(this)); // will not work if in shared mode
    mrb_int capa = m_aux.capa;

    if (capa < ARY_DEFAULT_LEN * 2)
        return;
    if (capa <= m_len * ARY_SHRINK_RATIO)
        return;
    do {
        capa /= 2;
        if (capa < ARY_DEFAULT_LEN) {
            capa = ARY_DEFAULT_LEN;
            break;
        }
    } while(capa > m_len * ARY_SHRINK_RATIO);

    if (capa > m_len && capa < m_aux.capa) {
        m_aux.capa = capa;
        m_ptr = (mrb_value *)m_vm->gc()._realloc(m_ptr, sizeof(mrb_value)*capa);
    }
}

RArray * RArray::s_create(mrb_state *mrb, mrb_value )
{
    mrb_value *vals;
    int len;

    mrb_get_args(mrb, "*", &vals, &len);
    return RArray::new_from_values(mrb, len, vals);
}

void RArray::ary_concat(const mrb_value *_ptr, mrb_int blen)
{
    mrb_int _len = m_len + blen;

    ary_modify();
    if (m_aux.capa < _len)
        ary_expand_capa(m_vm, _len);
    array_copy(m_ptr+m_len, _ptr, blen);
    m_vm->gc().mrb_write_barrier(this);
    m_len = _len;
}

void RArray::concat(const RArray *a2)
{
    this->ary_concat(a2->m_ptr, a2->m_len);
}

void RArray::concat_m()
{
    mrb_value *ptr;
    mrb_int blen;
    mrb_get_args(m_vm, "a", &ptr, &blen);
    this->ary_concat(ptr, blen);
}

RArray *RArray::plus() const
{
    mrb_value *ptr;
    mrb_int blen;

    mrb_get_args(m_vm, "a", &ptr, &blen);
    RArray *a2 = RArray::create(m_vm, m_len + blen);
    assert(a2->m_ptr && m_ptr && ptr);
    array_copy(a2->m_ptr, m_ptr, m_len);
    array_copy(a2->m_ptr + m_len, ptr, blen);
    a2->m_len = m_len + blen;
    return a2;
}

/*
 *  call-seq:
 *     ary <=> other_ary   ->  -1, 0, +1 or nil
 *
 *  Comparison---Returns an integer (-1, 0, or +1)
 *  if this array is less than, equal to, or greater than <i>other_ary</i>.
 *  Each object in each array is compared (using <=>). If any value isn't
 *  equal, then that inequality is the return value. If all the
 *  values found are equal, then the return is based on a
 *  comparison of the array lengths.  Thus, two arrays are
 *  ``equal'' according to <code>Array#<=></code> if and only if they have
 *  the same length and the value of each element is equal to the
 *  value of the corresponding element in the other array.
 *
 *     [ "a", "a", "c" ]    <=> [ "a", "b", "c" ]   #=> -1
 *     [ 1, 2, 3, 4, 5, 6 ] <=> [ 1, 2 ]            #=> +1
 *
 */
mrb_value RArray::cmp() const
{
    mrb_value ary2;
    mrb_value r;

    ary2 = m_vm->get_arg<mrb_value>();
    if (!ary2.is_array())
        return mrb_value::nil();
    RArray *a2 = RARRAY(ary2);
    if (m_len == a2->m_len && m_ptr == a2->m_ptr)
        return mrb_fixnum_value(0);
    else {
        mrb_sym cmp_sym = mrb_intern(m_vm, "<=>", 3);

        mrb_int _len = std::min(m_len,a2->m_len);
        assert(a2->m_ptr);
        assert(m_ptr);
        for (mrb_int i=0; i<_len; i++) {
            mrb_value v = a2->m_ptr[i];
            r = mrb_funcall_argv(m_vm, m_ptr[i], cmp_sym, 1, &v);
            if (mrb_type(r) != MRB_TT_FIXNUM || mrb_fixnum(r) != 0)
                return r;
        }
    }
    mrb_int _len = m_len - a2->m_len;
    return mrb_fixnum_value((_len == 0)? 0: (_len > 0)? 1: -1);
}

void RArray::ary_replace(const mrb_value *argv, mrb_int len)
{
    ary_modify();
    if (m_aux.capa < len)
        ary_expand_capa(m_vm, len);
    array_copy(m_ptr, argv, len);
    m_vm->gc().mrb_write_barrier(this);
    m_len = len;
}

void RArray::replace(const mrb_value &other)
{
    RArray *a2 = mrb_ary_ptr(other);

    this->ary_replace(a2->m_ptr, a2->m_len);
}

void RArray::replace_m()
{
    mrb_value other;

    mrb_get_args(m_vm, "A", &other);
    replace(other);
}

RArray* RArray::times()
{
    mrb_int _times = m_vm->get_arg<int>();
    if (_times < 0) {
        m_vm->mrb_raise(A_ARGUMENT_ERROR(m_vm), "negative argument");
    }
    RArray *a2 = RArray::create(m_vm,m_len * _times);
    mrb_value *ptr = a2->m_ptr;
    while(_times--) {
        array_copy(ptr, m_ptr, m_len);
        ptr += m_len;
        a2->m_len += m_len;
    }
    return a2;
}

void RArray::reverse_bang()
{
    if (m_len <= 1)
        return;

    ary_modify();
    mrb_value *p1 = m_ptr;
    mrb_value *p2 = m_ptr + m_len - 1;

    while(p1 < p2) {
        mrb_value tmp = *p1;
        *p1++ = *p2;
        *p2-- = tmp;
    }
}

RArray * RArray::reverse()
{
    RArray *b = RArray::ary_new_capa(m_vm, m_len);
    if (m_len <= 0)
        return b;
    assert(m_ptr);
    mrb_value * p1  = m_ptr;
    mrb_value * e   = p1 + m_len;
    mrb_value * p2  = b->m_ptr + m_len - 1;
    while(p1 < e) {
        *p2-- = *p1++;
    }
    b->m_len = m_len;
    return b;
}

RArray * RArray::new_from_values(mrb_state *mrb, mrb_int _size, const mrb_value *vals)
{
    RArray *a = RArray::ary_new_capa(mrb, _size);
    array_copy(a->m_ptr, vals, _size);
    a->m_len = _size;

    return a;
}
RArray * RArray::new_from_values(mrb_state *mrb, const std::vector<mrb_value> &values)
{
    size_t _size = values.size();
    RArray *a = RArray::ary_new_capa(mrb, _size);
    array_copy(a->m_ptr, &values[0], _size);
    a->m_len = _size;

    return a;
}

void RArray::release() {
    if (flags & MRB_ARY_SHARED)
        mrb_ary_decref(m_vm, m_aux.shared);
    else
        m_vm->gc()._free(m_ptr);
}

void RArray::push(const mrb_value &elem) /* mrb_ary_push */
{
    ary_modify();
    if (m_len == m_aux.capa)
        ary_expand_capa(m_vm, m_len + 1);
    m_ptr[m_len++] = elem;
    m_vm->gc().mrb_write_barrier(this);
}

void RArray::push_m()
{
    mrb_value *argv;
    int len;

    mrb_get_args(m_vm, "*", &argv, &len);
    while(len--) {
        push(*argv++);
    }

}

mrb_value RArray::pop()
{
    if (m_len == 0)
        return mrb_value::nil();
    assert(m_ptr!=0);
    return m_ptr[--m_len];
}


mrb_value RArray::shift()
{

    if (m_len == 0)
        return mrb_value::nil();
    assert(m_ptr);
    if (this->flags & MRB_ARY_SHARED) {
        mrb_value val = m_ptr[0];
        m_ptr++;
        m_len--;
        return val;
    }
    if (m_len > ARY_SHIFT_SHARED_MIN) {
        this->ary_make_shared();
        mrb_value val = m_ptr[0];
        m_ptr++;
        m_len--;
        return val;
    }
    mrb_value val = *m_ptr;
    --m_len;
    assert(m_len<m_aux.capa);
    memmove(m_ptr,m_ptr+1,sizeof(mrb_value)*m_len);
    return val;
}

/* self = [1,2,3]
   item = 0
   self.unshift item
   p self #=> [0, 1, 2, 3] */
void RArray::unshift(const mrb_value &item)
{
    if (ARY_SHARED_P(this)
            && m_aux.shared->refcnt == 1 /* shared only referenced from this array */
            && m_ptr - base_ptr() >= 1) /* there's room for unshifted item */ {
        m_ptr--;
    }
    else {
        this->ary_modify();
        if (m_aux.capa < m_len + 1)
            this->ary_expand_capa(m_vm, m_len + 1);
        value_move(m_ptr + 1, m_ptr, m_len);
    }
    m_ptr[0] = item;
    m_len+= 1;
    m_vm->gc().mrb_write_barrier(this);
}

void RArray::unshift_m()
{
    mrb_value *vals;
    int len;

    mrb_get_args(m_vm, "*", &vals, &len);
    if (ARY_SHARED_P(this)
            && m_aux.shared->refcnt == 1 /* shared only referenced from this array */
            && m_ptr - base_ptr() >= len) /* there's room for unshifted item */ {
        m_ptr -= len;
    }
    else {
        this->ary_modify();
        if (len == 0)
            return;
        if (m_aux.capa < m_len + len)
            this->ary_expand_capa(m_vm, m_len + len);
        value_move(m_ptr + len, m_ptr, m_len);
    }
    array_copy(m_ptr, vals, len);
    m_len += len;
    m_vm->gc().mrb_write_barrier(this);
}

mrb_value RArray::ref(mrb_int n) const
{
    /* range check */
    if (n < 0)
        n += m_len;
    if (n < 0 || m_len <= (int)n)
        return mrb_value::nil();
    return m_ptr[n];
}

void RArray::set(mrb_int n, const mrb_value &val) /* rb_ary_store */
{
    ary_modify();
    /* range check */
    if (n < 0) {
        n += m_len;
        if (n < 0) {
            m_vm->mrb_raisef(A_INDEX_ERROR(m_vm), "index %S out of array", mrb_fixnum_value(n - m_len));
        }
    }
    if (m_len <= (int)n) {
        if (m_aux.capa <= (int)n)
            this->ary_expand_capa(m_vm, n + 1);
        ary_fill_with_nil(m_ptr + m_len, n + 1 - m_len);
        m_len = n + 1;
    }

    m_ptr[n] = val;
    m_vm->gc().mrb_write_barrier(this);


}

void RArray::splice(mrb_int head, mrb_int len, const mrb_value &rpl)
{
    const mrb_value *argv= &rpl;
    mrb_int argc=1;

    this->ary_modify();
    /* range check */
    if (head < 0) {
        head += m_len;
        if (head < 0) {
            m_vm->mrb_raise(A_INDEX_ERROR(m_vm), "index is out of array");
        }
    }
    if (m_len < len || m_len < head + len) {
        len = m_len - head;
    }
    mrb_int tail = head + len;

    /* size check */
    if (rpl.is_array()) {
        argc = RARRAY_LEN(rpl);
        argv = RARRAY_PTR(rpl);
    }
    mrb_int _size = head + argc;

    if (tail < m_len)
        _size += m_len - tail;
    if (_size > m_aux.capa)
        this->ary_expand_capa(m_vm, _size);

    if (head > m_len) {
        ary_fill_with_nil(m_ptr + m_len, (int)(head - m_len));
    }
    else if (head < m_len) {
        value_move(m_ptr + head + argc, m_ptr + tail, m_len - tail);
    }

    for(mrb_int i = 0; i < argc; i++) {
        m_ptr[head + i] = argv[i];
    }

    m_len = _size;
}


void mrb_ary_decref(mrb_state *mrb, mrb_shared_array *shared)
{
    shared->refcnt--;
    if (shared->refcnt == 0) {
        mrb->gc()._free(shared->ptr);
        mrb->gc()._free(shared);
    }
}

RArray *RArray::ary_subseq(mrb_int beg, mrb_int len)
{

    ary_make_shared();
    RArray *b = m_vm->gc().obj_alloc<RArray>(m_vm->array_class);
    b->m_ptr = m_ptr + beg;
    b->m_len = len;
    b->m_aux.shared = m_aux.shared;
    b->m_aux.shared->refcnt++;
    ARY_SET_SHARED_FLAG(b);

    return b;
}
mrb_int RArray::aget_index(mrb_value index)
{
    if (index.is_fixnum()) {
        return mrb_fixnum(index);
    }
    else {
        mrb_int i;
        mrb_get_args(m_vm, "i", &i);
        return i;
    }
}

/*
 *  call-seq:
 *     ary[index]                -> obj     or nil
 *     ary[start, length]        -> new_ary or nil
 *     ary[range]                -> new_ary or nil
 *     ary.slice(index)          -> obj     or nil
 *     ary.slice(start, length)  -> new_ary or nil
 *     ary.slice(range)          -> new_ary or nil
 *
 *  Element Reference --- Returns the element at +index+, or returns a
 *  subarray starting at the +start+ index and continuing for +length+
 *  elements, or returns a subarray specified by +range+ of indices.
 *
 *  Negative indices count backward from the end of the array (-1 is the last
 *  element).  For +start+ and +range+ cases the starting index is just before
 *  an element.  Additionally, an empty array is returned when the starting
 *  index for an element range is at the end of the array.
 *
 *  Returns +nil+ if the index (or starting index) are out of range.
 *
 *  a = [ "a", "b", "c", "d", "e" ]
 *  a[1]     => "b"
 *  a[1,2]   => ["b", "c"]
 *  a[1..-2] => ["b", "c", "d"]
 *
 */

mrb_value RArray::get()
{
    mrb_int i, len;
    mrb_value index;

    if (mrb_get_args(m_vm, "o|i", &index, &len) == 1) {
        switch (mrb_type(index)) {
            case MRB_TT_RANGE:
                len = this->m_len;
                if (mrb_range_beg_len(m_vm, index, &i, &len, len)) {
                    return ary_subseq(i, len)->wrap();
                }
                else {
                    return mrb_value::nil();
                }
            case MRB_TT_FIXNUM:
                return ref(mrb_fixnum(index));
            default:
                return ref(aget_index(index));
        }
    }

    i = aget_index(index);
    if (i < 0)
        i += m_len;
    if (i < 0 || m_len < (int)i)
        return mrb_value::nil();
    if (len < 0)
        return mrb_value::nil();
    if (m_len == (int)i)
        return RArray::create(m_vm)->wrap();
    if (len > m_len - i)
        len = m_len - i;

    return ary_subseq(i, len)->wrap();
}
/*
 *  call-seq:
 *     ary[index]         = obj                      ->  obj
 *     ary[start, length] = obj or other_ary or nil  ->  obj or other_ary or nil
 *     ary[range]         = obj or other_ary or nil  ->  obj or other_ary or nil
 *
 *  Element Assignment --- Sets the element at +index+, or replaces a subarray
 *  from the +start+ index for +length+ elements, or replaces a subarray
 *  specified by the +range+ of indices.
 *
 *  If indices are greater than the current capacity of the array, the array
 *  grows automatically.  Elements are inserted into the array at +start+ if
 *  +length+ is zero.
 *
 *  Negative indices will count backward from the end of the array.  For
 *  +start+ and +range+ cases the starting index is just before an element.
 *
 *  An IndexError is raised if a negative index points past the beginning of
 *  the array.
 *
 *  See also Array#push, and Array#unshift.
 *
 *     a = Array.new
 *     a[4] = "4";                 #=> [nil, nil, nil, nil, "4"]
 *     a[0, 3] = [ 'a', 'b', 'c' ] #=> ["a", "b", "c", nil, "4"]
 *     a[1..2] = [ 1, 2 ]          #=> ["a", 1, 2, nil, "4"]
 *     a[0, 2] = "?"               #=> ["?", 2, nil, "4"]
 *     a[0..2] = "A"               #=> ["A", "4"]
 *     a[-1]   = "Z"               #=> ["A", "Z"]
 *     a[1..-1] = nil              #=> ["A", nil]
 *     a[1..-1] = []               #=> ["A"]
 *     a[0, 0] = [ 1, 2 ]          #=> [1, 2, "A"]
 *     a[3, 0] = "B"               #=> [1, 2, "A", "B"]
 */
mrb_value RArray::aset()
{
    mrb_value v1, v2, v3;
    mrb_int i, len;

    if (mrb_get_args(m_vm, "oo|o", &v1, &v2, &v3) == 2) {
        switch (mrb_type(v1)) {
            /* a[n..m] = v */
            case MRB_TT_RANGE:
                if (mrb_range_beg_len(m_vm, v1, &i, &len, m_len)) {
                    splice(i, len, v2);
                }
                break;
                /* a[n] = v */
            case MRB_TT_FIXNUM:
                set(mrb_fixnum(v1), v2);
                break;
            default:
                set(aget_index(v1), v2);
                break;
        }
        return v2;
    }

    /* a[n,m] = v */
    splice(aget_index(v1), aget_index(v2), v3);
    return v3;
}

mrb_value RArray::delete_at()
{
    mrb_int index = m_vm->get_arg<mrb_int>();
    if (index < 0)
        index += m_len;
    if (index < 0 || m_len <= (int)index)
        return mrb_value::nil();

    this->ary_modify();

    mrb_value val = m_ptr[index];
    mrb_value *ptr = m_ptr + index;
    mrb_int len = m_len - index;

    while((int)(--len)) {
        *ptr = *(ptr+1);
        ++ptr;
    }
    --m_len;

    ary_shrink_capa();

    return val;
}

mrb_value RArray::first()
{
    mrb_int _size;

    if (mrb_get_args(m_vm, "|i", &_size) == 0) {
        return (m_len > 0)? m_ptr[0]: mrb_value::nil();
        }
        if (_size < 0) {
        m_vm->mrb_raise(A_ARGUMENT_ERROR(m_vm), "negative array size");
    }

    if (_size > m_len)
        _size = m_len;
    if (ARY_SHARED_P(this)) {
        return ary_subseq(0, _size)->wrap();
    }
    return mrb_value::wrap(RArray::new_from_values(m_vm, _size, m_ptr));
}
mrb_value RArray::last()
{
    mrb_int _size;
    mrb_value *vals;
    int _len;

    mrb_get_args(m_vm, "*", &vals, &_len);
    if (_len > 1) {
        m_vm->mrb_raise(A_ARGUMENT_ERROR(m_vm), "wrong number of arguments");
    }

    if (_len == 0)
        return (m_len > 0)? m_ptr[m_len - 1]: mrb_value::nil();

            /* len == 1 */
            _size = mrb_fixnum(*vals);
            if (_size < 0) {
                m_vm->mrb_raise(A_ARGUMENT_ERROR(m_vm), "negative array size");
            }
            if (_size > m_len)
                _size = m_len;
            if (ARY_SHARED_P(this) || (_size > ARY_DEFAULT_LEN)) {
                return ary_subseq(m_len - _size, _size)->wrap();
            }
            assert((_size>=0) && _size <= ARY_DEFAULT_LEN);
            return mrb_value::wrap(RArray::new_from_values(m_vm, _size, m_ptr + (m_len - _size)));
        }

        mrb_value RArray::index_m()
        {
        mrb_value obj(m_vm->get_arg<mrb_value>());
    for (mrb_int i = 0; i < m_len; i++) {
        if (mrb_equal(m_vm, m_ptr[i], obj)) {
            return mrb_fixnum_value(i);
        }
    }
    return mrb_value::nil();
}

mrb_value RArray::rindex_m()
{
    mrb_value obj(m_vm->get_arg<mrb_value>());
    for (mrb_int i = m_len - 1; i >= 0; i--) {
        if (mrb_equal(m_vm, m_ptr[i], obj)) {
            return mrb_fixnum_value(i);
        }
    }
    return mrb_value::nil();
}

RArray *RArray::splat(mrb_state *mrb, const mrb_value &v)
{
    if (v.is_array()) {
        return v.ptr<RArray>();
    }
    if (v.respond_to(mrb, mrb_intern_lit(mrb, "to_a"))) {
        return mrb->funcall(v, "to_a", 0).ptr<RArray>();
    }
    RArray *a = RArray::ary_new_capa(mrb, 1);
    a->m_ptr[a->m_len++] = v;
    return a;
    //    return RArray::new_from_values(mrb, 1, &v);
}

mrb_int RArray::size()
{
    return m_len;
}

void RArray::clear()
{
    ary_modify();
    m_len = 0;
    m_aux.capa = 0;
    m_vm->gc()._free(m_ptr);
    m_ptr = 0;
}

bool RArray::empty_p() const
{
    return m_len == 0;
}

mrb_value mrb_check_array_type(mrb_state *mrb, const mrb_value &ary)
{
    return mrb_check_convert_type(mrb, ary, MRB_TT_ARRAY, "Array", "to_ary");
}

mrb_value RArray::entry(mrb_int offset)
{
    if (offset < 0) {
        offset += m_len;
    }
    return ary_elt(offset);
}

static constexpr bool simpleArrComp(const RArray *a, const mrb_value &b) {
    return (a == mrb_cptr(b));
}
RString * RArray::inspect_ary(RArray *list_arr)
{
    mrb_int i;
    RString * s;
    static constexpr char head[] = { '[' };
    static constexpr char sep[] = { ',', ' ' };
    static constexpr char tail[] = { ']' };
    /* check recursive */
    for(i=0; i<list_arr->m_len; i++) {
        if (simpleArrComp(this, list_arr->m_ptr[i])) {
            return mrb_str_new_lit(m_vm, "[...]");
        }
    }
    mrb_value t = {{(void *)this},MRB_TT_ARRAY};

    list_arr->push(t);
    RString *strr = RString::create(m_vm,64);

    //mrb_str_buf_cat(m_vm, arystr, head, sizeof(head));
    strr->str_cat(head,sizeof(head));
    for(i=0; i<m_len; i++) {
        assert(m_ptr!=0);
        int ai = m_vm->gc().arena_save();

        if (i > 0) {
            strr->str_cat(sep, sizeof(sep));
            //mrb_str_buf_cat(m_vm, arystr, sep, sizeof(sep));
        }
        if (m_ptr[i].is_array()) {
            s = RARRAY(m_ptr[i])->inspect_ary(list_arr);
        } else {
            s = mrb_inspect(m_vm, m_ptr[i]);
        }
        strr->str_cat(s->m_ptr, s->len);
        m_vm->gc().arena_restore(ai);
    }
    strr->str_buf_cat(tail,sizeof(tail));
    list_arr->pop();

    return strr;
}

/* 15.2.12.5.31 (x) */
/*
 *  call-seq:
 *     ary.to_s -> string
 *     ary.inspect  -> string
 *
 *  Creates a string representation of +self+.
 */

RString *RArray::inspect()
{
    if (m_len == 0)
        return RString::create(m_vm,"[]",2);
    //temporary array -> TODO: change this to stack allocated object
    RArray *tmp=RArray::create(m_vm,0);
    return RArray::inspect_ary(tmp);
}
RString *RArray::join_ary(const mrb_value &sep, RArray *list_arr)
{
    if(sep.is_nil()) {
        return join_ary(nullptr,list_arr);
    }
    assert(sep.is_string());
    return join_ary(sep.ptr<RString>(),list_arr);
}
RString *RArray::join_ary(const RString *sep, RArray *list_arr)
{
    mrb_value tmp;
    /* check recursive */
    for(mrb_int i=0; i<list_arr->m_len; i++) {
        if (simpleArrComp(this, list_arr->m_ptr[i])) {
            m_vm->mrb_raise(A_ARGUMENT_ERROR(m_vm), "recursive array join");
        }
    }
    mrb_value t = {{(void *)this},MRB_TT_ARRAY};
    list_arr->push(t);
    RString *str  = RString::create(m_vm,64);

    for(mrb_int i=0; i<m_len; i++) {
        if(i>0 && sep)
            str->str_buf_cat(sep->m_ptr, sep->len);
        mrb_value val = m_ptr[i];
        RString *p_str = val.ptr<RString>();
        switch(mrb_type(val)) {
            case MRB_TT_ARRAY:
                p_str = RARRAY(val)->join_ary(sep, list_arr);
                /* fall through */

            case MRB_TT_STRING:
                str->str_buf_cat(p_str->m_ptr, p_str->len);
                break;

            default:
                tmp = mrb_check_string_type(m_vm, val);
                if (!tmp.is_nil()) {
                    val = tmp;
                    str->str_buf_cat(RSTRING_PTR(tmp), RSTRING_LEN(tmp));
                    break;
                }
                tmp = mrb_check_convert_type(m_vm, val, MRB_TT_ARRAY, "Array", "to_ary");
                if (!tmp.is_nil()) {
                    RString *p_str = RARRAY(tmp)->join_ary(sep, list_arr);
                    str->str_buf_cat(p_str->m_ptr, p_str->len);
                    //str->str_buf_cat(RSTRING_PTR(val), RSTRING_LEN(val));
                    break;
                }
                str->str_cat(mrb_obj_as_string(m_vm, val));
                break;
        }
    }

    list_arr->pop();
    return str;
}
RString *RArray::join(mrb_value sep)
{
    RString *_sep = mrb_obj_as_string(m_vm, sep);
    RArray *arr = RArray::ary_new_capa(m_vm, 0);
    return join_ary(_sep, arr);
}

/*
 *  call-seq:
 *     ary.join(sep="")    -> str
 *
 *  Returns a string created by converting each element of the array to
 *  a string, separated by <i>sep</i>.
 *
 *     [ "a", "b", "c" ].join        #=> "abc"
 *     [ "a", "b", "c" ].join("-")   #=> "a-b-c"
 */

RString *RArray::join_m()
{
    mrb_value sep = mrb_value::nil();

    mrb_get_args(m_vm, "|S", &sep);
    return RArray::join(sep);
}

/* 15.2.12.5.33 (x) */
/*
 *  call-seq:
 *     ary == other_ary   ->   bool
 *
 *  Equality---Two arrays are equal if they contain the same number
 *  of elements and if each element is equal to (according to
 *  Object.==) the corresponding element in the other array.
 *
 *     [ "a", "c" ]    == [ "a", "c", 7 ]     #=> false
 *     [ "a", "c", 7 ] == [ "a", "c", 7 ]     #=> true
 *     [ "a", "c", 7 ] == [ "a", "d", "f" ]   #=> false
 *
 */

bool RArray::mrb_ary_equal()
{
    mrb_value ary2(m_vm->get_arg<mrb_value>());

    if ( this == ary2.basic_ptr()) {
        return true;
    }
    if (ary2.is_special_const()) {
        return false;
    }
    if (!ary2.is_array()) {
        if (ary2.respond_to(m_vm, mrb_intern(m_vm, "to_ary", 6))) {
            return mrb_equal(m_vm, ary2, mrb_value::wrap(this));
        }
        return false;
    }
    if (m_len != RARRAY_LEN(ary2))
        return false;
    RArray *other = RARRAY(ary2);
    for (mrb_int i=0; i<m_len; i++) {
        if (!mrb_equal(m_vm, m_ptr[i], other->m_ptr[i])) {
            return false;
        }
    }
    return true;
}

/* 15.2.12.5.34 (x) */
/*
 *  call-seq:
 *     ary.eql?(other)  -> true or false
 *
 *  Returns <code>true</code> if +self+ and _other_ are the same object,
 *  or are both arrays with the same content.
 */

bool RArray::mrb_ary_eql()
{
    mrb_value ary2(m_vm->get_arg<mrb_value>());

    if ( this == ary2.basic_ptr()) {  //was mrb_obj_equal(m_vm, ary1, ary2)
        return true;
    }
    if (!ary2.is_array()) {
        return false;
    }
    RArray *ary_2p = RARRAY(ary2);
    if (m_len != ary_2p->m_len)
        return false;

    for (mrb_int i=m_len-1; i>=0; --i) {
        if (!mrb_eql(m_vm, m_ptr[i], ary_2p->m_ptr[i])) {
            return false;
        }
    }

    return true;
}
namespace {
#define FORWARD_TO_INSTANCE(name)\
    static mrb_value name(mrb_state *mrb, mrb_value self) {\
    return mrb_value::wrap(mrb_ary_ptr(self)->name());\
}
#define FORWARD_TO_STATIC(CLS,name)\
    static mrb_value name(mrb_state *mrb, mrb_value self) {\
    return mrb_value::wrap(CLS::name(mrb,self));\
}
#define FORWARD_TO_INSTANCE_RET_SELF(name)\
    static mrb_value name(mrb_state *mrb, mrb_value self) {\
    mrb_ary_ptr(self)->name();\
    return self;\
}
FORWARD_TO_STATIC(RArray,s_create)
FORWARD_TO_INSTANCE(pop)
FORWARD_TO_INSTANCE(plus)
FORWARD_TO_INSTANCE(aset)
FORWARD_TO_INSTANCE(shift)
FORWARD_TO_INSTANCE(first)
FORWARD_TO_INSTANCE(last)
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
FORWARD_TO_INSTANCE(delete_at)
FORWARD_TO_INSTANCE(mrb_ary_equal)
FORWARD_TO_INSTANCE(mrb_ary_eql)
FORWARD_TO_INSTANCE_RET_SELF(replace_m)
FORWARD_TO_INSTANCE_RET_SELF(push_m)
FORWARD_TO_INSTANCE_RET_SELF(reverse_bang)
FORWARD_TO_INSTANCE_RET_SELF(clear)
FORWARD_TO_INSTANCE_RET_SELF(concat_m)
FORWARD_TO_INSTANCE_RET_SELF(unshift_m)

#undef FORWARD_TO_INSTANCE
#undef FORWARD_TO_INSTANCE_RET_SELF

}


void mrb_init_array(mrb_state *mrb)
{
    RClass *a = mrb->array_class = &mrb->define_class("Array", mrb->object_class);
    a->instance_tt(MRB_TT_ARRAY)
            .define_class_method("[]",        s_create,       MRB_ARGS_ANY())    /* 15.2.12.4.1 */
            .define_method("+",               plus,           MRB_ARGS_REQ(1)) /* 15.2.12.5.1  */
            .define_method("*",               times,          MRB_ARGS_REQ(1)) /* 15.2.12.5.2  */
            .define_method("<<",              push_m,         MRB_ARGS_REQ(1)) /* 15.2.12.5.3  */
            .define_method("[]",              get,            MRB_ARGS_ANY())  /* 15.2.12.5.4  */
            .define_method("[]=",             aset,           MRB_ARGS_ANY())  /* 15.2.12.5.5  */
            .define_method("clear",           clear,          MRB_ARGS_NONE()) /* 15.2.12.5.6  */
            .define_method("concat",          concat_m,       MRB_ARGS_REQ(1)) /* 15.2.12.5.8  */
            .define_method("delete_at",       delete_at,      MRB_ARGS_REQ(1)) /* 15.2.12.5.9  */
            .define_method("empty?",          empty_p,        MRB_ARGS_NONE()) /* 15.2.12.5.12 */
            .define_method("first",           first,          MRB_ARGS_OPT(1)) /* 15.2.12.5.13 */
            .define_method("index",           index_m,        MRB_ARGS_REQ(1)) /* 15.2.12.5.14 */
            .define_method("initialize_copy", replace_m,      MRB_ARGS_REQ(1)) /* 15.2.12.5.16 */
            .define_method("join",            join_m,         MRB_ARGS_ANY())  /* 15.2.12.5.17 */
            .define_method("last",            last,           MRB_ARGS_ANY())  /* 15.2.12.5.18 */
            .define_method("length",          size,           MRB_ARGS_NONE()) /* 15.2.12.5.19 */
            .define_method("pop",             pop,            MRB_ARGS_NONE()) /* 15.2.12.5.21 */
            .define_method("push",            push_m,         MRB_ARGS_ANY())  /* 15.2.12.5.22 */
            .define_method("replace",         replace_m,      MRB_ARGS_REQ(1)) /* 15.2.12.5.23 */
            .define_method("reverse",         reverse,        MRB_ARGS_NONE()) /* 15.2.12.5.24 */
            .define_method("reverse!",        reverse_bang,   MRB_ARGS_NONE()) /* 15.2.12.5.25 */
            .define_method("rindex",          rindex_m,       MRB_ARGS_REQ(1)) /* 15.2.12.5.26 */
            .define_method("shift",           shift,          MRB_ARGS_NONE()) /* 15.2.12.5.27 */
            .define_method("size",            size,           MRB_ARGS_NONE()) /* 15.2.12.5.28 */
            .define_method("slice",           get,            MRB_ARGS_ANY())  /* 15.2.12.5.29 */
            .define_method("unshift",         unshift_m,      MRB_ARGS_ANY())  /* 15.2.12.5.30 */
            .define_method("inspect",         inspect,        MRB_ARGS_NONE()) /* 15.2.12.5.31 (x) */
            .define_alias("to_s", "inspect")                                       /* 15.2.12.5.32 (x) */
            .define_method("==",              mrb_ary_equal,  MRB_ARGS_REQ(1)) /* 15.2.12.5.33 (x) */
            .define_method("eql?",            mrb_ary_eql,    MRB_ARGS_REQ(1)) /* 15.2.12.5.34 (x) */
            .define_method("<=>",             cmp,            MRB_ARGS_REQ(1)) /* 15.2.12.5.36 (x) */
            .fin();
}
