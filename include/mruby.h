/*
** mruby - An embeddable Ruby implementaion
**
** Copyright (c) mruby developers 2010-2012
**
** Permission is hereby granted, free of charge, to any person obtaining
** a copy of this software and associated documentation files (the
** "Software"), to deal in the Software without restriction, including
** without limitation the rights to use, copy, modify, merge, publish,
** distribute, sublicense, and/or sell copies of the Software, and to
** permit persons to whom the Software is furnished to do so, subject to
** the following conditions:
**
** The above copyright notice and this permission notice shall be
** included in all copies or substantial portions of the Software.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
** EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
** MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
** IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
** CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
** TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
** SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
**
** [ MIT license: http://www.opensource.org/licenses/mit-license.php ]
*/

#pragma once
#include <memory>
#include <cstring>
#include <stdint.h>
#include <cstddef>

#include "mrbconf.h"
#include "mruby/value.h"
#include "mruby/class.h"
#include "mruby/array.h"
#include "mruby/mem_manager.h"
#include "mruby/version.h"

/* macros to get typical exception objects
 note:
 + those E_* macros requires mrb_state* variable named mrb.
 + exception objects obtained from those macros are local to mrb
 */
#define A_RUNTIME_ERROR(x)          ((x)->class_get("RuntimeError"))
#define E_RUNTIME_ERROR             (mrb->class_get( "RuntimeError"))
#define A_TYPE_ERROR(x)             ((x)->class_get("TypeError"))
#define E_TYPE_ERROR                (mrb->class_get( "TypeError"))
#define I_TYPE_ERROR                (this->class_get("TypeError"))
#define A_ARGUMENT_ERROR(x)         ((x)->class_get("ArgumentError"))
#define E_ARGUMENT_ERROR            (mrb->class_get( "ArgumentError"))
#define I_ARGUMENT_ERROR            (this->class_get("ArgumentError"))
#define A_INDEX_ERROR(x)            ((x)->class_get("IndexError"))
#define E_INDEX_ERROR               (mrb->class_get( "IndexError"))
#define I_RANGE_ERROR               (this->class_get("RangeError"))
#define E_RANGE_ERROR               (mrb->class_get( "RangeError"))
#define E_NAME_ERROR                (mrb->class_get( "NameError"))
#define E_NOMETHOD_ERROR            (mrb->class_get( "NoMethodError"))
#define I_NOMETHOD_ERROR            (this->class_get("NoMethodError"))
#define E_SCRIPT_ERROR              (mrb->class_get( "ScriptError"))
#define E_SYNTAX_ERROR              (mrb->class_get( "SyntaxError"))
#define E_LOCALJUMP_ERROR           (mrb->class_get( "LocalJumpError"))
#define I_LOCALJUMP_ERROR           (this->class_get("LocalJumpError"))
#define E_REGEXP_ERROR              (mrb->class_get( "RegexpError"))

#define E_NOTIMP_ERROR              (mrb->class_get( "NotImplementedError"))
#define E_FLOATDOMAIN_ERROR         (mrb->class_get( "FloatDomainError"))

#define E_KEY_ERROR                 (mrb->class_get( "KeyError"))
struct RClass;
struct RProc;
struct REnv;
struct mrb_state;
struct mrb_pool;
typedef void (each_object_callback)(mrb_state *mrb, RBasic* obj, void *data);
RClass *mrb_define_class(mrb_state *, const char*, RClass*);
RClass *mrb_define_module(mrb_state *, const char*);
#ifndef __clang__
#define NORET(x)  x __attribute__ ((noreturn))
#define NORET_DEF(x)  x
#else
#define NORET(x)  [[noreturn]] x
#define NORET_DEF(x)  [[noreturn]] x
#endif
mrb_value mrb_exc_new(struct RClass *c, const char *ptr, long len);
NORET(void mrb_exc_raise(mrb_state *mrb, mrb_value exc));
NORET(void mrb_raise(RClass *c, const char *msg));
//NORET(void mrb_raisef(mrb_state *mrb, RClass *c, const char *fmt, ...));
NORET(void mrb_name_error(mrb_state *mrb, mrb_sym id, const char *fmt, ...));
int mrb_get_args(mrb_state *mrb, const char *format, ...);

