/*
** array.c - Array class
**
** See Copyright Notice in mruby.h
*/

#ifndef SIZE_MAX
/* Some versions of VC++
  * has SIZE_MAX in stdint.h
  */
#include <limits.h>
#endif
#include "mruby.h"
#include "mruby/array.h"
#include "mruby/class.h"
#include "mruby/string.h"
#include "value_array.h"

#define ARY_DEFAULT_LEN   4
#define ARY_SHRINK_RATIO  5 /* must be larger than 2 */
#define ARY_C_MAX_SIZE (SIZE_MAX / sizeof(mrb_value))
#define ARY_MAX_SIZE ((ARY_C_MAX_SIZE < (size_t)MRB_INT_MAX) ? (mrb_int)ARY_C_MAX_SIZE : MRB_INT_MAX)

RArray* RArray::ary_new_capa(mrb_state *mrb, size_t capa)
{
    size_t blen;

    if (capa > ARY_MAX_SIZE) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "array size too big");
    }
    blen = capa * sizeof(mrb_value) ;
    if (blen < capa) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "array size too big");
    }

    RArray *a = mrb->gc().obj_alloc<RArray>(mrb->array_class);
    a->m_ptr = (mrb_value *)mrb->gc()._malloc(blen);
    a->m_aux.capa = capa;
    a->m_len = 0;

    return a;
}

mrb_value RArray::new_capa(mrb_state *mrb, mrb_int capa)
{
    RArray *a = RArray::ary_new_capa(mrb, capa);
    return mrb_obj_value(a);
}

mrb_value mrb_ary_new(mrb_state *mrb)
{
    return RArray::new_capa(mrb, 0);
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
static inline void
array_copy(mrb_value *dst, const mrb_value *src, size_t size)
{
    size_t i;

    for (i = 0; i < size; i++) {
        dst[i] = src[i];
    }
}

mrb_value mrb_assoc_new(mrb_state *mrb, const mrb_value &car, const mrb_value &cdr)
{
    mrb_value arv[2] = {car,cdr};
    return RArray::new_from_values(mrb, 2, arv);
}

static void ary_fill_with_nil(mrb_value *ptr, mrb_int size)
{
    mrb_value nil = mrb_nil_value();

    while((int)(size--)) {
        *ptr++ = nil;
    }
}

void RArray::ary_modify(mrb_state *mrb)
{
    if (this->flags & MRB_ARY_SHARED) {
        mrb_shared_array *shared = m_aux.shared;

        if (shared->refcnt == 1 && m_ptr == shared->ptr) {
            m_ptr = shared->ptr;
            m_aux.capa = m_len;
            mrb->gc()._free(shared);
        }
        else {
            mrb_value *_ptr, *p;
            mrb_int len;

            p = m_ptr;
            len = m_len * sizeof(mrb_value);
            _ptr = (mrb_value *)mrb->gc()._malloc(len);
            if (p) {
                array_copy(_ptr, p, m_len);
            }
            m_ptr = _ptr;
            m_aux.capa = m_len;
            mrb_ary_decref(mrb, shared);
        }
        this->flags &= ~MRB_ARY_SHARED;
    }
}

void
RArray::ary_make_shared(mrb_state *mrb)
{
    if (!(this->flags & MRB_ARY_SHARED)) {
        mrb_shared_array *shared = (mrb_shared_array *)mrb->gc()._malloc(sizeof(mrb_shared_array));

        shared->refcnt = 1;
        if (m_aux.capa > m_len) {
            m_ptr = shared->ptr = (mrb_value *)mrb->gc()._realloc(m_ptr, sizeof(mrb_value)*m_len+1);
        }
        else {
            shared->ptr = m_ptr;
        }
        shared->len = m_len;
        m_aux.shared = shared;
        this->flags |= MRB_ARY_SHARED;
    }
}

void RArray::ary_expand_capa(mrb_state *mrb, size_t _len)
{
    mrb_int capa = m_aux.capa;

    if (_len > ARY_MAX_SIZE) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "array size too big");
    }

    while(capa < _len) {
        if (capa == 0) {
            capa = ARY_DEFAULT_LEN;
        }
        else {
            capa *= 2;
        }
    }

    if (capa > ARY_MAX_SIZE)
        capa = ARY_MAX_SIZE; /* len <= capa <= ARY_MAX_SIZE */

    if (capa > m_aux.capa) {
        mrb_value *expanded_ptr = (mrb_value *)mrb->gc()._realloc(m_ptr, sizeof(mrb_value)*capa);

        if(!expanded_ptr) {
            mrb_raise(mrb, E_RUNTIME_ERROR, "out of memory");
        }

        m_aux.capa = capa;
        m_ptr = expanded_ptr;
    }
}

