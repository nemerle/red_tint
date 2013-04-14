/*
** codegen.c - mruby code generator
**
** See Copyright Notice in mruby.h
*/

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdexcept>
#include "mruby.h"
#include "mruby/compile.h"
#include "mruby/irep.h"
#include "mruby/numeric.h"
#include "mruby/string.h"
#include "mruby/node.h"
#include "mruby/NodeVisitor.h"
#include "opcode.h"
#include "re.h"
#include <algorithm>
typedef mrb_ast_node node;
typedef struct mrb_parser_state parser_state;

enum looptype {
    LOOP_NORMAL,
    LOOP_BLOCK,
    LOOP_FOR,
    LOOP_BEGIN,
    LOOP_RESCUE
};

struct loopinfo {
    enum looptype type;
    int pc1, pc2, pc3, acc;
    int ensure_level;
    loopinfo *prev;
};

struct codegen_scope : public NodeVisitor {
    mrb_state *m_mrb = nullptr;
    mrb_pool *m_mpool  = nullptr;
    jmp_buf jmp;

    codegen_scope *m_prev= nullptr;

    tLocals m_locals;
    int m_sp=0;
    int m_pc=0;
    int m_lastlabel=0;
    int m_ainfo:15;
    mrb_bool m_mscope:1;

    loopinfo *m_loop = nullptr;
    int m_ensure_level =0;
    const char *m_filename = nullptr;
    short m_lineno = 0;

    mrb_code *m_iseq = nullptr;
    short *lines = nullptr;
    int m_icapa = 0;

    mrb_irep *m_irep = nullptr;
    int m_pcapa = 0;
    int m_scapa = 0;

    int m_nlocals=0;
    int m_nregs=0;
    int m_ai=0;

    int m_idx=0;
    std::vector<bool> m_val_stack;
    void finish();

public:
    codegen_scope() {
        m_ainfo = 0;
        m_mscope = false;
    }
    static codegen_scope *create(mrb_state *m_mrb, codegen_scope *m_prev, node *lv);
    static codegen_scope *create(mrb_state *mrb, codegen_scope *prev, const tLocals &lv);

    void genop(mrb_code i)
    {
        if (m_pc == m_icapa) {
            m_icapa *= 2;
            m_iseq = (mrb_code *)s_realloc(m_iseq, sizeof(mrb_code)*m_icapa);
            if (lines) {
                lines = (short*)s_realloc(lines, sizeof(short)*m_icapa);
            }
        }
        m_iseq[m_pc] = i;
        if (lines) {
            lines[m_pc] = m_lineno;
        }
        m_pc++;
    }
    void dispatch(int _pc)
    {
        int diff = m_pc - _pc;
        mrb_code i = m_iseq[_pc];
        int c = GET_OPCODE(i);

        m_lastlabel = m_pc;
        switch (c) {
            case OP_JMP:
            case OP_JMPIF:
            case OP_JMPNOT:
            case OP_ONERR:
                break;
            default:
#ifdef ENABLE_STDIO
                fprintf(stderr, "bug: dispatch on non JMP op\n");
#endif
                scope_error();
                break;
        }
        m_iseq[_pc] = MKOP_AsBx(c, GETARG_A(i), diff);
    }

    void error(const char *message);
    void *palloc(size_t len);
    //void *s_malloc(size_t len);
    void *s_realloc(void *p, size_t len);
    int new_label();
    void genop_peep(mrb_code i, int val);
    void push_();
    void pop_sp(int v=1) {
        m_sp-=v;
    }
    int new_lit(mrb_value val);
    int gen_values(node *t, int val);
    int new_msym(mrb_sym sym);
    int new_sym(mrb_sym sym);
    int lv_idx(mrb_sym id);
    void for_body(node *tree);
    int codegen(node *tree);
    mrb_sym attrsym(mrb_sym a);
    void    gen_call(CallNode *tree, mrb_sym name, int m_sp, int val);
    void    gen_assignment(mrb_ast_node *node, int m_sp, int val);
    void    loop_pop(int val);
    void    gen_vmassignment(node *tree, int rhs, int val);
    void    gen_send_intern();
    void    gen_literal_array(node *tree, int sym, int val);
    void    raise_error(const char *msg);
    mrb_int readint_mrb_int(const char *p, int base, int neg, int *overflow);
    double  readint_float(const char *p, int base);
    void    codegen(node *tree, bool val);
    loopinfo *loop_push(looptype t);
    void    dispatch_linked(int m_pc);
    void    loop_break(node *tree);
    void    gen_lvar_assignment(int sp, LVarNode *node, int val);
    void    gen_colon2_assignment(int sp, Colon2Node *node, int val);
    void    gen_call_assignment(int val, int sp, CallNode *node);
protected:
    void do_lambda_body(mrb_ast_node *body);
    void do_lambda_args(ArgsStore * args);
    int do_for_body(ForNode * tree);
    int do_lambda_internal(LambdaCommonNode *tree);
    int do_lambda_internal(DefCommonNode * tree);
    int do_for_body(node *tree);
    void scope_error();
    void walk_string(mrb_ast_node *n);
    void visit(DxstrNode *n);
    void visit(CallCommonNode *node);
    void visit(CallNode *n);
    void visit(FCallNode *n);
    int  visit(ScopeNode *n);
    void visit(PostExeNode *n);
    void visit(SelfNode *sn);
    void visit(WordsNode *n);
    void visit(SymbolsNode *n);
    void visit(NilNode *n);
    void visit(TrueNode *n);
    void visit(FalseNode *n);
    void visit(RetryNode *nn);
    void visit(RedoNode *nn);
    void visit(EnsureNode *nn);
    void visit(NegateNode *nn);
    void visit(StrNode *sn);
    void visit(XstrNode *xn);
    void visit(RegxNode *xn);
    void visit(DregxNode *dn);
    void visit(BeginNode *n);
    void visit(LambdaNode *node);
    void visit(BlockNode *node);
    void visit(ModuleNode *nd);
    void visit(ClassNode * nd);
    void visit(SclassNode * nd);
    void visit(CaseNode * n);
    void visit(RescueNode * node);
    void visit(ForNode * n);
    void visit(SdefNode * n);
    void visit(DefNode * n);
    void visit(OpAsgnNode * node);
    void visit(IfNode * node);
    void visit(HeredocNode *node);
    void visit(DsymNode *node);
    void visit(DstrNode *node);
    void visit(AsgnNode *node);
    void visit(SymNode *node);
    void visit(Colon2Node *node);
    void visit(AndNode *node);
    void visit(OrNode *node);
    void visit(Dot3Node *node);
    void visit(Dot2Node *node);
    void visit(YieldNode *node);
    void visit(UntilNode *n);
    void visit(WhileNode *n);
    void visit(AliasNode *n);
    void visit(SplatNode *node);
    void visit(MAsgnNode *node);
    void visit(UndefNode * n);
    void visit(HashNode * node);
    void visit(SuperNode * node);
    void visit(Colon3Node * node);
    void visit(ReturnNode * node);
    void visit(NextNode * node);
    void visit(ArrayNode * node);
    void visit(NthRefNode * n);
    void visit(BackRefNode * n);
    void visit(ZsuperNode * n);
    void visit(IntLiteralNode * n);
    void visit(FloatLiteralNode * n);
    void visit(ConstNode * n);
    void visit(IVarNode * n);
    void visit(CVarNode * n);
    void visit(GVarNode * n);
    void visit(LVarNode * n);
    void visit(BlockArgNode * n);
    void visit(BreakNode * n);
    void visit(ArgNode *n);
    void visit(LiteralDelimNode *n);
protected:
    bool m_negate=false; // flag set by negate node, if set next float/int will be negated
};

void codegen_scope::error(const char *message)
{
    if (!this)
        return;
    codegen_scope *p=this;
    while (p->m_prev) {
        p->m_mpool->mrb_pool_close();
        p = p->m_prev;
    }
    p->m_mpool->mrb_pool_close();
#ifdef ENABLE_STDIO
    if (p->m_filename && p->m_lineno) {
        fprintf(stderr, "codegen error:%s:%d: %s\n", p->m_filename, p->m_lineno, message);
    }
    else {
        fprintf(stderr, "codegen error: %s\n", message);
    }
#endif
    longjmp(p->jmp, 1);
}

void* codegen_scope::palloc(size_t len)
{
    void *p = m_mpool->mrb_pool_alloc(len);

    if (!p)
        error("pool memory allocation");
    return p;
}

void* codegen_scope::s_realloc(void *p, size_t len)
{
    p = m_mrb->gc()._realloc(p, len);
    if (!p && len > 0)
        error("mrb_realloc");
    return p;
}

int codegen_scope::new_label()
{
    m_lastlabel = m_pc;
    return m_pc;
}