//mrb_value mrb_funcall(mrb_state*, mrb_value, const char*, int,...);
mrb_value mrb_funcall_argv(mrb_state*, mrb_value, mrb_sym, int, mrb_value*);
mrb_value mrb_funcall_with_block(mrb_state*, mrb_value, mrb_sym, int, const mrb_value *, mrb_value);
mrb_sym mrb_intern_cstr(mrb_state*,const char*);
mrb_sym mrb_intern(mrb_state*,const char*,size_t);
mrb_sym mrb_intern_str(mrb_state*,mrb_value);
mrb_sym mrb_intern_static(mrb_state*,const char*,size_t);
#define mrb_intern_lit(mrb, lit) mrb_intern_static(mrb, (lit), sizeof(lit) - 1)
mrb_value mrb_check_intern_cstr(mrb_state*,const char*);
mrb_value mrb_check_intern_str(mrb_state*,mrb_value);
mrb_value mrb_check_intern(mrb_state*,const char*,size_t);
const char *mrb_sym2name(mrb_state*,mrb_sym);
const char *mrb_sym2name_len(mrb_state*,mrb_sym,size_t&);
mrb_value mrb_sym2str(mrb_state*,mrb_sym);

struct mrb_callinfo {
    mrb_sym mid;
    RProc *proc;
    mrb_value *stackent;
    int nregs;
    int argc;
    mrb_code *pc;                 /* return address */
    mrb_code *err;                /* error position */
    int acc;
    RClass *target_class;
    int ridx;
    int eidx;
    REnv *env;
};
enum mrb_fiber_state {
     MRB_FIBER_CREATED = 0,
    MRB_FIBER_RUNNING,
    MRB_FIBER_RESUMED,
    MRB_FIBER_TERMINATED,
};
struct mrb_context {
  mrb_context *prev;

  mrb_value *m_stack;
  mrb_value *m_stbase, *stend;

  mrb_callinfo *m_ci;
  mrb_callinfo *cibase, *ciend;

  mrb_code **rescue;
  int m_rsize;
  RProc **m_ensure;
  int m_esize;
  enum mrb_fiber_state status;
  struct RFiber *fib;
};
struct SysInterface {
    virtual void print_f(const char *fmt, ...);
    virtual void error_f(const char *fmt,...);
};
struct ArgStore {

    ArgStore &operator>>(mrb_value &v) {
        return *this;
    }
    ArgStore &operator>>(mrb_sym &v) {
        return *this;
    }
};
struct mrb_jmpbuf;
struct mrb_state {
    mrb_jmpbuf *jmp;
    MemManager m_gc;
    mrb_context *m_ctx;
    mrb_context *root_c;

    RObject *m_exc;
    struct iv_tbl *globals;

    struct mrb_irep **m_irep;
    size_t irep_len, irep_capa;
    SysInterface sys; // TODO: convert to pointer
    mrb_sym init_sym;
    RObject *top_self;
    RClass *object_class;
    RClass *class_class;
    RClass *module_class;
    RClass *proc_class;
    RClass *string_class;
    RClass *array_class;
    RClass *hash_class;

    RClass *float_class;
    RClass *fixnum_class;
    RClass *true_class;
    RClass *false_class;
    RClass *nil_class;
    RClass *symbol_class;
    RClass *kernel_module;
    MemManager &gc() {return m_gc;}
    mrb_sym symidx;
    struct SymTable *name2sym;      /* symbol table */

#ifdef ENABLE_DEBUG
    void (*code_fetch_hook)(struct mrb_state* mrb, struct mrb_irep *irep, mrb_code *pc, mrb_value *regs);
    void (*debug_op_hook)(struct mrb_state* mrb, struct mrb_irep *irep, mrb_code *pc, mrb_value *regs);
#endif

    RClass *eException_class;
    RClass *eStandardError_class;

    RClass & define_module(const char *name) {
        return *mrb_define_module(this, name);
    }
    RClass *class_get(const char *name) {
        return object_class->from_sym(intern_cstr(name));
    }
    mrb_sym intern_cstr(const char *name)
    {
        return intern2(name, strlen(name));
    }
    mrb_sym intern2(const char *name, size_t len, bool lit=false);
    ArgStore arg_store() {return ArgStore(); }
public:
    static mrb_state *create(mrb_allocf f, void *ud);
    RClass &define_class(const char *name, RClass *super);
    mrb_value const_get(mrb_value mod, mrb_sym sym);
    mrb_value mrb_run(RProc *proc, mrb_value self);
    mrb_value mrb_context_run(RProc*, mrb_value, unsigned int);
    void codedump_all(RProc *proc);
    template<class T>
    T get_arg() {
        T res;
        mrb_value *arg_src = arg_read_prepare(1);
        get_arg(*arg_src,res);
        return res;
    }
    NORET(void mrb_raise(RClass *m_ctx, const char *msg));
    NORET(void mrb_raisef(RClass *c, const char *fmt...));