void RArray::ary_shrink_capa(mrb_state *mrb)
{
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
        m_ptr = (mrb_value *)mrb->gc()._realloc(m_ptr, sizeof(mrb_value)*capa);
    }
}

mrb_value RArray::s_create(mrb_state *mrb, mrb_value )
{
    mrb_value *vals;
    int len;

    mrb_get_args(mrb, "*", &vals, &len);
    return RArray::new_from_values(mrb, len, vals);
}

void RArray::ary_concat(mrb_state *mrb, const mrb_value *_ptr, mrb_int blen)
{
    mrb_int _len = m_len + blen;

    ary_modify(mrb);
    if (m_aux.capa < _len)
        ary_expand_capa(mrb, _len);
    array_copy(m_ptr+m_len, _ptr, blen);
    mrb->gc().mrb_write_barrier(this);
    m_len = _len;
}

void RArray::concat(mrb_state *mrb, const mrb_value &self, const mrb_value &other)
{
    RArray *a2 = mrb_ary_ptr(other);

    mrb_ary_ptr(self)->ary_concat(mrb, a2->m_ptr, a2->m_len);
}

mrb_value RArray::concat_m(mrb_state *mrb, mrb_value self)
{
    mrb_value *ptr;
    mrb_int blen;
    mrb_get_args(mrb, "a", &ptr, &blen);
    mrb_ary_ptr(self)->ary_concat(mrb, ptr, blen);
    return self;
}

mrb_value RArray::plus(mrb_state *mrb, mrb_value self)
{
    RArray *a1 = mrb_ary_ptr(self);
    RArray *a2;
    mrb_value ary;
    mrb_value *ptr;
    mrb_int blen;

    mrb_get_args(mrb, "a", &ptr, &blen);
    ary = RArray::new_capa(mrb, a1->m_len + blen);
    a2 = mrb_ary_ptr(ary);
    array_copy(a2->m_ptr, a1->m_ptr, a1->m_len);
    array_copy(a2->m_ptr + a1->m_len, ptr, blen);
    a2->m_len = a1->m_len + blen;

    return ary;
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
mrb_value RArray::cmp(mrb_state *mrb) const
{
    mrb_value ary2;
    RArray *a2;
    mrb_value r = mrb_nil_value();
    mrb_int _len;

    mrb_get_args(mrb, "o", &ary2);
    if (!mrb_array_p(ary2))
        return r;
    a2 = RARRAY(ary2);
    if (m_len == a2->m_len && m_ptr == a2->m_ptr)
        return mrb_fixnum_value(0);
    else {
        mrb_sym cmp_sym = mrb_intern2(mrb, "<=>", 3);

        _len = m_len;
        if (_len > a2->m_len) {
            _len = a2->m_len;
        }
        assert(a2->m_ptr);
        assert(m_ptr);
        for (mrb_int i=0; i<_len; i++) {
            mrb_value v = a2->m_ptr[i];
            r = mrb_funcall_argv(mrb, m_ptr[i], cmp_sym, 1, &v);
            if (mrb_type(r) != MRB_TT_FIXNUM || mrb_fixnum(r) != 0)
                return r;
        }
    }
    _len = m_len - a2->m_len;
    return mrb_fixnum_value((_len == 0)? 0: (_len > 0)? 1: -1);
}

void RArray::ary_replace(mrb_state *mrb, const mrb_value *argv, mrb_int len)
{
    ary_modify(mrb);
    if (m_aux.capa < len)
        ary_expand_capa(mrb, len);
    array_copy(m_ptr, argv, len);
    mrb->gc().mrb_write_barrier(this);
    m_len = len;
}

void RArray::replace(mrb_state *mrb, const mrb_value &self, const mrb_value &other)
{
    RArray *a2 = mrb_ary_ptr(other);

    mrb_ary_ptr(self)->ary_replace(mrb, a2->m_ptr, a2->m_len);
}

mrb_value RArray::replace_m(mrb_state *mrb, mrb_value self)
{
    mrb_value other;

    mrb_get_args(mrb, "A", &other);
    replace(mrb, self, other);

    return self;
}

mrb_value RArray::times(mrb_state *mrb)
{
    RArray *a2;
    mrb_value ary;
    mrb_value *ptr;
    mrb_int times;

    mrb_get_args(mrb, "i", &times);
    if (times < 0) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "negative argument");
    }
    if (times == 0)
        return mrb_ary_new(mrb);

    ary = RArray::new_capa(mrb, m_len * times);
    a2 = mrb_ary_ptr(ary);
    ptr = a2->m_ptr;
    while(times--) {
        array_copy(ptr, m_ptr, m_len);
        ptr += m_len;
        a2->m_len += m_len;
    }

    return ary;
}