void codegen_scope::genop_peep(mrb_code i, int val)
{
    /* peephole optimization */
    if (m_lastlabel != m_pc && m_pc > 0) {
        mrb_code i0 = m_iseq[m_pc-1];
        int c1 = GET_OPCODE(i);
        int c0 = GET_OPCODE(i0);

        switch (c1) {
            case OP_MOVE:
                if (GETARG_A(i) == GETARG_B(i)) {
                    /* skip useless OP_MOVE */
                    return;
                }
                if (val)
                    break;
                switch (c0) {
                    case OP_MOVE:
                        if (GETARG_B(i) == GETARG_A(i0) && GETARG_A(i) == GETARG_B(i0) && GETARG_A(i) >= m_nlocals) {
                            /* skip swapping OP_MOVE */
                            return;
                        }
                        if (GETARG_B(i) == GETARG_A(i0) && GETARG_A(i0) >= m_nlocals) {
                            m_iseq[m_pc-1] = MKOP_AB(OP_MOVE, GETARG_A(i), GETARG_B(i0));
                            return;
                        }
                        break;
                    case OP_LOADI:
                        if (GETARG_B(i) == GETARG_A(i0) && GETARG_A(i0) >= m_nlocals) {
                            m_iseq[m_pc-1] = MKOP_AsBx(OP_LOADI, GETARG_A(i), GETARG_sBx(i0));
                            return;
                        }
                        break;
                    case OP_ARRAY:
                    case OP_HASH:
                    case OP_RANGE:
                    case OP_AREF:
                    case OP_GETUPVAR:
                        if (GETARG_B(i) == GETARG_A(i0) && GETARG_A(i0) >= m_nlocals) {
                            m_iseq[m_pc-1] = MKOP_ABC(c0, GETARG_A(i), GETARG_B(i0), GETARG_C(i0));
                            return;
                        }
                        break;
                    case OP_LOADSYM:
                    case OP_GETGLOBAL:
                    case OP_GETIV:
                    case OP_GETCV:
                    case OP_GETCONST:
                    case OP_GETSPECIAL:
                    case OP_LOADL:
                    case OP_STRING:
                        if (GETARG_B(i) == GETARG_A(i0) && GETARG_A(i0) >= m_nlocals) {
                            m_iseq[m_pc-1] = MKOP_ABx(c0, GETARG_A(i), GETARG_Bx(i0));
                            return;
                        }
                        break;
                    case OP_SCLASS:
                        if (GETARG_B(i) == GETARG_A(i0) && GETARG_A(i0) >= m_nlocals) {
                            m_iseq[m_pc-1] = MKOP_AB(c0, GETARG_A(i), GETARG_B(i0));
                            return;
                        }
                        break;
                    case OP_LOADNIL:
                    case OP_LOADSELF:
                    case OP_LOADT:
                    case OP_LOADF:
                    case OP_OCLASS:
                        if (GETARG_B(i) == GETARG_A(i0) && GETARG_A(i0) >= m_nlocals) {
                            m_iseq[m_pc-1] = MKOP_A(c0, GETARG_A(i));
                            return;
                        }
                        break;
                    default:
                        break;
                }
                break;
            case OP_SETIV:
            case OP_SETCV:
            case OP_SETCONST:
            case OP_SETMCNST:
            case OP_SETGLOBAL:
                if (val) break;
                if (c0 == OP_MOVE) {
                    if (GETARG_A(i) == GETARG_A(i0)) {
                        m_iseq[m_pc-1] = MKOP_ABx(c1, GETARG_B(i0), GETARG_Bx(i));
                        return;
                    }
                }
                break;
            case OP_SETUPVAR:
                if (val) break;
                if (c0 == OP_MOVE) {
                    if (GETARG_A(i) == GETARG_A(i0)) {
                        m_iseq[m_pc-1] = MKOP_ABC(c1, GETARG_B(i0), GETARG_B(i), GETARG_C(i));
                        return;
                    }
                }
                break;
            case OP_EPOP:
                if (c0 == OP_EPOP) {
                    m_iseq[m_pc-1] = MKOP_A(OP_EPOP, GETARG_A(i0)+GETARG_A(i));
                    return;
                }
                break;
            case OP_POPERR:
                if (c0 == OP_POPERR) {
                    m_iseq[m_pc-1] = MKOP_A(OP_POPERR, GETARG_A(i0)+GETARG_A(i));
                    return;
                }
                break;
            case OP_RETURN:
                switch (c0) {
                    case OP_RETURN:
                        return;
                    case OP_MOVE:
                        m_iseq[m_pc-1] = MKOP_AB(OP_RETURN, GETARG_B(i0), OP_R_NORMAL);
                        return;
                    case OP_LOADI:
                        m_iseq[m_pc-1] = MKOP_AsBx(OP_LOADI, 0, GETARG_sBx(i0));
                        genop(MKOP_AB(OP_RETURN, 0, OP_R_NORMAL));
                        return;
                    case OP_ARRAY:
                    case OP_HASH:
                    case OP_RANGE:
                    case OP_AREF:
                    case OP_GETUPVAR:
                        m_iseq[m_pc-1] = MKOP_ABC(c0, 0, GETARG_B(i0), GETARG_C(i0));
                        genop(MKOP_AB(OP_RETURN, 0, OP_R_NORMAL));
                        return;
                    case OP_SETIV:
                    case OP_SETCV:
                    case OP_SETCONST:
                    case OP_SETMCNST:
                    case OP_SETUPVAR:
                    case OP_SETGLOBAL:
                        m_pc--;
                        genop_peep(i0, false);
                        i0 = m_iseq[m_pc-1];
                        genop(MKOP_AB(OP_RETURN, GETARG_A(i0), OP_R_NORMAL));
                        return;
                    case OP_LOADSYM:
                    case OP_GETGLOBAL:
                    case OP_GETIV:
                    case OP_GETCV:
                    case OP_GETCONST:
                    case OP_GETSPECIAL:
                    case OP_LOADL:
                    case OP_STRING:
                        m_iseq[m_pc-1] = MKOP_ABx(c0, 0, GETARG_Bx(i0));
                        genop(MKOP_AB(OP_RETURN, 0, OP_R_NORMAL));
                        return;
                    case OP_SCLASS:
                        m_iseq[m_pc-1] = MKOP_AB(c0, GETARG_A(i), GETARG_B(i0));
                        genop(MKOP_AB(OP_RETURN, 0, OP_R_NORMAL));
                        return;
                    case OP_LOADNIL:
                    case OP_LOADSELF:
                    case OP_LOADT:
                    case OP_LOADF:
                    case OP_OCLASS:
                        m_iseq[m_pc-1] = MKOP_A(c0, 0);
                        genop(MKOP_AB(OP_RETURN, 0, OP_R_NORMAL));
                        return;
#if 0
                    case OP_SEND:
                        if (GETARG_B(i) == OP_R_NORMAL && GETARG_A(i) == GETARG_A(i0)) {
                            m_iseq[m_pc-1] = MKOP_ABC(OP_TAILCALL, GETARG_A(i0), GETARG_B(i0), GETARG_C(i0));
                            return;
                        }
                    break;
#endif
                    default:
                        break;
                }
                break;
            case OP_ADD:
            case OP_SUB:
                if (c0 == OP_LOADI) {
                    int c = GETARG_sBx(i0);

                    if (c1 == OP_SUB) c = -c;
                    if (c > 127 || c < -127) break;
                    if (0 <= c)
                        m_iseq[m_pc-1] = MKOP_ABC(OP_ADDI, GETARG_A(i), GETARG_B(i), c);
                    else
                        m_iseq[m_pc-1] = MKOP_ABC(OP_SUBI, GETARG_A(i), GETARG_B(i), -c);
                    return;
                }
            case OP_STRCAT:
                if (c0 == OP_STRING) {
                    int i = GETARG_Bx(i0);

                    if (mrb_type(m_irep->pool[i]) == MRB_TT_STRING &&
                            RSTRING_LEN(m_irep->pool[i]) == 0) {
                        m_pc--;
                        return;
                    }
                }
                break;
            default:
                break;
        }
    }
    genop(i);
}

void codegen_scope::scope_error()
{
    throw std::logic_error("Scope Error!");
}

void codegen_scope::dispatch_linked(int pc)
{
    if (!pc)
        return;
    for (;;) {
        mrb_code i = m_iseq[pc];
        int pos = GETARG_sBx(i);
        dispatch(pc);
        if (!pos)
            break;
        pc = pos;
    }
}

//#define nregs_update do {if (s->sp > s->nregs) s->nregs = s->sp;} while (0)
void codegen_scope::push_()
{
    if (m_sp > 511) {
        error( "too complex expression");
    }
    m_sp++;
    do {
        if (m_sp > m_nregs)
            m_nregs = m_sp;
    } while (0);
    //nregs_update;
}


//#define s->push_() push_(s)
#define pop_(s) ((s)->sp--)
#define pop() pop_(s)
#define pop_n(n) (s->sp-=(n))
#define cursp() (s->sp)

int codegen_scope::new_lit(mrb_value val)
{
    int i;


    switch (mrb_type(val)) {
        case MRB_TT_STRING:
            for (i=0; i<m_irep->plen; i++) {
                mrb_value pv = m_irep->pool[i];
                mrb_int len;

                if (mrb_type(pv) != MRB_TT_STRING) continue;
                if ((len = RSTRING_LEN(pv)) != RSTRING_LEN(val)) continue;
                if (memcmp(RSTRING_PTR(pv), RSTRING_PTR(val), len) == 0)
                    return i;
            }
            break;
        case MRB_TT_FLOAT:
        default:
            for (i=0; i<m_irep->plen; i++) {
                if (mrb_obj_equal(m_mrb, m_irep->pool[i], val))
                    return i;
            }
            break;
    }

    if (m_irep->plen == m_pcapa) {
        m_pcapa *= 2;
        m_irep->pool = (mrb_value *)s_realloc(m_irep->pool, sizeof(mrb_value)*m_pcapa);
    }
    m_irep->pool[m_irep->plen] = val;
    i = m_irep->plen++;

    return i;
}

inline int codegen_scope::new_msym(mrb_sym sym)
{
    int i, len;

    len = m_irep->slen;
    if (len > 256) len = 256;
    for (i=0; i<len; i++) {
        if (m_irep->syms[i] == sym) return i;
        if (m_irep->syms[i] == 0) break;
    }
    if (i == 256) {
        error("too many symbols (max 256)");
    }
    m_irep->syms[i] = sym;
    if (i == m_irep->slen)
        m_irep->slen++;
    return i;
}

inline int codegen_scope::new_sym(mrb_sym sym)
{
    int i;

    for (i=0; i<m_irep->slen; i++) {
        if (m_irep->syms[i] == sym)
            return i;
    }
    if (m_irep->slen > 125 && m_irep->slen < 256) {
        m_irep->syms = (mrb_sym *)s_realloc(m_irep->syms, sizeof(mrb_sym)*65536);
        for (i = 0; i < 256 - m_irep->slen; i++) {
            static const mrb_sym mrb_sym_zero = { 0 };
            m_irep->syms[i + m_irep->slen] = mrb_sym_zero;
        }
        m_irep->slen = 256;
    }
    m_irep->syms[m_irep->slen] = sym;
    return m_irep->slen++;
}

static int node_len(node *tree)
{
    int n = 0;

    while (tree) {
        n++;
        tree = tree->right();
    }
    return n;
}
static mrb_sym sym(mrb_ast_node *x) { return ((mrb_sym)(intptr_t)(x));}

//#define sym(x) ((mrb_sym)(intptr_t)(x))
#define lv_name(lv) sym((lv)->left())
int codegen_scope::lv_idx(mrb_sym id)
{
    auto res = std::find(m_locals.begin(),m_locals.end(),id);
    if(res==m_locals.end())
        return 0;
    return 1+(res-m_locals.begin());
}
int codegen_scope::do_for_body(ForNode * tree)
{
    int _idx = m_idx;

    loopinfo *lp = loop_push( LOOP_FOR);
    lp->pc1 = new_label();

    // generate loop variable
    node *n2 = tree->var();
    if (n2->left() && !n2->left()->right() && !n2->right()) {
        genop( MKOP_Ax(OP_ENTER, 0x40000));
        gen_assignment(n2->left()->left(), 1, false);
    }
    else {
        genop( MKOP_Ax(OP_ENTER, 0x40000));
        gen_vmassignment( n2, 1, true);
    }
    codegen(tree->body(), true);
    pop_sp();
    if (m_pc > 0) {
        mrb_code c = m_iseq[m_pc-1];
        if (GET_OPCODE(c) != OP_RETURN || GETARG_B(c) != OP_R_NORMAL || m_pc == m_lastlabel)
            genop_peep(MKOP_AB(OP_RETURN, m_sp, OP_R_NORMAL), false);
    }
    loop_pop(false);
    finish();

    return _idx;
}

int codegen_scope::do_for_body(node *tree)
{
    int _idx = m_idx;

    loopinfo *lp = loop_push( LOOP_FOR);
    lp->pc1 = new_label();

    // generate loop variable
    node *n2 = tree->left();
    if (n2->left() && !n2->left()->right() && !n2->right()) {
        genop( MKOP_Ax(OP_ENTER, 0x40000));
        gen_assignment(n2->left()->left(), 1, false);
    }
    else {
        genop( MKOP_Ax(OP_ENTER, 0x40000));
        gen_vmassignment( n2, 1, true);
    }
    codegen(tree->right()->right()->left(), true);
    pop_sp();
    if (m_pc > 0) {
        mrb_code c = m_iseq[m_pc-1];
        if (GET_OPCODE(c) != OP_RETURN || GETARG_B(c) != OP_R_NORMAL || m_pc == m_lastlabel)
            genop_peep(MKOP_AB(OP_RETURN, m_sp, OP_R_NORMAL), false);
    }
    loop_pop(false);
    finish();

    return _idx;
}

void codegen_scope::for_body(node *tree)
{
    int base = m_idx;
    // generate receiver
    codegen(tree->right()->left(), true);
    // generate loop-block
    codegen_scope *s = codegen_scope::create(m_mrb, this, tree->left());
    int idx = s->do_for_body(tree);
    genop( MKOP_Abc(OP_LAMBDA, m_sp, idx - base, OP_L_BLOCK));
    pop_sp();
    idx = new_msym(mrb_intern2(m_mrb, "each", 4));
    genop( MKOP_ABC(OP_SENDB, m_sp, idx, 0));
}
void codegen_scope::do_lambda_args(ArgsStore *args)
{
    int32_t a;
    int ma, oa, ra, pa, ka, kd, ba;
    int pos, i;
    node *n, *opt;

    ma = node_len(args->m_mandatory);
    n = args->m_mandatory;
    while (n) {
        n = n->right();
    }
    oa = node_len(args->m_opt);
    ra = args->m_rest ? 1 : 0;
    pa = node_len(args->m_post_mandatory);
    ka = kd = 0;
    ba = args->m_blk ? 1 : 0;

    a = ((int32_t)(ma & 0x1f) << 18)
            | ((int32_t)(oa & 0x1f) << 13)
            | ((ra & 1) << 12)
            | ((pa & 0x1f) << 7)
            | ((ka & 0x1f) << 2)
            | ((kd & 1)<< 1)
            | (ba & 1);
    m_ainfo = (((ma+oa) & 0x3f) << 6) /* (12bits = 6:1:5) */
            | ((ra & 1) << 5)
            | (pa & 0x1f);
    genop( MKOP_Ax(OP_ENTER, a));
    pos = new_label();
    for (i=0; i<oa; i++) {
        new_label();
        genop( MKOP_sBx(OP_JMP, 0));
    }
    if (oa > 0) {
        genop( MKOP_sBx(OP_JMP, 0));
    }
    opt = args->m_opt;
    i = 0;
    while (opt) {
        dispatch(pos+i);
        codegen(opt->left()->right(), true);
        int idx = lv_idx( (mrb_sym)(intptr_t)opt->left()->left());
        pop_sp();
        genop_peep(MKOP_AB(OP_MOVE, idx, m_sp), false);
        i++;
        opt = opt->right();
    }
    if (oa > 0) {
        dispatch(pos+i);
    }
}

