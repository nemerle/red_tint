/*
** mruby/value.h - mrb_value definition
**
** See Copyright Notice in mruby.h
*/

#pragma once
#include "mrbconf.h"

#ifdef MRB_USE_FLOAT
typedef float mrb_float;
# define mrb_float_to_str(buf, i) sprintf(buf, "%.7e", i)
# define str_to_mrb_float(buf) strtof(buf, NULL)
#else
typedef double mrb_float;
# define mrb_float_to_str(buf, i) sprintf(buf, "%.16e", i)
# define str_to_mrb_float(buf) strtod(buf, NULL)
#endif

#if defined(MRB_INT16) && defined(MRB_INT64)
# error "You can't define MRB_INT16 and MRB_INT64 at the same time."
#endif

#if defined(MRB_INT64)
# ifdef MRB_NAN_BOXING
#  error Cannot use NaN boxing when mrb_int is 64bit
# else
typedef int64_t mrb_int;
#  define MRB_INT_MIN INT64_MIN
#  define MRB_INT_MAX INT64_MAX
#  define PRIdMRB_INT PRId64
#  define PRIiMRB_INT PRIi64
#  define PRIoMRB_INT PRIo64
#  define PRIxMRB_INT PRIx64
#  define PRIXMRB_INT PRIX64
# endif
#elif defined(MRB_INT16)
typedef int16_t mrb_int;
# define MRB_INT_MIN INT16_MIN
# define MRB_INT_MAX INT16_MAX
#else
typedef int32_t mrb_int;
# define MRB_INT_MIN INT32_MIN
# define MRB_INT_MAX INT32_MAX
# define PRIdMRB_INT PRId32
# define PRIiMRB_INT PRIi32
# define PRIoMRB_INT PRIo32
# define PRIxMRB_INT PRIx32
# define PRIXMRB_INT PRIX32
#endif

typedef uint16_t mrb_sym;

#ifdef _MSC_VER
# define _ALLOW_KEYWORD_MACROS
# include <float.h>
# define inline __inline
# define snprintf _snprintf
# define isnan _isnan
# define isinf(n) (!_finite(n) && !_isnan(n))
# define signbit(n) (_copysign(1.0, (n)) < 0.0)
# define strtoll _strtoi64
# define PRId32 "I32d"
# define PRIi32 "I32i"
# define PRIo32 "I32o"
# define PRIx32 "I32x"
# define PRIX32 "I32X"
# define PRId64 "I64d"
# define PRIi64 "I64i"
# define PRIo64 "I64o"
# define PRIx64 "I64x"
# define PRIX64 "I64X"
#else
# include <inttypes.h>
#endif


#include <inttypes.h>
typedef bool mrb_bool;

#ifndef MRB_NAN_BOXING

struct RClass;
struct mrb_context;
struct mrb_state;
typedef uint32_t mrb_code;
typedef uint32_t mrb_aspec;

enum mrb_vtype : uint8_t {
    MRB_TT_FALSE = 0,   /*   0 */
    MRB_TT_FREE,        /*   1 */
    MRB_TT_TRUE,        /*   2 */
    MRB_TT_FIXNUM,      /*   3 */
    MRB_TT_SYMBOL,      /*   4 */
    MRB_TT_UNDEF,       /*   5 */
    MRB_TT_FLOAT,       /*   6 */
    MRB_TT_CPTR,       /*   7 */
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
    MRB_TT_MAXDEFINE    /*  23 */
};

#if defined(MRB_WORD_BOXING)

#define MRB_TT_HAS_BASIC  MRB_TT_FLOAT

enum mrb_special_consts {
    MRB_Qnil    = 0,
    MRB_Qfalse  = 2,
    MRB_Qtrue   = 4,
    MRB_Qundef  = 6,
};

#define MRB_FIXNUM_FLAG   0x01
#define MRB_FIXNUM_SHIFT  1
#define MRB_SYMBOL_FLAG   0x0e
#define MRB_SPECIAL_SHIFT 8

typedef union mrb_value {
    union {
        void *p;
        struct {
            unsigned int i_flag : MRB_FIXNUM_SHIFT;
            mrb_int i : (sizeof(mrb_int) * 8 - MRB_FIXNUM_SHIFT);
        };
        struct {
            unsigned int sym_flag : MRB_SPECIAL_SHIFT;
            int sym : (sizeof(mrb_sym) * 8);
        };
        struct RBasic *bp;
        struct RFloat *fp;
        struct RCptr *vp;
    } value;
    unsigned long w;
} mrb_value;

#define mrb_float(o)  (o).value.fp->f