    mrb_value funcall(mrb_value self, const char *name, int argc...);
    bool class_defined(const char *name);
    void mrb_objspace_each_objects(each_object_callback *callback, void *data);
    mrb_value run_proc(RProc *proc, mrb_value self, int stack_keep);
protected:
    void get_arg(const mrb_value &arg, mrb_int &tgt);
    void get_arg(const mrb_value &arg, mrb_sym &tgt);
    void get_arg(const mrb_value &arg, RClass *&tgt);
    void get_arg(const mrb_value &arg, mrb_value &tgt) { tgt = arg; }
    RProc *prepare_method_missing(RClass *c, mrb_sym mid, const int &a, int &n, mrb_value *regs);
    template<bool optional=false>
    mrb_value *arg_read_prepare(int args) {
        mrb_value *sp = m_ctx->m_stack + 1;
        int argc = m_ctx->m_ci->argc;
        if (argc < 0) {
            RArray *a = mrb_ary_ptr(m_ctx->m_stack[1]);
            argc = a->m_len;
            sp = a->m_ptr;
        }
        if(!optional) {
            if (argc != args) {
                mrb_raise(I_ARGUMENT_ERROR, "wrong number of arguments");
            }
        }
        else {
            if(argc>args) // too many arguments !
                mrb_raise(I_ARGUMENT_ERROR, "wrong number of arguments");
            else if(argc!=args)
                return nullptr; // not enough args, that's ok when processing optional args
        }
        return sp;
    }
private:
    void codedump_recur(mrb_irep *irep);
    void codedump(mrb_irep *irep);
};

typedef mrb_value (*mrb_func_t)(mrb_state *mrb, mrb_value);


mrb_value mrb_singleton_class(mrb_state*, mrb_value);
mrb_value mrb_instance_new(mrb_state *mrb, mrb_value cv);
RClass * mrb_module_new(mrb_state *mrb);
//int mrb_class_defined(mrb_state *mrb, const char *name);

mrb_value mrb_obj_dup(mrb_state *mrb, mrb_value obj);
mrb_value mrb_check_to_integer(mrb_state *mrb, const mrb_value &val, const char *method);

/* required arguments */
#define MRB_ARGS_REQ(n) ((mrb_aspec)((n)&0x1f) << 18)
/* optional arguments */
#define MRB_ARGS_OPT(n) ((mrb_aspec)((n)&0x1f) << 13)
/* mandatory and optinal arguments */
#define MRB_ARGS_ARG(n1,n2) (MRB_ARGS_REQ(n1)|MRB_ARGS_OPT(n2))

/* rest argument */
#define MRB_ARGS_REST() ((mrb_aspec)(1 << 12))
/* required arguments after rest */
#define MRB_ARGS_POST(n) ((mrb_aspec)((n)&0x1f) << 7)
/* keyword arguments (n of keys, kdict) */
#define MRB_ARGS_KEY(n1,n2) ((mrb_aspec)((((n1)&0x1f) << 2) | ((n2)?(1<<1):0)))
/* block argument */
#define MRB_ARGS_BLOCK() ((mrb_aspec)1)

/* accept any number of arguments */
#define MRB_ARGS_ANY() MRB_ARGS_REST()
/* accept no arguments */
#define MRB_ARGS_NONE() ((mrb_aspec)0)


mrb_value mrb_str_new(mrb_state *mrb, const char *p, size_t len);
mrb_value mrb_str_new_cstr(mrb_state*, const char*);
mrb_value mrb_str_new_static(mrb_state *mrb, const char *p, size_t len);
#define mrb_str_new_lit(mrb, lit) mrb_str_new_static(mrb, (lit), sizeof(lit) - 1)

mrb_state* mrb_open(void);
//mrb_state* mrb_open_allocf(mrb_allocf, void *ud);
void mrb_close(mrb_state*);

mrb_value mrb_top_self(mrb_state *);
//mrb_value mrb_run(mrb_state*, struct RProc*, mrb_value);

void mrb_p(mrb_state*, mrb_value);
mrb_int mrb_obj_id(const mrb_value &obj);
mrb_sym mrb_obj_to_sym(mrb_state *mrb, mrb_value name);

bool mrb_obj_eq(mrb_value , mrb_value );
bool mrb_obj_equal(mrb_value , mrb_value );
bool mrb_equal(mrb_state *mrb, mrb_value obj1, mrb_value obj2);
mrb_value mrb_Integer(mrb_state *mrb, mrb_value val);
mrb_value mrb_Float(mrb_state *mrb, mrb_value val);
mrb_value mrb_inspect(mrb_state *mrb, mrb_value obj);
bool mrb_eql(mrb_state *mrb, mrb_value obj1, mrb_value obj2);

#define mrb_gc_mark_value(mrb,val) do {\
    if (mrb_type(val) >= MRB_TT_OBJECT) (mrb)->gc().mark((val).basic_ptr());\
} while (0)
#define mrb_field_write_barrier_value(mrb, obj, val) do{\
    if ((val.tt >= MRB_TT_OBJECT)) (mrb)->gc().mrb_field_write_barrier((obj), mrb_basic_ptr(val));\
} while (0)
//void mrb_write_barrier(mrb_state *, struct RBasic*);

