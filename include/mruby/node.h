/*
** node.h - nodes of abstract syntax tree
**
** See Copyright Notice in mruby.h
*/
//FIXME: use statful allocators for Begin/Undef node vectors.
#pragma once
#include <cassert>
#include <stdint.h>
#include <string>
#include <vector>

#include "mruby.h"
#include "mruby/NodeVisitor.h"

struct mrb_parser_heredoc_info;
enum node_type : uint8_t {
    NODE_SCOPE,
    NODE_BLOCK,
    NODE_IF,
    NODE_CASE,
    NODE_WHILE,
    NODE_UNTIL,
    NODE_FOR,
    NODE_BREAK,
    NODE_NEXT,
    NODE_REDO,
    NODE_RETRY,
    NODE_BEGIN,
    NODE_RESCUE,
    NODE_ENSURE,
    NODE_AND,
    NODE_OR,
    //  NODE_NOT,
    NODE_MASGN,
    NODE_ASGN,
    NODE_OP_ASGN,
    NODE_CALL,
    NODE_FCALL,
    NODE_SUPER,
    NODE_ZSUPER,
    NODE_ARRAY,
    //    NODE_ZARRAY,
    NODE_HASH,
    NODE_RETURN,
    NODE_YIELD,
    NODE_LVAR,
    //    NODE_DVAR,
    NODE_GVAR,
    NODE_IVAR,
    NODE_CONST,
    NODE_CVAR,
    NODE_NTH_REF,
    NODE_BACK_REF,
    NODE_MATCH,
    NODE_INT,
    NODE_FLOAT,
    NODE_NEGATE,
    NODE_LAMBDA,
    NODE_SYM,
    NODE_STR,
    NODE_DSTR,
    NODE_XSTR,
    NODE_DXSTR,
    NODE_REGX,
    NODE_DREGX,
    NODE_ARG,
    NODE_SPLAT,
    NODE_BLOCK_ARG,
    NODE_DEF,
    NODE_SDEF,
    NODE_ALIAS,
    NODE_UNDEF,
    NODE_CLASS,
    NODE_MODULE,
    NODE_SCLASS,
    NODE_COLON2,
    NODE_COLON3,
    NODE_DOT2,
    NODE_DOT3,
    NODE_SELF,
    NODE_NIL,
    NODE_TRUE,
    NODE_FALSE,
    NODE_POSTEXE,
    NODE_DSYM,
    NODE_HEREDOC,
    NODE_LITERAL_DELIM,
    NODE_WORDS,
    NODE_SYMBOLS,
    //    NODE_WHEN,
    //    NODE_METHOD,
    //    NODE_FBODY,
    //    NODE_CFUNC,
    //    NODE_OPT_N,
    //    NODE_ITER,
    //    NODE_CDECL,
    //    NODE_CVASGN,
    //    NODE_CVDECL,
    //    NODE_VCALL,
    //    NODE_MATCH2,
    //    NODE_MATCH3,
    //    NODE_DREGX_ONCE,
    //    NODE_LIST,
    //    NODE_ARGSCAT,
    //    NODE_ARGSPUSH,
    //    NODE_TO_ARY,
    //    NODE_SVALUE,
    //    NODE_CREF,
    //    NODE_FLIP2,
    //    NODE_FLIP3,
    //    NODE_ATTRSET,
    //    NODE_DEFINED,
    //    NODE_NEWLINE,
    //    NODE_ALLOCA,
    //    NODE_DMETHOD,
    //    NODE_BMETHOD,
    //    NODE_MEMO,
    //    NODE_IFUNC,
    //    NODE_ATTRASGN,
    NODE_LAST
};
typedef std::vector<mrb_sym> tLocals;
/* AST node structure */
struct mrb_ast_node {
    uint16_t lineno;
    uint16_t filename_index;
    virtual node_type getType() const=0;
    virtual mrb_ast_node *left() const=0;
    virtual void left(mrb_ast_node *v)=0;
    virtual mrb_ast_node *right() const=0;
    virtual void right(mrb_ast_node *v)=0;
    virtual void init(mrb_ast_node *a,mrb_ast_node *b,uint16_t lin,uint16_t f) =0;
    virtual void accept(NodeVisitor *v) { assert(false); }
    void locationInit(uint16_t lin, uint16_t f) {
        lineno = lin;
        filename_index = f;
    }
};
struct mrb_ast_list_like_node : public mrb_ast_node {
    virtual node_type getType() const;
    virtual mrb_ast_node *left() const {return m_car; }
    virtual void left(mrb_ast_node *v) {
        m_car=v;
    }
    virtual mrb_ast_node *right() const {return m_cdr; }
    virtual void right(mrb_ast_node *v) { m_cdr=v; }
    virtual void init(mrb_ast_node *a,mrb_ast_node *b,uint16_t lin,uint16_t f) override {
        m_car = a;
        m_cdr = b;
        locationInit(lin,f);
    }
protected:
    mrb_ast_node *m_car, *m_cdr;
};
struct UpdatedNode : public mrb_ast_node {
    mrb_ast_node *left() const { assert(false); return nullptr; }
    void left(mrb_ast_node *v) { assert(false); }
    mrb_ast_node *right() const { assert(false); return nullptr; }
    void right(mrb_ast_node *v) { assert(false); }
    virtual void accept(NodeVisitor *v) = 0;
    virtual void init(mrb_ast_node *a,mrb_ast_node *b,uint16_t lin,uint16_t f) override {
        assert(false);
    }
};
struct ArgsStore {
    ArgsStore(mrb_ast_node *m, mrb_ast_node *opt, mrb_sym rest, mrb_ast_node *m2, mrb_sym blk) {
        m_mandatory = m;
        m_opt = opt;
        m_rest = rest;
        m_post_mandatory = m2;
        m_blk = blk;
    }
    mrb_ast_node *m_mandatory;
    mrb_ast_node *m_opt;
    mrb_sym m_rest;
    mrb_ast_node *m_post_mandatory;
    mrb_sym m_blk;
};
struct CommandArgs {
    CommandArgs(mrb_ast_node *args, mrb_ast_node *blk=nullptr) {
        m_args = args;
        m_blk = blk;
    }