void codegen_scope::do_lambda_body(mrb_ast_node *body)
{
    codegen(body, true);
    pop_sp();
    if (m_pc <= 0)
        return;
    mrb_code c = m_iseq[m_pc-1];
    if (GET_OPCODE(c) != OP_RETURN || GETARG_B(c) != OP_R_NORMAL || m_pc == m_lastlabel) {
        if (m_nregs == 0) {
            genop( MKOP_A(OP_LOADNIL, 0));
            genop( MKOP_AB(OP_RETURN, 0, OP_R_NORMAL));
        }
        else {
            genop_peep(MKOP_AB(OP_RETURN, m_sp, OP_R_NORMAL), false);
        }
    }
}

int codegen_scope::do_lambda_internal(LambdaCommonNode *tree)
{
    int idx = m_idx;
    m_mscope = 0;

    loopinfo *lp = loop_push(LOOP_BLOCK);
    lp->pc1 = new_label();

    if(tree->args())
        do_lambda_args(tree->args());
    do_lambda_body(tree->body());

    loop_pop(false);
    finish();
    return idx;
}
int codegen_scope::do_lambda_internal(DefCommonNode *tree)
{
    int idx = m_idx;
    m_mscope = 1;

    if(tree->args())
        do_lambda_args(tree->args());
    do_lambda_body(tree->body());

    finish();
    return idx;
}

int codegen_scope::codegen(node *tree)
{
    codegen_scope *scope = codegen_scope::create(m_mrb, this, tree->left());
    int idx = scope->m_idx;

    scope->codegen(tree->right(), true);
    if (!m_iseq) {
        scope->genop( MKOP_A(OP_STOP, 0));
    }
    else {
        if (scope->m_nregs == 0) {
            scope->genop( MKOP_A(OP_LOADNIL, 0));
            scope->genop( MKOP_AB(OP_RETURN, 0, OP_R_NORMAL));
        }
        else {
            scope->genop_peep(MKOP_AB(OP_RETURN, scope->m_sp, OP_R_NORMAL), false);
        }
    }
    scope->finish();

    return idx - m_idx;
}
int codegen_scope::visit(ScopeNode *n)
{
    codegen_scope *scope = codegen_scope::create(m_mrb, this, n->locals());
    int idx = scope->m_idx;

    scope->codegen(n->body(), true);
    if (!m_iseq) {
        scope->genop( MKOP_A(OP_STOP, 0));
    }
    else {
        if (scope->m_nregs == 0) {
            scope->genop( MKOP_A(OP_LOADNIL, 0));
            scope->genop( MKOP_AB(OP_RETURN, 0, OP_R_NORMAL));
        }
        else {
            scope->genop_peep(MKOP_AB(OP_RETURN, scope->m_sp, OP_R_NORMAL), false);
        }
    }
    scope->finish();

    return idx - m_idx;
}

static bool nosplat(node *t)
{
    while (t) {
        if (t->left()->getType() == NODE_SPLAT)
            return false;
        t = t->right();
    }
    return true;
}

mrb_sym codegen_scope::attrsym(mrb_sym a)
{
    const char *name;
    size_t len;
    char *name2;

    name = mrb_sym2name_len(m_mrb, a, &len);
    name2 = (char *)palloc(len+1);
    memcpy(name2, name, len);
    name2[len] = '=';
    name2[len+1] = '\0';

    return mrb_intern2(m_mrb, name2, len+1);
}

int codegen_scope::gen_values(node *t, int val)
{
    int n = 0;

    while (t) {
        if (n >= 127 || t->left()->getType() == NODE_SPLAT) { // splat mode
            if (val) {
                pop_sp(n);
                genop( MKOP_ABC(OP_ARRAY, m_sp, m_sp, n));
                push_();
                codegen(t->left(), true);
                pop_sp(2);
                genop( MKOP_AB(OP_ARYCAT, m_sp, m_sp+1));
                t = t->right();
                while (t) {
                    push_();
                    codegen(t->left(), true);
                    pop_sp(2);
                    if (t->left()->getType() == NODE_SPLAT) {
                        genop( MKOP_AB(OP_ARYCAT, m_sp, m_sp+1));
                    }
                    else {
                        genop( MKOP_AB(OP_ARYPUSH, m_sp, m_sp+1));
                    }
                    t = t->right();
                }
            }
            else {
                codegen(t->left()->right(), false);
                t = t->right();
                while (t) {
                    codegen(t->left(), false);
                    t = t->right();
                }
            }
            return -1;
        }
        // normal (no splat) mode
        codegen(t->left(), val);
        n++;
        t = t->right();
    }
    return n;
}

#define CALL_MAXARGS 127

void codegen_scope::gen_call(CallNode *node, mrb_sym name, int sp, int val)
{
    mrb_sym _sym = name ? name : node->m_method;
    int idx;
    int n = 0, noop = 0, sendv = 0, blk = 0;

    codegen(node->m_receiver, true); /* receiver */
    idx = new_msym(_sym);
    CommandArgs *tree = node->m_cmd_args;
    if (tree) {
        n = gen_values(tree->m_args, true);
        if (n < 0) {
            n = noop = sendv = 1;
            push_();
        }
    }
    if (sp) {
        if (sendv) {
            pop_sp();
            genop( MKOP_AB(OP_ARYPUSH, m_sp, sp));
            push_();
        }
        else {
            genop( MKOP_AB(OP_MOVE, m_sp, sp));
            push_();
            n++;
        }
    }
    if (tree && tree->m_blk) {
        noop = 1;
        codegen(tree->m_blk, true);
        pop_sp();
    }
    else {
        blk = m_sp;
    }
    pop_sp(n+1);
    {
        size_t len;
        eOpEnum op = OP_LAST;
        const char *name = mrb_sym2name_len(m_mrb, _sym, &len);
        bool was_peep = false;
        if(!noop) {
            if (len == 1 && name[0] == '+')  {
                was_peep = true;
                op = OP_ADD;
            }
            else if (len == 1 && name[0] == '-')  {
                was_peep = true;
                op = OP_SUB;
            }
            else if (len == 1 && name[0] == '*')  {
                op = OP_MUL;
            }
            else if (len == 1 && name[0] == '/')  {
                op = OP_DIV;
            }
            else if (len == 1 && name[0] == '<')  {
                op = OP_LT;
            }
            else if (len == 2 && name[0] == '<' && name[1] == '=')  {
                op = OP_LE;
            }
            else if (len == 1 && name[0] == '>')  {
                op = OP_GT;
            }
            else if (len == 2 && name[0] == '>' && name[1] == '=')  {
                op = OP_GE;
            }
            else if (len == 2 && name[0] == '=' && name[1] == '=')  {
                op = OP_EQ;
            }
        }
        if(op == OP_LAST) {
            if (sendv)
                n = CALL_MAXARGS;
            op = OP_SENDB;
            if (blk > 0) {                   /* no block */
                op = OP_SEND;
            }
        }
        mrb_code cd = MKOP_ABC(op, m_sp, idx, n);
        if(was_peep)
            genop_peep(cd,val);
        else
            genop( cd );

    }
    if (val) {
        push_();
    }
}

void codegen_scope::gen_lvar_assignment(int sp, LVarNode *node, int val)
{
    int idx = lv_idx(node->sym());
    if (idx > 0) {
        if (idx != sp) {
            genop_peep(MKOP_AB(OP_MOVE, idx, sp), val);
        }
        return;
    }
    /* upvar */
    int lv = 0;
    codegen_scope *up = m_prev;

    while (up) {
        idx = up->lv_idx(node->sym());
        if (idx > 0) {
            genop_peep(MKOP_ABC(OP_SETUPVAR, sp, idx, lv), val);
            break;
        }
        lv++;
        up = up->m_prev;
    }
}

void codegen_scope::gen_colon2_assignment(int sp, Colon2Node *node, int val)
{
    int idx = new_sym(node->m_sym);
    genop_peep(MKOP_AB(OP_MOVE, m_sp, sp), false);
    push_();
    codegen(node->m_val, true);
    pop_sp(2);
    genop_peep(MKOP_ABx(OP_SETMCNST, m_sp, idx), val);
}

void codegen_scope::gen_call_assignment(int val, int sp, CallNode *node)
{
    push_();
    gen_call(node, attrsym(node->m_method), sp, false);
    pop_sp();
    if (val) {
        genop_peep(MKOP_AB(OP_MOVE, m_sp, sp), val);
    }
}

void codegen_scope::gen_assignment(mrb_ast_node *node, int sp, int val)
{
    struct AssignGenerator : public NodeVisitor_Null {
        AssignGenerator(int v,int sp,codegen_scope *sc) : inner_val(v),inner_sp(sp),s(sc) {}
        void visit(IVarNode * n) {  common_assign(OP_SETIV,n->sym()); }
        void visit(CVarNode * n) {  common_assign(OP_SETCV,n->sym()); }
        void visit(GVarNode * n) {  common_assign(OP_SETGLOBAL,n->sym()); }
        void visit(ConstNode * n) { common_assign(OP_SETCONST,n->sym()); }
        void visit(LVarNode * n) {
            s->gen_lvar_assignment(inner_sp, n, inner_val);
        }
        void visit(CallNode * n) {
            s->gen_call_assignment(inner_val, inner_sp, n);
        }
        void visit(Colon2Node * n) {
            s->gen_colon2_assignment(inner_sp, n, inner_val);
        }
    protected:
        void common_assign(eOpEnum op,mrb_sym sym) {
            int idx = s->new_sym( sym );
            s->genop_peep(MKOP_ABx(op, inner_sp, idx), inner_val);
        }
        int inner_val;
        int inner_sp;
        codegen_scope *s;

    };
    AssignGenerator gen { val,sp,this };
    node_type type = node->getType();
    assert(dynamic_cast<UpdatedNode *>(node));
    switch (type) {
        case NODE_GVAR: case NODE_LVAR: case NODE_IVAR: case NODE_CVAR:
        case NODE_CONST: case NODE_COLON2: case NODE_CALL:
            node->accept(&gen);
            break;
        default:
            assert(!"unknown lhs");
#ifdef ENABLE_STDIO
            printf("unknown lhs %d\n", type);
#endif
            break;
    }
    if (val)
        push_();
}

void codegen_scope::gen_vmassignment(node *tree, int rhs, int val)
{
    int n = 0;
    node *t, *p;

    if (tree->left()) {              /* pre */
        t = tree->left();
        n = 0;
        while (t) {
            genop( MKOP_ABC(OP_AREF, m_sp, rhs, n));
            gen_assignment(t->left(), m_sp, false);
            n++;
            t = t->right();
        }
    }
    t = tree->right();
    if (t) {
        int post = 0;
        if (t->right()) {               /* post count */
            p = t->right()->left();
            while (p) {
                post++;
                p = p->right();
            }
        }
        if (val) {
            genop( MKOP_AB(OP_MOVE, m_sp, rhs));
            push_();
        }
        pop_sp();
        genop( MKOP_ABC(OP_APOST, m_sp, n, post));
        n = 1;
        if (t->left()) {               /* rest */
            gen_assignment(t->left(), m_sp, false);
        }
        if (t->right() && t->right()->left()) {
            t = t->right()->left();
            while (t) {
                gen_assignment(t->left(), m_sp+n, false);
                t = t->right();
                n++;
            }
        }
    }
}

