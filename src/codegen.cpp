/*
** codegen.c - mruby code generator
**
** See Copyright Notice in mruby.h
*/

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdexcept>
#include <algorithm>

#include "mruby.h"
#include "mruby/compile.h"
#include "mruby/irep.h"
#include "mruby/numeric.h"
#include "mruby/string.h"
#include "mruby/node.h"
#include "mruby/NodeVisitor.h"
#include "mruby/debug.h"
#include "opcode.h"
#include "re.h"

#define CALL_MAXARGS 127

typedef mrb_ast_node node;
typedef struct mrb_parser_state parser_state;
namespace { // using anonymous namespace to inform compielr that those types is local to this file

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
    uint16_t m_lineno = 0;

    mrb_code *m_iseq = nullptr;
    uint16_t *lines = nullptr;
    int m_icapa = 0;

    mrb_irep *m_irep = nullptr;
    size_t m_pcapa = 0;
    int m_scapa = 0;

    int m_nlocals=0;
    int m_nregs=0;
    int m_ai=0;

    int m_idx=0;

    int debug_start_pos=0;
    uint16_t filename_index;
    parser_state* parser;
    std::vector<bool> m_val_stack;
    void finish();

public:
    codegen_scope() {
        m_ainfo = 0;
        m_mscope = false;
    }
    static codegen_scope *create(mrb_state *m_mrb, codegen_scope *m_prev, node *lv);
    static codegen_scope *create(mrb_state *mrb, codegen_scope *prev, const tLocals &lv);
    void genop(eOpEnum op,int a,int b,int c) {
        genop(MKOP_ABC(op, a, b, c));
    }
    void genop(eOpEnum op,int a,int b) {
        genop(MKOP_AsBx(op, a, b));
    }