    void accept(NodeVisitor *v) { assert(!"NOT A NODE"); }
    mrb_ast_node *  block() const { return m_blk; }
    mrb_ast_node *  m_args;
    mrb_ast_node *  m_blk;
};

struct SymContainerNode : public UpdatedNode {
    SymContainerNode(mrb_sym v)  : m_sym(v) {}
    mrb_sym sym() { return m_sym; }
    mrb_sym m_sym;
};
struct Colon3Node : public SymContainerNode {
    Colon3Node(mrb_sym v)  : SymContainerNode(v) {}
    void            accept(NodeVisitor *v) { v->visit(this); }
    node_type       getType() const { return NODE_COLON3; }
};
struct Colon2Node : public UpdatedNode {
    Colon2Node(mrb_ast_node *n,mrb_sym v)  :m_sym(v), m_val(n) {}
    node_type       getType() const { return NODE_COLON2; }
    void            accept(NodeVisitor *v) { v->visit(this); }
    mrb_sym         m_sym;
    mrb_ast_node *  m_val;
};

struct GVarNode : public SymContainerNode {
    GVarNode(mrb_sym v)  : SymContainerNode(v) {}
    node_type       getType() const { return NODE_GVAR; }
    void            accept(NodeVisitor *v) { v->visit(this); }
};
struct IVarNode : public SymContainerNode {
    IVarNode(mrb_sym v)  : SymContainerNode(v) {}
    node_type       getType() const { return NODE_IVAR; }
    void            accept(NodeVisitor *v) { v->visit(this); }
};
struct LVarNode : public SymContainerNode {
    LVarNode(mrb_sym v)  : SymContainerNode(v) {}
    node_type       getType() const { return NODE_LVAR; }
    void            accept(NodeVisitor *v) { v->visit(this); }
};
struct CVarNode : public SymContainerNode {
    CVarNode(mrb_sym v)  : SymContainerNode(v) {}
    node_type       getType() const { return NODE_CVAR; }
    void            accept(NodeVisitor *v) { v->visit(this); }
};
struct ConstNode : public SymContainerNode {
    ConstNode(mrb_sym v)  : SymContainerNode(v) {}
    node_type   getType() const { return NODE_CONST; }
    void        accept(NodeVisitor *v) { v->visit(this); }
};
struct UndefNode : public UpdatedNode {
    UndefNode(mrb_sym v)   { m_syms.push_back(v);}
    node_type   getType() const { return NODE_UNDEF; }
    void        push_back(mrb_sym v) { m_syms.push_back(v);}
    std::vector<mrb_sym> m_syms;
    void        accept(NodeVisitor *v) { v->visit(this); }
};
struct SymNode : public SymContainerNode {
    SymNode(mrb_sym v)  : SymContainerNode(v) {}
    void accept(NodeVisitor *v) { v->visit(this); }
    node_type getType() const { return NODE_SYM; }
};
struct ArgNode : public SymContainerNode {
    ArgNode(mrb_sym v)  : SymContainerNode(v) {}
    node_type getType() const { return NODE_ARG; }
    void accept(NodeVisitor *v) { v->visit(this); }
};
struct NilNode : public UpdatedNode {
    node_type getType() const { return NODE_NIL; }
    void accept(NodeVisitor *v) { v->visit(this); }
};
struct TrueNode : public UpdatedNode {
    node_type getType() const { return NODE_TRUE; }
    void accept(NodeVisitor *v) { v->visit(this); }
};
struct FalseNode : public UpdatedNode {
    node_type getType() const { return NODE_FALSE; }
    void accept(NodeVisitor *v) { v->visit(this); }
};
struct SelfNode : public UpdatedNode {
    node_type getType() const { return NODE_SELF; }
    void accept(NodeVisitor *v) { v->visit(this); }
};
struct ZsuperNode : public UpdatedNode {
    node_type getType() const { return NODE_ZSUPER; }
    CommandArgs *cmd_args = nullptr;
    void accept(NodeVisitor *v) { v->visit(this); }
};
struct RedoNode : public UpdatedNode {
    node_type getType() const { return NODE_REDO; }
    void accept(NodeVisitor *v) { v->visit(this); }
};
struct RetryNode : public UpdatedNode {
    node_type getType() const { return NODE_RETRY; }
    void accept(NodeVisitor *v) { v->visit(this); }
};
struct LiteralDelimNode : public UpdatedNode {
    node_type   getType() const { return NODE_LITERAL_DELIM; }
    void        accept(NodeVisitor *v) { v->visit(this); }
};
struct BackRefNode : public UpdatedNode {
    BackRefNode(int v)  : m_ref(v) {}
    node_type   getType() const { return NODE_BACK_REF; }
    void        accept(NodeVisitor *v) { v->visit(this); }
    int         m_ref;
};
struct NthRefNode : public UpdatedNode {
    NthRefNode(int v)  : m_ref(v) {}
    virtual node_type getType() const { return NODE_NTH_REF; }
    int m_ref;
    void accept(NodeVisitor *v) { v->visit(this); }
};
struct UnaryNode : public UpdatedNode {
    UnaryNode(mrb_ast_node *n) : m_chld(n) {}
    mrb_ast_node *child() {return m_chld;}
    mrb_ast_node *m_chld;
};
struct DstrNode : public UnaryNode {
    DstrNode(mrb_ast_node *arglist)  : UnaryNode(arglist) {}
    virtual node_type getType() const { return NODE_DSTR; }
    void accept(NodeVisitor *v) { v->visit(this); }
};
struct StrCommonNode : public UpdatedNode {
    StrCommonNode(char *_str, size_t len) : m_str(_str),m_length(len){}
    char *m_str;
    size_t m_length;
};
struct StrNode : public StrCommonNode {
    StrNode(char *_str, size_t len) : StrCommonNode(_str,len){}
    virtual node_type getType() const { return NODE_STR; }
    void accept(NodeVisitor *v) { v->visit(this); }
};
struct XstrNode : public StrCommonNode {
    XstrNode(char *_str, size_t len) : StrCommonNode(_str,len){}
    virtual node_type getType() const { return NODE_XSTR; }
    void accept(NodeVisitor *v) { v->visit(this); }
};
struct RegxNode : public UpdatedNode {
    RegxNode(const char *_exp,const char *_str) : m_expr(_exp),m_str(_str) {}
    node_type       getType() const { return NODE_REGX; }
    void            accept(NodeVisitor *v) { v->visit(this); }
    const char *    m_expr;
    const char *    m_str;
};
struct DregxNode : public UpdatedNode {
    DregxNode(mrb_ast_node *a,mrb_ast_node *b) : m_a(a),m_b(b) {}
    virtual node_type getType() const { return NODE_DREGX; }
    void accept(NodeVisitor *v) { v->visit(this); }
    mrb_ast_node *m_a;
    mrb_ast_node *m_b;
};