void codegen_scope::gen_send_intern()
{
    pop_sp();
    genop(MKOP_ABC(OP_SEND, m_sp, new_msym(mrb_intern2(m_mrb, "intern", 6)), 0));
    push_();
}
void codegen_scope::gen_literal_array(node *tree, int sym, int val)
{
    if (val) {
        int i = 0, j = 0;

        while (tree) {
            switch (tree->left()->getType()) {
                case NODE_STR:
                {
                    StrNode *sn = (StrNode *)tree->left();
                    if ((tree->right() == NULL) && (sn->m_length == 0))
                        break;
                }
                    codegen(tree->left(), true);
                    ++j;
                    break;
                case NODE_BEGIN:
                    codegen(tree->left(), true);
                    ++j;
                    break;

                case NODE_LITERAL_DELIM:
                    if (j > 0) {
                        j = 0;
                        ++i;
                        if (sym)
                            gen_send_intern();
                    }
                    break;
            }
            if (j >= 2) {
                pop_sp(2);
                genop_peep(MKOP_AB(OP_STRCAT, m_sp, m_sp+1), true);
                push_();
                j = 1;
            }
            tree = tree->right();
        }
        if (j > 0) {
            j = 0;
            ++i;
            if (sym)
                gen_send_intern();
        }
        pop_sp(i);
        genop(MKOP_ABC(OP_ARRAY, m_sp, m_sp, i));
        push_();
    }
    else {
        //for_each(tree,[](entry) { if(entry->getType()==NODE_BEGIN...) codegen(entry); })
        while (tree) {
            switch (tree->left()->getType()) {
                case NODE_BEGIN:
                case NODE_BLOCK:
                    codegen(tree->left(), false);
            }
            tree = tree->right();
        }
    }
}

void codegen_scope::raise_error(const char *msg)
{
    int idx = new_lit(mrb_str_new_cstr(m_mrb, msg));

    genop(MKOP_ABx(OP_ERR, 1, idx));
}

double codegen_scope::readint_float(const char *p, int base)
{
    const char *e = p + strlen(p);
    double f = 0;
    int n;

    if (*p == '+') p++;
    while (p < e) {
        char c = *p;
        c = tolower((unsigned char)c);
        for (n=0; n<base; n++) {
            if (mrb_digitmap[n] == c) {
                f *= base;
                f += n;
                break;
            }
        }
        if (n == base) {
            error("malformed readint input");
        }
        p++;
    }
    return f;
}

mrb_int codegen_scope::readint_mrb_int(const char *p, int base, int neg, int *overflow)
{
    const char *e = p + strlen(p);
    mrb_int result = 0;
    int n;

    if (*p == '+') p++;
    while (p < e) {
        char c = *p;
        c = tolower((unsigned char)c);
        for (n=0; n<base; n++) {
            if (mrb_digitmap[n] == c) {
                break;
            }
        }
        if (n == base) {
            error("malformed readint input");
        }

        if (neg) {
            if ((MRB_INT_MIN + n)/base > result) {
                *overflow = TRUE;
                return 0;
            }
            result *= base;
            result -= n;
        }
        else {
            if ((MRB_INT_MAX - n)/base < result) {
                *overflow = TRUE;
                return 0;
            }
            result *= base;
            result += n;
        }
        p++;
    }
    *overflow = FALSE;
    return result;
}
void codegen_scope::visit(ZsuperNode *n) {
    bool val(m_val_stack.back());
    codegen_scope *s2 = this;
    int lv = 0, ainfo = 0;

    push_();        /* room for receiver */
    while (!s2->m_mscope) {
        lv++;
        s2 = s2->m_prev;
        if (!s2)
            break;
    }
    if (s2) ainfo = s2->m_ainfo;
    genop( MKOP_ABx(OP_ARGARY, m_sp, (ainfo<<4)|(lv & 0xf)));
    CommandArgs *z_chld = n->cmd_args;
    if (z_chld && z_chld->m_blk) {
        push_();
        codegen( z_chld->m_blk, true);
        pop_sp(2);
    }
    pop_sp();
    genop( MKOP_ABC(OP_SUPER, m_sp, 0, CALL_MAXARGS));
    if (val)
        push_();
}
void codegen_scope::visit(BackRefNode *n) {
    char buf[2] = { '$' };
    mrb_value str;
    int sym;

    buf[1] = n->m_ref;
    str = mrb_str_new(m_mrb, buf, 2);
    sym = new_sym( mrb_intern_str(m_mrb, str));
    genop( MKOP_ABx(OP_GETGLOBAL, m_sp, sym));
    push_();
}
void codegen_scope::visit(NthRefNode *n) {
    int sym;
    mrb_state *mrb = m_mrb;
    mrb_value fix = mrb_fixnum_value(n->m_ref);
    mrb_value str = mrb_str_buf_new(mrb, 4);

    mrb_str_buf_cat(mrb, str, "$", 1);
    mrb_str_buf_append(mrb, str, mrb_fixnum_to_str(mrb, fix, 10));
    sym = new_sym( mrb_intern_str(mrb, str));
    genop( MKOP_ABx(OP_GETGLOBAL, m_sp, sym));
    push_();
}

void codegen_scope::visit(IntLiteralNode *n) {
    if (!m_val_stack.back())
        return;
    char *p = n->m_val;
    int base = n->m_base;
    mrb_int i;
    mrb_code co;
    int overflow;

    i = readint_mrb_int(p, base, m_negate, &overflow);
    if (overflow) {
        double f = readint_float(p, base);
        if(m_negate)
            f = -f;
        int off = new_lit(mrb_float_value(f));

        genop( MKOP_ABx(OP_LOADL, m_sp, off));
    }
    else {
        if (i < MAXARG_sBx && i > -MAXARG_sBx) {
            co = MKOP_AsBx(OP_LOADI, m_sp, i);
        }
        else {
            int off = new_lit(mrb_fixnum_value(i));
            co = MKOP_ABx(OP_LOADL, m_sp, off);
        }
        genop( co);
    }
    push_();
}
void codegen_scope::visit(ArrayNode *node) {
    bool val = m_val_stack.back();
    int n = gen_values( node->child(), val);
    if (n >= 0) {
        if (val) {
            pop_sp(n);
            genop( MKOP_ABC(OP_ARRAY, m_sp, m_sp, n));
            push_();
        }
    }
    else if (val) {
        push_();
    }
}
void codegen_scope::visit(NextNode *node) {
    bool val = m_val_stack.back();
    if (!m_loop) {
        raise_error("unexpected next");
    }
    else if (m_loop->type == LOOP_NORMAL) {
        if (m_ensure_level > m_loop->ensure_level) {
            genop_peep(MKOP_A(OP_EPOP, m_ensure_level - m_loop->ensure_level), false);
        }
        codegen( node->child(), false);
        genop( MKOP_sBx(OP_JMP, m_loop->pc1 - m_pc));
    }
    else {
        if (node->child()) {
            codegen( node->child(), true);
            pop_sp();
        }
        else {
            genop( MKOP_A(OP_LOADNIL, m_sp));
        }
        genop_peep(MKOP_AB(OP_RETURN, m_sp, OP_R_NORMAL), false);
    }
    if (val)
        push_();
}
void codegen_scope::visit(ReturnNode *node) {
    bool val = m_val_stack.back();

    if (node->child()) {
        codegen( node->child(), true);
        pop_sp();
    }
    else {
        genop( MKOP_A(OP_LOADNIL, m_sp));
    }
    if (m_loop) {
        genop( MKOP_AB(OP_RETURN, m_sp, OP_R_RETURN));
    }
    else {
        genop_peep(MKOP_AB(OP_RETURN, m_sp, OP_R_NORMAL), false);
    }
    if (val)
        push_();
}
void codegen_scope::visit(SuperNode *node)
{
    bool val = m_val_stack.back();
    int n = 0, sendv = 0;

    push_();        /* room for receiver */
    if (node->args()) {
        mrb_ast_node *args = node->args();
        n = gen_values( args, true);
        if (n < 0) {
            n = sendv = 1;
            push_();
        }
    }
    if (node->block()) {
        codegen( node->block(), true);
        pop_sp();
    }
    else {
        genop( MKOP_A(OP_LOADNIL, m_sp));
    }
    pop_sp(n+1);
    if (sendv) n = CALL_MAXARGS;
    genop( MKOP_ABC(OP_SUPER, m_sp, 0, n));
    if (val) push_();
}