mrb_value RArray::reverse_bang(mrb_state *mrb, mrb_value self)
{
    RArray *a = mrb_ary_ptr(self);

    if (a->m_len > 1) {
        mrb_value *p1, *p2;

        a->ary_modify(mrb);
        p1 = a->m_ptr;
        p2 = a->m_ptr + a->m_len - 1;

        while(p1 < p2) {
            mrb_value tmp = *p1;
            *p1++ = *p2;
            *p2-- = tmp;
        }
    }
    return self;
}

mrb_value RArray::reverse(mrb_state *mrb)
{
    mrb_value ary = RArray::new_capa(mrb, m_len);
    if (m_len <= 0)
        return ary;
    RArray *b = mrb_ary_ptr(ary);
        mrb_value *p1, *p2, *e;

        p1 = m_ptr;
        e  = p1 + m_len;
        p2 = b->m_ptr + m_len - 1;
        while(p1 < e) {
            *p2-- = *p1++;
        }
        b->m_len = m_len;
    return ary;
}

mrb_value RArray::new_from_values(mrb_state *mrb, mrb_int _size, const mrb_value *vals)
{
    mrb_value ary;
    RArray *a;

    ary = RArray::new_capa(mrb, _size);
    a = mrb_ary_ptr(ary);
    array_copy(a->m_ptr, vals, _size);
    a->m_len = _size;

    return ary;
}

void RArray::push(mrb_state *mrb, mrb_value elem) /* mrb_ary_push */
{
    ary_modify(mrb);
    if (m_len == m_aux.capa)
        ary_expand_capa(mrb, m_len + 1);
    m_ptr[m_len++] = elem;
    mrb->gc().mrb_write_barrier(this);
}

mrb_value RArray::push_m(mrb_state *mrb, mrb_value self)
{
    mrb_value *argv;
    int len;

    mrb_get_args(mrb, "*", &argv, &len);
    while(len--) {
        push(mrb, self, *argv++);
    }

    return self;
}

mrb_value RArray::pop(mrb_state *)
{
    if (m_len == 0)
        return mrb_nil_value();
    return m_ptr[--m_len];
}

#define ARY_SHIFT_SHARED_MIN 10

mrb_value RArray::shift(mrb_state *mrb, mrb_value self)
{
    RArray *a = mrb_ary_ptr(self);
    mrb_value val;

    if (a->m_len == 0)
        return mrb_nil_value();
    if (a->flags & MRB_ARY_SHARED) {
//L_SHIFT:
        val = a->m_ptr[0];
        a->m_ptr++;
        a->m_len--;
        return val;
    }
    if (a->m_len > ARY_SHIFT_SHARED_MIN) {
        a->ary_make_shared(mrb);
        //goto L_SHIFT;
        val = a->m_ptr[0];
        a->m_ptr++;
        a->m_len--;
        return val;
    }
    else {
        mrb_value *ptr = a->m_ptr;
        mrb_int size = a->m_len;

        val = *ptr;
        while((int)(--size)) {
            *ptr = *(ptr+1);
            ++ptr;
        }
        --a->m_len;
    }
    return val;
}

/* self = [1,2,3]
   item = 0
   self.unshift item
   p self #=> [0, 1, 2, 3] */