struct HeredocNode : public UpdatedNode {
    HeredocNode(mrb_parser_heredoc_info*v)  : m_doc(v) {}
    virtual node_type getType() const { return NODE_HEREDOC; }
    mrb_parser_heredoc_info* contents() {return m_doc;}
    void accept(NodeVisitor *v) { v->visit(this); }
protected:
    mrb_parser_heredoc_info* m_doc;
};
struct DxstrNode : public UnaryNode {
    DxstrNode(mrb_ast_node *arglist)  : UnaryNode(arglist) {}
    virtual node_type getType() const { return NODE_DXSTR; }
    void accept(NodeVisitor *v) { v->visit(this); }
};
struct BlockArgNode : public UnaryNode {
    BlockArgNode(mrb_ast_node *arglist)  : UnaryNode(arglist) {}
    virtual node_type getType() const { return NODE_BLOCK_ARG; }
    void accept(NodeVisitor *v) { v->visit(this); }
};
struct SymbolsNode : public UnaryNode {
    SymbolsNode(mrb_ast_node *arglist)  : UnaryNode(arglist) {}
    virtual node_type getType() const { return NODE_SYMBOLS; }
    void accept(NodeVisitor *v) { v->visit(this); }
};
struct WordsNode : public UnaryNode {
    WordsNode(mrb_ast_node *arglist)  : UnaryNode(arglist) {}
    virtual node_type getType() const { return NODE_WORDS; }
    void accept(NodeVisitor *v) { v->visit(this); }
};
struct PostExeNode : public UnaryNode {
    PostExeNode(mrb_ast_node *arglist)  : UnaryNode(arglist) {}
    virtual node_type getType() const { return NODE_POSTEXE; }
    void accept(NodeVisitor *v) { v->visit(this); }
};
struct BreakNode : public UnaryNode {
    BreakNode(mrb_ast_node *arglist)  : UnaryNode(arglist) {}
    virtual node_type getType() const { return NODE_BREAK; }
    void accept(NodeVisitor *v) { v->visit(this); }
};
struct NegateNode : public UnaryNode {
    NegateNode(mrb_ast_node *chld)  : UnaryNode(chld) {}
    virtual node_type getType() const { return NODE_NEGATE; }
    void accept(NodeVisitor *v) { v->visit(this); }
};
struct NextNode : public UnaryNode {
    NextNode(mrb_ast_node *chld)  : UnaryNode(chld) {}
    virtual node_type getType() const { return NODE_NEXT; }
    void accept(NodeVisitor *v) { v->visit(this); }
};
struct ArrayNode : public UnaryNode {
    ArrayNode(mrb_ast_node *arglist)  : UnaryNode(arglist) {}
    virtual node_type getType() const { return NODE_ARRAY; }
    void accept(NodeVisitor *v) { v->visit(this); }
};
struct SplatNode : public UnaryNode {
    SplatNode(mrb_ast_node *arglist)  : UnaryNode(arglist) {}
    virtual node_type getType() const { return NODE_SPLAT; }
    void accept(NodeVisitor *v) { v->visit(this); }
};
struct ReturnNode : public UnaryNode {
    ReturnNode(mrb_ast_node *arglist)  : UnaryNode(arglist) {}
    virtual node_type getType() const { return NODE_RETURN; }
    void accept(NodeVisitor *v) { v->visit(this); }
};
struct YieldNode : public UnaryNode {
    YieldNode(mrb_ast_node *arglist)  : UnaryNode(arglist) {}
    virtual node_type getType() const { return NODE_YIELD; }
    void accept(NodeVisitor *v) { v->visit(this); }
};
struct SuperNode : public UpdatedNode {
    SuperNode(CommandArgs *carg) : cmd_args(carg) {}
    virtual node_type getType() const { return NODE_SUPER; }
    mrb_ast_node *args() const { return cmd_args->m_args;}
    mrb_ast_node *block() const { return cmd_args->m_blk;}
    bool hasParams() const { return cmd_args!=0; }
    void accept(NodeVisitor *v) { v->visit(this); }
    CommandArgs *cmd_args;
};
struct HashNode : public UnaryNode {
    HashNode(mrb_ast_node *arglist)  : UnaryNode(arglist) {}
    virtual node_type getType() const { return NODE_HASH; }
    void accept(NodeVisitor *v) { v->visit(this); }
};