void codegen_scope::visit(Colon3Node *node) {
    bool val = m_val_stack.back();
    int sym_ = new_sym( node->sym() );

    genop( MKOP_A(OP_OCLASS, m_sp));
    genop( MKOP_ABx(OP_GETMCNST, m_sp, sym_));
    if (val)
        push_();
}
void codegen_scope::visit(HashNode *node) {
    bool val = m_val_stack.back();

    int len = 0;
    mrb_ast_node *mem = node->child();
    while (mem) {
        codegen( mem->left()->left(), val);
        codegen( mem->left()->right(), val);
        len++;
        mem = mem->right();
    }
    if (val) {
        pop_sp(len*2);
        genop( MKOP_ABC(OP_HASH, m_sp, m_sp, len));
        push_();
    }
}
void codegen_scope::visit(UndefNode *no) {
    bool val = m_val_stack.back();
    int undef = new_msym(mrb_intern2(m_mrb, "undef_method", 12));
    int num = 0;

    genop( MKOP_A(OP_TCLASS, m_sp));
    push_();
    for(auto iter=no->m_syms.begin(); iter!=no->m_syms.end(); ++iter) {
        int symbol = new_msym(*iter);
        genop( MKOP_ABx(OP_LOADSYM, m_sp, symbol));
        push_();
        num++;
    }
    pop_sp(num + 1);
    genop( MKOP_ABC(OP_SEND, m_sp, undef, num));
    if (val) {
        push_();
    }
}
void codegen_scope::visit(MAsgnNode *node) {
    bool val = m_val_stack.back();
    mrb_ast_node *t = node->rhs(), *p;
    int rhs = m_sp;

    ArrayNode *n_arr = dynamic_cast<ArrayNode *>(t);
    if (n_arr && nosplat(n_arr->child())) {
        int n = 0;
        int len = 0;
        // fixed rhs
        t = n_arr->child();
        while (t) {
            codegen( t->left(), true);
            len++;
            t = t->right();
        }
        mrb_ast_node *tree = node->lhs();
        if (tree->left()) {                /* pre */
            t = tree->left();
            n = 0;
            while (t) {
                gen_assignment(t->left(), rhs+n, false);
                n++;
                t = t->right();
            }
        }
        t = tree->right();
        if (t) {
            int post = 0;            if (t->right()) {         /* post count */
                p = t->right()->left();
                while (p) {
                    post++;
                    p = p->right();
                }
            }
            if (t->left()) {         /* rest (len - pre - post) */
                int rn = len - post - n;

                genop( MKOP_ABC(OP_ARRAY, m_sp, rhs+n, rn));
                gen_assignment(t->left(), m_sp, false);
                n += rn;
            }
            if (t->right() && t->right()->left()) {
                t = t->right()->left();
                while (n<len) {
                    gen_assignment(t->left(), rhs+n, false);
                    t = t->right();
                    n++;
                }
            }
        }
        pop_sp(len);
        if (val) {
            genop( MKOP_ABC(OP_ARRAY, rhs, rhs, len));
            push_();
        }
    }
    else {
        // variable rhs
        codegen( t, true);
        gen_vmassignment(node->lhs(), rhs, val);
        if (!val)
            pop_sp();
    }
}
void codegen_scope::visit(AliasNode *n) {
    int a = new_msym(n->m_from);
    int b = new_msym(n->m_to);
    int c = new_msym(mrb_intern2(m_mrb, "alias_method", 12));

    genop( MKOP_A(OP_TCLASS, m_sp));
    push_();
    genop( MKOP_ABx(OP_LOADSYM, m_sp, a));
    push_();
    genop( MKOP_ABx(OP_LOADSYM, m_sp, b));
    push_();
    genop( MKOP_A(OP_LOADNIL, m_sp));
    pop_sp(3);
    genop( MKOP_ABC(OP_SEND, m_sp, c, 2));
    if (m_val_stack.back()) {
        push_();
    }
}
void codegen_scope::visit(WhileNode *n) {
    bool val = m_val_stack.back();

    loopinfo *lp = loop_push(LOOP_NORMAL);

    lp->pc1 = new_label();
    genop(MKOP_sBx(OP_JMP, 0));
    lp->pc2 = new_label();
    codegen(n->rhs(), false);
    dispatch(lp->pc1);
    codegen(n->lhs(), true);
    pop_sp();
    genop(MKOP_AsBx(OP_JMPIF, m_sp, lp->pc2 - m_pc));

    loop_pop(val);
}
void codegen_scope::visit(UntilNode *n) {
    bool val = m_val_stack.back();
    loopinfo *lp = loop_push(LOOP_NORMAL);

    lp->pc1 = new_label();
    genop(MKOP_sBx(OP_JMP, 0));
    lp->pc2 = new_label();
    codegen(n->rhs(), false);
    dispatch(lp->pc1);
    codegen(n->lhs(), true);
    pop_sp();
    genop(MKOP_AsBx(OP_JMPNOT, m_sp, lp->pc2 - m_pc));

    loop_pop(val);
}
void codegen_scope::visit(YieldNode *node) {
    bool val = m_val_stack.back();
    codegen_scope *s2 = this;
    int lv = 0, ainfo = 0;
    int n = 0, sendv = 0;

    while (!s2->m_mscope) {
        lv++;
        s2 = s2->m_prev;
        if (!s2) break;
    }
    if (s2) ainfo = s2->m_ainfo;
    genop( MKOP_ABx(OP_BLKPUSH, m_sp, (ainfo<<4)|(lv & 0xf)));
    push_();
    if (node->child()) {
        n = gen_values( node->child(), true);
        if (n < 0) {
            n = sendv = 1;
            push_();
        }
    }
    pop_sp(n+1);
    if (sendv)
        n = CALL_MAXARGS;
    genop( MKOP_ABC(OP_SEND, m_sp, new_msym(mrb_intern2(m_mrb, "call", 4)), n));
    if (val)
        push_();
}
void codegen_scope::visit(Dot2Node *node) {
    bool val = m_val_stack.back();
    codegen( node->lhs(), val);
    codegen( node->rhs(), val);
    if (val) {
        pop_sp(2);
        genop( MKOP_ABC(OP_RANGE, m_sp, m_sp, 0));
        push_();
    }
}
void codegen_scope::visit(Dot3Node *node) {
    bool val = m_val_stack.back();
    codegen( node->lhs(), val);
    codegen( node->rhs(), val);
    if (val) {
        pop_sp(2);
        genop( MKOP_ABC(OP_RANGE, m_sp, m_sp, 1));
        push_();
    }
}
void codegen_scope::visit(AndNode *node) {
    bool val = m_val_stack.back();
    codegen(node->lhs(), true);
    int pos = new_label();
    pop_sp();
    genop(MKOP_AsBx(OP_JMPNOT, m_sp, 0));
    codegen(node->rhs(), val);
    dispatch(pos);
}
void codegen_scope::visit(OrNode *node) {
    bool val = m_val_stack.back();
    codegen(node->lhs(), true);
    int pos = new_label();
    pop_sp();
    genop(MKOP_AsBx(OP_JMPIF, m_sp, 0));
    codegen(node->rhs(), val);
    dispatch(pos);
}
void codegen_scope::visit(Colon2Node *node) {
    bool val = m_val_stack.back();
    int sym = new_sym( node->m_sym);

    codegen( node->m_val, true);
    pop_sp();
    genop( MKOP_ABx(OP_GETMCNST, m_sp, sym));
    if (val) push_();
}
void codegen_scope::visit(SymNode *node) {
    if (!m_val_stack.back())
        return;
    int sym_ = new_sym( node->sym() );
    genop( MKOP_ABx(OP_LOADSYM, m_sp, sym_));
    push_();
}
void codegen_scope::visit(AsgnNode *node) {
    bool val = m_val_stack.back();
    codegen( node->rhs(), true);
    pop_sp();
    gen_assignment(node->lhs(), m_sp, val);
}
void codegen_scope::visit(SplatNode *node) {
    codegen(node->child(),true);
}
void codegen_scope::walk_string(mrb_ast_node *n) {
    bool val = m_val_stack.back();
    if (val) {
        codegen( n->left(), true);
        n = n->right();
        while (n) {
            codegen( n->left(), true);
            pop_sp(2);
            genop_peep(MKOP_AB(OP_STRCAT, m_sp, m_sp+1), true);
            push_();
            n = n->right();
        }
    }
    else {
        while (n) {
            if (n->left()->getType() != NODE_STR) {
                codegen( n->left(), false);
            }
            n = n->right();
        }
    }
}
void codegen_scope::visit(DstrNode *node) {
    mrb_ast_node *n = node->child();
    walk_string(n);
}
void codegen_scope::visit(HeredocNode *node) {
    mrb_ast_node *n = node->contents()->doc;
    walk_string(n);
}
void codegen_scope::visit(DsymNode *node) {
    bool val = m_val_stack.back();
    codegen( node->m_str, val);
    if (val) {
        gen_send_intern();
    }
}
void codegen_scope::visit(CallCommonNode *node) {
    bool val = m_val_stack.back();
    mrb_sym _sym = node->m_method;
    int idx;
    int n = 0, noop = 0, sendv = 0, blk = 0;
    codegen(node->m_receiver, true); /* receiver */
    idx = new_msym(_sym);
    if (node->m_cmd_args) {
        n = gen_values(node->m_cmd_args->m_args, true);
        if (n < 0) {
            n = noop = sendv = 1;
            push_();
        }
    }
    if (node->m_cmd_args && node->m_cmd_args->m_blk) {
        noop = 1;
        codegen(node->m_cmd_args->m_blk, true);
        pop_sp();
    }
    else {
        blk = m_sp;
    }
    pop_sp(n+1);
    {
        size_t len;
        eOpEnum op = OP_LAST;
        const char *name = mrb_sym2name_len(m_mrb, _sym, &len);
        bool was_peep = false;
        if(!noop) {
            if (len == 1 && name[0] == '+')  {
                was_peep = true;
                op = OP_ADD;
            }
            else if (len == 1 && name[0] == '-')  {
                was_peep = true;
                op = OP_SUB;
            }
            else if (len == 1 && name[0] == '*')  {
                op = OP_MUL;
            }
            else if (len == 1 && name[0] == '/')  {
                op = OP_DIV;
            }
            else if (len == 1 && name[0] == '<')  {
                op = OP_LT;
            }
            else if (len == 2 && name[0] == '<' && name[1] == '=')  {
                op = OP_LE;
            }
            else if (len == 1 && name[0] == '>')  {
                op = OP_GT;
            }
            else if (len == 2 && name[0] == '>' && name[1] == '=')  {
                op = OP_GE;
            }
            else if (len == 2 && name[0] == '=' && name[1] == '=')  {
                op = OP_EQ;
            }
        }
        if(op == OP_LAST) {
            if (sendv)
                n = CALL_MAXARGS;
            op = OP_SENDB;
            if (blk > 0) {                   /* no block */
                op = OP_SEND;
            }
        }
        mrb_code cd = MKOP_ABC(op, m_sp, idx, n);
        if(was_peep)
            genop_peep(cd,val);
        else
            genop( cd );

    }
    if (val) {
        push_();
    }
}
void codegen_scope::visit(IfNode *node) {
    bool val = m_val_stack.back();
    int pos1, pos2;
    mrb_ast_node *e = node->false_body(); //tree->right()->right()->left()

    codegen(node->cond(), true);
    pop_sp();
    pos1 = new_label();
    genop(MKOP_AsBx(OP_JMPNOT, m_sp, 0));

    codegen(node->true_body(), val);
    if (val && !node->true_body()) {
        genop(MKOP_A(OP_LOADNIL, m_sp));
        push_();
    }
    if (e) {
        if (val)
            pop_sp();
        pos2 = new_label();
        genop(MKOP_sBx(OP_JMP, 0));
        dispatch(pos1);
        codegen(e, val);
        dispatch(pos2);
    }
    else {
        if (val) {
            pop_sp();
            pos2 = new_label();
            genop(MKOP_sBx(OP_JMP, 0));
            dispatch(pos1);
            genop(MKOP_A(OP_LOADNIL, m_sp));
            dispatch(pos2);
            push_();
        }
        else {
            dispatch(pos1);
        }
    }
}
void codegen_scope::visit(OpAsgnNode *node) {
    bool val = m_val_stack.back();
    mrb_sym sym = node->op_sym;
    size_t len;
    const char *name = mrb_sym2name_len(m_mrb, sym, &len);
    int idx;

    codegen( node->lhs(), true);
    if (len == 2 &&
            ((name[0] == '|' && name[1] == '|') ||
             (name[0] == '&' && name[1] == '&'))) {
        int pos;

        pop_sp();
        pos = new_label();
        genop( MKOP_AsBx(name[0] == '|' ? OP_JMPIF : OP_JMPNOT, m_sp, 0));
        codegen( node->rhs(), true);
        pop_sp();
        gen_assignment(node->lhs(), m_sp, val);
        dispatch(pos);
        return;
    }
    codegen( node->rhs(), true);
    pop_sp(2);

    idx = new_msym(sym);
    if (len == 1 && name[0] == '+')  {
        genop_peep(MKOP_ABC(OP_ADD, m_sp, idx, 1), val);
    }
    else if (len == 1 && name[0] == '-')  {
        genop_peep(MKOP_ABC(OP_SUB, m_sp, idx, 1), val);
    }
    else if (len == 1 && name[0] == '*')  {
        genop( MKOP_ABC(OP_MUL, m_sp, idx, 1));
    }
    else if (len == 1 && name[0] == '/')  {
        genop( MKOP_ABC(OP_DIV, m_sp, idx, 1));
    }
    else if (len == 1 && name[0] == '<')  {
        genop( MKOP_ABC(OP_LT, m_sp, idx, 1));
    }
    else if (len == 2 && name[0] == '<' && name[1] == '=')  {
        genop( MKOP_ABC(OP_LE, m_sp, idx, 1));
    }
    else if (len == 1 && name[0] == '>')  {
        genop( MKOP_ABC(OP_GT, m_sp, idx, 1));
    }
    else if (len == 2 && name[0] == '>' && name[1] == '=')  {
        genop( MKOP_ABC(OP_GE, m_sp, idx, 1));
    }
    else {
        genop( MKOP_ABC(OP_SEND, m_sp, idx, 1));
    }
    gen_assignment(node->lhs(), m_sp, val);

}
void codegen_scope::visit(DefNode *n) {
    bool val = m_val_stack.back();
    int sym = new_msym(n->name());
    int idx, base = m_idx;

    codegen_scope *s = codegen_scope::create(m_mrb, this, n->ve_locals());
    idx = s->do_lambda_internal(n) - base;


    genop( MKOP_A(OP_TCLASS, m_sp));
    push_();
    genop( MKOP_Abc(OP_LAMBDA, m_sp, idx, OP_L_METHOD));
    pop_sp();
    genop( MKOP_AB(OP_METHOD, m_sp, sym));
    if (val) {
        genop( MKOP_A(OP_LOADNIL, m_sp));
        push_();
    }
}
void codegen_scope::visit(SdefNode *n) {
    bool val = m_val_stack.back();
    node *recv = n->receiver();

    int sym = new_msym(n->name());
    int idx, base = m_idx;

    codegen_scope *s = codegen_scope::create(m_mrb, this, n->ve_locals());
    idx = s->do_lambda_internal(n) - base;

    //    int sym = new_msym(::sym(tree->right()->left()));
    //    int idx = lambda_body(tree->right()->right(), 0);

    codegen( recv, true);
    pop_sp();
    genop( MKOP_AB(OP_SCLASS, m_sp, m_sp));
    push_();
    genop( MKOP_Abc(OP_LAMBDA, m_sp, idx, OP_L_METHOD));
    pop_sp();
    genop( MKOP_AB(OP_METHOD, m_sp, sym));
    if (val) {
        genop( MKOP_A(OP_LOADNIL, m_sp));
        push_();
    }
}
void codegen_scope::visit(ForNode *n) {
    bool val = m_val_stack.back();
    int base = m_idx;
    // (:for var obj body)
    // generate receiver
    codegen(n->object(), true);
    // generate loop-block
    codegen_scope *s = codegen_scope::create(m_mrb, this, n->var());
    int idx = s->do_for_body(n);
    genop( MKOP_Abc(OP_LAMBDA, m_sp, idx - base, OP_L_BLOCK));
    pop_sp();
    idx = new_msym(mrb_intern2(m_mrb, "each", 4));
    genop( MKOP_ABC(OP_SENDB, m_sp, idx, 0));

    if (val)
        push_();
}
// (:rescue body rescue else)
void codegen_scope::visit(RescueNode *node) {
    bool val = m_val_stack.back();
    int onerr, noexc, exend, pos1;
    loopinfo *lp;

    onerr = new_label();
    genop( MKOP_Bx(OP_ONERR, 0));
    lp = loop_push( LOOP_BEGIN);
    lp->pc1 = onerr;
    if (node->body()) {
        codegen(node->body(), val);
        if (val)
            pop_sp();
    }
    lp->type = LOOP_RESCUE;
    noexc = new_label();
    genop(MKOP_Bx(OP_JMP, 0));
    dispatch(onerr);
    exend = 0;
    pos1 = 0;
    if (node->rescue()) {
        mrb_ast_node *n2 = node->rescue();
        int exc = m_sp;

        genop(MKOP_A(OP_RESCUE, exc));
        push_();
        while (n2) {
            mrb_ast_node *n3 = n2->left();
            mrb_ast_node *n4 = n3->left();

            if (pos1)
                dispatch(pos1);
            int pos2 = 0;
            do {
                if (n4) {
                    codegen(n4->left(), true);
                }
                else {
                    genop(MKOP_ABx(OP_GETCONST, m_sp, new_msym(mrb_intern2(m_mrb, "StandardError", 13))));
                    push_();
                }
                genop(MKOP_AB(OP_MOVE, m_sp, exc));
                pop_sp();
                genop(MKOP_ABC(OP_SEND, m_sp, new_msym(mrb_intern2(m_mrb, "===", 3)), 1));
                int tmp = new_label();
                genop(MKOP_AsBx(OP_JMPIF, m_sp, pos2));
                pos2 = tmp;
                if (n4) {
                    n4 = n4->right();
                }
            } while (n4);
            pos1 = new_label();
            genop(MKOP_sBx(OP_JMP, 0));
            dispatch_linked(pos2);

            pop_sp();
            if (n3->right()->left()) {
                gen_assignment(n3->right()->left(), exc, false);
            }
            if (n3->right()->right()->left()) {
                codegen(n3->right()->right()->left(), val);
                if (val)
                    pop_sp();
            }
            int tmp = new_label();
            genop(MKOP_sBx(OP_JMP, exend));
            exend = tmp;
            n2 = n2->right();
            push_();
        }
        if (pos1) {
            dispatch(pos1);
            genop(MKOP_A(OP_RAISE, exc));
        }
    }
    pop_sp();

    dispatch(noexc);
    genop(MKOP_A(OP_POPERR, 1));
    if (node->r_else()) {
        codegen(node->r_else(), val);
    }
    else if (val) {
        push_();
    }
    dispatch_linked(exend);
    loop_pop(false);
}
void codegen_scope::visit(CaseNode *node) {
    bool val = m_val_stack.back();
    int head = 0;
    mrb_ast_node *n;

    int pos3 = 0;

    if (node->switched_on()) {
        head = m_sp;
        codegen(node->switched_on(), true);
    }
    mrb_ast_node *tree = node->cases();
    while (tree) {
        n = tree->left()->left();
        int pos1 = 0,pos2 = 0;
        while (n) {
            codegen(n->left(), true);
            if (head) {
                genop(MKOP_AB(OP_MOVE, m_sp, head));
                pop_sp();
                genop(MKOP_ABC(OP_SEND, m_sp, new_msym(mrb_intern2(m_mrb, "===", 3)), 1));
            }
            else {
                pop_sp();
            }
            int tmp = new_label();
            genop(MKOP_AsBx(OP_JMPIF, m_sp, pos2));
            pos2 = tmp;
            n = n->right();
        }
        if (tree->left()->left()) {
            pos1 = new_label();
            genop(MKOP_sBx(OP_JMP, 0));
            dispatch_linked(pos2);
        }
        codegen(tree->left()->right(), val);
        if (val)
            pop_sp();
        int tmp = new_label();
        genop(MKOP_sBx(OP_JMP, pos3));
        pos3 = tmp;
        if (pos1)
            dispatch(pos1);
        tree = tree->right();
    }
    if (val) {
        genop(MKOP_A(OP_LOADNIL, m_sp));
        push_();
    }
    if (pos3)
        dispatch_linked(pos3);
}
void codegen_scope::visit(SclassNode *nd) {
    bool val = m_val_stack.back();
    int idx;

    codegen( nd->receiver(), true);
    pop_sp();
    genop( MKOP_AB(OP_SCLASS, m_sp, m_sp));
    idx = visit( nd->scope() );
    genop( MKOP_ABx(OP_EXEC, m_sp, idx));
    if (val) {
        push_();
    }
}
void codegen_scope::visit(ClassNode *nd) {
    bool val = m_val_stack.back();
    int idx;

    if (nd->receiver()->left() == (node*)0) {
        genop( MKOP_A(OP_LOADNIL, m_sp));
        push_();
    }
    else if (nd->receiver()->left() == (node*)1) {
        genop( MKOP_A(OP_OCLASS, m_sp));
        push_();
    }
    else {
        codegen( nd->receiver()->left(), true);
    }
    if (nd->super()) {
        codegen( nd->super(), true);
    }
    else {
        genop( MKOP_A(OP_LOADNIL, m_sp));
        push_();
    }
    pop_sp(2);
    idx = new_msym(sym(nd->receiver()->right()));
    genop( MKOP_AB(OP_CLASS, m_sp, idx));
    idx = visit( nd->scope() );
    genop( MKOP_ABx(OP_EXEC, m_sp, idx));
    if (val) {
        push_();
    }
}
void codegen_scope::visit(ModuleNode *nd) {
    bool val = m_val_stack.back();
    int idx;
    node *rcv = nd->receiver();
    if (rcv->left() == (node*)0) {
        genop( MKOP_A(OP_LOADNIL, m_sp));
        push_();
    }
    else if (rcv->left() == (node*)1) {
        genop( MKOP_A(OP_OCLASS, m_sp));
        push_();
    }
    else {
        codegen( rcv->left(), true);
    }
    pop_sp();
    idx = new_msym(sym(rcv->right()));
    genop( MKOP_AB(OP_MODULE, m_sp, idx));
    idx = visit( nd->scope() );
    genop( MKOP_ABx(OP_EXEC, m_sp, idx));
    if (val) {
        push_();
    }
}
void codegen_scope::visit(LVarNode *n) {
    if (!m_val_stack.back())
        return;
    int idx = lv_idx(n->sym());

    if (idx > 0) {
        genop( MKOP_AB(OP_MOVE, m_sp, idx));
    }
    else {
        int lv = 0;
        codegen_scope *up = m_prev;

        while (up) {
            idx = up->lv_idx(n->sym());
            if (idx > 0) {
                genop( MKOP_ABC(OP_GETUPVAR, m_sp, idx, lv));
                break;
            }
            lv++;
            up = up->m_prev;
        }
    }
    push_();
}
void codegen_scope::visit(BlockNode *node) {
    int idx, base = m_idx;

    codegen_scope *s = codegen_scope::create(m_mrb, this, node->locals());
    idx = s->do_lambda_internal(node)-base;

    genop(MKOP_Abc(OP_LAMBDA, m_sp, idx, OP_L_BLOCK));
    push_();
}
void codegen_scope::visit(LambdaNode *node) {
    int idx, base = m_idx;

    codegen_scope *s = codegen_scope::create(m_mrb, this, node->locals());
    idx = s->do_lambda_internal(node)-base;

    genop(MKOP_Abc(OP_LAMBDA, m_sp, idx, OP_L_LAMBDA));
    push_();
}
void codegen_scope::visit(BeginNode *n) {
    if (m_val_stack.back() && n->m_entries.empty()) {
        genop(MKOP_A(OP_LOADNIL, m_sp));
        push_();
    }
    for(auto iter=n->m_entries.begin(); iter!=n->m_entries.end(); ++iter) {

        codegen(*iter, ((iter+1)!=n->m_entries.end()) ? false : m_val_stack.back());
    }
}
void codegen_scope::visit(StrNode *sn) {
    if (!m_val_stack.back())
        return;
    char *p = sn->m_str;
    size_t len = sn->m_length;
    int ai = m_mrb->gc().arena_save();
    int off = new_lit(mrb_str_new(m_mrb, p, len));

    m_mrb->gc().arena_restore(ai);
    genop( MKOP_ABx(OP_STRING, m_sp, off));
    push_();
}
void codegen_scope::visit(NegateNode *nn) {
    mrb_ast_node * chld = nn->child();
    node_type nt = chld->getType();
    if( (nt==NODE_FLOAT) || (nt==NODE_INT)) {
        m_negate=true;
        chld->accept(this);
        m_negate=false;
    }
    else
    {
        int sym = new_msym(m_mrb->intern2("-", 1));
        genop( MKOP_ABx(OP_LOADI, m_sp, 0));
        push_();
        codegen( chld, true);
        pop_sp(2);
        genop( MKOP_ABC(OP_SUB, m_sp, sym, 2));
    }
}
void codegen_scope::visit(EnsureNode *nn) {
    bool val = m_val_stack.back();
    int idx;
    int epush = m_pc;

    genop(MKOP_Bx(OP_EPUSH, 0));
    m_ensure_level++;
    codegen(nn->body(), val);
    idx = visit(nn->ensure());
    m_iseq[epush] = MKOP_Bx(OP_EPUSH, idx);
    m_ensure_level--;
    genop_peep(MKOP_A(OP_EPOP, 1), false);
}
void codegen_scope::visit(RedoNode *nn) {
    if (!m_loop) {
        raise_error("unexpected redo");
    }
    else {
        if (m_ensure_level > m_loop->ensure_level) {
            genop_peep(MKOP_A(OP_EPOP, m_ensure_level - m_loop->ensure_level), false);
        }
        genop( MKOP_sBx(OP_JMP, m_loop->pc2 - m_pc));
    }
}
void codegen_scope::visit(RetryNode *nn) {
    const char *msg = "unexpected retry";

    if (!m_loop) {
        raise_error(msg);
    }
    else {
        struct loopinfo *lp = m_loop;
        int n = 0;

        while (lp && lp->type != LOOP_RESCUE) {
            if (lp->type == LOOP_BEGIN) {
                n++;
            }
            lp = lp->prev;
        }
        if (!lp) {
            raise_error(msg);
        }
        else {
            if (n > 0) {
                while (n--) {
                    genop_peep(MKOP_A(OP_POPERR, 1), false);
                }
            }
            if (m_ensure_level > lp->ensure_level) {
                genop_peep(MKOP_A(OP_EPOP, m_ensure_level - lp->ensure_level), false);
            }
            genop( MKOP_sBx(OP_JMP, lp->pc1 - m_pc));
        }
    }
}
void codegen_scope::visit(XstrNode *xn) {
    if (!m_val_stack.back())
        return;
    char *p = xn->m_str;
    size_t len = xn->m_length;
    int ai = m_mrb->gc().arena_save();
    int sym = new_sym( mrb_intern2(m_mrb, "Kernel", 6));
    int off = new_lit(mrb_str_new(m_mrb, p, len));

    genop( MKOP_A(OP_OCLASS, m_sp));
    genop( MKOP_ABx(OP_GETMCNST, m_sp, sym));
    push_();
    genop( MKOP_ABx(OP_STRING, m_sp, off));
    pop_sp();
    sym = new_sym( mrb_intern2(m_mrb, "`", 1));
    genop( MKOP_ABC(OP_SEND, m_sp, sym, 1));
    m_mrb->gc().arena_restore(ai);
    push_();
}
void codegen_scope::visit(RegxNode *xn) {
    if (!m_val_stack.back())
        return;
    const char *p1 = xn->m_expr;
    const char *p2 = xn->m_str;
    int ai = m_mrb->gc().arena_save();
    int sym = new_sym( mrb_intern(m_mrb, REGEXP_CLASS));
    int off = new_lit(mrb_str_new(m_mrb, p1, strlen(p1)));
    int argc = 1;

    genop( MKOP_A(OP_OCLASS, m_sp));
    genop( MKOP_ABx(OP_GETMCNST, m_sp, sym));
    push_();
    genop( MKOP_ABx(OP_STRING, m_sp, off));
    if (p2) {
        push_();
        off = new_lit(mrb_str_new(m_mrb, p2, strlen(p2)));
        genop( MKOP_ABx(OP_STRING, m_sp, off));
        argc++;
        pop_sp();
    }
    pop_sp();
    sym = new_sym( mrb_intern2(m_mrb, "compile", 7));
    genop( MKOP_ABC(OP_SEND, m_sp, sym, argc));
    m_mrb->gc().arena_restore(ai);
    push_();
}
void codegen_scope::visit(DregxNode *dn) {
    node *n = dn->m_a;
    bool val = m_val_stack.back();

    if (!val)
    {
        while (n) {
            if (n->left()->getType() != NODE_STR) {
                codegen( n->left(), false);
            }
            n = n->right();
        }
        return;
    }
    int ai = m_mrb->gc().arena_save();
    int sym = new_sym( mrb_intern(m_mrb, REGEXP_CLASS));
    int argc = 1;

    genop( MKOP_A(OP_OCLASS, m_sp));
    genop( MKOP_ABx(OP_GETMCNST, m_sp, sym));
    push_();
    codegen( n->left(), true);
    n = n->right();
    while (n) {
        codegen( n->left(), true);
        pop_sp(2);
        genop_peep(MKOP_AB(OP_STRCAT, m_sp, m_sp+1), true);
        push_();
        n = n->right();
    }
    n = dn->m_b->right();
    if (n->left()) {
        const char *p = (const char*)n->left();
        int off = new_lit(mrb_str_new(m_mrb, p, strlen(p)));
        codegen( dn->m_a, true);
        genop( MKOP_ABx(OP_STRING, m_sp, off));
        pop_sp();
        genop_peep(MKOP_AB(OP_STRCAT, m_sp, m_sp+1), true);
    }
    if (n->right()) {
        char *p2 = (char*)n->right();
        int off;

        push_();
        off = new_lit(mrb_str_new(m_mrb, p2, strlen(p2)));
        genop( MKOP_ABx(OP_STRING, m_sp, off));
        argc++;
        pop_sp();
    }
    pop_sp();
    sym = new_sym( mrb_intern2(m_mrb, "compile", 7));
    genop( MKOP_ABC(OP_SEND, m_sp, sym, argc));
    m_mrb->gc().arena_restore(ai);
    push_();
}
void codegen_scope::visit(SelfNode *sn) {
    if (!m_val_stack.back())
        return;
    genop( MKOP_A(OP_LOADSELF, m_sp));
    push_();
}
void codegen_scope::visit(WordsNode *n) {
    bool val = m_val_stack.back();
    gen_literal_array(n->child(), FALSE, val);
}
void codegen_scope::visit(SymbolsNode *n) {
    bool val = m_val_stack.back();
    gen_literal_array(n->child(), TRUE, val);
}
void codegen_scope::visit(PostExeNode *n) {
    codegen( n->child(), false);
}
void codegen_scope::visit(NilNode *n) {
    if (!m_val_stack.back())
        return;
    genop( MKOP_A(OP_LOADNIL, m_sp));
    push_();
}
void codegen_scope::visit(TrueNode *n) {
    if (!m_val_stack.back())
        return;
    genop( MKOP_A(OP_LOADT, m_sp));
    push_();
}
void codegen_scope::visit(FalseNode *n) {
    if (!m_val_stack.back())
        return;
    genop( MKOP_A(OP_LOADF, m_sp));
    push_();
}
void codegen_scope::visit(FloatLiteralNode *n) {

    if (!m_val_stack.back())
        return;
    mrb_float f = n->value();
    if(m_negate)
        f = -f;
    int off = new_lit(mrb_float_value(f));

    genop( MKOP_ABx(OP_LOADL, m_sp, off));
    push_();
}
void codegen_scope::visit(BreakNode *n) {
    loop_break(n->child());
    if (m_val_stack.back())
        push_();
}
void codegen_scope::visit(ConstNode *n) {
    int sym_ = new_sym( n->sym() );
    genop( MKOP_ABx(OP_GETCONST, m_sp, sym_));
    push_();
}
void codegen_scope::visit(GVarNode *n) {
    int sym_ = new_sym( n->sym() );

    genop( MKOP_ABx(OP_GETGLOBAL, m_sp, sym_));
    push_();
}
void codegen_scope::visit(IVarNode *n)  {
    int sym_ = new_sym( n->sym() );
    genop( MKOP_ABx(OP_GETIV, m_sp, sym_));
    push_();
}
void codegen_scope::visit(CVarNode *n) {
    int sym_ = new_sym( n->sym() );
    genop( MKOP_ABx(OP_GETCV, m_sp, sym_));
    push_();
}
void codegen_scope::visit(BlockArgNode *n) {
    codegen( n->child(), true);
}
void codegen_scope::visit(ArgNode *n) {
    assert(false); // should not happen
}
void codegen_scope::visit(LiteralDelimNode * n) {
    assert(false); // should not happen
}

