/*
** vm.c - virtual machine for mruby
**
** See Copyright Notice in mruby.h
*/

#include <setjmp.h>
#include <stddef.h>
#include <stdarg.h>
#include "mruby.h"
#include "mruby/array.h"
#include "mruby/class.h"
#include "mruby/hash.h"
#include "mruby/irep.h"
#include "mruby/proc.h"
#include "mruby/range.h"
#include "mruby/string.h"
#include "mruby/variable.h"
#include "mruby/error.h"
#include "opcode.h"
#include "value_array.h"
#include "mrb_throw.h"
// overflow check in addition/subtraction etc. are based on:
// https://www.securecoding.cert.org/confluence/display/seccode/INT32-C.+Ensure+that+operations+on+signed+integers+do+not+result+in+overflow


static constexpr int STACK_INIT_SIZE=128;
static constexpr int CALLINFO_INIT_SIZE=32;

/* Define amount of linear stack growth. */
#ifndef MRB_STACK_GROWTH
#define MRB_STACK_GROWTH 128
#endif

/* Maximum stack depth. Should be set lower on memory constrained systems.
The value below allows about 60000 recursive calls in the simplest case. */
#ifndef MRB_STACK_MAX
#define MRB_STACK_MAX (0x40000 - MRB_STACK_GROWTH)
#endif

#ifdef VM_DEBUG
# define DEBUG(x) (x)
#else
# define DEBUG(x)
#endif

#define TO_STR(x) TO_STR_(x)
#define TO_STR_(x) #x
namespace {
static inline void
stack_clear(mrb_value *from, size_t count)
{
#ifndef MRB_NAN_BOXING
    constexpr mrb_value mrb_value_zero = mrb_value::nil();

    while(count-- > 0) {
        *from++ = mrb_value_zero;
    }
#else
    while(count-- > 0) {
        SET_NIL_VALUE(*from);
        from++;
    }
#endif
}
static inline void
stack_copy(mrb_value *dst, const mrb_value *src, size_t size)
{
    while (size-- > 0) {
        *dst++ = *src++;
    }
}

void stack_init(mrb_state *mrb)
{
    mrb_context *c = mrb->m_ctx;

    /* assert(mrb->stack == NULL); */
    c->m_stbase = (mrb_value *)mrb->gc()._calloc(STACK_INIT_SIZE, sizeof(mrb_value));
    c->stend = c->m_stbase + STACK_INIT_SIZE;
    c->m_stack = c->m_stbase;

    /* assert(mrb->ci == NULL); */
    c->cibase = (mrb_callinfo *)mrb->gc()._calloc(CALLINFO_INIT_SIZE, sizeof(mrb_callinfo));
    c->ciend = c->cibase + CALLINFO_INIT_SIZE;
    c->m_ci = c->cibase;
    c->m_ci->target_class = mrb->object_class;
    c->m_ci->stackent = c->m_stack;
}
inline void envadjust(mrb_state *mrb, mrb_value *oldbase, mrb_value *newbase)
{
    mrb_callinfo *ci = mrb->m_ctx->cibase;
    if (newbase == oldbase) return;
    while (ci <= mrb->m_ctx->m_ci) {
        REnv *e = ci->env;
        if (e && e->cioff >= 0) {
            ptrdiff_t off = e->stack - oldbase;

            e->stack = newbase + off;
        }
        ci->stackent = newbase + (ci->stackent - oldbase);
        ci++;
    }
}
/** def rec ; $deep =+ 1 ; if $deep > 1000 ; return 0 ; end ; rec ; end  */
void stack_extend_alloc(mrb_state *mrb, int room)
{
    mrb_value *oldbase = mrb->m_ctx->m_stbase;
    int size = mrb->m_ctx->stend - mrb->m_ctx->m_stbase;
    int off = mrb->m_ctx->m_stack - mrb->m_ctx->m_stbase;

    /* Use linear stack growth.
     It is slightly slower than doubling the stack space,
     but it saves memory on small devices. */
    if (room <= size)
        size += MRB_STACK_GROWTH;
    else
        size += room;

    mrb->m_ctx->m_stbase = (mrb_value *)mrb->gc()._realloc(mrb->m_ctx->m_stbase, sizeof(mrb_value) * size);
    mrb->m_ctx->m_stack = mrb->m_ctx->m_stbase + off;
    mrb->m_ctx->stend = mrb->m_ctx->m_stbase + size;
    envadjust(mrb, oldbase, mrb->m_ctx->m_stbase);
    /* Raise an exception if the new stack size will be too large,
     to prevent infinite recursion. However, do this only after resizing the stack, so mrb_raise has stack space to work with. */
    if (size > MRB_STACK_MAX) {
        mrb->mrb_raise(E_RUNTIME_ERROR, "stack level too deep. (limit=" TO_STR(MRB_STACK_MAX) ")");
    }
}
static
inline void stack_extend(mrb_state *mrb, int room, int keep)
{
    if (mrb->m_ctx->m_stack + room >= mrb->m_ctx->stend) {
        stack_extend_alloc(mrb, room);
    }

    if (room > keep) {
        /* do not leave uninitialized malloc region */
        stack_clear(&(mrb->m_ctx->m_stack[keep]), room - keep);
    }
}
inline REnv* uvenv(mrb_state *mrb, int up)
{
    REnv *e = mrb->m_ctx->m_ci->proc->env;

    while (up--) {
        if (!e)
            return nullptr;
        e = (REnv*)e->c;
    }
    return e;
}
inline mrb_bool is_strict(mrb_state *mrb, REnv *e)
{
    int cioff = e->cioff;

    if (cioff >= 0 && mrb->m_ctx->cibase[cioff].proc &&
            MRB_PROC_STRICT_P(mrb->m_ctx->cibase[cioff].proc)) {
        return true;
    }
    return false;
}
inline REnv* top_env(mrb_state *mrb, struct RProc *proc)
{
    REnv *e = proc->env;

    if (is_strict(mrb, e))
        return e;
    while (e->c) {
        e = (REnv*)e->c;
        if (is_strict(mrb, e))
            return e;
    }
    return e;
}
#define CI_ACC_SKIP    -1
#define CI_ACC_DIRECT  -2

mrb_callinfo* cipush(mrb_state *mrb)
{
    mrb_context *c = mrb->m_ctx;
    mrb_callinfo *ci = c->m_ci;
    int eidx = ci->eidx;
    int ridx = ci->ridx;

    if (ci + 1 == c->ciend) {
        size_t size = ci - c->cibase;

        c->cibase = (mrb_callinfo *)mrb->gc()._realloc(c->cibase, sizeof(mrb_callinfo)*size*2);
        c->m_ci = mrb->m_ctx->cibase + size;
        c->ciend = c->cibase + size * 2;
    }
    ci = ++c->m_ci;
    ci->nregs = 2;   /* protect method_missing arg and block */
    ci->eidx = eidx;
    ci->ridx = ridx;
    ci->env = 0;
    ci->pc = 0;
    ci->err = 0;
    return ci;
}
void cipop(mrb_state *mrb)
{
    mrb_context *c = mrb->m_ctx;
    if (c->m_ci->env) {
        REnv *e = c->m_ci->env;
        size_t len = (size_t)e->flags;
        mrb_value *p = (mrb_value *)mrb->gc()._malloc(sizeof(mrb_value)*len);

        e->cioff = -1;
        stack_copy(p, e->stack, len);
        e->stack = p;
    }

    c->m_ci--;
}
void ecall(mrb_state *mrb, int i)
{
    mrb_value *self = mrb->m_ctx->m_stack;

    RProc *p = mrb->m_ctx->m_ensure[i];
    if (!p)
        return;
    if (mrb->m_ctx->m_ci->eidx > i)
        mrb->m_ctx->m_ci->eidx = i;
    mrb_callinfo *ci = cipush(mrb);
    ci->stackent = mrb->m_ctx->m_stack;
    ci->mid = ci[-1].mid;
    ci->acc = CI_ACC_SKIP;
    ci->argc = 0;
    ci->proc = p;
    ci->nregs = p->ireps()->nregs;
    ci->target_class = p->m_target_class;
    mrb->m_ctx->m_stack = mrb->m_ctx->m_stack + ci[-1].nregs;
    RObject *exc = mrb->m_exc;
    mrb->m_exc = 0;
    mrb->mrb_run(p, *self);
    mrb->m_ctx->m_ensure[i] = nullptr;
    if (!mrb->m_exc)
        mrb->m_exc = exc;
}
void call_stack_sizing(mrb_state *mrb,const mrb_callinfo *_ci,const mrb_irep *irep) {
    if (_ci->argc < 0) {
        stack_extend(mrb, (irep->nregs < 3) ? 3 : irep->nregs, 3);
    }
    else {
        stack_extend(mrb, irep->nregs,  _ci->argc+2);
    }
}
////////////////////////////////////////////////////
// end of anonymous namespace
////////////////////////////////////////////////////
}



#ifndef MRB_FUNCALL_ARGC_MAX
#define MRB_FUNCALL_ARGC_MAX 16
#endif

mrb_value mrb_state::funcall(mrb_value self, const char *name, int argc, ...)
{
    mrb_sym mid = mrb_intern_cstr(this, name);

    if (argc == 0) {
        return mrb_funcall_argv(this, self, mid, 0, 0);
    }
    mrb_value argv[MRB_FUNCALL_ARGC_MAX];
    if (argc > MRB_FUNCALL_ARGC_MAX) {
        mrb_raise(I_ARGUMENT_ERROR, "Too long arguments. (limit=" TO_STR(MRB_FUNCALL_ARGC_MAX) ")");
    }
    va_list ap;
    va_start(ap, argc);
    for (int i = 0; i < argc; i++) {
        argv[i] = va_arg(ap, mrb_value);
    }
    va_end(ap);
    return mrb_funcall_argv(this, self, mid, argc, argv);
}