struct DsymNode : public UpdatedNode {
    DsymNode(DstrNode *s)  : m_str(s) {}
    virtual node_type getType() const { return NODE_DSYM; }
    void            accept(NodeVisitor *v) { v->visit(this); }
    DstrNode *m_str;
};
struct BeginNode : public UpdatedNode {
    BeginNode(mrb_ast_node *arglist) {
        if(arglist)
            m_entries.push_back(arglist);
    }
    void push_back(mrb_ast_node *n) {m_entries.push_back(n);}
    virtual node_type getType() const { return NODE_BEGIN; }
    std::vector<mrb_ast_node *> m_entries;
    void accept(NodeVisitor *v) { v->visit(this); }
};
struct FloatLiteralNode : public UpdatedNode {
    FloatLiteralNode(const char *str)  : m_val(strdup(str)) {}
    virtual node_type getType() const { return NODE_FLOAT; }
    char * m_val;
    mrb_float value() const { return str_to_mrb_float(m_val);}
    void            accept(NodeVisitor *v) { v->visit(this); }
};
struct IntLiteralNode : public UpdatedNode {
    IntLiteralNode(const char *str,int base)  : m_val(strdup(str)),m_base(base) {}
    virtual node_type getType() const { return NODE_INT; }
    char * m_val;
    int m_base;
    void            accept(NodeVisitor *v) { v->visit(this); }
};
struct AliasNode : public UpdatedNode {
    AliasNode(mrb_sym from,mrb_sym to) : m_from(from),m_to(to) {}
    virtual node_type getType() const { return NODE_ALIAS; }
    mrb_sym m_from;
    mrb_sym m_to;
    void            accept(NodeVisitor *v) { v->visit(this); }
};