void codegen_scope::visit(FCallNode *n) {
    visit((CallCommonNode *)n);
}
void codegen_scope::visit(CallNode *n) {
    visit((CallCommonNode *)n);
}
void codegen_scope::codegen(node *tree, bool val)
{
    if (!tree)
        return;
    m_val_stack.push_back(val);
    m_lineno = tree->lineno;
    tree->accept(this);
    m_val_stack.pop_back();
}
void codegen_scope::visit(DxstrNode * n) { assert(false); }
codegen_scope* codegen_scope::create(mrb_state *mrb, codegen_scope *prev, const tLocals &lv)
{
    mrb_pool *pool = mrb->gc().mrb_pool_open();
    codegen_scope *p = new(pool->mrb_pool_alloc(sizeof(codegen_scope))) codegen_scope;

    if (!p)
        return 0;
    p->m_mrb = mrb;
    p->m_mpool = pool;
    if (!prev) return p;
    p->m_prev = prev;
    p->m_ainfo = -1;
    p->m_mscope = 0;

    p->m_irep = mrb_add_irep(mrb);
    p->m_idx = p->m_irep->idx;

    p->m_icapa = 1024;
    p->m_iseq = (mrb_code*)mrb->gc()._malloc(sizeof(mrb_code)*p->m_icapa);

    p->m_pcapa = 32;
    p->m_irep->pool = (mrb_value*)mrb->gc()._malloc(sizeof(mrb_value)*p->m_pcapa);
    p->m_irep->plen = 0;

    p->m_scapa = 256;
    p->m_irep->syms = (mrb_sym*)mrb->gc()._malloc(sizeof(mrb_sym)*256);
    p->m_irep->slen = 0;
    p->m_locals = lv;
    p->m_sp += p->m_locals.size() +1;
    p->m_nlocals = p->m_sp;
    p->m_ai = mrb->gc().arena_save();

    p->m_filename = prev->m_filename;
    if (p->m_filename) {
        p->lines = (short*)mrb->gc()._malloc(sizeof(short)*p->m_icapa);
    }
    p->m_lineno = prev->m_lineno;
    return p;
}