    void genop(mrb_code i)
    {
        if (m_pc == m_icapa) {
            m_icapa *= 2;
            m_iseq = (mrb_code *)s_realloc(m_iseq, sizeof(mrb_code)*m_icapa);
            if (lines) {
                lines = (uint16_t*)s_realloc(lines, sizeof(uint16_t)*m_icapa);
                m_irep->lines = lines;
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
    int     new_lit(mrb_value val);
    int     gen_values(node *t, int val);
    int     new_msym(mrb_sym sym);
    int     new_sym(mrb_sym sym);
    int     lv_idx(mrb_sym id);
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
    void    gen_call_common(mrb_sym _sym, int sendv, int noop, int idx, int val, int n, int blk);
    int     do_PostCount(mrb_ast_node *t);
protected:
    void do_lambda_body(mrb_ast_node *body);
    void do_lambda_args(ArgsStore * args);
    int do_for_body(ForNode * tree);
    int do_lambda_internal(LambdaCommonNode *tree);
    int do_lambda_internal(DefCommonNode * tree);
    void scope_error();
    void walk_string(mrb_ast_node *n);
    void visit(DxstrNode * n) { assert(false); }
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
    void visit_loop(BinaryNode *n, eOpEnum op);
    void visit_DefCommon(DefCommonNode *n, eOpEnum op);
};

mrb_sym sym(mrb_ast_node *x) { return ((mrb_sym)(intptr_t)(x));}
int node_len(node *tree) {

    int n = 0;
    while (tree) {
        n++;
        tree = tree->right();
    }
    return n;
}

bool nosplat(node *t) {
    while (t) {
        if (t->left()->getType() == NODE_SPLAT)
            return false;
        t = t->right();
    }
    return true;
}
int codegen_start(mrb_state *mrb, parser_state *p) {

    codegen_scope *scope = codegen_scope::create(mrb, 0, 0);

    if (!scope || (setjmp(scope->jmp) != 0)) {
        return -1;
    }
    scope->m_mrb = mrb;
    scope->parser = p;
    scope->m_filename = p->m_filename;
    scope->filename_index = p->current_filename_index;
    // prepare irep
    scope->codegen(p->m_tree, false);
    scope->m_mpool->mrb_pool_close();
    return 0;
}

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
    if (p->m_filename && p->m_lineno) {
        m_mrb->sys.error_f("codegen error:%s:%d: %s\n", p->m_filename, p->m_lineno, message);
    }
    else {
        m_mrb->sys.error_f("codegen error: %s\n", message);
    }
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
    if (m_lastlabel == m_pc || m_pc <= 0) {
        genop(i);
        return;
    }
    /* peephole optimization */
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
        if (val)
            break;
        if (c0 == OP_MOVE) {
            if (GETARG_A(i) == GETARG_A(i0)) {
                m_iseq[m_pc-1] = MKOP_ABx(c1, GETARG_B(i0), GETARG_Bx(i));
                return;
            }
        }
        break;
    case OP_SETUPVAR:
        if (val)
            break;
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

            if (mrb_type(m_irep->m_pool[i]) == MRB_TT_STRING &&
                    RSTRING_LEN(m_irep->m_pool[i]) == 0) {
                m_pc--;
                return;
            }
        }
        break;
    default:
        break;
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

int codegen_scope::new_lit(mrb_value val)
{
    int i;


    switch (mrb_type(val)) {
    case MRB_TT_STRING:
        for (i=0; i<m_irep->plen; i++) {
            mrb_value pv = m_irep->m_pool[i];
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
            if (mrb_obj_equal(m_irep->m_pool[i], val))
                return i;
        }
        break;
    }

    if (m_irep->plen == m_pcapa) {
        m_pcapa *= 2;
        m_irep->m_pool = (mrb_value *)s_realloc(m_irep->m_pool, sizeof(mrb_value)*m_pcapa);
    }
    m_irep->m_pool[m_irep->plen] = val;
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

void codegen_scope::do_lambda_args(ArgsStore *args)
{
    mrb_aspec a;
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

    a = ((mrb_aspec)(ma & 0x1f) << 18)
            | ((mrb_aspec)(oa & 0x1f) << 13)
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
            pop_sp();
            //cursp()
            scope->genop_peep(MKOP_AB(OP_RETURN, scope->m_sp, OP_R_NORMAL), false);
            push_();
        }
    }
    scope->finish();

    return idx - m_idx;
}

mrb_sym codegen_scope::attrsym(mrb_sym a)
{
    const char *name;
    size_t len;
    char *name2;

    name = mrb_sym2name_len(m_mrb, a, len);
    name2 = (char *)palloc(len+1);
    memcpy(name2, name, len);
    name2[len] = '=';
    name2[len+1] = '\0';

    return mrb_intern2(m_mrb, name2, len+1);
}

int codegen_scope::gen_values(node *t, int val)
{
    int n = 0;
    int is_splat;

    while (t) {
        is_splat = (intptr_t)t->left()->getType() == NODE_SPLAT; // splat mode

        if (n >= 127 || is_splat) {
            if (val) {
                pop_sp(n);
                genop( MKOP_ABC(OP_ARRAY, m_sp, m_sp, n));
                push_();
                codegen(t->left(), true);
                pop_sp(2);
                if (is_splat) {
                    genop(MKOP_AB(OP_ARYCAT, m_sp, m_sp+1));
                }
                else {
                    genop(MKOP_AB(OP_ARYPUSH, m_sp, m_sp+1));
                }
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

void codegen_scope::gen_call_common(mrb_sym _sym, int sendv, int noop, int idx, int val, int n, int blk)
{
    pop_sp(n+1);
    {
        size_t len;
        const char *name = mrb_sym2name_len(m_mrb, _sym, len);

        eOpEnum op = OP_SEND;
        bool was_peep = false;
        if(!noop) {
            if (len == 1) {
                switch(name[0]) {
                case '+':
                    was_peep = true;
                    op = OP_ADD;
                    break;
                case '-':
                    was_peep = true;
                    op = OP_SUB;
                    break;
                case '*': op = OP_MUL; break;
                case '/': op = OP_DIV; break;
                case '<': op = OP_LT; break;
                case '>': op = OP_GT; break;
                }
            }
            else if(len==2 && name[1]=='=') {

                if (name[0] == '<')  {
                    op = OP_LE;
                }
                else if (name[0] == '>')  {
                    op = OP_GE;
                }
                else if (name[0] == '=')  {
                    op = OP_EQ;
                }
            }
        }
        if(op == OP_SEND) {
            if (sendv)
                n = CALL_MAXARGS;
            if (blk <= 0) { /* with block */
                op = OP_SENDB;
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

void codegen_scope::gen_call(CallNode *node, mrb_sym name, int sp, int val)
{
    mrb_sym _sym = name ? name : node->m_method;
    int n = 0, noop = 0, sendv = 0, blk = 0;

    codegen(node->m_receiver, true); /* receiver */

    int idx = new_msym(_sym);

    CommandArgs *tree = node->m_cmd_args;
    if (tree) {
        n = gen_values(tree->m_args, true);
        if (n < 0) {
            n = noop = sendv = 1;
            push_();
        }
    }
    if (sp) {
        eOpEnum op;
        if (sendv) {
            pop_sp();
            op=OP_ARYPUSH;
        }
        else {
            n++;
            op=OP_MOVE;
        }
        genop( MKOP_AB(op, m_sp, sp));
        push_();
    }
    if (tree && tree->m_blk) {
        noop = 1;
        codegen(tree->m_blk, true);
        pop_sp();
    }
    else {
        blk = m_sp;
    }
    gen_call_common(_sym, sendv, noop, idx, val, n, blk);
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
        void visit(IVarNode * n)    { common_assign(OP_SETIV,n->sym()); }
        void visit(CVarNode * n)    { common_assign(OP_SETCV,n->sym()); }
        void visit(GVarNode * n)    { common_assign(OP_SETGLOBAL,n->sym()); }
        void visit(ConstNode * n)   { common_assign(OP_SETCONST,n->sym()); }
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
        m_mrb->sys.print_f("unknown lhs %d\n", type);
        break;
    }
    if (val)
        push_();
}

void codegen_scope::gen_vmassignment(node *tree, int rhs, int val)
{
    int n = 0;

    if (tree->left()) {              /* pre */
        node *t = tree->left();
        n = 0;
        while (t) {
            genop( MKOP_ABC(OP_AREF, m_sp, rhs, n));
            gen_assignment(t->left(), m_sp, false);
            n++;
            t = t->right();
        }
    }
    node *t = tree->right();
    if (!t)
        return;
    int post = do_PostCount(t);
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
    else {
        pop_sp();
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
                *overflow = true;
                return 0;
            }
            result *= base;
            result -= n;
        }
        else {
            if ((MRB_INT_MAX - n)/base < result) {
                *overflow = true;
                return 0;
            }
            result *= base;
            result += n;
        }
        p++;
    }
    *overflow = false;
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
    if (s2)
        ainfo = s2->m_ainfo;
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
    char buf[2] = { '$',char(n->m_ref) };

    mrb_value str = mrb_str_new(m_mrb, buf, 2);
    int sym = new_sym( mrb_intern_str(m_mrb, str));
    genop( MKOP_ABx(OP_GETGLOBAL, m_sp, sym));
    push_();
}
void codegen_scope::visit(NthRefNode *n) {
    mrb_state *mrb = m_mrb;
    mrb_value fix = mrb_fixnum_value(n->m_ref);
    mrb_value str = mrb_str_buf_new(mrb, 4);

    mrb_str_buf_cat(str, "$", 1);
    mrb_str_buf_append(mrb, str, mrb_fixnum_to_str(mrb, fix, 10));
    int sym = new_sym( mrb_intern_str(mrb, str));
    genop( MKOP_ABx(OP_GETGLOBAL, m_sp, sym));
    push_();
}

void codegen_scope::visit(IntLiteralNode *n) {
    if (!m_val_stack.back())
        return;
    char *p = n->m_val;
    int base = n->m_base;
    mrb_code co;
    int overflow;

    mrb_int i = readint_mrb_int(p, base, m_negate, &overflow);
    if (overflow) {
        double f = readint_float(p, base);
        if(m_negate)
            f = -f;
        int off = new_lit(mrb_float_value(f));
        co = MKOP_ABx(OP_LOADL, m_sp, off);
    }
    else {
        if (i < MAXARG_sBx && i > -MAXARG_sBx) {
            co = MKOP_AsBx(OP_LOADI, m_sp, i);
        }
        else {
            int off = new_lit(mrb_fixnum_value(i));
            co = MKOP_ABx(OP_LOADL, m_sp, off);
        }
    }
    genop( co );
    push_();
}
void codegen_scope::visit(ArrayNode *node) {
    bool val = m_val_stack.back();
    int n = gen_values( node->child(), val);
    if( !val)
        return;
    if (n >= 0) {
        pop_sp(n);
        genop( MKOP_ABC(OP_ARRAY, m_sp, m_sp, n));
    }
    // no matter the n, if 'val', we push
    push_();
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

    push_();         /* room for receiver */
    if(node->hasParams() && node->args()) {
        mrb_ast_node *args = node->args();
        n = gen_values( args, true);
        if (n < 0) {
            n = sendv = 1;
            push_();
        }
    }
    if (node->hasParams() && node->block()) {
        codegen( node->block(), true);
        pop_sp();
    }
    else {
        genop( MKOP_A(OP_LOADNIL, m_sp));
    }
    pop_sp(n+1);
    if (sendv)
        n = CALL_MAXARGS;
    genop( MKOP_ABC(OP_SUPER, m_sp, 0, n));
    if (val)
        push_();
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
int codegen_scope::do_PostCount(mrb_ast_node *t)
{
    int post = 0;
    if (t->right()) {         /* post count */
        mrb_ast_node *p = t->right()->left();
        while (p) {
            post++;
            p = p->right();
        }
    }
    return post;
}

void codegen_scope::visit(MAsgnNode *node) {
    bool val = m_val_stack.back();
    mrb_ast_node *t = node->rhs();
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
            int post = do_PostCount(t);
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
        //        if (!val)
        //            pop_sp();
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
void codegen_scope::visit_loop(BinaryNode *n,eOpEnum op) {
    bool val = m_val_stack.back();

    loopinfo *lp = loop_push(LOOP_NORMAL);

    lp->pc1 = new_label();
    genop(MKOP_sBx(OP_JMP, 0));
    lp->pc2 = new_label();
    codegen(n->rhs(), false);
    dispatch(lp->pc1);
    codegen(n->lhs(), true);
    pop_sp();
    genop(MKOP_AsBx(op , m_sp, lp->pc2 - m_pc));

    loop_pop(val);

}

void codegen_scope::visit(WhileNode *n) {
    visit_loop(n,OP_JMPIF);
}
void codegen_scope::visit(UntilNode *n) {
    visit_loop(n,OP_JMPNOT);
}
void codegen_scope::visit(YieldNode *node) {
    bool val = m_val_stack.back();
    codegen_scope *s2 = this;
    int lv = 0, ainfo = 0;
    int n = 0, sendv = 0;

    while (!s2->m_mscope) {
        lv++;
        s2 = s2->m_prev;
        if (!s2)
            break;
    }
    if (s2)
        ainfo = s2->m_ainfo;
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
        genop( OP_RANGE, m_sp, m_sp, 0);
        push_();
    }
}
void codegen_scope::visit(Dot3Node *node) {
    bool val = m_val_stack.back();
    codegen( node->lhs(), val);
    codegen( node->rhs(), val);
    if (val) {
        pop_sp(2);
        genop( OP_RANGE, m_sp, m_sp, 1);
        push_();
    }
}
void codegen_scope::visit(AndNode *node) {
    bool val = m_val_stack.back();
    codegen(node->lhs(), true);
    int pos = new_label();
    pop_sp();
    genop(OP_JMPNOT, m_sp, 0);
    codegen(node->rhs(), val);
    dispatch(pos);
}
void codegen_scope::visit(OrNode *node) {
    bool val = m_val_stack.back();
    codegen(node->lhs(), true);
    int pos = new_label();
    pop_sp();
    genop(OP_JMPIF, m_sp, 0);
    codegen(node->rhs(), val);
    dispatch(pos);
}
void codegen_scope::visit(Colon2Node *node) {
    bool val = m_val_stack.back();
    int sym = new_sym( node->m_sym);

    codegen( node->m_val, true);
    pop_sp();
    genop( MKOP_ABx(OP_GETMCNST, m_sp, sym));
    if (val)
        push_();
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
        if (node->m_cmd_args->m_blk) {
            noop = 1;
            codegen(node->m_cmd_args->m_blk, true);
            pop_sp();
        }
        else
            blk = m_sp;
    }
    else
        blk = m_sp;

    gen_call_common(_sym, sendv, noop, idx, val, n, blk);
}
void codegen_scope::visit(IfNode *node) {
    bool val = m_val_stack.back();
    int pos1, pos2;
    mrb_ast_node *false_body = node->false_body(); //tree->right()->right()->left()

    codegen(node->cond(), true);
    pop_sp();
    pos1 = new_label();
    genop(MKOP_AsBx(OP_JMPNOT, m_sp, 0));

    codegen(node->true_body(), val);
    if (val ) {
        if(!node->true_body()) {
            genop(MKOP_A(OP_LOADNIL, m_sp));
            push_();
        }
        pop_sp(); // both cases 'e' and '!e' pop_sp if 'val'
    }
    if (false_body) {
        pos2 = new_label();
        genop(MKOP_sBx(OP_JMP, 0));
        dispatch(pos1);
        codegen(false_body, val);
        dispatch(pos2);
    }
    else {
        if (val) {
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
    const char *name = mrb_sym2name_len(m_mrb, sym, len);
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

    eOpEnum op = OP_SEND;
    bool was_peep = false;

    if( 1 == len ) {
        switch(name[0]) {
        case '+':
            was_peep = true;
            op = OP_ADD;
            break;
        case '-':
            was_peep = true;
            op = OP_SUB;
            break;
        case '*':   op = OP_MUL; break;
        case '/':   op = OP_DIV; break;
        case '<':   op = OP_LT;  break;
        case '>':   op = OP_GT;  break;
        }
    }
    else if( ( 2==len ) && name[1] == '=' ) {
        assert(false && "OpAsgn has no form <== or >== ");
    }
    mrb_code cd = MKOP_ABC(op, m_sp, idx, 1);
    if(was_peep)
        genop_peep(cd,val);
    else
        genop( cd );

    gen_assignment(node->lhs(), m_sp, val);

}
void codegen_scope::visit_DefCommon(DefCommonNode *n,eOpEnum op) {
    bool val = m_val_stack.back();
    int sym = new_msym(n->name());
    int base = m_idx;

    codegen_scope *s = codegen_scope::create(m_mrb, this, n->ve_locals());
    int idx = s->do_lambda_internal(n) - base;
    // SdefNode called "codegen( n->receiver(), true); pop_sp();" here
    mrb_code cd = (op==OP_TCLASS) ? MKOP_A(op, m_sp) : MKOP_AB(OP_SCLASS, m_sp, m_sp);
    genop( cd );
    push_();
    genop( MKOP_Abc(OP_LAMBDA, m_sp, idx, OP_L_METHOD));
    pop_sp();
    genop( MKOP_AB(OP_METHOD, m_sp, sym));
    if (val) {
        genop( MKOP_ABx(OP_LOADSYM, m_sp, sym));
        push_();
    }
}
void codegen_scope::visit(DefNode *n) {
    visit_DefCommon(n,OP_TCLASS);
}
void codegen_scope::visit(SdefNode *n) {

    codegen( n->receiver(), true);
    pop_sp();
    visit_DefCommon(n,OP_SCLASS);
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
    int sym = new_sym( m_mrb->intern2(REGEXP_CLASS,REGEXP_CLASS_CSTR_LEN));
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
    sym = new_sym( m_mrb->intern2("compile", 7));
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
    int sym = new_sym( m_mrb->intern2(REGEXP_CLASS,REGEXP_CLASS_CSTR_LEN));
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
    gen_literal_array(n->child(), false, val);
}
void codegen_scope::visit(SymbolsNode *n) {
    bool val = m_val_stack.back();
    gen_literal_array(n->child(), true, val);
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
    m_lineno = tree->lineno;
    if (m_irep && m_pc > 0 && filename_index != tree->filename_index) {
        m_irep->filename = parser->mrb_parser_get_filename(filename_index);
        mrb_debug_info_append_file(m_mrb, m_irep, debug_start_pos, m_pc);
        debug_start_pos = m_pc;
        filename_index = tree->filename_index;
        m_filename = parser->mrb_parser_get_filename(tree->filename_index);
    }
    m_val_stack.push_back(val);
    tree->accept(this);
    m_val_stack.pop_back();
}
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
    p->m_irep->m_pool = (mrb_value*)mrb->gc()._malloc(sizeof(mrb_value)*p->m_pcapa);
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
        p->lines = (uint16_t *)mrb->gc()._malloc(p->m_icapa*sizeof(uint16_t));
    }
    p->m_lineno = prev->m_lineno;
    // debug setting
    p->debug_start_pos = 0;
    if(p->m_filename) {
        mrb_debug_info_alloc(mrb, p->m_irep);
        p->m_irep->filename = p->m_filename;
        p->m_irep->lines = p->lines;
    }
    else {
        p->m_irep->debug_info = NULL;
    }
    p->parser = prev->parser;
    p->filename_index = prev->filename_index;
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
    size_t fname_len;
    char *fname;
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
    irep->m_pool = (mrb_value *)s_realloc(irep->m_pool, sizeof(mrb_value)*irep->plen);
    irep->syms = (mrb_sym *)s_realloc(irep->syms, sizeof(mrb_sym)*irep->slen);
    if (m_filename) {
        m_irep->filename = parser->mrb_parser_get_filename(filename_index);
        mrb_debug_info_append_file(mrb, m_irep, debug_start_pos, m_pc);
        //        irep->filename = m_filename;
        fname_len = strlen(m_filename);
        fname = (char *)m_mrb->gc()._malloc(fname_len+1); // todo: check fname
        memcpy(fname,m_filename,fname_len);
        fname[fname_len] = 0;
        irep->filename = fname;
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
        return;
    }

    if (tree) {
        codegen(tree, true);
        pop_sp();
    }

    loopinfo *loop = m_loop;
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


} // end of anonymous namespace

int mrb_generate_code(mrb_state *mrb, parser_state *p)
{
    int start = mrb->irep_len;

    int n = codegen_start(mrb, p);

    if (n < 0) return n;

    return start;
}