mrb_value mrb_funcall_with_block(mrb_state *mrb, mrb_value self, mrb_sym mid, int argc, const mrb_value *argv, mrb_value blk)
{
    mrb_value val;

    if (!mrb->jmp) {
        mrb_jmpbuf c_jmp;
        mrb_callinfo *old_ci = mrb->m_ctx->m_ci;
        MRB_TRY(&c_jmp) {
            mrb->jmp = &c_jmp;
            /* recursive call */
            val = mrb_funcall_with_block(mrb, self, mid, argc, argv, blk);
            mrb->jmp = 0;
        }
        MRB_CATCH(&c_jmp) { /* error */
            while (old_ci != mrb->m_ctx->m_ci) {
                mrb->m_ctx->m_stack = mrb->m_ctx->m_ci->stackent;
                cipop(mrb);
            }
            mrb->jmp = nullptr;
            val = mrb_value::nil();
        }
        MRB_END_EXC(&c_jmp);
    }
    else {
        mrb_sym undef = 0;

        if (!mrb->m_ctx->m_stack) {
            stack_init(mrb);
        }
        int n = mrb->m_ctx->m_ci->nregs;
        if (argc < 0) {
            mrb->mrb_raisef(E_ARGUMENT_ERROR, "negative argc for funcall (%S)", mrb_fixnum_value(argc));
        }
        RClass *c = RClass::mrb_class(mrb, self);
        RProc *p = RClass::method_search_vm(&c, mid);
        if (!p) {
            undef = mid;
            mid = mrb_intern(mrb, "method_missing", 14);
            assert(c);
            p = RClass::method_search_vm(&c, mid);
            assert(p);
            n++;
            argc++;
        }
        mrb_callinfo *ci = cipush(mrb);
        ci->mid = mid;
        ci->proc = p;
        ci->stackent = mrb->m_ctx->m_stack;
        ci->argc = argc;
        ci->target_class = c;
        if (p->is_cfunc()) {
            ci->nregs = argc + 2;
        }
        else {
            ci->nregs = p->ireps()->nregs + n;
        }
        mrb->m_ctx->m_stack = mrb->m_ctx->m_stack + n;

        stack_extend(mrb, ci->nregs, 0);
        mrb->m_ctx->m_stack[0] = self;
        if (undef) {
            mrb->m_ctx->m_stack[1] = mrb_symbol_value(undef);
            stack_copy(mrb->m_ctx->m_stack+2, argv, argc-1);
        }
        else if (argc > 0) {
            stack_copy(mrb->m_ctx->m_stack+1, argv, argc);
        }
        mrb->m_ctx->m_stack[argc+1] = blk;

        if (p->is_cfunc()) {
            int ai = mrb->gc().arena_save();

            ci->acc = CI_ACC_DIRECT;
            val = p->call_cfunc(self);
            mrb->m_ctx->m_stack = mrb->m_ctx->m_ci->stackent;
            cipop(mrb);
            mrb->gc().arena_restore(ai);
        }
        else {
            ci->acc = CI_ACC_SKIP;
            val = mrb->mrb_run(p, self);
        }
    }
    mrb_gc_protect(mrb, val);
    return val;
}

mrb_value mrb_funcall_argv(mrb_state *mrb, mrb_value self, mrb_sym mid, int argc, mrb_value *argv)
{
    return mrb_funcall_with_block(mrb, self, mid, argc, argv, mrb_value::nil());
}
/* 15.3.1.3.4  */
/* 15.3.1.3.44 */
/*
 *  call-seq:
 *     obj.send(symbol [, args...])        -> obj
 *     obj.__send__(symbol [, args...])      -> obj
 *
 *  Invokes the method identified by _symbol_, passing it any
 *  arguments specified. You can use <code>__send__</code> if the name
 *  +send+ clashes with an existing method in _obj_.
 *
 *     class Klass
 *       def hello(*args)
 *         "Hello " + args.join(' ')
 *       end
 *     end
 *     k = Klass.new
 *     k.send :hello, "gentle", "readers"   #=> "Hello gentle readers"
 */
mrb_value mrb_f_send(mrb_state *mrb, mrb_value self)
{
    mrb_sym name;
    mrb_value block, *argv, *regs;
    int argc, i, len;
    mrb_callinfo *ci;

    mrb_get_args(mrb, "n*&", &name, &argv, &argc, &block);

    RClass *c = RClass::mrb_class(mrb, self);
    RProc *p  = RClass::method_search_vm(&c,name);
    if (!p) { /* call method_mising */
        return mrb_funcall_with_block(mrb, self, name, argc, argv, block);
    }

    ci = mrb->m_ctx->m_ci;
    ci->mid = name;
    ci->target_class = c;
    ci->proc = p;
    regs = mrb->m_ctx->m_stack+1;
    /* remove first symbol from arguments */
    if (ci->argc >= 0) {
        for (i=0,len=ci->argc; i<len; i++) {
            regs[i] = regs[i+1];
        }
        ci->argc--;
    }
    else {                     /* variable length arguments */
        RARRAY(regs[0])->shift();
        //mrb_ary_shift(mrb, regs[0]);
    }
    if (p->is_cfunc()) {
        return p->call_cfunc(self);
    }
    cipush(mrb);
    ci = mrb->m_ctx->m_ci;
    ci->target_class = nullptr;
    ci->pc = p->ireps()->iseq;
    ci->stackent = mrb->m_ctx->m_stack;
    ci->acc = 0;

    return self;
}
mrb_value mrb_yield_internal(mrb_state *mrb, mrb_value b, int argc, mrb_value *argv, mrb_value self, RClass *c)
{
    mrb_sym mid = mrb->m_ctx->m_ci->mid;
    int n = mrb->m_ctx->m_ci->nregs;
    mrb_value val;
    if (b.is_nil()) {
        mrb->mrb_raise(E_ARGUMENT_ERROR, "no block given");
    }
    RProc *p = b.ptr<RProc>();
    mrb_callinfo *ci = cipush(mrb);
    ci->mid = mid;
    ci->proc = p;
    ci->stackent = mrb->m_ctx->m_stack;
    ci->argc = argc;
    ci->target_class = c;
    if (p->is_cfunc()) {
        ci->nregs = argc + 2;
    }
    else {
        ci->nregs = p->ireps()->nregs + 1;
    }
    ci->acc = CI_ACC_SKIP;
    mrb->m_ctx->m_stack += n;

    stack_extend(mrb, ci->nregs, 0);
    mrb->m_ctx->m_stack[0] = self;
    if (argc > 0) {
        stack_copy(mrb->m_ctx->m_stack+1, argv, argc);
    }
    mrb->m_ctx->m_stack[argc+1] = mrb_value::nil();

    if (p->is_cfunc()) {
        val = p->call_cfunc(self);
        mrb->m_ctx->m_stack = mrb->m_ctx->m_ci->stackent;
        cipop(mrb);
    }
    else {
        val = mrb->mrb_run( p, self);
    }
    return val;
}

mrb_value mrb_yield_argv(mrb_state *mrb, mrb_value b, int argc, mrb_value *argv)
{
    RProc *p = b.ptr<RProc>();

    return mrb_yield_internal(mrb, b, argc, argv, p->env->stack[0], p->m_target_class);
}

mrb_value mrb_yield(mrb_state *mrb, mrb_value b, mrb_value v)
{
    RProc *p = b.ptr<RProc>();

    return mrb_yield_internal(mrb, b, 1, &v, p->env->stack[0], p->m_target_class);
}

enum localjump_error_kind {
    LOCALJUMP_ERROR_RETURN = 0,
    LOCALJUMP_ERROR_BREAK = 1,
    LOCALJUMP_ERROR_YIELD = 2
};

static void localjump_error(mrb_state *mrb, localjump_error_kind kind)
{
    const char kind_str[3][7] = { "return", "break", "yield" };
    char kind_str_len[] = { 6, 5, 5 };
    static const char lead[] = "unexpected ";

    RString *msg = RString::create(mrb, sizeof(lead) + 7);
    msg->str_buf_cat(lead, sizeof(lead) - 1);
    msg->str_buf_cat(kind_str[kind], kind_str_len[kind]);
    mrb_value exc = mrb_exc_new_str(E_LOCALJUMP_ERROR, msg);
    mrb->m_exc = exc.object_ptr();
}

static void argnum_error(mrb_state *mrb, int num)
{
    mrb_value exc;
    RString *str;

    if (mrb->m_ctx->m_ci->mid) {
        str = mrb_format(mrb, "'%S': wrong number of arguments (%S for %S)",
                         mrb_sym2str(mrb, mrb->m_ctx->m_ci->mid),
                         mrb_fixnum_value(mrb->m_ctx->m_ci->argc), mrb_fixnum_value(num));
    }
    else {
        str = mrb_format(mrb, "wrong number of arguments (%S for %S)",
                         mrb_fixnum_value(mrb->m_ctx->m_ci->argc), mrb_fixnum_value(num));
    }
    exc = mrb_exc_new_str(E_ARGUMENT_ERROR, str);
    mrb->m_exc = exc.object_ptr();
}
#define ERR_PC_SET(mrb, pc) mrb->m_ctx->m_ci->err = pc;
#define ERR_PC_CLR(mrb)     mrb->m_ctx->m_ci->err = 0;
#ifdef ENABLE_DEBUG
#define CODE_FETCH_HOOK(mrb, irep, pc, regs) if ((mrb)->code_fetch_hook) (mrb)->code_fetch_hook((mrb), (irep), (pc), (regs));
#else
#define CODE_FETCH_HOOK(mrb, irep, pc, regs)
#endif