mrb_value RArray::unshift(mrb_state *mrb, const mrb_value &self, const mrb_value &item)
{
    RArray *a = mrb_ary_ptr(self);

    if ((a->flags & MRB_ARY_SHARED)
            && a->m_aux.shared->refcnt == 1 /* shared only referenced from this array */
            && a->m_ptr - a->m_aux.shared->ptr >= 1) /* there's room for unshifted item */ {
        a->m_ptr--;
        a->m_ptr[0] = item;
    }
    else {
        a->ary_modify(mrb);
        if (a->m_aux.capa < a->m_len + 1)
            a->ary_expand_capa(mrb, a->m_len + 1);
        value_move(a->m_ptr + 1, a->m_ptr, a->m_len);
        a->m_ptr[0] = item;
    }
    a->m_len++;
    mrb->gc().mrb_write_barrier(a);

    return self;
}

mrb_value RArray::unshift_m(mrb_state *mrb, mrb_value self)
{
    RArray *a = mrb_ary_ptr(self);
    mrb_value *vals;
    int len;

    mrb_get_args(mrb, "*", &vals, &len);
    if ((a->flags & MRB_ARY_SHARED)
            && a->m_aux.shared->refcnt == 1 /* shared only referenced from this array */
            && a->m_ptr - a->m_aux.shared->ptr >= len) /* there's room for unshifted item */ {
        a->m_ptr -= len;
    }
    else {
        a->ary_modify(mrb);
        if (len == 0) return self;
        if (a->m_aux.capa < a->m_len + len)
            a->ary_expand_capa(mrb, a->m_len + len);
        value_move(a->m_ptr + len, a->m_ptr, a->m_len);
    }
    array_copy(a->m_ptr, vals, len);
    a->m_len += len;
    mrb->gc().mrb_write_barrier(a);

    return self;
}

mrb_value RArray::ref(mrb_state *, mrb_int n) const
{
    /* range check */
    if (n < 0)
        n += m_len;
    if (n < 0 || m_len <= (int)n)
        return mrb_nil_value();
    return m_ptr[n];
}

void RArray::set(mrb_state *mrb, mrb_int n, const mrb_value &val) /* rb_ary_store */
{
    ary_modify(mrb);
    /* range check */
    if (n < 0) {
        n += m_len;
        if (n < 0) {
            mrb_raisef(mrb, E_INDEX_ERROR, "index %S out of array", mrb_fixnum_value(n - m_len));
        }
    }
    if (m_len <= (int)n) {
        if (m_aux.capa <= (int)n)
            this->ary_expand_capa(mrb, n + 1);
        ary_fill_with_nil(m_ptr + m_len, n + 1 - m_len);
        m_len = n + 1;
    }

    m_ptr[n] = val;
    mrb->gc().mrb_write_barrier(this);
}

mrb_value RArray::splice(mrb_state *mrb, const mrb_value &ary, mrb_int head, mrb_int len, mrb_value rpl)
{
    RArray *a = mrb_ary_ptr(ary);
    mrb_int tail, _size;
    mrb_value *argv;
    mrb_int i, argc;

    a->ary_modify(mrb);
    /* range check */
    if (head < 0) {
        head += a->m_len;
        if (head < 0) {
            mrb_raise(mrb, E_INDEX_ERROR, "index is out of array");
        }
    }
    if (a->m_len < len || a->m_len < head + len) {
        len = a->m_len - head;
    }
    tail = head + len;

    /* size check */
    if (mrb_array_p(rpl)) {
        argc = RARRAY_LEN(rpl);
        argv = RARRAY_PTR(rpl);
    }
    else {
        argc = 1;
        argv = &rpl;
    }
    _size = head + argc;

    if (tail < a->m_len) _size += a->m_len - tail;
    if (_size > a->m_aux.capa)
        a->ary_expand_capa(mrb, _size);

    if (head > a->m_len) {
        ary_fill_with_nil(a->m_ptr + a->m_len, (int)(head - a->m_len));
    }
    else if (head < a->m_len) {
        value_move(a->m_ptr + head + argc, a->m_ptr + tail, a->m_len - tail);
    }

    for(i = 0; i < argc; i++) {
        *(a->m_ptr + head + i) = *(argv + i);
    }

    a->m_len = _size;

    return ary;
}


void mrb_ary_decref(mrb_state *mrb, mrb_shared_array *shared)
{
    shared->refcnt--;
    if (shared->refcnt == 0) {
        mrb->gc()._free(shared->ptr);
        mrb->gc()._free(shared);
    }
}