codegen_scope* codegen_scope::create(mrb_state *mrb, codegen_scope *prev, node *lv)
{
    tLocals locals;
    while (lv) {
        locals.push_back(sym(lv->left()));
        lv=lv->right();
    }
    return create(mrb,prev,locals);
}

void codegen_scope::finish()
{
    mrb_state *mrb = m_mrb;
    mrb_irep *irep = m_irep;

    irep->flags = 0;
    if (m_iseq) {
        irep->iseq = (mrb_code *)s_realloc(m_iseq, sizeof(mrb_code)*m_pc);
        irep->ilen = m_pc;
        if (this->lines) {
            irep->lines = (uint16_t *)s_realloc(this->lines, sizeof(uint16_t)*m_pc);
        }
        else {
            irep->lines = 0;
        }
    }
    irep->pool = (mrb_value *)s_realloc(irep->pool, sizeof(mrb_value)*irep->plen);
    irep->syms = (mrb_sym *)s_realloc(irep->syms, sizeof(mrb_sym)*irep->slen);
    if (m_filename) {
        irep->filename = m_filename;
    }

    irep->nlocals = m_nlocals;
    irep->nregs = m_nregs;

    mrb->gc().arena_restore(m_ai);
    this->~codegen_scope();
    m_mpool->mrb_pool_close();
}

loopinfo* codegen_scope::loop_push(enum looptype t)
{
    loopinfo *p = (loopinfo *)palloc(sizeof(loopinfo));

    p->type = t;
    p->pc1 = p->pc2 = p->pc3 = 0;
    p->prev = m_loop;
    p->ensure_level = m_ensure_level;
    p->acc = m_sp;
    m_loop = p;

    return p;
}
void codegen_scope::loop_break(node *tree)
{
    if (!m_loop) {
        codegen(tree, false);
        raise_error("unexpected break");
    }
    else {
        loopinfo *loop;

        if (tree) {
            codegen(tree, true);
            pop_sp();
        }

        loop = m_loop;
        while (loop->type == LOOP_BEGIN) {
            genop_peep(MKOP_A(OP_POPERR, 1), false);
            loop = loop->prev;
        }
        while (loop->type == LOOP_RESCUE) {
            loop = loop->prev;
        }
        if (loop->type == LOOP_NORMAL) {
            int tmp;

            if (m_ensure_level > m_loop->ensure_level) {
                genop_peep(MKOP_A(OP_EPOP, m_ensure_level - m_loop->ensure_level), false);
            }
            if (tree) {
                genop_peep(MKOP_AB(OP_MOVE, loop->acc, m_sp), false);
            }
            tmp = new_label();
            genop(MKOP_sBx(OP_JMP, loop->pc3));
            loop->pc3 = tmp;
        }
        else {
            genop(MKOP_AB(OP_RETURN, m_sp, OP_R_BREAK));
        }
    }
}

void codegen_scope::loop_pop(int val)
{
    if (val) {
        genop(MKOP_A(OP_LOADNIL, m_sp));
    }
    dispatch_linked(m_loop->pc3);
    m_loop = m_loop->prev;
    if (val)
        push_();
}