#define MRB_SET_VALUE(o, ttt, attr, v) do {\
    (o).w = 0;\
    (o).attr = (v);\
    switch (ttt) {\
    case MRB_TT_FALSE:  (o).w = (v) ? MRB_Qfalse : MRB_Qnil; break;\
    case MRB_TT_TRUE:   (o).w = MRB_Qtrue; break;\
    case MRB_TT_UNDEF:  (o).w = MRB_Qundef; break;\
    case MRB_TT_FIXNUM: (o).value.i_flag = MRB_FIXNUM_FLAG; break;\
    case MRB_TT_SYMBOL: (o).value.sym_flag = MRB_SYMBOL_FLAG; break;\
    default:            if ((o).value.bp) (o).value.bp->tt = ttt; break;\
    }\
    } while (0)

extern mrb_value
mrb_float_value(struct mrb_state *mrb, mrb_float f);

#else /* No MRB_xxx_BOXING */

#define MRB_TT_HAS_BASIC  MRB_TT_OBJECT

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
    constexpr bool is_fixnum() const { return tt == MRB_TT_FIXNUM; }
    constexpr bool is_float() const { return tt == MRB_TT_FLOAT; }
    constexpr bool is_nil() const { return (tt==MRB_TT_FALSE) && value.i==0; }
    constexpr bool is_undef() const { return tt==MRB_TT_UNDEF; }
    constexpr bool is_symbol() const { return tt==MRB_TT_SYMBOL; }
    constexpr bool is_string() const {return tt==MRB_TT_STRING; }
    constexpr bool is_array() const {return tt==MRB_TT_ARRAY; }
    constexpr bool is_hash() const {return tt==MRB_TT_HASH; }

    constexpr bool is_immediate() const { return tt<=MRB_TT_CPTR; }
    constexpr bool is_special_const() const { return is_immediate() ; }

    constexpr bool to_bool() const { return tt!=MRB_TT_FALSE; }

    mrb_value check_type(mrb_state *mrb, mrb_vtype t, const char *c, const char *m) const;
    constexpr struct RBasic *basic_ptr() { return (struct RBasic *)value.p;}
    constexpr struct RObject *object_ptr() { return (struct RObject *)value.p;}
    // TODO: consider using a constructor to ease the return value conversions from void *, to mrb_values
};
//#define mrb_ptr(v)   ((RObject*)((v).value.p))

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
#endif  /* no boxing */

#else  /* MRB_NAN_BOXING */

#error ---->> No NAN boxing <<----

#endif	/* MRB_NAN_BOXING */

#define mrb_fixnum(o) (o).value.i
#define mrb_symbol(o) (o).value.sym
#define mrb_cptr(o) (o).value.p

/* white: 011, black: 100, gray: 000 */
enum eGcColor {
    MRB_GC_GRAY = 0,
    MRB_GC_WHITE_A = 1,
    MRB_GC_WHITE_B = (1 << 1),
    MRB_GC_BLACK = (1 << 2),
    MRB_GC_WHITES = (MRB_GC_WHITE_A | MRB_GC_WHITE_B),
    MRB_GC_COLOR_MASK = 7
};
#define paint_partial_white(s, o) ((o)->color = (s)->current_white_part)
#define is_dead(s, o) (((o)->color & other_white_part(s) & MRB_GC_WHITES) || (o)->tt == MRB_TT_FREE)
#define flip_white_part(s) ((s)->current_white_part = other_white_part(s))
#define other_white_part(s) ((s)->current_white_part ^ MRB_GC_WHITES)

struct RBasic {
    enum mrb_vtype tt:8;
    uint32_t color:3;
    // REnv uses flags to store number of children.
    //
    uint32_t flags:21;
    RClass *c;
    RBasic *gcnext;
    mrb_state *m_vm;
    void paint_gray()  { color = MRB_GC_GRAY; }
    void paint_black() { color = MRB_GC_BLACK; }
    void paint_white() { color = MRB_GC_WHITES; }
    constexpr bool is_gray() const { return color == MRB_GC_GRAY;}
    constexpr bool is_white() const { return (color & MRB_GC_WHITES);}
    constexpr bool is_black() const { return (color & MRB_GC_BLACK);}
};


typedef mrb_value (*mrb_func_t)(mrb_state *mrb, mrb_value);

struct RObject : public RBasic {
    struct iv_tbl *iv;
public:
    void iv_set(mrb_sym sym, const mrb_value &v);
    mrb_value iv_get(mrb_sym sym);
    void define_singleton_method(const char *name, mrb_func_t func, mrb_aspec aspec);
};

/* obsolete macro mrb_object; will be removed soon */
struct RFiber : public RObject {
    mrb_context *cxt;
};

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

static inline mrb_value mrb_obj_value(RBasic* p)
{
    return {{p},p->tt};
}

static inline mrb_value mrb_cptr_value(void *p)
{
    return {{p},MRB_TT_CPTR};
    ;
}
struct Values {
    static constexpr mrb_value _nil   {{0},MRB_TT_FALSE};
    //static constexpr mrb_value _false {{1},MRB_TT_FALSE};
};
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

namespace mruby {
static inline mrb_value toRuby(RBasic *p)
{
    return {{p},p->tt};
}

} // end of mruby namespace