mrb_value RArray::ary_subseq(mrb_state *mrb, mrb_int beg, mrb_int len)
{

    ary_make_shared(mrb);
    RArray *b = mrb->gc().obj_alloc<RArray>(mrb->array_class);
    b->m_ptr = m_ptr + beg;
    b->m_len = len;
    b->m_aux.shared = m_aux.shared;
    b->m_aux.shared->refcnt++;
    b->flags |= MRB_ARY_SHARED;

    return mrb_obj_value(b);
}

mrb_value
RArray::get(mrb_state *mrb)
{
    mrb_int index, len;
    mrb_value *argv;
    int size;

    mrb_get_args(mrb, "i*", &index, &argv, &size);
    switch(size) {
        case 0:
            return RArray::ref(mrb, index);

        case 1:
            if (mrb_type(argv[0]) != MRB_TT_FIXNUM) {
                mrb_raise(mrb, E_TYPE_ERROR, "expected Fixnum");
            }
            if (index < 0)
                index += m_len;
            if (index < 0 || m_len < (int)index)
                return mrb_nil_value();
            len = mrb_fixnum(argv[0]);
            if (len < 0)
                return mrb_nil_value();
            if (m_len == (int)index)
                return mrb_ary_new(mrb);
            if (len > m_len - index)
                len = m_len - index;
            return this->ary_subseq(mrb, index, len);

        default:
            mrb_raise(mrb, E_ARGUMENT_ERROR, "wrong number of arguments");
            break;
    }

    return mrb_nil_value(); /* dummy to avoid warning : not reach here */
}

mrb_value RArray::aset(mrb_state *mrb, mrb_value self)
{
    mrb_value *argv;
    int argc;

    mrb_get_args(mrb, "*", &argv, &argc);
    switch(argc) {
        case 2:
            if (!mrb_fixnum_p(argv[0])) {
                /* Should we support Range object for 1st arg ? */
                mrb_raise(mrb, E_TYPE_ERROR, "expected Fixnum for 1st argument");
            }
            set(mrb, self, mrb_fixnum(argv[0]), argv[1]);
            return argv[1];

        case 3:
            splice(mrb, self, mrb_fixnum(argv[0]), mrb_fixnum(argv[1]), argv[2]);
            return argv[2];

        default:
            mrb_raise(mrb, E_ARGUMENT_ERROR, "wrong number of arguments");
            return mrb_nil_value();
    }
}

mrb_value RArray::delete_at(mrb_state *mrb, mrb_value self)
{
    RArray *a = mrb_ary_ptr(self);
    mrb_int   index;
    mrb_value val;
    mrb_value *ptr;
    mrb_int len;

    mrb_get_args(mrb, "i", &index);
    if (index < 0) index += a->m_len;
    if (index < 0 || a->m_len <= (int)index) return mrb_nil_value();

    a->ary_modify(mrb);
    val = a->m_ptr[index];

    ptr = a->m_ptr + index;
    len = a->m_len - index;
    while((int)(--len)) {
        *ptr = *(ptr+1);
        ++ptr;
    }
    --a->m_len;

    a->ary_shrink_capa(mrb);

    return val;
}

mrb_value RArray::first(mrb_state *mrb)
{
    mrb_int size;

    if (mrb_get_args(mrb, "|i", &size) == 0) {
        return (m_len > 0)? m_ptr[0]: mrb_nil_value();
    }
    if (size < 0) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "negative array size");
    }

    if (size > m_len)
        size = m_len;
    if (this->flags & MRB_ARY_SHARED) {
        return ary_subseq(mrb, 0, size);
    }
    return RArray::new_from_values(mrb, size, m_ptr);
}
mrb_value RArray::last(mrb_state *mrb)
{
    mrb_int _size;
    mrb_value *vals;
    int _len;

    mrb_get_args(mrb, "*", &vals, &_len);
    if (_len > 1) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "wrong number of arguments");
    }

    if (_len == 0) return (m_len > 0)? m_ptr[m_len - 1]: mrb_nil_value();

    /* len == 1 */
    _size = mrb_fixnum(*vals);
    if (_size < 0) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "negative array size");
    }
    if (_size > m_len) _size = m_len;
    if ((this->flags & MRB_ARY_SHARED) || _size > ARY_DEFAULT_LEN) {
        return ary_subseq(mrb, m_len - _size, _size);
    }
    return RArray::new_from_values(mrb, _size, m_ptr + m_len - _size);
}