#if defined __GNUC__ || defined __clang__ || defined __INTEL_COMPILER__
#define DIRECT_THREADED
#endif
#undef DIRECT_THREADED
#ifndef DIRECT_THREADED

#define INIT_DISPATCH for (;;) { i = *pc; CODE_FETCH_HOOK(mrb, irep, pc, regs); switch (GET_OPCODE(i)) {
#define CASE(op) case op:
#define NEXT pc++; break
#define JUMP break
#define END_DISPATCH }}

#else

#define INIT_DISPATCH JUMP; return mrb_value::nil();
#define CASE(op) L_ ## op:
#define NEXT i=*++pc; CODE_FETCH_HOOK(mrb, irep, pc, regs); goto *optable[GET_OPCODE(i)]
#define JUMP i=*pc; CODE_FETCH_HOOK(mrb, irep, pc, regs); goto *optable[GET_OPCODE(i)]

#define END_DISPATCH

#endif

mrb_value mrb_gv_val_get(mrb_state *mrb, mrb_sym sym);
void mrb_gv_val_set(mrb_state *mrb, mrb_sym sym, mrb_value val);

#define CALL_MAXARGS 127
RProc * mrb_state::prepare_method_missing(RClass *c,mrb_sym mid_,const int &a,int &n,mrb_value *regs) {
    mrb_value sym = mrb_symbol_value(mid_);

    mrb_sym missing_id = mrb_intern_lit(this,"method_missing");
    RProc *m =  RClass::method_search_vm(&c, missing_id);
    if (n == CALL_MAXARGS) {
        RARRAY(regs[a+1])->unshift(sym);
    }
    else {
        value_move(regs+a+2, regs+a+1, ++n);
        regs[a+1] = sym;
    }
    return m;
}
mrb_value mrb_state::mrb_context_run(RProc *proc, mrb_value self,unsigned int stack_keep)
{
    mrb_irep *irep = proc->ireps();
    mrb_code *pc = irep->iseq;
    auto *pool = irep->pool;
    mrb_sym *syms = irep->syms;
    mrb_value *regs = NULL;
    mrb_code i;
    int ai = gc().arena_save();
    mrb_jmpbuf *prev_jmp = this->jmp;
    mrb_jmpbuf c_jmp;

#ifdef DIRECT_THREADED
    static void *optable[] = {
        &&L_OP_NOP, &&L_OP_MOVE,
        &&L_OP_LOADL, &&L_OP_LOADI, &&L_OP_LOADSYM, &&L_OP_LOADNIL,
        &&L_OP_LOADSELF, &&L_OP_LOADT, &&L_OP_LOADF,
        &&L_OP_GETGLOBAL, &&L_OP_SETGLOBAL, &&L_OP_GETSPECIAL, &&L_OP_SETSPECIAL,
        &&L_OP_GETIV, &&L_OP_SETIV, &&L_OP_GETCV, &&L_OP_SETCV,
        &&L_OP_GETCONST, &&L_OP_SETCONST, &&L_OP_GETMCNST, &&L_OP_SETMCNST,
        &&L_OP_GETUPVAR, &&L_OP_SETUPVAR,
        &&L_OP_JMP, &&L_OP_JMPIF, &&L_OP_JMPNOT,
        &&L_OP_ONERR, &&L_OP_RESCUE, &&L_OP_POPERR, &&L_OP_RAISE, &&L_OP_EPUSH, &&L_OP_EPOP,
        &&L_OP_SEND, &&L_OP_SENDB, &&L_OP_FSEND,
        &&L_OP_CALL, &&L_OP_SUPER, &&L_OP_ARGARY, &&L_OP_ENTER,
        &&L_OP_KARG, &&L_OP_KDICT, &&L_OP_RETURN, &&L_OP_TAILCALL, &&L_OP_BLKPUSH,
        &&L_OP_ADD, &&L_OP_ADDI, &&L_OP_SUB, &&L_OP_SUBI, &&L_OP_MUL, &&L_OP_DIV,
        &&L_OP_EQ, &&L_OP_LT, &&L_OP_LE, &&L_OP_GT, &&L_OP_GE,
        &&L_OP_ARRAY, &&L_OP_ARYCAT, &&L_OP_ARYPUSH, &&L_OP_AREF, &&L_OP_ASET, &&L_OP_APOST,
        &&L_OP_STRING, &&L_OP_STRCAT, &&L_OP_HASH,
        &&L_OP_LAMBDA, &&L_OP_RANGE, &&L_OP_OCLASS,
        &&L_OP_CLASS, &&L_OP_MODULE, &&L_OP_EXEC,
        &&L_OP_METHOD, &&L_OP_SCLASS, &&L_OP_TCLASS,
        &&L_OP_DEBUG, &&L_OP_STOP, &&L_OP_ERR,
    };
#endif
    mrb_bool exc_catched = false;
RETRY_TRY_BLOCK:
    MRB_TRY(&c_jmp) {

        if (exc_catched) {
            exc_catched = false;
            goto L_RAISE;
        }
        this->jmp = &c_jmp;
        if (!m_ctx->m_stack) {
            stack_init(this);
        }
        stack_extend(this, irep->nregs, stack_keep); /* argc + 2 (receiver and block) */
        m_ctx->m_ci->proc = proc;
        m_ctx->m_ci->nregs = irep->nregs + 1;
        regs = m_ctx->m_stack;
        regs[0] = self;

        INIT_DISPATCH {
            CASE(OP_NOP) {
                /* do nothing */
                NEXT;
            }

            CASE(OP_MOVE) {
                /* A B    R(A) := R(B) */
                regs[GETARG_A(i)] = regs[GETARG_B(i)];
                NEXT;
            }

            CASE(OP_LOADL) {
                /* A Bx   R(A) := Pool(Bx) */
                regs[GETARG_A(i)] =  pool[GETARG_Bx(i)];
                NEXT;
            }

            CASE(OP_LOADI) {
                /* A Bx   R(A) := sBx */
                regs[GETARG_A(i)] = mrb_fixnum_value(GETARG_sBx(i));
                NEXT;
            }

            CASE(OP_LOADSYM) {
                /* A B    R(A) := Sym(B) */
                regs[GETARG_A(i)] = mrb_symbol_value(syms[GETARG_Bx(i)]);
                NEXT;
            }

            CASE(OP_LOADSELF) {
                /* A      R(A) := self */
                regs[GETARG_A(i)] = regs[0];
                NEXT;
            }

            CASE(OP_LOADT) {
                /* A      R(A) := true */
                regs[GETARG_A(i)] = mrb_true_value();
                NEXT;
            }

            CASE(OP_LOADF) {
                /* A      R(A) := false */
                regs[GETARG_A(i)] = mrb_value::_false();
                NEXT;
            }

            CASE(OP_GETGLOBAL) {
                /* A B    R(A) := getglobal(Sym(B)) */
                regs[GETARG_A(i)] = this->mrb_gv_get(syms[GETARG_Bx(i)]);
                NEXT;
            }

            CASE(OP_SETGLOBAL) {
                /* setglobal(Sym(b), R(A)) */
                this->gv_set(syms[GETARG_Bx(i)], regs[GETARG_A(i)]);
                NEXT;
            }

            CASE(OP_GETSPECIAL) {
                /* A Bx   R(A) := Special[Bx] */
                regs[GETARG_A(i)] = mrb_vm_special_get(this, GETARG_Bx(i));
                NEXT;
            }

            CASE(OP_SETSPECIAL) {
                /* A Bx   Special[Bx] := R(A) */
                mrb_vm_special_set(this, GETARG_Bx(i), regs[GETARG_A(i)]);
                NEXT;
            }

            CASE(OP_GETIV) {
                /* A Bx   R(A) := ivget(Bx) */
                regs[GETARG_A(i)] = this->vm_iv_get(syms[GETARG_Bx(i)]);
                NEXT;
            }

            CASE(OP_SETIV) {
                /* ivset(Sym(B),R(A)) */
                vm_iv_set(syms[GETARG_Bx(i)], regs[GETARG_A(i)]);
                NEXT;
            }

            CASE(OP_GETCV) {
                /* A B    R(A) := ivget(Sym(B)) */
                ERR_PC_SET(this, pc);
                regs[GETARG_A(i)] = this->vm_cv_get(syms[GETARG_Bx(i)]);
                ERR_PC_CLR(this);
                NEXT;
            }

            CASE(OP_SETCV) {
                /* ivset(Sym(B),R(A)) */
                this->vm_cv_set(syms[GETARG_Bx(i)], regs[GETARG_A(i)]);
                NEXT;
            }

            CASE(OP_GETCONST) {
                /* A B    R(A) := constget(Sym(B)) */
                ERR_PC_SET(this, pc);
                mrb_value val = this->mrb_vm_const_get(syms[GETARG_Bx(i)]);
                ERR_PC_CLR(this);
                regs =m_ctx->m_stack;
                regs[GETARG_A(i)] = val;
                NEXT;
            }

            CASE(OP_SETCONST) {
                /* A B    constset(Sym(B),R(A)) */
                mrb_vm_const_set(this, syms[GETARG_Bx(i)], regs[GETARG_A(i)]);
                NEXT;
            }

            CASE(OP_GETMCNST) {
                /* A B C  R(A) := R(C)::Sym(B) */
                int a = GETARG_A(i);
                ERR_PC_SET(this, pc);
                mrb_value val = this->const_get(regs[a], syms[GETARG_Bx(i)]);
                ERR_PC_CLR(this);
                regs = m_ctx->m_stack;
                regs[a] = val;
                NEXT;
            }

            CASE(OP_SETMCNST) {
                /* A B C  R(A+1)::Sym(B) := R(A) */
                int a = GETARG_A(i);

                this->const_set(regs[a+1], syms[GETARG_Bx(i)], regs[a]);
                NEXT;
            }

            CASE(OP_GETUPVAR) {
                /* A B C  R(A) := uvget(B,C) */
                mrb_value &regs_a(regs[GETARG_A(i)]);
                REnv *e = uvenv(this, GETARG_C(i));
                regs_a = e ? e->stack[GETARG_B(i)] : mrb_value::nil();
                NEXT;
            }

            CASE(OP_SETUPVAR) {
                /* A B C  uvset(B,C,R(A)) */
                /* A B C  R(A) := uvget(B,C) */
                REnv *e = uvenv(this, GETARG_C(i));

                if (e) {
                    e->stack[GETARG_B(i)] = regs[GETARG_A(i)];
                    gc().mrb_write_barrier(e);
                }
                NEXT;
            }

            CASE(OP_JMP) {
                /* sBx    pc+=sBx */
                pc += GETARG_sBx(i);
                JUMP;
            }

            CASE(OP_JMPIF) {
                /* A sBx  if R(A) pc+=sBx */
                if (regs[GETARG_A(i)].to_bool()) {
                    pc += GETARG_sBx(i);
                    JUMP;
                }
                NEXT;
            }

            CASE(OP_JMPNOT) {
                /* A sBx  if R(A) pc+=sBx */
                if (!regs[GETARG_A(i)].to_bool()) {
                    pc += GETARG_sBx(i);
                    JUMP;
                }
                NEXT;
            }

            CASE(OP_ONERR) {
                /* sBx    pc+=sBx on exception */
                if (m_ctx->m_rsize <= m_ctx->m_ci->ridx) {
                    if (m_ctx->m_rsize == 0)
                        m_ctx->m_rsize = 16;
                    else
                        m_ctx->m_rsize *= 2;
                    m_ctx->rescue = (mrb_code **)gc()._realloc(m_ctx->rescue, sizeof(mrb_code*) * m_ctx->m_rsize);
                }
                m_ctx->rescue[m_ctx->m_ci->ridx++] = pc + GETARG_sBx(i);
                NEXT;
            }

            CASE(OP_RESCUE) {
                /* A      R(A) := exc; clear(exc) */
                regs[GETARG_A(i)] = mrb_value::wrap(m_exc);
                m_exc = 0;
                NEXT;
            }

            CASE(OP_POPERR) {
                int a = GETARG_A(i);
                assert(a>=0);
                m_ctx->m_ci->ridx-=a;
                NEXT;
            }

            CASE(OP_RAISE) {
                /* A      raise(R(A)) */
                m_exc = regs[GETARG_A(i)].object_ptr();
                goto L_RAISE;
            }

            CASE(OP_EPUSH) {
                /* Bx     ensure_push(SEQ[Bx]) */
                RProc *p = RProc::new_closure(this, irep->reps[GETARG_Bx(i)]);
                /* push ensure_stack */
                if (m_ctx->m_esize <= m_ctx->m_ci->eidx) {
                    if (m_ctx->m_esize == 0)
                        m_ctx->m_esize = 16;
                    else
                        m_ctx->m_esize *= 2;
                    m_ctx->m_ensure = (RProc **)gc()._realloc(m_ctx->m_ensure, sizeof(RProc*) * m_ctx->m_esize);
                }
                m_ctx->m_ensure[m_ctx->m_ci->eidx++] = p;
                gc().arena_restore(ai);
                NEXT;
            }

            CASE(OP_EPOP) {
                /* A      A.times{ensure_pop().call} */
                int a = GETARG_A(i);
                mrb_callinfo *ci = m_ctx->m_ci;
                int eidx = ci->eidx;

                for (int n=0; n<a && eidx > ci[-1].eidx; n++) {
                    ecall(this,--eidx);
                    gc().arena_restore(ai);
                }
                NEXT;
            }

            CASE(OP_LOADNIL) {
                /* A B    R(A) := nil */
                regs[GETARG_A(i)] = mrb_value::nil();
                NEXT;
            }

            CASE(OP_SENDB) {
                /* fall through */
            };

L_SEND:
            CASE(OP_SEND) {
                /* A B C  R(A) := call(R(A),Sym(B),R(A+1),... ,R(A+C-1)) */
                int a = GETARG_A(i);
                int n = GETARG_C(i);

                mrb_sym mid = syms[GETARG_B(i)];

                mrb_value recv =regs[a];
                auto t=recv.tt;
                recv.tt=t;
                if (GET_OPCODE(i) != OP_SENDB) {
                    if (n == CALL_MAXARGS) {
                        regs[a+2] = mrb_value::nil();
                    }
                    else {
                        regs[a+n+1]= mrb_value::nil();
                    }
                }

                RClass *c = RClass::mrb_class(this, recv);
                RProc *m = RClass::method_search_vm(&c, mid);
                if (!m) {
                    m = prepare_method_missing(c,mid,a,n,regs);
                }

                /* push callinfo */
                mrb_callinfo *_ci = cipush(this);
                _ci->mid = mid;
                _ci->proc = m;
                _ci->stackent = m_ctx->m_stack;
                _ci->argc = n;
                if (n == CALL_MAXARGS) {
                    _ci->argc = -1;
                }
                _ci->target_class = c;
                if (c->tt == MRB_TT_ICLASS) {
                    _ci->target_class = c->c;
                }
                _ci->pc = pc + 1;
                _ci->acc = a;

                /* prepare stack */
                m_ctx->m_stack += a;

                if (m->is_cfunc()) {
                    if (n == CALL_MAXARGS) {
                        _ci->nregs = 3;
                    }
                    else {
                        _ci->nregs = n + 2;
                    }
                    mrb_value result = m->call_cfunc(recv);
                    m_ctx->m_stack[0] = result;
                    gc().arena_restore(ai);
                    if (m_exc)
                        goto L_RAISE;
                    /* pop stackpos */
                    _ci = m_ctx->m_ci;
                    if (!_ci[-1].proc->is_cfunc()) {
                        proc = _ci[-1].proc;
                        irep = proc->ireps();
                        pool = irep->pool;
                        syms = irep->syms;
                    }
                    regs = m_ctx->m_stack = m_ctx->m_ci->stackent;
                    pc = _ci->pc;
                    cipop(this);
                    JUMP;
                }
                else {
                    /* setup environment for calling method */
                    proc = m_ctx->m_ci->proc = m;
                    irep = m->ireps();
                    pool = irep->pool;
                    syms = irep->syms;
                    _ci->nregs = irep->nregs;
                    call_stack_sizing(this,_ci,irep);
                    regs = m_ctx->m_stack;
                    pc = irep->iseq;
                    JUMP;
                }
            }

            CASE(OP_FSEND) {
                /* A B C  R(A) := fcall(R(A),Sym(B),R(A+1),... ,R(A+C)) */
                NEXT;
            }

            CASE(OP_CALL) {
                /* A      R(A) := self.call(frame.argc, frame.argv) */
                mrb_value recv = m_ctx->m_stack[0];
                RProc *m = recv.ptr<RProc>();

                /* replace callinfo */
                mrb_callinfo *ci = m_ctx->m_ci;
                ci->target_class = m->m_target_class;
                ci->proc = m;
                if (m->env) {
                    if (m->env->mid) {
                        ci->mid = m->env->mid;
                    }
                    if (!m->env->stack) {
                        m->env->stack = m_ctx->m_stack;
                    }
                }

                /* prepare stack */
                if (m->is_cfunc()) {
                    recv = m->call_cfunc(recv);
                    gc().arena_restore(ai);
                    if (m_exc)
                        goto L_RAISE;
                    /* pop stackpos */
                    regs = m_ctx->m_stack = m_ctx->m_ci->stackent;
                    regs[m_ctx->m_ci->acc] = recv;
                    pc = m_ctx->m_ci->pc;
                    cipop(this);
                    irep = m_ctx->m_ci->proc->ireps();
                }
                else {
                    /* setup environment for calling method */
                    proc = m;
                    irep = m->ireps();
                    if (!irep) {
                        m_ctx->m_stack[0] = mrb_value::nil();
                        goto L_RETURN;
                    }
                    ci->nregs = irep->nregs;
                    call_stack_sizing(this,ci,irep);
                    regs = m_ctx->m_stack;
                    regs[0] = m->env->stack[0];
                    pc = m->ireps()->iseq;
                }
                pool = irep->pool;
                syms = irep->syms;
                JUMP;
            }

            CASE(OP_SUPER) {
                /* A B C  R(A) := super(R(A+1),... ,R(A+C-1)) */
                mrb_callinfo *ci = m_ctx->m_ci;
                mrb_sym mid = ci->mid;
                int a = GETARG_A(i);
                int n = GETARG_C(i);

                mrb_value recv = regs[0];
                RClass *c = m_ctx->m_ci->target_class->super;
                RProc *m = RClass::method_search_vm(&c, mid);
                if (!m) {
                    m = prepare_method_missing(c,ci->mid,a,n,regs);
                }

                /* push callinfo */
                ci = cipush(this);
                ci->mid = mid;
                ci->proc = m;
                ci->stackent = m_ctx->m_stack;
                ci->argc = n;
                if (n == CALL_MAXARGS) {
                    ci->argc = -1;
                }
                ci->target_class = c;
                ci->pc = pc + 1;

                /* prepare stack */
                m_ctx->m_stack += a;
                m_ctx->m_stack[0] = recv;

                if (m->is_cfunc()) {
                    m_ctx->m_stack[0] = m->call_cfunc(recv);
                    gc().arena_restore(ai);
                    if (m_exc)
                        goto L_RAISE;
                    /* pop stackpos */
                    regs = m_ctx->m_stack = m_ctx->m_ci->stackent;
                    cipop(this);
                    NEXT;
                }
                else {
                    /* fill callinfo */
                    ci->acc = a;

                    /* setup environment for calling method */
                    ci->proc = m;
                    irep = m->ireps();
                    pool = irep->pool;
                    syms = irep->syms;
                    ci->nregs = irep->nregs;
                    call_stack_sizing(this,ci,irep);
                    regs = m_ctx->m_stack;
                    pc = irep->iseq;
                    JUMP;
                }
            }

            CASE(OP_ARGARY) {
                /* A Bx   R(A) := argument array (16=6:1:5:4) */
                int a = GETARG_A(i);
                int bx = GETARG_Bx(i);
                int m1 = (bx>>10)&0x3f;
                int r  = (bx>>9)&0x1;
                int m2 = (bx>>4)&0x1f;
                int lv = (bx>>0)&0xf;
                mrb_value *stack = regs + 1;

                if (lv != 0) {
                    REnv *e = uvenv(this, lv-1);
                    if (!e) {
                        mrb_value exc;
                        static const char m[] = "super called outside of method";
                        exc = mrb_exc_new(I_NOMETHOD_ERROR, m, sizeof(m) - 1);
                        m_exc = exc.object_ptr();
                        goto L_RAISE;
                    }
                    stack = e->stack + 1;
                }
                if (r == 0) {
                    regs[a] = mrb_value::wrap(RArray::new_from_values(this, m1+m2, stack));
                }
                else {
                    mrb_value *pp = nullptr;
                    int len = 0;

                    if (stack[m1].is_array()) {
                        RArray *ary = mrb_ary_ptr(stack[m1]);

                        pp = ary->m_ptr;
                        len = ary->m_len;
                    }

                    RArray *rest = RArray::create(this, m1+len+m2);
                    stack_copy(rest->m_ptr, stack, m1);
                    if (len > 0) {
                        stack_copy(rest->m_ptr+m1, pp, len);
                    }
                    if (m2 > 0) {
                        stack_copy(rest->m_ptr+m1+len, stack+m1+1, m2);
                    }
                    rest->m_len = m1+len+m2;
                    regs[a] = rest->wrap();
                }
                regs[a+1] = stack[m1+r+m2];
                gc().arena_restore(ai);
                NEXT;
            }

            CASE(OP_ENTER) {
                /* Ax             arg setup according to flags (24=5:5:1:5:5:1:1) */
                /* number of optional arguments times OP_JMP should follow */
                mrb_aspec ax = GETARG_Ax(i);
                int m1 = (ax>>18)&0x1f;
                int o  = (ax>>13)&0x1f;
                int r  = (ax>>12)&0x1;
                int m2 = (ax>>7)&0x1f;
                /* unused
              int k  = (ax>>2)&0x1f;
              int kd = (ax>>1)&0x1;
              int b  = (ax>>0)& 0x1;
              */
                int argc = m_ctx->m_ci->argc;
                mrb_value *argv = regs+1;
                mrb_value *argv0 = argv;
                int len = m1 + o + r + m2;
                mrb_value *blk = &argv[argc < 0 ? 1 : argc];

                if (argc < 0) {
                    RArray *ary = mrb_ary_ptr(regs[1]);
                    argv = ary->m_ptr;
                    argc = ary->m_len;
                    mrb_gc_protect(this, regs[1]);
                }
                if (m_ctx->m_ci->proc && MRB_PROC_STRICT_P(m_ctx->m_ci->proc)) {
                    if (argc >= 0) {
                        if (argc < m1 + m2 || (r == 0 && argc > len)) {
                            argnum_error(this, m1+m2);
                            goto L_RAISE;
                        }
                    }
                }
                else if (len > 1 && argc == 1 && argv[0].is_array()) {
                    argc = mrb_ary_ptr(argv[0])->m_len;
                    argv = mrb_ary_ptr(argv[0])->m_ptr;
                }
                m_ctx->m_ci->argc = len;
                if (argc < len) {
                    regs[len+1] = *blk; /* move block */
                    if (argv0 != argv) {
                        value_move(&regs[1], argv, argc-m2); /* m1 + o */
                    }
                    if (m2) {
                        int mlen = m2;
                        if (argc-m2 <= m1) {
                            mlen = argc - m1;
                        }
                        value_move(&regs[len-m2+1], &argv[argc-mlen], mlen);
                    }
                    if (r) {
                        regs[m1+o+1] = RArray::create(this)->wrap();
                    }
                    pc++;
                    if (o != 0)
                        pc += argc - m1 - m2;
                }
                else {
                    if (argv0 != argv) {
                        regs[len+1] = *blk; /* move block */
                        value_move(&regs[1], argv, m1+o);
                    }
                    if (r) {                  /* r */
                        regs[m1+o+1] = RArray::new_from_values(this, argc-m1-o-m2, argv+m1+o)->wrap();
                    }
                    if (m2) {
                        if (argc-m2 > m1)
                            value_move(&regs[m1+o+r+1], &argv[argc-m2], m2);
                    }
                    if (argv0 == argv) {
                        regs[len+1] = *blk; /* move block */
                    }
                    pc += o + 1;
                }
                JUMP;
            }

            CASE(OP_KARG) {
                /* A B C          R(A) := kdict[Sym(B)]; if C kdict.rm(Sym(B)) */
                /* if C == 2; raise unless kdict.empty? */
                /* OP_JMP should follow to skip init code */
                NEXT;
            }

            CASE(OP_KDICT) {
                /* A C            R(A) := kdict */
                NEXT;
            }

L_RETURN:
            i = MKOP_AB(OP_RETURN, GETARG_A(i), OP_R_NORMAL);
            /* fall through */
            CASE(OP_RETURN) {
                /* A      return R(A) */
                if (m_exc) {
                    mrb_callinfo *_ci;
                    int eidx;

L_RAISE:
                    _ci = m_ctx->m_ci;
                    m_exc->iv_ifnone(mrb_intern(this, "lastpc", 6), mrb_cptr_value(pc));
                    m_exc->iv_ifnone(mrb_intern(this, "ciidx", 5), mrb_fixnum_value(_ci - m_ctx->cibase));
                    eidx = _ci->eidx;
                    if (_ci == m_ctx->cibase) {
                        if (_ci->ridx == 0) goto L_STOP;
                        goto L_RESCUE;
                    }
                    while (eidx > _ci[-1].eidx) {
                        ecall(this, --eidx);
                    }
                    while (_ci[0].ridx == _ci[-1].ridx) {
                        cipop(this);
                        _ci = m_ctx->m_ci;
                        m_ctx->m_stack = _ci[1].stackent;
                        if (_ci[1].acc == CI_ACC_SKIP && prev_jmp) {
                            this->jmp = prev_jmp;
                            MRB_THROW(prev_jmp);
                        }
                        if (_ci > this->m_ctx->cibase) {
                            while (eidx > _ci[-1].eidx) {
                                ecall(this, --eidx);
                            }
                        } else  if (_ci == m_ctx->cibase) {
                            if (_ci->ridx == 0) {
                                regs = m_ctx->m_stack = m_ctx->m_stbase;
                                // exception in fiber should make resume to raise in parent fiber context
                                if (m_ctx == root_c) {
                                    regs = m_ctx->m_stack = m_ctx->m_stbase;
                                    goto L_STOP;
                                }
                                else {
                                    mrb_context *c = m_ctx;

                                    m_ctx = c->prev;
                                    c->prev = NULL;
                                    goto L_RAISE;
                                }
                            }
                            break;
                        }
                    }
L_RESCUE:
                    irep = _ci->proc->ireps();
                    pool = irep->pool;
                    syms = irep->syms;
                    regs = m_ctx->m_stack = _ci[1].stackent;
                    pc = m_ctx->rescue[--_ci->ridx];
                }
                else {
                    mrb_callinfo *ci = m_ctx->m_ci;
                    int acc, eidx = m_ctx->m_ci->eidx;
                    mrb_value v = regs[GETARG_A(i)];

                    switch (GETARG_B(i)) {
                    case OP_R_RETURN:
                        // Fall through to OP_R_NORMAL otherwise
                        if (proc->env && !MRB_PROC_STRICT_P(proc)) {
                            REnv *e = top_env(this, proc);

                            if (e->cioff < 0) {
                                localjump_error(this, LOCALJUMP_ERROR_RETURN);
                                goto L_RAISE;
                            }
                            ci = m_ctx->cibase + e->cioff;
                            if (ci == m_ctx->cibase) {
                                localjump_error(this, LOCALJUMP_ERROR_RETURN);
                                goto L_RAISE;
                            }
                            m_ctx->m_ci = ci;
                            break;
                        }
                    case OP_R_NORMAL:
                        if (ci == m_ctx->cibase) {
                            if (!m_ctx->prev) { /* toplevel return */
                                localjump_error(this, LOCALJUMP_ERROR_RETURN);
                                goto L_RAISE;
                            }
                            if (m_ctx->prev->m_ci == m_ctx->prev->cibase) {
                                mrb_value exc = mrb_exc_new_str(A_RUNTIME_ERROR(this),
                                                                mrb_str_new_lit(this, "double resume"));
                                m_exc = exc.object_ptr();
                                goto L_RAISE;
                            }
                            /* automatic yield at the end */
                            m_ctx->status = MRB_FIBER_TERMINATED;
                            m_ctx = m_ctx->prev;
                            m_ctx->status = MRB_FIBER_RUNNING;
                        }
                        ci = m_ctx->m_ci;
                        break;
                    case OP_R_BREAK:
                        if (!proc->env || proc->env->cioff < 0) {
                            localjump_error(this, LOCALJUMP_ERROR_BREAK);
                            goto L_RAISE;
                        }
                        /* break from fiber block */
                        if (m_ctx->m_ci == m_ctx->cibase && m_ctx->m_ci->pc) {
                            mrb_context *c = m_ctx;
                            m_ctx = c->prev;
                            c->prev = NULL;
                        }
                        ci = m_ctx->m_ci = m_ctx->cibase + proc->env->cioff + 1;
                        break;
                    default:
                        /* cannot happen */
                        break;
                    }
                    while (eidx > m_ctx->m_ci[-1].eidx) {
                        ecall(this, --eidx);
                    }
                    cipop(this);
                    acc = ci->acc;
                    pc = ci->pc;
                    regs = m_ctx->m_stack = ci->stackent;
                    if (acc == CI_ACC_SKIP) {
                        this->jmp = prev_jmp;
                        return v;
                    }
                    DEBUG(printf("from :%s\n", mrb_sym2name(this, ci->mid)));
                    proc = m_ctx->m_ci->proc;
                    irep = proc->ireps();
                    pool = irep->pool;
                    syms = irep->syms;

                    regs[acc] = v;
                }
                JUMP;
            }

            CASE(OP_TAILCALL) {
                /* A B C  return call(R(A),Sym(B),R(A+1),... ,R(A+C-1)) */
                int a = GETARG_A(i);
                int n = GETARG_C(i);

                mrb_sym mid = syms[GETARG_B(i)];
                mrb_value recv = regs[a];
                RClass *c = RClass::mrb_class(this, recv);
                RProc *m = RClass::method_search_vm(&c, mid);
                if (!m) {
                    m = prepare_method_missing(c,mid,a,n,regs);
                }

                /* replace callinfo */
                mrb_callinfo *_ci = m_ctx->m_ci;
                _ci->mid = mid;
                _ci->target_class = c;
                _ci->argc = (n == CALL_MAXARGS) ? -1 : n;
                /* move stack */
                value_move(m_ctx->m_stack, &regs[a], _ci->argc+1);

                if (m->is_cfunc()) {
                    m_ctx->m_stack[0] = m->call_cfunc(recv);
                    gc().arena_restore(ai);
                    goto L_RETURN;
                }
                else {
                    /* setup environment for calling method */
                    irep = m->ireps();
                    pool = irep->pool;
                    syms = irep->syms;
                    call_stack_sizing(this,_ci,irep);
                    regs = m_ctx->m_stack;
                    pc = irep->iseq;
                }
                JUMP;
            }

            CASE(OP_BLKPUSH) {
                /* A Bx   R(A) := block (16=6:1:5:4) */
                int a = GETARG_A(i);
                int bx = GETARG_Bx(i);
                int m1 = (bx>>10)&0x3f;
                int r  = (bx>>9)&0x1;
                int m2 = (bx>>4)&0x1f;
                int lv = (bx>>0)&0xf;
                mrb_value *stack = regs + 1;

                if (lv != 0) {
                    REnv *e = uvenv(this, lv-1);
                    if (!e) {
                        localjump_error(this, LOCALJUMP_ERROR_YIELD);
                        goto L_RAISE;
                    }
                    stack = e->stack + 1;
                }
                regs[a] = stack[m1+r+m2];
                NEXT;
            }

#define attr_i value.i
#ifdef MRB_NAN_BOXING
#define attr_f f
#elif defined(MRB_WORD_BOXING)
#define attr_f value.fp->f
#else
#define attr_f value.f
#endif
#define SAME_SIGN(a,b) ((((a)<0) ^ ((b)<0)) == 0)
#define DIFFERENT_SIGN(a,b) (((a)<0) != ((b)<0))
#define TYPES2(a,b) ((((uint16_t)(a))<<8)|(((uint16_t)(b))&0xff))
#define OP_MATH_BODY(op,v1,v2) do {\
    regs[a].v1 = regs[a].v1 op regs[a+1].v2;\
        } while(0)

            CASE(OP_ADD) {
                /* A B C  R(A) := R(A)+R(A+1) (Syms[B]=:+,C=1)*/
                int a = GETARG_A(i);
                mrb_value &regs_a(regs[a]);

                /* need to check if op is overridden */
                switch (TYPES2(mrb_type(regs_a),mrb_type(regs[a+1]))) {
                case TYPES2(MRB_TT_FIXNUM,MRB_TT_FIXNUM):
                {
                    mrb_int x, y;
                    const mrb_value &regs_b(regs[a + 1]);
                    x = mrb_fixnum(regs_a);
                    y = mrb_fixnum(regs_b);

                    if ( ((x^y) | (((x^(~(x^y) & std::numeric_limits<mrb_int>::min())) + y)^y)) >= 0) {
                        regs_a = mrb_float_value((mrb_float)x + (mrb_float)y);
                    } else {
                        regs_a.value.i =  x+y;
                    }
                }
                    break;
                case TYPES2(MRB_TT_FIXNUM,MRB_TT_FLOAT):
                {
                    mrb_int x = mrb_fixnum(regs_a);
                    mrb_float y = mrb_float(regs[a+1]);
                    regs_a = mrb_float_value((mrb_float)x + y); //SET_FLT_VALUE(regs_a, (mrb_float)x + y);
                }
                    break;
                case TYPES2(MRB_TT_FLOAT,MRB_TT_FIXNUM):
#ifdef MRB_WORD_BOXING
                {
                    mrb_float x = mrb_float(regs[a]);
                    mrb_int y = mrb_fixnum(regs[a+1]);
                    SET_FLT_VALUE(mrb, regs[a], x + y);
                }
#else
                    OP_MATH_BODY(+,attr_f,value.i);
#endif
                    break;
                case TYPES2(MRB_TT_FLOAT,MRB_TT_FLOAT):
#ifdef MRB_WORD_BOXING
                {
                    mrb_float x = mrb_float(regs[a]);
                    mrb_float y = mrb_float(regs[a+1]);
                    SET_FLT_VALUE(mrb, regs[a], x + y);
                }
#else
                    OP_MATH_BODY(+,attr_f,attr_f);
#endif
                    break;
                case TYPES2(MRB_TT_STRING,MRB_TT_STRING):
                    regs_a = mrb_str_plus(this, regs_a, regs[a+1]);
                    break;
                default:
                    goto L_SEND;
                }
                gc().arena_restore(ai);
                NEXT;
            }

            CASE(OP_SUB) {
                /* A B C  R(A) := R(A)-R(A+1) (Syms[B]=:-,C=1)*/
                int a = GETARG_A(i);

                /* need to check if op is overridden */
                switch (TYPES2(mrb_type(regs[a]),mrb_type(regs[a+1]))) {
                case TYPES2(MRB_TT_FIXNUM,MRB_TT_FIXNUM):
                {
                    mrb_int x, y;
                    x = mrb_fixnum(regs[a]);
                    y = mrb_fixnum(regs[a+1]);
                    if (((x^y) & (((x ^ ((x^y)
                                         & (1 << (sizeof(mrb_int)*8-1))))-y)^y)) < 0) {
                        /* integer overflow */
                        regs[a] = mrb_float_value((mrb_float)x - (mrb_float)y); // SET_FLT_VALUE(regs[a], (mrb_float)x - (mrb_float)y);
                    }
                    else {
                        regs[a]=mrb_fixnum_value(x-y);
                    }
                }
                    break;
                case TYPES2(MRB_TT_FIXNUM,MRB_TT_FLOAT):
                {
                    mrb_int x = mrb_fixnum(regs[a]);
                    mrb_float y = mrb_float(regs[a+1]);
                    regs[a] = mrb_float_value((mrb_float)x - y); // SET_FLT_VALUE(regs[a], (mrb_float)x - y);

                }
                    break;
                case TYPES2(MRB_TT_FLOAT,MRB_TT_FIXNUM):
#ifdef MRB_WORD_BOXING
                {
                    mrb_float x = mrb_float(regs[a]);
                    mrb_int y = mrb_fixnum(regs[a+1]);
                    SET_FLT_VALUE(mrb, regs[a], x - y);
                }
#else
                    OP_MATH_BODY(-,attr_f,value.i);
#endif
                    break;
                case TYPES2(MRB_TT_FLOAT,MRB_TT_FLOAT):
#ifdef MRB_WORD_BOXING
                {
                    mrb_float x = mrb_float(regs[a]);
                    mrb_float y = mrb_float(regs[a+1]);
                    SET_FLT_VALUE(mrb, regs[a], x - y);
                }
#else
                    OP_MATH_BODY(-,attr_f,attr_f);
#endif
                    break;
                default:
                    goto L_SEND;
                }
                NEXT;
            }

            CASE(OP_MUL) {
                /* A B C  R(A) := R(A)*R(A+1) (Syms[B]=:*,C=1)*/
                int a = GETARG_A(i);

                /* need to check if op is overridden */
                switch (TYPES2(mrb_type(regs[a]),mrb_type(regs[a+1]))) {
                case TYPES2(MRB_TT_FIXNUM,MRB_TT_FIXNUM):
                {
                    mrb_int x, y;
                    static_assert(
                                sizeof(long long) >= 2 * sizeof(mrb_int),
                                "Unable to detect overflow after multiplication"
                                );
                    x = mrb_fixnum(regs[a]);
                    y = mrb_fixnum(regs[a+1]);
                    long long z = x * y;
                    if ( (z > std::numeric_limits<mrb_int>::max()) || (z < std::numeric_limits<mrb_int>::min()) ) {
                        regs[a] = mrb_float_value(z); // SET_FLT_VALUE(regs[a], (mrb_float)z);
                    }
                    else {
                        regs[a]=mrb_fixnum_value(z);
                    }
                }
                    break;
                case TYPES2(MRB_TT_FIXNUM,MRB_TT_FLOAT):
                {
                    mrb_int x = mrb_fixnum(regs[a]);
                    mrb_float y = mrb_float(regs[a+1]);
                    regs[a] = mrb_float_value((mrb_float)x * y); //SET_FLT_VALUE(regs[a], );
                }
                    break;
                case TYPES2(MRB_TT_FLOAT,MRB_TT_FIXNUM):
#ifdef MRB_WORD_BOXING
                {
                    mrb_float x = mrb_float(regs[a]);
                    mrb_int y = mrb_fixnum(regs[a+1]);
                    SET_FLT_VALUE(mrb, regs[a], x * y);
                }
#else
                    OP_MATH_BODY(*,attr_f,attr_i);
#endif
                    break;
                case TYPES2(MRB_TT_FLOAT,MRB_TT_FLOAT):
#ifdef MRB_WORD_BOXING
                {
                    mrb_float x = mrb_float(regs[a]);
                    mrb_float y = mrb_float(regs[a+1]);
                    SET_FLT_VALUE(mrb, regs[a], x * y);
                }
#else
                    OP_MATH_BODY(*,attr_f,attr_f);
#endif
                    break;
                default:
                    goto L_SEND;
                }
                NEXT;
            }

            CASE(OP_DIV) {
                /* A B C  R(A) := R(A)/R(A+1) (Syms[B]=:/,C=1)*/
                int a = GETARG_A(i);
                mrb_value &regs_a(regs[a]);

                /* need to check if op is overridden */
                switch (TYPES2(mrb_type(regs_a),mrb_type(regs[a+1]))) {
                case TYPES2(MRB_TT_FIXNUM,MRB_TT_FIXNUM):
                {
                    mrb_int x = mrb_fixnum(regs_a);
                    mrb_int y = mrb_fixnum(regs[a+1]);

                    /* Initialize sl1 and sl2 */

                    if ( (y == 0) || ( (x == std::numeric_limits<mrb_int>::min()) && (y == -1) ) ) {
                        regs_a = mrb_float_value((mrb_float)x / (mrb_float)y); // SET_FLT_VALUE(regs_a, (mrb_float)x / (mrb_float)y);
                    }
                    else {
                        regs_a.value.i = x / y;
                    }
                }
                    break;
                case TYPES2(MRB_TT_FIXNUM,MRB_TT_FLOAT):
                {
                    mrb_int x = mrb_fixnum(regs[a]);
                    mrb_float y = mrb_float(regs[a+1]);
                    regs[a] = mrb_float_value((mrb_float)x / y); //SET_FLT_VALUE(regs[a], (mrb_float)x / y);
                }
                    break;
                case TYPES2(MRB_TT_FLOAT,MRB_TT_FIXNUM):
#ifdef MRB_WORD_BOXING
                {
                    mrb_float x = mrb_float(regs[a]);
                    mrb_int y = mrb_fixnum(regs[a+1]);
                    SET_FLT_VALUE(mrb, regs[a], x / y);
                }
#else
                    OP_MATH_BODY(/,attr_f,attr_i);
#endif
                    break;
                case TYPES2(MRB_TT_FLOAT,MRB_TT_FLOAT):
#ifdef MRB_WORD_BOXING
                {
                    mrb_float x = mrb_float(regs[a]);
                    mrb_float y = mrb_float(regs[a+1]);
                    SET_FLT_VALUE(mrb, regs[a], x / y);
                }
#else
                    OP_MATH_BODY(/,attr_f,attr_f);
#endif
                    break;
                default:
                    goto L_SEND;
                }
                NEXT;
            }

            CASE(OP_ADDI) {
                /* A B C  R(A) := R(A)+C (Syms[B]=:+)*/
                int a = GETARG_A(i);

                /* need to check if + is overridden */
                switch (mrb_type(regs[a])) {
                case MRB_TT_FIXNUM:
                {
                    mrb_value &regs_a(regs[a]);
                    mrb_int x = mrb_fixnum(regs_a);
                    mrb_int y = GETARG_C(i);

                    if ( ((x^y) | (((x^(~(x^y) & std::numeric_limits<mrb_int>::min())) + y)^y)) >= 0) {
                        regs_a = mrb_float_value((mrb_float)x + (mrb_float)y); //SET_FLT_VALUE(regs_a, (mrb_float)x + (mrb_float)y);
                    } else {
                        regs_a.value.i = x+y;
                    }
                }
                    break;
                case MRB_TT_FLOAT:
#ifdef MRB_WORD_BOXING
                {
                    mrb_float x = mrb_float(regs[a]);
                    SET_FLT_VALUE(mrb, regs[a], x + GETARG_C(i));
                }
#else
                    regs[a].attr_f += GETARG_C(i);
#endif
                    break;
                default:
                    regs[a+1]=mrb_fixnum_value(GETARG_C(i));
                    i = MKOP_ABC(OP_SEND, a, GETARG_B(i), 1);
                    goto L_SEND;
                }
                NEXT;
            }

            CASE(OP_SUBI) {
                /* A B C  R(A) := R(A)-C (Syms[B]=:-)*/
                int a = GETARG_A(i);
                mrb_value *regs_a = regs + a;

                /* need to check if + is overridden */
                switch (mrb_type(regs_a[0])) {
                case MRB_TT_FIXNUM:
                {
                    mrb_int x = mrb_fixnum(regs_a[0]);
                    mrb_int y = GETARG_C(i);
                    if (((x^y) & (((x ^ ((x^y)
                                         & (1 << (sizeof(mrb_int)*8-1))))-y)^y)) < 0) {
                        /* integer overflow */
                        *regs_a = mrb_float_value((mrb_float)x - (mrb_float)y); //SET_FLT_VALUE(regs[a], (mrb_float)x - (mrb_float)y);
                    }
                    else {
                        regs[a]=mrb_fixnum_value(x-y);
                    }
                }
                    break;
                case MRB_TT_FLOAT:
#ifdef MRB_WORD_BOXING
                {
                    mrb_float x = mrb_float(regs[a]);
                    SET_FLT_VALUE(mrb, regs[a], x - GETARG_C(i));
                }
#else
                    regs_a[0].attr_f -= GETARG_C(i);
#endif
                    break;
                default:
                    regs_a[1] = mrb_fixnum_value(GETARG_C(i));
                    i = MKOP_ABC(OP_SEND, a, GETARG_B(i), 1);
                    goto L_SEND;
                }
                NEXT;
            }

#define OP_CMP_BODY(op,v1,v2) do {\
    if (regs[a].v1 op regs[a+1].v2) {\
    regs[a]=mrb_true_value();\
        }\
    else {\
    regs[a]=mrb_value::_false();\
        }\
        } while(0)

#define OP_CMP(op) do {\
    int a = GETARG_A(i);\
    /* need to check if - is overridden */\
    switch (TYPES2(mrb_type(regs[a]),mrb_type(regs[a+1]))) {\
    case TYPES2(MRB_TT_FIXNUM,MRB_TT_FIXNUM):\
    OP_CMP_BODY(op,attr_i,attr_i);\
    break;\
    case TYPES2(MRB_TT_FIXNUM,MRB_TT_FLOAT):\
    OP_CMP_BODY(op,attr_i,attr_f);\
    break;\
    case TYPES2(MRB_TT_FLOAT,MRB_TT_FIXNUM):\
    OP_CMP_BODY(op,attr_f,attr_i);\
    break;\
    case TYPES2(MRB_TT_FLOAT,MRB_TT_FLOAT):\
    OP_CMP_BODY(op,attr_f,attr_f);\
    break;\
    default:\
    goto L_SEND;\
        }\
        } while (0)

            CASE(OP_EQ) {
                /* A B C  R(A) := R(A)<R(A+1) (Syms[B]=:==,C=1)*/
                int a = GETARG_A(i);
                if (mrb_obj_eq(regs[a], regs[a+1])) {
                    regs[a] = mrb_true_value();
                }
                else {
                    OP_CMP(==);
                }
                NEXT;
            }

            CASE(OP_LT) {
                /* A B C  R(A) := R(A)<R(A+1) (Syms[B]=:<,C=1)*/
                OP_CMP(<);
                NEXT;
            }

            CASE(OP_LE) {
                /* A B C  R(A) := R(A)<=R(A+1) (Syms[B]=:<=,C=1)*/
                OP_CMP(<=);
                NEXT;
            }

            CASE(OP_GT) {
                /* A B C  R(A) := R(A)<R(A+1) (Syms[B]=:>,C=1)*/
                OP_CMP(>);
                NEXT;
            }

            CASE(OP_GE) {
                /* A B C  R(A) := R(A)<R(A+1) (Syms[B]=:>=,C=1)*/
                OP_CMP(>=);
                NEXT;
            }

            CASE(OP_ARRAY) {
                /* A B C          R(A) := ary_new(R(B),R(B+1)..R(B+C)) */
                regs[GETARG_A(i)] = mrb_value::wrap(RArray::new_from_values(this, GETARG_C(i), &regs[GETARG_B(i)]));
                gc().arena_restore(ai);
                NEXT;
            }

            CASE(OP_ARYCAT) {
                /* A B            mrb_ary_concat(R(A),R(B)) */
                mrb_ary_ptr(regs[GETARG_A(i)])->concat(RArray::splat(this, regs[GETARG_B(i)]));
                gc().arena_restore(ai);
                NEXT;
            }

            CASE(OP_ARYPUSH) {
                /* A B            R(A).push(R(B)) */
                mrb_ary_ptr(regs[GETARG_A(i)])->push(regs[GETARG_B(i)]);
                NEXT;
            }

            CASE(OP_AREF) {
                /* A B C          R(A) := R(B)[C] */
                int a = GETARG_A(i);
                int c = GETARG_C(i);
                mrb_value v = regs[GETARG_B(i)];

                if (!v.is_array()) {
                    if (c == 0) {
                        regs[GETARG_A(i)] = v;
                    }
                    else {
                        regs[a] = mrb_value::nil();
                    }
                }
                else {
                    regs[GETARG_A(i)] = RARRAY(v)->ref(c);
                }
                NEXT;
            }

            CASE(OP_ASET) {
                /* A B C          R(B)[C] := R(A) */
                RARRAY(regs[GETARG_B(i)])->set(GETARG_C(i), regs[GETARG_A(i)]);
                NEXT;
            }

            CASE(OP_APOST) {
                /* A B C  *R(A),R(A+1)..R(A+C) := R(A) */
                int a = GETARG_A(i);
                mrb_value v = regs[a];
                int pre  = GETARG_B(i);
                int post = GETARG_C(i);

                if (!v.is_array()) {
                    regs[a++] = RArray::create(this)->wrap();
                    while (post--) {
                        regs[a] = mrb_value::nil();
                        a++;
                    }
                }
                else {
                    RArray *ary = mrb_ary_ptr(v);
                    int len = ary->m_len;

                    if (len > pre + post) {
                        regs[a++] = RArray::new_from_values(this, len - pre - post, ary->m_ptr+pre)->wrap();
                        while (post--) {
                            regs[a++] = ary->m_ptr[len-post-1];
                        }
                    }
                    else {
                        regs[a++] = RArray::create(this)->wrap();
                        int i;
                        for (i=0; i+pre<len; i++) {
                            regs[a+i] = ary->m_ptr[pre+i];
                        }
                        while (i < post) {
                            regs[a+i] = mrb_value::nil();
                            i++;
                        }
                    }
                }
                gc().arena_restore(ai);
                NEXT;
            }

            CASE(OP_STRING) {
                /* A Bx           R(A) := str_new(Lit(Bx)) */
                regs[GETARG_A(i)] = pool[GETARG_Bx(i)].ptr<RString>()->dup()->wrap();
                gc().arena_restore(ai);
                NEXT;
            }

            CASE(OP_STRCAT) {
                /* A B    R(A).concat(R(B)) */
                mrb_str_concat(this, regs[GETARG_A(i)], regs[GETARG_B(i)]);
                NEXT;
            }

            CASE(OP_HASH) {
                /* A B C   R(A) := hash_new(R(B),R(B+1)..R(B+C)) */
                int b = GETARG_B(i);
                int c = GETARG_C(i);
                int lim = b+c*2;
                RHash *_hash = RHash::new_capa(this, c);

                while (b < lim) {
                    _hash->set(regs[b], regs[b+1]);
                    b+=2;
                }
                regs[GETARG_A(i)] = mrb_value::wrap(_hash);
                gc().arena_restore(ai);
                NEXT;
            }

            CASE(OP_LAMBDA) {
                /* A b c  R(A) := lambda(SEQ[b],c) (b:c = 14:2) */
                RProc *p;
                int c = GETARG_c(i);

                if (c & OP_L_CAPTURE) {
                    p = RProc::new_closure(this, irep->reps[GETARG_b(i)]);
                }
                else {
                    p = RProc::create(this, irep->reps[GETARG_b(i)]);
                }
                if (c & OP_L_STRICT)
                    p->flags |= MRB_PROC_STRICT;
                regs[GETARG_A(i)] = mrb_value::wrap(p);
                gc().arena_restore(ai);
                NEXT;
            }

            CASE(OP_OCLASS) {
                /* A      R(A) := ::Object */
                regs[GETARG_A(i)] = mrb_value::wrap(this->object_class);
                NEXT;
            }

            CASE(OP_CLASS) {
                /* A B    R(A) := newclass(R(A),Sym(B),R(A+1)) */
                int a = GETARG_A(i);
                mrb_sym id = syms[GETARG_B(i)];

                mrb_value base  = regs[a];
                mrb_value super = regs[a+1];
                if (base.is_nil()) {
                    base = mrb_value::wrap(m_ctx->m_ci->target_class);
                }
                RClass *c = mrb_vm_define_class(base, super, id);
                regs[a] = mrb_value::wrap(c);
                gc().arena_restore(ai);
                NEXT;
            }

            CASE(OP_MODULE) {
                /* A B            R(A) := newmodule(R(A),Sym(B)) */
                int a = GETARG_A(i);
                mrb_sym id = syms[GETARG_B(i)];
                mrb_value base = regs[a];
                if (base.is_nil()) {
                    base = mrb_value::wrap(m_ctx->m_ci->target_class);
                }
                RClass *c = base.ptr<RClass>()->define_module_under(id);
                regs[a] = mrb_value::wrap(c);
                gc().arena_restore(ai);
                NEXT;
            }

            CASE(OP_EXEC) {
                /* A Bx   R(A) := blockexec(R(A),SEQ[Bx]) */
                int a = GETARG_A(i);
                mrb_value recv = regs[a];

                /* prepare stack */
                mrb_callinfo *ci = cipush(this);
                ci->pc = pc + 1;
                ci->acc = a;
                ci->mid = 0;
                ci->stackent = m_ctx->m_stack;
                ci->argc = 0;
                ci->target_class = mrb_class_ptr(recv);

                /* prepare stack */
                m_ctx->m_stack += a;

                RProc *p = RProc::create(this, irep->reps[GETARG_Bx(i)]);
                p->m_target_class = ci->target_class;
                ci->proc = p;

                if (p->is_cfunc()) {
                    m_ctx->m_stack[0] = p->call_cfunc(recv);
                    gc().arena_restore(ai);
                    if (m_exc)
                        goto L_RAISE;
                    /* pop stackpos */
                    regs = m_ctx->m_stack = m_ctx->m_ci->stackent;
                    cipop(this);
                    NEXT;
                }
                else {
                    irep = p->ireps();
                    pool = irep->pool;
                    syms = irep->syms;
                    stack_extend(this, irep->nregs, 1);
                    ci->nregs = irep->nregs;
                    regs = m_ctx->m_stack;
                    pc = irep->iseq;
                    JUMP;
                }
            }

            CASE(OP_METHOD) {
                /* A B            R(A).newmethod(Sym(B),R(A+1)) */
                int a = GETARG_A(i);
                RClass *c = mrb_class_ptr(regs[a]);
                c->define_method_vm(syms[GETARG_B(i)], regs[a+1]);
                gc().arena_restore(ai);
                NEXT;
            }

            CASE(OP_SCLASS) {
                /* A B    R(A) := R(B).singleton_class */
                regs[GETARG_A(i)] = mrb_singleton_class(this, regs[GETARG_B(i)]);
                gc().arena_restore(ai);
                NEXT;
            }

            CASE(OP_TCLASS) {
                /* A B    R(A) := target_class */
                if (!m_ctx->m_ci->target_class) {
                    static const char msg[] = "no target class or module";
                    mrb_value exc = mrb_exc_new(I_TYPE_ERROR, msg, sizeof(msg) - 1);
                    m_exc = exc.object_ptr();
                    goto L_RAISE;
                }
                regs[GETARG_A(i)] = mrb_value::wrap(m_ctx->m_ci->target_class);
                NEXT;
            }

            CASE(OP_RANGE) {
                /* A B C  R(A) := range_new(R(B),R(B+1),C) */
                int b = GETARG_B(i);
                regs[GETARG_A(i)] = mrb_range_new(this, regs[b], regs[b+1], GETARG_C(i));
                gc().arena_restore(ai);
                NEXT;
            }

            CASE(OP_DEBUG) {
                /* A      debug print R(A),R(B),R(C) */
#ifdef ENABLE_DEBUG
                this->debug_op_hook(this, irep, pc, regs);
#else
#ifdef ENABLE_STDIO
                printf("OP_DEBUG %d %d %d\n", GETARG_A(i), GETARG_B(i), GETARG_C(i));
#else
                abort();
#endif
#endif
                NEXT;
            }

            CASE(OP_STOP) {
                /*        stop VM */
L_STOP:
                {
                    int n = m_ctx->m_ci->eidx;

                    while (n--) {
                        ecall(this, n);
                    }
                }
                ERR_PC_CLR(this);
                this->jmp = prev_jmp;
                if (m_exc) {
                    return mrb_value::wrap(m_exc);
                }
                return regs[irep->nlocals];
            }

            CASE(OP_ERR) {
                /* Bx     raise RuntimeError with message Lit(Bx) */
                RString *msg = pool[GETARG_Bx(i)].ptr<RString>()->dup();
                RClass *excep_class = A_RUNTIME_ERROR(this);

                if (GETARG_A(i) != 0) {
                    excep_class = I_LOCALJUMP_ERROR;
                }
                m_exc = mrb_exc_new_str(excep_class, msg).object_ptr();
                goto L_RAISE;
            }
        }
        END_DISPATCH;
    }
    MRB_CATCH(&c_jmp) {
        exc_catched = true;
        goto RETRY_TRY_BLOCK;
    }
    MRB_END_EXC(&c_jmp);
}
mrb_value mrb_state::mrb_run(RProc *proc, mrb_value self)
{
    return mrb_context_run(proc, self, m_ctx->m_ci->argc + 2); /* argc + 2 (receiver and block) */
}

mrb_value mrb_toplevel_run(mrb_state *mrb, RProc *proc)
{
    mrb_callinfo *ci;
    mrb_value v;

    if (!mrb->m_ctx->cibase || mrb->m_ctx->m_ci == mrb->m_ctx->cibase) {
        return mrb->mrb_context_run(proc, mrb_top_self(mrb), 0);
    }
    ci = cipush(mrb);
    ci->acc = CI_ACC_SKIP;
    ci->eidx = 0;
    ci->ridx = 0;
    ci->target_class = mrb->object_class;
    v = mrb->mrb_context_run(proc, mrb_top_self(mrb), 0);
    cipop(mrb);

    return v;
}