struct BinaryNode : public UpdatedNode {
    BinaryNode(mrb_ast_node *l,mrb_ast_node *r) : m_lhs(l),m_rhs(r) {}
    mrb_ast_node *lhs() {return m_lhs;}
    mrb_ast_node *rhs() {return m_rhs;}
    mrb_ast_node *m_lhs;
    mrb_ast_node *m_rhs;
};
struct ScopeNode : public UpdatedNode {
    ScopeNode(const tLocals &l,mrb_ast_node *b) : m_locals(l),m_body(b) {}
    ScopeNode(mrb_ast_node *b) : m_body(b) {}
    virtual node_type       getType() const { return NODE_SCOPE; }
    tLocals &       locals() {return m_locals;}
    mrb_ast_node *  body() {return m_body;}
    tLocals         m_locals;
    mrb_ast_node *  m_body;
    void            accept(NodeVisitor *v) { v->visit(this); }
};

struct MAsgnNode : public BinaryNode {
    MAsgnNode(mrb_ast_node *l,mrb_ast_node *r) : BinaryNode(l,r) {}
    virtual node_type getType() const { return NODE_MASGN; }
    void            accept(NodeVisitor *v) { v->visit(this); }
};
struct WhileNode : public BinaryNode {
    WhileNode(mrb_ast_node *l,mrb_ast_node *r) : BinaryNode(l,r) {}
    virtual node_type getType() const { return NODE_WHILE; }
    void            accept(NodeVisitor *v) { v->visit(this); }
};
struct UntilNode : public BinaryNode {
    UntilNode(mrb_ast_node *l,mrb_ast_node *r) : BinaryNode(l,r) {}
    virtual node_type getType() const { return NODE_UNTIL; }
    void            accept(NodeVisitor *v) { v->visit(this); }
};
struct Dot2Node : public BinaryNode {
    Dot2Node(mrb_ast_node *l,mrb_ast_node *r) : BinaryNode(l,r) {}
    virtual node_type getType() const { return NODE_DOT2; }
    void            accept(NodeVisitor *v) { v->visit(this); }
};
struct Dot3Node : public BinaryNode {
    Dot3Node(mrb_ast_node *l,mrb_ast_node *r) : BinaryNode(l,r) {}
    virtual node_type getType() const { return NODE_DOT3; }
    void            accept(NodeVisitor *v) { v->visit(this); }
};
struct AndNode : public BinaryNode {
    AndNode(mrb_ast_node *l,mrb_ast_node *r) : BinaryNode(l,r) {}
    virtual node_type       getType() const { return NODE_AND; }
    void            accept(NodeVisitor *v) { v->visit(this); }
};
struct OrNode : public BinaryNode {
    OrNode(mrb_ast_node *l,mrb_ast_node *r) : BinaryNode(l,r) {}
    node_type       getType() const { return NODE_OR; }
    void            accept(NodeVisitor *v) { v->visit(this); }
};
struct AsgnNode : public BinaryNode {
    AsgnNode(mrb_ast_node *l,mrb_ast_node *r) : BinaryNode(l,r) { }
    node_type       getType() const { return NODE_ASGN; }
    void            accept(NodeVisitor *v) { v->visit(this); }
};
struct OpAsgnNode : public BinaryNode {
    OpAsgnNode(mrb_ast_node *l,mrb_sym op,mrb_ast_node *r) :
        BinaryNode(l,r),op_sym(op) { }
    node_type       getType() const { return NODE_OP_ASGN; }
    void            accept(NodeVisitor *v) { v->visit(this); }
    mrb_sym         op_sym;
};
struct CallCommonNode : public UpdatedNode {
    CallCommonNode(mrb_ast_node *a, mrb_sym b, CommandArgs *c) :
        m_receiver(a),m_method(b),m_cmd_args(c)
    {}
    mrb_ast_node *  m_receiver;
    mrb_sym         m_method;
    CommandArgs *   m_cmd_args;
};