mrb_value RArray::index_m(mrb_state *mrb)
{
    mrb_value obj;
    mrb_get_args(mrb, "o", &obj);
    for (mrb_int i = 0; i < m_len; i++) {
        if (mrb_equal(mrb, m_ptr[i], obj)) {
            return mrb_fixnum_value(i);
        }
    }
    return mrb_nil_value();
}

mrb_value RArray::rindex_m(mrb_state *mrb)
{
    mrb_value obj;
    mrb_int i;

    mrb_get_args(mrb, "o", &obj);
    for (i = m_len - 1; i >= 0; i--) {
        if (mrb_equal(mrb, m_ptr[i], obj)) {
            return mrb_fixnum_value(i);
        }
    }
    return mrb_nil_value();
}

mrb_value RArray::splat(mrb_state *mrb, mrb_value v)
{
    if (mrb_array_p(v)) {
        return v;
    }
        return RArray::new_from_values(mrb, 1, &v);
    }

mrb_value RArray::size(mrb_state *mrb)
{
    return mrb_fixnum_value(m_len);
}

mrb_value RArray::clear(mrb_state *mrb, mrb_value self)
{
    RArray *a = mrb_ary_ptr(self);

    a->ary_modify(mrb);
    a->m_len = 0;
    a->m_aux.capa = 0;
    mrb->gc()._free(a->m_ptr);
    a->m_ptr = 0;

    return self;
}

mrb_value RArray::empty_p(mrb_state *mrb)
{
    return mrb_bool_value(m_len == 0);
}

mrb_value mrb_check_array_type(mrb_state *mrb, mrb_value ary)
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

static constexpr bool simpleArrComp(RArray *a, const mrb_value &b) {
    return (a == b.value.p);
}
mrb_value RArray::inspect_ary(mrb_state *mrb, RArray *list_arr)
{
    mrb_int i;
    mrb_value s, arystr;
    static constexpr char head[] = { '[' };
    static constexpr char sep[] = { ',', ' ' };
    static constexpr char tail[] = { ']' };
    /* check recursive */
    for(i=0; i<list_arr->m_len; i++) {
        if (simpleArrComp(this, list_arr->m_ptr[i])) {
            return mrb_str_new(mrb, "[...]", 5);
        }
    }
    mrb_value t = {{(void *)this},MRB_TT_ARRAY};

    list_arr->push(mrb, t);

    arystr = mrb_str_buf_new(mrb, 64);
    mrb_str_buf_cat(mrb, arystr, head, sizeof(head));

    for(i=0; i<m_len; i++) {
        int ai = mrb->gc().arena_save();

        if (i > 0) {
            mrb_str_buf_cat(mrb, arystr, sep, sizeof(sep));
        }
        if (mrb_array_p(m_ptr[i])) {
            s = RARRAY(m_ptr[i])->inspect_ary(mrb, list_arr);
        } else {
            s = mrb_inspect(mrb, m_ptr[i]);
        }
        mrb_str_buf_cat(mrb, arystr, RSTRING_PTR(s), RSTRING_LEN(s));
        mrb->gc().arena_restore(ai);
    }

    mrb_str_buf_cat(mrb, arystr, tail, sizeof(tail));
    list_arr->pop(mrb);

    return arystr;
}

/* 15.2.12.5.31 (x) */
/*
 *  call-seq:
 *     ary.to_s -> string
 *     ary.inspect  -> string
 *
 *  Creates a string representation of +self+.
 */