mrb_value mrb_check_convert_type(mrb_state *mrb, const mrb_value &val, mrb_vtype type, const char *tname, const char *method);
mrb_value mrb_any_to_s(mrb_state *mrb, mrb_value obj);
const char * mrb_obj_classname(mrb_state *mrb, mrb_value obj);
RClass* mrb_obj_class(mrb_state *mrb, mrb_value obj);
mrb_value mrb_convert_type(mrb_state *mrb, const mrb_value &val, mrb_vtype type, const char *tname, const char *method);
int mrb_obj_is_kind_of(mrb_state *mrb, mrb_value obj, RClass *c);
mrb_value mrb_obj_inspect(mrb_state *mrb, mrb_value self);
mrb_value mrb_obj_clone(mrb_state *mrb, mrb_value self);

/* need to include <ctype.h> to use these macros */
#ifndef ISPRINT
//#define ISASCII(c) isascii((int)(unsigned char)(c))
#define ISASCII(c) 1
#define ISPRINT(c) (ISASCII(c) && isprint((int)(unsigned char)(c)))
#define ISSPACE(c) (ISASCII(c) && isspace((int)(unsigned char)(c)))
#define ISUPPER(c) (ISASCII(c) && isupper((int)(unsigned char)(c)))
#define ISLOWER(c) (ISASCII(c) && islower((int)(unsigned char)(c)))
#define ISALNUM(c) (ISASCII(c) && isalnum((int)(unsigned char)(c)))
#define ISALPHA(c) (ISASCII(c) && isalpha((int)(unsigned char)(c)))
#define ISDIGIT(c) (ISASCII(c) && isdigit((int)(unsigned char)(c)))
#define ISXDIGIT(c) (ISASCII(c) && isxdigit((int)(unsigned char)(c)))
#define TOUPPER(c) (ISASCII(c) ? toupper((int)(unsigned char)(c)) : (c))
#define TOLOWER(c) (ISASCII(c) ? tolower((int)(unsigned char)(c)) : (c))
#endif

void mrb_warn(mrb_state *mrb, const char *fmt, ...);
void mrb_bug(mrb_state *mrb, const char *fmt, ...);
void mrb_print_backtrace(mrb_state *mrb);
void mrb_print_error(mrb_state *mrb);

mrb_value mrb_yield(mrb_state *mrb, mrb_value v, mrb_value blk);
mrb_value mrb_yield_argv(mrb_state *mrb, mrb_value b, int argc, mrb_value *argv);
//mrb_value mrb_class_new_instance(mrb_state *mrb, int, mrb_value*, struct RClass *);
mrb_value mrb_class_new_instance_m(mrb_state *mrb, mrb_value klass);

void mrb_gc_protect(mrb_state *mrb, mrb_value obj);
mrb_value mrb_to_int(mrb_state *mrb, mrb_value val);
void mrb_check_type(mrb_state *mrb, mrb_value x, enum mrb_vtype t);

typedef enum call_type {
    CALL_PUBLIC,
    CALL_FCALL,
    CALL_VCALL,
    CALL_TYPE_MAX
} call_type;

void mrb_define_global_const(mrb_state *mrb, const char *name, mrb_value val);

mrb_value mrb_block_proc(void);
mrb_value mrb_attr_get(mrb_value obj, mrb_sym id);

bool mrb_respond_to(mrb_state *mrb, const mrb_value &obj, mrb_sym mid);
bool mrb_obj_is_instance_of(mrb_state *mrb, mrb_value obj, struct RClass* c);

/* fiber functions (you need to link mruby-fiber mrbgem to use) */
mrb_value mrb_fiber_yield(mrb_state *mrb, int argc, mrb_value *argv);

/* memory pool implementation */
struct mrb_pool {
    typedef struct mrb_pool_page tPage;
    mrb_state *mrb;
    tPage *pages;

public:
    tPage *     page_alloc(size_t len);
    void        mrb_pool_close();
    void *      mrb_pool_alloc(size_t len);
    mrb_bool    mrb_pool_can_realloc(void *p, size_t len);
    void *      mrb_pool_realloc(void *p, size_t oldlen, size_t newlen);
};
// protect given object from GC, used to access mruby values in the external context without fear
void mrb_lock(mrb_state *mrb, mrb_value obj);
void mrb_unlock(mrb_state *mrb, mrb_value obj);
#ifdef MRB_DEBUG
#include <assert.h>
#define mrb_assert(p) assert(p)
#else
#define mrb_assert(p) ((void)0)
#endif