struct CallNode : public CallCommonNode {
    CallNode(mrb_ast_node *a, mrb_sym b, CommandArgs *c) :
        CallCommonNode(a,b,c) {}
    node_type       getType() const { return NODE_CALL; }
    void            accept(NodeVisitor *v) { v->visit(this); }
};
struct FCallNode : public CallCommonNode {
    FCallNode(mrb_ast_node *a, mrb_sym b, CommandArgs *c) : CallCommonNode(a,b,c) {}
    node_type       getType() const { return NODE_FCALL; }
    void            accept(NodeVisitor *v) { v->visit(this); }
};
struct LambdaCommonNode : public UpdatedNode {
    LambdaCommonNode(const tLocals &lc,ArgsStore *arg,mrb_ast_node *bd) :
        m_locals(lc),m_args(arg),m_body(bd)
    {}
    ArgsStore *     args() { return m_args;}
    mrb_ast_node *  body() { return m_body;}
    tLocals         locals() { return m_locals;}
protected:
    tLocals         m_locals;
    ArgsStore *     m_args;
    mrb_ast_node *  m_body;
};

struct BlockNode : public LambdaCommonNode {
    BlockNode(const tLocals &lc,ArgsStore *arg,mrb_ast_node *bd) :
        LambdaCommonNode(lc,arg,bd) {}
    virtual node_type       getType() const { return NODE_BLOCK; }
    void            accept(NodeVisitor *v) { v->visit(this); }
};
struct LambdaNode : public LambdaCommonNode {
    LambdaNode(const tLocals &lc,ArgsStore *arg,mrb_ast_node *bd) :
        LambdaCommonNode(lc,arg,bd) {}
    virtual node_type       getType() const { return NODE_LAMBDA; }
    void            accept(NodeVisitor *v) { v->visit(this); }
};
struct IfNode : public UpdatedNode {
    IfNode(mrb_ast_node *c,mrb_ast_node *t,mrb_ast_node *f) :
        m_cond(c),m_tr(t),m_fl(f)
    {}
    virtual node_type       getType() const { return NODE_IF; }
    void            accept(NodeVisitor *v) { v->visit(this); }
    mrb_ast_node *  cond() {return m_cond;}
    mrb_ast_node *  true_body() {return m_tr;}
    mrb_ast_node *  false_body() {return m_fl;}
protected:
    mrb_ast_node *  m_cond;
    mrb_ast_node *  m_tr;
    mrb_ast_node *  m_fl;
};
struct CaseNode : public UpdatedNode {
    CaseNode(mrb_ast_node *c,mrb_ast_node *t) : m_cond(c),m_cases(t) {}
    virtual node_type       getType() const { return NODE_CASE; }
    mrb_ast_node *  switched_on() {return m_cond;}
    mrb_ast_node *  cases() {return m_cases;}
    void            accept(NodeVisitor *v) { v->visit(this); }
protected:
    mrb_ast_node *  m_cond;
    mrb_ast_node *  m_cases;
};
struct RescueNode : public UpdatedNode {
    RescueNode(mrb_ast_node *bd,mrb_ast_node *rs,mrb_ast_node *re) :
        m_body(bd),m_rescue(rs),m_else(re)
    {}
    virtual node_type       getType() const { return NODE_RESCUE; }
    mrb_ast_node *  body() {return m_body;}
    mrb_ast_node *  rescue() {return m_rescue;}
    mrb_ast_node *  r_else() {return m_else;}
    void            accept(NodeVisitor *v) { v->visit(this); }
protected:
    mrb_ast_node *m_body;
    mrb_ast_node *m_rescue;
    mrb_ast_node *m_else;
};
struct EnsureNode : public UpdatedNode {
    EnsureNode(mrb_ast_node *bd,ScopeNode *en) : m_body(bd),m_ensure(en) {}
    virtual node_type       getType() const { return NODE_ENSURE; }
    mrb_ast_node *  body() {return m_body;}
    ScopeNode *     ensure() {return m_ensure;}
    void            accept(NodeVisitor *v) { v->visit(this); }
protected:
    mrb_ast_node *  m_body;
    ScopeNode *     m_ensure;
};
struct DefCommonNode : public UpdatedNode {
    DefCommonNode( mrb_sym n, const tLocals &l,ArgsStore *args, mrb_ast_node *b) :
        m_name(n),v_locals(l),m_args(args),m_body(b) {}
    mrb_ast_node *  locals() {return m_locals;}
    tLocals         ve_locals() {return v_locals;}
    ArgsStore *     args() {return m_args;}
    mrb_ast_node *  body() {return m_body;}
    mrb_sym         name() {return m_name;}
protected:
    mrb_sym         m_name;
    mrb_ast_node *  m_locals;
    tLocals         v_locals;
    ArgsStore *     m_args;
    mrb_ast_node *  m_body;
};