mrb_value RArray::inspect(mrb_state *mrb)
{
    if (m_len == 0)
        return mrb_str_new(mrb, "[]", 2);
    //RArray *tmp ; // temporary array -> TODO: change this to stack allocated object
    mrb_value tmp=mrb_ary_new(mrb);
    return RArray::inspect_ary(mrb, RARRAY(tmp));
}
mrb_value RArray::join_ary(mrb_state *mrb, mrb_value sep, RArray *list_arr)
{
    mrb_int i;
    mrb_value result, val, tmp;
    /* check recursive */
    for(i=0; i<list_arr->m_len; i++) {
        if (simpleArrComp(this, list_arr->m_ptr[i])) {
            mrb_raise(mrb, E_ARGUMENT_ERROR, "recursive array join");
        }
    }
    mrb_value t = {{(void *)this},MRB_TT_ARRAY};
    list_arr->push(mrb,t);

    result = mrb_str_buf_new(mrb, 64);

    for(i=0; i<m_len; i++) {
        if (i > 0 && !mrb_nil_p(sep)) {
            mrb_str_buf_cat(mrb, result, RSTRING_PTR(sep), RSTRING_LEN(sep));
        }

        val = m_ptr[i];
        switch(mrb_type(val)) {
            case MRB_TT_ARRAY:
ary_join:
                val = RARRAY(val)->join_ary(mrb,sep, list_arr);
                /* fall through */

            case MRB_TT_STRING:
str_join:
                mrb_str_buf_cat(mrb, result, RSTRING_PTR(val), RSTRING_LEN(val));
                break;

            default:
                tmp = mrb_check_string_type(mrb, val);
                if (!mrb_nil_p(tmp)) {
                    val = tmp;
                    goto str_join;
                }
                tmp = mrb_check_convert_type(mrb, val, MRB_TT_ARRAY, "Array", "to_ary");
                if (!mrb_nil_p(tmp)) {
                    val = tmp;
                    goto ary_join;
                }
                val = mrb_obj_as_string(mrb, val);
                goto str_join;
        }
    }

    list_arr->pop(mrb);
    return result;
}

