/*
** mruby/value.h - mrb_value definition
**
** See Copyright Notice in mruby.h
*/

#pragma once
#include "mruby.h"
#ifndef MRB_NAN_BOXING

struct RClass;
struct mrb_context;
struct mrb_state;
typedef uint32_t mrb_code;
typedef uint32_t mrb_aspec;

enum mrb_vtype {
    MRB_TT_FALSE = 0,   /*   0 */
    MRB_TT_FREE,        /*   1 */
    MRB_TT_TRUE,        /*   2 */
    MRB_TT_FIXNUM,      /*   3 */
    MRB_TT_SYMBOL,      /*   4 */
    MRB_TT_UNDEF,       /*   5 */
    MRB_TT_FLOAT,       /*   6 */
    MRB_TT_VOIDP,       /*   7 */
    MRB_TT_OBJECT,      /*   8 */
    MRB_TT_CLASS,       /*   9 */
    MRB_TT_MODULE,      /*  10 */
    MRB_TT_ICLASS,      /*  11 */
    MRB_TT_SCLASS,      /*  12 */
    MRB_TT_PROC,        /*  13 */
    MRB_TT_ARRAY,       /*  14 */
    MRB_TT_HASH,        /*  15 */
    MRB_TT_STRING,      /*  16 */
    MRB_TT_RANGE,       /*  17 */
    MRB_TT_EXCEPTION,   /*  18 */
    MRB_TT_FILE,        /*  19 */
    MRB_TT_ENV,         /*  20 */
    MRB_TT_DATA,        /*  21 */
    MRB_TT_FIBER,       /*  22 */
    MRB_TT_MAXDEFINE    /*  22 */
};

struct mrb_value {
    union {
        void *p;
        mrb_float f;
        mrb_int i;
        mrb_sym sym;
    } value;
    enum mrb_vtype tt; // TODO: use c++11 typed unions.
    mrb_value to_str(mrb_state *mrb) const
    {
        return check_type(mrb, MRB_TT_STRING, "String", "to_str");
    }
    mrb_value check_type(mrb_state *mrb, mrb_vtype t, const char *c, const char *m) const;
// TODO: consider using a constructor to ease the return value conversions from void *, to mrb_values
};

#define mrb_type(o)   (o).tt
#define mrb_float(o)  (o).value.f

#define MRB_SET_VALUE(o, ttt, attr, v) do {\
    (o).tt = ttt;\
    (o).attr = v;\
    } while (0)

static inline mrb_value mrb_float_value(mrb_float f)
{
    mrb_value v;

    MRB_SET_VALUE(v, MRB_TT_FLOAT, value.f, f);
    return v;
}
#else  /* MRB_NAN_BOXING */

#ifdef MRB_USE_FLOAT
# error ---->> MRB_NAN_BOXING and MRB_USE_FLOAT conflict <<----
#endif

enum mrb_vtype {
    MRB_TT_FALSE = 1,   /*   1 */
    MRB_TT_FREE,        /*   2 */
    MRB_TT_TRUE,        /*   3 */
    MRB_TT_FIXNUM,      /*   4 */
    MRB_TT_SYMBOL,      /*   5 */
    MRB_TT_UNDEF,       /*   6 */
    MRB_TT_FLOAT,       /*   7 */
    MRB_TT_VOIDP,       /*   8 */
    MRB_TT_OBJECT,      /*   9 */
    MRB_TT_CLASS,       /*  10 */
    MRB_TT_MODULE,      /*  11 */
    MRB_TT_ICLASS,      /*  12 */
    MRB_TT_SCLASS,      /*  13 */
    MRB_TT_PROC,        /*  14 */
    MRB_TT_ARRAY,       /*  15 */
    MRB_TT_HASH,        /*  16 */
    MRB_TT_STRING,      /*  17 */
    MRB_TT_RANGE,       /*  18 */
    MRB_TT_EXCEPTION,   /*  19 */
    MRB_TT_FILE,        /*  20 */
    MRB_TT_ENV,         /*  21 */
    MRB_TT_DATA,        /*  22 */
    MRB_TT_FIBER,       /*  22 */
    MRB_TT_MAXDEFINE    /*  23 */
};

#ifdef MRB_ENDIAN_BIG
#define MRB_ENDIAN_LOHI(a,b) a b
#else
#define MRB_ENDIAN_LOHI(a,b) b a
#endif

typedef struct mrb_value {
    union {
        mrb_float f;
        struct {
            MRB_ENDIAN_LOHI(
                    uint32_t ttt;
            ,union {
                void *p;
                mrb_int i;
                mrb_sym sym;
            } value;
            )
        };
    };
} mrb_value;

#define mrb_tt(o)     ((o).ttt & 0xff)
#define mrb_mktt(tt)  (0xfff00000|(tt))
#define mrb_type(o)   ((uint32_t)0xfff00000 < (o).ttt ? mrb_tt(o) : MRB_TT_FLOAT)
#define mrb_float(o)  (o).f

#define MRB_SET_VALUE(o, tt, attr, v) do {\
    (o).ttt = mrb_mktt(tt);\
    (o).attr = v;\
    } while (0)

static inline mrb_value
mrb_float_value(mrb_float f)
{
    mrb_value v;

    if (f != f) {
        v.ttt = 0x7ff80000;
        v.value.i = 0;
    } else {
        v.f = f;
    }
    return v;
}
#endif	/* MRB_NAN_BOXING */