struct DefNode : public DefCommonNode {
    DefNode( mrb_sym n, const tLocals &l,ArgsStore *args, mrb_ast_node *b) :
        DefCommonNode(n,l,args,b) {}

    virtual node_type       getType() const { return NODE_DEF; }
    void            accept(NodeVisitor *v) { v->visit(this); }
};
struct SdefNode : public DefCommonNode {
    SdefNode( mrb_ast_node *r,mrb_sym n, const tLocals &l,ArgsStore *args, mrb_ast_node *b) :
        DefCommonNode(n,l,args,b),m_receiver(r) {}
    virtual node_type       getType() const { return NODE_SDEF; }
    mrb_ast_node *  receiver() {return m_receiver;}
    void            accept(NodeVisitor *v) { v->visit(this); }
protected:
    mrb_ast_node *  m_receiver;
};
struct ForNode : public UpdatedNode {
    ForNode( mrb_ast_node *v,mrb_ast_node *obj, mrb_ast_node *b) :
        m_var(v),m_obj(obj),m_body(b) {}
    virtual node_type       getType() const { return NODE_FOR; }
    mrb_ast_node *  var() {return m_var;}
    mrb_ast_node *  object() {return m_obj;}
    mrb_ast_node *  body() {return m_body;}
    void            accept(NodeVisitor *v) { v->visit(this); }
protected:
    mrb_ast_node *  m_var;
    mrb_ast_node *  m_obj;
    mrb_ast_node *  m_body;
};

struct ClassifierNode : public UpdatedNode {
    ClassifierNode(mrb_ast_node *ob,ScopeNode *sc) : m_receiver(ob),m_scope(sc) {}
    ScopeNode *     scope() { return m_scope;}
    mrb_ast_node *  receiver() { return m_receiver;}
protected:
    mrb_ast_node *  m_receiver;
    ScopeNode *     m_scope;
};
struct SclassNode : public ClassifierNode {
    SclassNode(mrb_ast_node *ob,ScopeNode *sc) : ClassifierNode(ob,sc){}
    virtual node_type       getType() const { return NODE_SCLASS; }
    void            accept(NodeVisitor *v) { v->visit(this); }
};

struct ClassNode : public ClassifierNode {
    ClassNode(mrb_ast_node *ob,mrb_ast_node *su,ScopeNode *sc) : ClassifierNode(ob,sc),
        m_super(su) {}
    virtual node_type       getType() const { return NODE_CLASS; }
    mrb_ast_node *  super() { return m_super;}
protected:
    mrb_ast_node *  m_super;
    void            accept(NodeVisitor *v) { v->visit(this); }
};
struct ModuleNode : public ClassifierNode {
    ModuleNode(mrb_ast_node *ob,ScopeNode *sc) : ClassifierNode(ob,sc) {}
    virtual node_type       getType() const { return NODE_MODULE; }
    void            accept(NodeVisitor *v) { v->visit(this); }
};