mrb_value
RArray::join(mrb_state *mrb, mrb_value sep)
{
    sep = mrb_obj_as_string(mrb, sep);
    mrb_value v = mrb_ary_new(mrb);
    return join_ary(mrb, sep, RARRAY(v)); //mrb_ary_new(mrb)
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

mrb_value
RArray::join_m(mrb_state *mrb)
{
    mrb_value sep = mrb_nil_value();

    mrb_get_args(mrb, "|S", &sep);
    return RArray::join(mrb, sep);
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

mrb_value
RArray::mrb_ary_equal(mrb_state *mrb, mrb_value ary1)
{
    RArray *self = RARRAY(ary1);
    mrb_value ary2;
    mrb_bool equal_p;

    mrb_get_args(mrb, "o", &ary2);
    if (mrb_obj_equal(mrb, ary1, ary2)) {
        equal_p = 1;
    }
    else if (mrb_special_const_p(ary2)) {
        equal_p = 0;
    }
    else if (!mrb_array_p(ary2)) {
        if (!mrb_respond_to(mrb, ary2, mrb_intern2(mrb, "to_ary", 6))) {
            equal_p = 0;
        }
        else {
            equal_p = mrb_equal(mrb, ary2, ary1);
        }
    }
    else if (self->m_len != RARRAY_LEN(ary2)) {
        equal_p = 0;
    }
    else {
        equal_p = 1;
        RArray *other = RARRAY(ary2);
        for (mrb_int i=0; i<self->m_len; i++) {
            if (!mrb_equal(mrb, self->m_ptr[i], other->m_ptr[i])) {
                equal_p = 0;
                break;
            }
        }
    }

    return mrb_bool_value(equal_p);
}

/* 15.2.12.5.34 (x) */
/*
 *  call-seq:
 *     ary.eql?(other)  -> true or false
 *
 *  Returns <code>true</code> if +self+ and _other_ are the same object,
 *  or are both arrays with the same content.
 */

mrb_value RArray::mrb_ary_eql(mrb_state *mrb, mrb_value ary1)
{
    mrb_value ary2;
    mrb_bool eql_p;

    mrb_get_args(mrb, "o", &ary2);
    if (mrb_obj_equal(mrb, ary1, ary2)) {
        eql_p = true;
    }
    else if (!mrb_array_p(ary2)) {
        eql_p = false;
    }
    else if (RARRAY_LEN(ary1) != RARRAY_LEN(ary2)) {
        eql_p = false;
    }
    else {
        mrb_int i;
        eql_p = 1;
        for (i=0; i<RARRAY_LEN(ary1); i++) {
            if (!mrb_eql(mrb, RARRAY(ary1)->ary_elt(i), RARRAY(ary2)->ary_elt(i))) {
                eql_p = false;
                break;
            }
        }
    }

    return mrb_bool_value(eql_p);
}

void mrb_init_array(mrb_state *mrb)
{
    RClass *a = mrb->array_class = mrb_define_class(mrb, "Array", mrb->object_class);
    a->instance_tt(MRB_TT_ARRAY)
            .include_module(mrb, "Enumerable")
            .define_class_method(mrb, "[]",         RArray::s_create,       ARGS_ANY())    /* 15.2.12.4.1 */
            .define_method(mrb, "*",                RArray::times,          ARGS_REQ(1))   /* 15.2.12.5.1  */
            .define_method(mrb, "+",                RArray::plus,           ARGS_REQ(1)) /* 15.2.12.5.2  */
            .define_method(mrb,  "<<",              RArray::push_m,         ARGS_REQ(1)) /* 15.2.12.5.3  */
            .define_method(mrb,  "[]",              RArray::get,            ARGS_ANY())  /* 15.2.12.5.4  */
            .define_method(mrb,  "[]=",             RArray::aset,           ARGS_ANY())  /* 15.2.12.5.5  */
            .define_method(mrb,  "clear",           RArray::clear,          ARGS_NONE()) /* 15.2.12.5.6  */
            .define_method(mrb,  "concat",          RArray::concat_m,       ARGS_REQ(1)) /* 15.2.12.5.8  */
            .define_method(mrb,  "delete_at",       RArray::delete_at,      ARGS_REQ(1)) /* 15.2.12.5.9  */
            .define_method(mrb,  "empty?",          RArray::empty_p,        ARGS_NONE()) /* 15.2.12.5.12 */
            .define_method(mrb,  "first",           RArray::first,          ARGS_OPT(1)) /* 15.2.12.5.13 */
            .define_method(mrb,  "index",           RArray::index_m,        ARGS_REQ(1)) /* 15.2.12.5.14 */
            .define_method(mrb,  "initialize_copy", RArray::replace_m,      ARGS_REQ(1)) /* 15.2.12.5.16 */
            .define_method(mrb,  "join",            RArray::join_m,         ARGS_ANY())  /* 15.2.12.5.17 */
            .define_method(mrb,  "last",            RArray::last,           ARGS_ANY())  /* 15.2.12.5.18 */
            .define_method(mrb,  "length",          RArray::size,           ARGS_NONE()) /* 15.2.12.5.19 */
            .define_method(mrb,  "pop",             RArray::pop,            ARGS_NONE()) /* 15.2.12.5.21 */
            .define_method(mrb,  "push",            RArray::push_m,         ARGS_ANY())  /* 15.2.12.5.22 */
            .define_method(mrb,  "replace",         RArray::replace_m,      ARGS_REQ(1)) /* 15.2.12.5.23 */
            .define_method(mrb,  "reverse",         RArray::reverse,        ARGS_NONE()) /* 15.2.12.5.24 */
            .define_method(mrb,  "reverse!",        RArray::reverse_bang,   ARGS_NONE()) /* 15.2.12.5.25 */
            .define_method(mrb,  "rindex",          RArray::rindex_m,       ARGS_REQ(1)) /* 15.2.12.5.26 */
            .define_method(mrb,  "shift",           RArray::shift,          ARGS_NONE()) /* 15.2.12.5.27 */
            .define_method(mrb,  "size",            RArray::size,           ARGS_NONE()) /* 15.2.12.5.28 */
            .define_method(mrb,  "slice",           RArray::get,            ARGS_ANY())  /* 15.2.12.5.29 */
            .define_method(mrb,  "unshift",         RArray::unshift_m,      ARGS_ANY())  /* 15.2.12.5.30 */
            .define_method(mrb,  "inspect",         RArray::inspect,        ARGS_NONE()) /* 15.2.12.5.31 (x) */
            .define_alias(mrb,  "to_s", "inspect")                                       /* 15.2.12.5.32 (x) */
            .define_method(mrb, "==",              RArray::mrb_ary_equal,   ARGS_REQ(1)) /* 15.2.12.5.33 (x) */
            .define_method(mrb, "eql?",            RArray::mrb_ary_eql,     ARGS_REQ(1)) /* 15.2.12.5.34 (x) */
            .define_method(mrb, "<=>",             RArray::cmp,             ARGS_REQ(1)) /* 15.2.12.5.36 (x) */
            .fin();
}