#define mrb_fixnum(o) (o).value.i
#define mrb_symbol(o) (o).value.sym
#define mrb_voidp(o) (o).value.p
#define mrb_fixnum_p(o) (mrb_type(o) == MRB_TT_FIXNUM)
#define mrb_float_p(o) (mrb_type(o) == MRB_TT_FLOAT)
#define mrb_undef_p(o) (mrb_type(o) == MRB_TT_UNDEF)
#define mrb_nil_p(o)  (mrb_type(o) == MRB_TT_FALSE && !(o).value.i)
#define mrb_symbol_p(o) (mrb_type(o) == MRB_TT_SYMBOL)
#define mrb_array_p(o) (mrb_type(o) == MRB_TT_ARRAY)
#define mrb_string_p(o) (mrb_type(o) == MRB_TT_STRING)
#define mrb_hash_p(o) (mrb_type(o) == MRB_TT_HASH)
#define mrb_voidp_p(o) (mrb_type(o) == MRB_TT_VOIDP)
#define mrb_as_bool(o)   (mrb_type(o) != MRB_TT_FALSE)
#define mrb_test(o)   mrb_as_bool(o)

/* white: 011, black: 100, gray: 000 */
#define MRB_GC_GRAY 0
#define MRB_GC_WHITE_A 1
#define MRB_GC_WHITE_B (1 << 1)
#define MRB_GC_BLACK (1 << 2)
#define MRB_GC_WHITES (MRB_GC_WHITE_A | MRB_GC_WHITE_B)
#define MRB_GC_COLOR_MASK 7

#define paint_partial_white(s, o) ((o)->color = (s)->current_white_part)
#define is_dead(s, o) (((o)->color & other_white_part(s) & MRB_GC_WHITES) || (o)->tt == MRB_TT_FREE)
#define flip_white_part(s) ((s)->current_white_part = other_white_part(s))
#define other_white_part(s) ((s)->current_white_part ^ MRB_GC_WHITES)

struct RBasic {
    enum mrb_vtype tt:8;
    uint32_t color:3;
    uint32_t flags:21;
    RClass *c;
    RBasic *gcnext;
    mrb_state *m_vm;
    void paint_gray()  { color = MRB_GC_GRAY; }
    void paint_black() { color = MRB_GC_BLACK; }
    void paint_white() { color = MRB_GC_WHITES; }
    bool is_gray() const { return color == MRB_GC_GRAY;}
    bool is_white() const { return (color & MRB_GC_WHITES);}
    bool is_black() const { return (color & MRB_GC_BLACK);}
};

#define mrb_basic_ptr(v) ((RBasic*)((v).value.p))
typedef mrb_value (*mrb_func_t)(mrb_state *mrb, mrb_value);

struct RObject : public RBasic {
    struct iv_tbl *iv;
public:
    void iv_set(mrb_sym sym, const mrb_value &v);
    mrb_value iv_get(mrb_sym sym);
    void define_singleton_method(const char *name, mrb_func_t func, mrb_aspec aspec);
};

#define mrb_obj_ptr(v)   ((RObject*)((v).value.p))
/* obsolete macro mrb_object; will be removed soon */
#define mrb_immediate_p(x) (mrb_type(x) <= MRB_TT_VOIDP)
#define mrb_special_const_p(x) mrb_immediate_p(x)
struct RFiber : public RObject {
  mrb_context *cxt;
};

namespace mruby {
static inline mrb_value toRuby(RBasic *p)
{
    mrb_value v;
    MRB_SET_VALUE(v, p->tt, value.p, p);
    return v;
}

} // end of mruby namespace

static inline mrb_value mrb_fixnum_value(mrb_int i)
{
    mrb_value v;

    MRB_SET_VALUE(v, MRB_TT_FIXNUM, value.i, i);
    return v;
}

static inline mrb_value mrb_symbol_value(mrb_sym i)
{
    mrb_value v;

    MRB_SET_VALUE(v, MRB_TT_SYMBOL, value.sym, i);
    return v;
}

static inline mrb_value mrb_obj_value(void *p)
{
    mrb_value v;
    RBasic *b = (RBasic*)p;

    MRB_SET_VALUE(v, b->tt, value.p, p);
    return v;
}

static inline mrb_value mrb_voidp_value(void *p)
{
    mrb_value v;

    MRB_SET_VALUE(v, MRB_TT_VOIDP, value.p, p);
    return v;
}

static inline mrb_value mrb_false_value(void)
{
    mrb_value v;

    MRB_SET_VALUE(v, MRB_TT_FALSE, value.i, 1);
    return v;
}

static inline mrb_value mrb_nil_value(void)
{
    mrb_value v;

    MRB_SET_VALUE(v, MRB_TT_FALSE, value.i, 0);
    return v;
}

static inline mrb_value mrb_true_value(void)
{
    mrb_value v;

    MRB_SET_VALUE(v, MRB_TT_TRUE, value.i, 1);
    return v;
}

static inline mrb_value mrb_undef_value(void)
{
    mrb_value v;

    MRB_SET_VALUE(v, MRB_TT_UNDEF, value.i, 0);
    return v;
}

static inline mrb_value mrb_bool_value(mrb_bool boolean)
{
    mrb_value v;

    MRB_SET_VALUE(v, boolean ? MRB_TT_TRUE : MRB_TT_FALSE, value.i, 1);
    return v;
}