static void
codedump(mrb_state *mrb, int n)
{
#ifdef ENABLE_STDIO
    mrb_irep *irep = mrb->irep[n];
    int i, ai;
    mrb_code c;

    if (!irep) return;
    printf("irep %d nregs=%d nlocals=%d pools=%d syms=%d\n", n,
           irep->nregs, irep->nlocals, (int)irep->plen, (int)irep->slen);
    for (i=0; i<irep->ilen; i++) {
        ai = mrb->gc().arena_save();
        printf("%03d ", i);
        c = irep->iseq[i];
        switch (GET_OPCODE(c)) {
            case OP_NOP:
                printf("OP_NOP\n");
                break;
            case OP_MOVE:
                printf("OP_MOVE\tR%d\tR%d\n", GETARG_A(c), GETARG_B(c));
                break;
            case OP_LOADL:
                printf("OP_LOADL\tR%d\tL(%d)\n", GETARG_A(c), GETARG_Bx(c));
                break;
            case OP_LOADI:
                printf("OP_LOADI\tR%d\t%d\n", GETARG_A(c), GETARG_sBx(c));
                break;
            case OP_LOADSYM:
                printf("OP_LOADSYM\tR%d\t:%s\n", GETARG_A(c),
                       mrb_sym2name(mrb, irep->syms[GETARG_Bx(c)]));
                break;
            case OP_LOADNIL:
                printf("OP_LOADNIL\tR%d\n", GETARG_A(c));
                break;
            case OP_LOADSELF:
                printf("OP_LOADSELF\tR%d\n", GETARG_A(c));
                break;
            case OP_LOADT:
                printf("OP_LOADT\tR%d\n", GETARG_A(c));
                break;
            case OP_LOADF:
                printf("OP_LOADF\tR%d\n", GETARG_A(c));
                break;
            case OP_GETGLOBAL:
                printf("OP_GETGLOBAL\tR%d\t:%s\n", GETARG_A(c),
                       mrb_sym2name(mrb, irep->syms[GETARG_Bx(c)]));
                break;
            case OP_SETGLOBAL:
                printf("OP_SETGLOBAL\t:%s\tR%d\n",
                       mrb_sym2name(mrb, irep->syms[GETARG_Bx(c)]),
                        GETARG_A(c));
                break;
            case OP_GETCONST:
                printf("OP_GETCONST\tR%d\t:%s\n", GETARG_A(c),
                       mrb_sym2name(mrb, irep->syms[GETARG_Bx(c)]));
                break;
            case OP_SETCONST:
                printf("OP_SETCONST\t:%s\tR%d\n",
                       mrb_sym2name(mrb, irep->syms[GETARG_Bx(c)]),
                        GETARG_A(c));
                break;
            case OP_GETMCNST:
                printf("OP_GETMCNST\tR%d\tR%d::%s\n", GETARG_A(c), GETARG_A(c),
                       mrb_sym2name(mrb, irep->syms[GETARG_Bx(c)]));
                break;
            case OP_SETMCNST:
                printf("OP_SETMCNST\tR%d::%s\tR%d\n", GETARG_A(c)+1,
                       mrb_sym2name(mrb, irep->syms[GETARG_Bx(c)]),
                        GETARG_A(c));
                break;
            case OP_GETIV:
                printf("OP_GETIV\tR%d\t%s\n", GETARG_A(c),
                       mrb_sym2name(mrb, irep->syms[GETARG_Bx(c)]));
                break;
            case OP_SETIV:
                printf("OP_SETIV\t%s\tR%d\n",
                       mrb_sym2name(mrb, irep->syms[GETARG_Bx(c)]),
                        GETARG_A(c));
                break;
            case OP_GETUPVAR:
                printf("OP_GETUPVAR\tR%d\t%d\t%d\n",
                       GETARG_A(c), GETARG_B(c), GETARG_C(c));
                break;
            case OP_SETUPVAR:
                printf("OP_SETUPVAR\tR%d\t%d\t%d\n",
                       GETARG_A(c), GETARG_B(c), GETARG_C(c));
                break;
            case OP_GETCV:
                printf("OP_GETCV\tR%d\t%s\n", GETARG_A(c),
                       mrb_sym2name(mrb, irep->syms[GETARG_Bx(c)]));
                break;
            case OP_SETCV:
                printf("OP_SETCV\t%s\tR%d\n",
                       mrb_sym2name(mrb, irep->syms[GETARG_Bx(c)]),
                        GETARG_A(c));
                break;
            case OP_JMP:
                printf("OP_JMP\t\t%03d\n", i+GETARG_sBx(c));
                break;
            case OP_JMPIF:
                printf("OP_JMPIF\tR%d\t%03d\n", GETARG_A(c), i+GETARG_sBx(c));
                break;
            case OP_JMPNOT:
                printf("OP_JMPNOT\tR%d\t%03d\n", GETARG_A(c), i+GETARG_sBx(c));
                break;
            case OP_SEND:
                printf("OP_SEND\tR%d\t:%s\t%d\n", GETARG_A(c),
                       mrb_sym2name(mrb, irep->syms[GETARG_B(c)]),
                        GETARG_C(c));
                break;
            case OP_SENDB:
                printf("OP_SENDB\tR%d\t:%s\t%d\n", GETARG_A(c),
                       mrb_sym2name(mrb, irep->syms[GETARG_B(c)]),
                        GETARG_C(c));
                break;
            case OP_TAILCALL:
                printf("OP_TAILCALL\tR%d\t:%s\t%d\n", GETARG_A(c),
                       mrb_sym2name(mrb, irep->syms[GETARG_B(c)]),
                        GETARG_C(c));
                break;
            case OP_SUPER:
                printf("OP_SUPER\tR%d\t%d\n", GETARG_A(c),
                       GETARG_C(c));
                break;
            case OP_ARGARY:
                printf("OP_ARGARY\tR%d\t%d:%d:%d:%d\n", GETARG_A(c),
                       (GETARG_Bx(c)>>10)&0x3f,
                       (GETARG_Bx(c)>>9)&0x1,
                       (GETARG_Bx(c)>>4)&0x1f,
                       (GETARG_Bx(c)>>0)&0xf);
                break;

            case OP_ENTER:
                printf("OP_ENTER\t%d:%d:%d:%d:%d:%d:%d\n",
                       (GETARG_Ax(c)>>18)&0x1f,
                       (GETARG_Ax(c)>>13)&0x1f,
                       (GETARG_Ax(c)>>12)&0x1,
                       (GETARG_Ax(c)>>7)&0x1f,
                       (GETARG_Ax(c)>>2)&0x1f,
                       (GETARG_Ax(c)>>1)&0x1,
                       GETARG_Ax(c) & 0x1);
                break;
            case OP_RETURN:
                printf("OP_RETURN\tR%d", GETARG_A(c));
                switch (GETARG_B(c)) {
                    case OP_R_NORMAL:
                        printf("\n"); break;
                    case OP_R_RETURN:
                        printf("\treturn\n"); break;
                    case OP_R_BREAK:
                        printf("\tbreak\n"); break;
                    default:
                        printf("\tbroken\n"); break;
                }
                break;
            case OP_BLKPUSH:
                printf("OP_BLKPUSH\tR%d\t%d:%d:%d:%d\n", GETARG_A(c),
                       (GETARG_Bx(c)>>10)&0x3f,
                       (GETARG_Bx(c)>>9)&0x1,
                       (GETARG_Bx(c)>>4)&0x1f,
                       (GETARG_Bx(c)>>0)&0xf);
                break;

            case OP_LAMBDA:
                printf("OP_LAMBDA\tR%d\tI(%+d)\t%d\n", GETARG_A(c), GETARG_b(c), GETARG_c(c));
                break;
            case OP_RANGE:
                printf("OP_RANGE\tR%d\tR%d\t%d\n", GETARG_A(c), GETARG_B(c), GETARG_C(c));
                break;
            case OP_METHOD:
                printf("OP_METHOD\tR%d\t:%s\n", GETARG_A(c),
                       mrb_sym2name(mrb, irep->syms[GETARG_B(c)]));
                break;

            case OP_ADD:
                printf("OP_ADD\tR%d\t:%s\t%d\n", GETARG_A(c),
                       mrb_sym2name(mrb, irep->syms[GETARG_B(c)]),
                        GETARG_C(c));
                break;
            case OP_ADDI:
                printf("OP_ADDI\tR%d\t:%s\t%d\n", GETARG_A(c),
                       mrb_sym2name(mrb, irep->syms[GETARG_B(c)]),
                        GETARG_C(c));
                break;
            case OP_SUB:
                printf("OP_SUB\tR%d\t:%s\t%d\n", GETARG_A(c),
                       mrb_sym2name(mrb, irep->syms[GETARG_B(c)]),
                        GETARG_C(c));
                break;
            case OP_SUBI:
                printf("OP_SUBI\tR%d\t:%s\t%d\n", GETARG_A(c),
                       mrb_sym2name(mrb, irep->syms[GETARG_B(c)]),
                        GETARG_C(c));
                break;
            case OP_MUL:
                printf("OP_MUL\tR%d\t:%s\t%d\n", GETARG_A(c),
                       mrb_sym2name(mrb, irep->syms[GETARG_B(c)]),
                        GETARG_C(c));
                break;
            case OP_DIV:
                printf("OP_DIV\tR%d\t:%s\t%d\n", GETARG_A(c),
                       mrb_sym2name(mrb, irep->syms[GETARG_B(c)]),
                        GETARG_C(c));
                break;
            case OP_LT:
                printf("OP_LT\tR%d\t:%s\t%d\n", GETARG_A(c),
                       mrb_sym2name(mrb, irep->syms[GETARG_B(c)]),
                        GETARG_C(c));
                break;
            case OP_LE:
                printf("OP_LE\tR%d\t:%s\t%d\n", GETARG_A(c),
                       mrb_sym2name(mrb, irep->syms[GETARG_B(c)]),
                        GETARG_C(c));
                break;
            case OP_GT:
                printf("OP_GT\tR%d\t:%s\t%d\n", GETARG_A(c),
                       mrb_sym2name(mrb, irep->syms[GETARG_B(c)]),
                        GETARG_C(c));
                break;
            case OP_GE:
                printf("OP_GE\tR%d\t:%s\t%d\n", GETARG_A(c),
                       mrb_sym2name(mrb, irep->syms[GETARG_B(c)]),
                        GETARG_C(c));
                break;
            case OP_EQ:
                printf("OP_EQ\tR%d\t:%s\t%d\n", GETARG_A(c),
                       mrb_sym2name(mrb, irep->syms[GETARG_B(c)]),
                        GETARG_C(c));
                break;

            case OP_STOP:
                printf("OP_STOP\n");
                break;

            case OP_ARRAY:
                printf("OP_ARRAY\tR%d\tR%d\t%d\n", GETARG_A(c), GETARG_B(c), GETARG_C(c));
                break;
            case OP_ARYCAT:
                printf("OP_ARYCAT\tR%d\tR%d\n", GETARG_A(c), GETARG_B(c));
                break;
            case OP_ARYPUSH:
                printf("OP_ARYPUSH\tR%d\tR%d\n", GETARG_A(c), GETARG_B(c));
                break;
            case OP_AREF:
                printf("OP_AREF\tR%d\tR%d\t%d\n", GETARG_A(c), GETARG_B(c), GETARG_C(c));
                break;
            case OP_APOST:
                printf("OP_APOST\tR%d\t%d\t%d\n", GETARG_A(c), GETARG_B(c), GETARG_C(c));
                break;
            case OP_STRING:
            {
                mrb_value s = irep->pool[GETARG_Bx(c)];

                s = mrb_str_dump(mrb, s);
                printf("OP_STRING\tR%d\t%s\n", GETARG_A(c), RSTRING_PTR(s));
            }
                break;
            case OP_STRCAT:
                printf("OP_STRCAT\tR%d\tR%d\n", GETARG_A(c), GETARG_B(c));
                break;
            case OP_HASH:
                printf("OP_HASH\tR%d\tR%d\t%d\n", GETARG_A(c), GETARG_B(c), GETARG_C(c));
                break;

            case OP_OCLASS:
                printf("OP_OCLASS\tR%d\n", GETARG_A(c));
                break;
            case OP_CLASS:
                printf("OP_CLASS\tR%d\t:%s\n", GETARG_A(c),
                       mrb_sym2name(mrb, irep->syms[GETARG_B(c)]));
                break;
            case OP_MODULE:
                printf("OP_MODULE\tR%d\t:%s\n", GETARG_A(c),
                       mrb_sym2name(mrb, irep->syms[GETARG_B(c)]));
                break;
            case OP_EXEC:
                printf("OP_EXEC\tR%d\tI(%d)\n", GETARG_A(c), n+GETARG_Bx(c));
                break;
            case OP_SCLASS:
                printf("OP_SCLASS\tR%d\tR%d\n", GETARG_A(c), GETARG_B(c));
                break;
            case OP_TCLASS:
                printf("OP_TCLASS\tR%d\n", GETARG_A(c));
                break;
            case OP_ERR:
                printf("OP_ERR\tL(%d)\n", GETARG_Bx(c));
                break;
            case OP_EPUSH:
                printf("OP_EPUSH\t:I(%d)\n", n+GETARG_Bx(c));
                break;
            case OP_ONERR:
                printf("OP_ONERR\t%03d\n", i+GETARG_sBx(c));
                break;
            case OP_RESCUE:
                printf("OP_RESCUE\tR%d\n", GETARG_A(c));
                break;
            case OP_RAISE:
                printf("OP_RAISE\tR%d\n", GETARG_A(c));
                break;
            case OP_POPERR:
                printf("OP_POPERR\t%d\n", GETARG_A(c));
                break;
            case OP_EPOP:
                printf("OP_EPOP\t%d\n", GETARG_A(c));
                break;

            default:
                printf("OP_unknown %d\t%d\t%d\t%d\n", GET_OPCODE(c),
                       GETARG_A(c), GETARG_B(c), GETARG_C(c));
                break;
        }
        mrb->gc().arena_restore(ai);
    }
    printf("\n");
#endif
}

void
codedump_all(mrb_state *mrb, int start)
{
    size_t i;

    for (i=start; i<mrb->irep_len; i++) {
        codedump(mrb, i);
    }
}

static int
codegen_start(mrb_state *mrb, parser_state *p)
{
    codegen_scope *scope = codegen_scope::create(mrb, 0, 0);

    if (!scope) {
        return -1;
    }
    scope->m_mrb = mrb;
    if (p->m_filename) {
        scope->m_filename = p->m_filename;
    }
    if (setjmp(scope->jmp) != 0) {
        return -1;
    }
    // prepare irep
    scope->codegen(p->m_tree, false);
    scope->m_mpool->mrb_pool_close();
    return 0;
}

int
mrb_generate_code(mrb_state *mrb, parser_state *p)
{
    int start = mrb->irep_len;
    int n;

    n = codegen_start(mrb, p);
    if (n < 0) return n;

    return start;
}
