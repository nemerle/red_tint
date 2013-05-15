/*
** mruby/compile.h - mruby parser
**
** See Copyright Notice in mruby.h
*/
#pragma once

#include <string>
#include <setjmp.h>
#include <vector>
#include <memory>

#include "mruby.h"
#include "mruby/node.h"

struct mrb_parser_state;

/* load context */
struct mrbc_context {
    mrb_sym *syms;
    int slen;
    char *filename;
    short lineno;
    int (*partial_hook)(mrb_parser_state*);
    void *partial_data;
    mrb_bool capture_errors:1;
    mrb_bool dump_result:1;
    mrb_bool no_exec:1;
};
/* lexer states */
enum mrb_lex_state_enum {
    EXPR_BEG,                   /* ignore newline, +/- is a sign. */
    EXPR_END,                   /* newline significant, +/- is an operator. */
    EXPR_ENDARG,                /* ditto, and unbound braces. */
    EXPR_ENDFN,                 /* ditto, and unbound braces. */
    EXPR_ARG,                   /* newline significant, +/- is an operator. */
    EXPR_CMDARG,                /* newline significant, +/- is an operator. */
    EXPR_MID,                   /* newline significant, +/- is an operator. */
    EXPR_FNAME,                 /* ignore newline, no reserved words. */
    EXPR_DOT,                   /* right after `.' or `::', no reserved words. */
    EXPR_CLASS,                 /* immediate after `class', no here document. */
    EXPR_VALUE,                 /* alike EXPR_BEG but label is disallowed. */
    EXPR_MAX_STATE
};

/* saved error message */
struct mrb_parser_message {
    int lineno;
    int column;
    char* message;
};

#define STR_FUNC_PARSING 0x01
#define STR_FUNC_EXPAND  0x02
#define STR_FUNC_REGEXP  0x04
#define STR_FUNC_WORD    0x08
#define STR_FUNC_SYMBOL  0x10
#define STR_FUNC_ARRAY   0x20
#define STR_FUNC_HEREDOC 0x40
#define STR_FUNC_XQUOTE  0x80

enum mrb_string_type {
    str_not_parsing  = (0),
    str_squote   = (STR_FUNC_PARSING),
    str_dquote   = (STR_FUNC_PARSING|STR_FUNC_EXPAND),
    str_regexp   = (STR_FUNC_PARSING|STR_FUNC_REGEXP|STR_FUNC_EXPAND),
    str_sword    = (STR_FUNC_PARSING|STR_FUNC_WORD|STR_FUNC_ARRAY),
    str_dword    = (STR_FUNC_PARSING|STR_FUNC_WORD|STR_FUNC_ARRAY|STR_FUNC_EXPAND),
    str_ssym     = (STR_FUNC_PARSING|STR_FUNC_SYMBOL),
    str_ssymbols = (STR_FUNC_PARSING|STR_FUNC_SYMBOL|STR_FUNC_ARRAY),
    str_dsymbols = (STR_FUNC_PARSING|STR_FUNC_SYMBOL|STR_FUNC_ARRAY|STR_FUNC_EXPAND),
    str_heredoc  = (STR_FUNC_PARSING|STR_FUNC_HEREDOC),
    str_xquote   = (STR_FUNC_PARSING|STR_FUNC_XQUOTE|STR_FUNC_EXPAND)
};

/* heredoc structure */
struct mrb_parser_heredoc_info {
    mrb_bool allow_indent:1;
    mrb_bool line_head:1;
    enum mrb_string_type type;
    const char *term;
    int term_len;
    mrb_ast_node *doc;
};

#ifndef MRB_PARSER_BUF_SIZE
# define MRB_PARSER_BUF_SIZE 1024
#endif
//struct mrb_pool;
/* parser structure */
struct mrb_lexer_state {
    int     paren_nest;
    char    buf[MRB_PARSER_BUF_SIZE];
    int     bidx;
    int     toklen() const { return bidx;}
    const char *tok() const {return buf;}
    int     toklast();
    void    tokadd(int c);
    bool    tokfix();

};
struct mrb_parser_state {
    mrb_state *m_mrb;
    mrb_pool *pool;
    mrb_ast_node *cells;
    std::string source;
    const char *s, *send;
#ifdef ENABLE_STDIO
    FILE *f;
#endif
    mrbc_context *m_cxt;
    const char *m_filename;
    int m_lineno;
    int m_column;
    typedef std::vector<mrb_sym> tLocals;
    typedef std::vector<tLocals> tLocalsStack;
    typedef std::vector<tLocalsStack *> tLocalsContext;
    tLocalsContext m_contexts;
    tLocalsStack *m_locals_stack;

    mrb_lex_state_enum m_lstate;
    mrb_ast_node *m_lex_strterm; /* (type nest_level beg . end) */
    mrb_lexer_state m_lexer;
    unsigned int cond_stack;
    unsigned int cmdarg_stack;
    int lpar_beg;
    int in_def, in_single;
    bool m_cmd_start;

    mrb_ast_node *pb;
    mrb_ast_node *heredocs;	/* list of mrb_parser_heredoc_info* */
    mrb_ast_node *parsing_heredoc;
    mrb_bool heredoc_starts_nextline:1;
    mrb_bool heredoc_end_now:1; /* for mirb */

    void *ylval;

    size_t nerr;
    size_t nwarn;
    mrb_ast_node *m_tree;

    int m_capture_errors;
    mrb_parser_message error_buffer[10];
    mrb_parser_message warn_buffer[10];


    void local_add(mrb_sym sym);
    size_t local_switch();
    void local_resume(size_t idx);
    void local_nest();
    void local_unnest( );
    bool local_var_p(mrb_sym sym_);
    // (:heredoc . a)
    mrb_ast_node* new_heredoc();
    void assignable(mrb_ast_node *lhs);

    mrb_ast_node* var_reference(mrb_ast_node *lhs);
    // (:call a op)
    mrb_ast_node* call_uni_op(mrb_ast_node *recv, const char *m) {
        return new_call(recv, intern(m), nullptr);
    }
    // (:call a op b)
    mrb_ast_node* call_bin_op(mrb_ast_node *recv, const char *m, mrb_ast_node *arg1) {
        return new_call(recv, intern(m), new_t<CommandArgs>(list1(arg1),nullptr));
    }

    jmp_buf jmp;

public:
    mrb_sym intern(const char *s);
    mrb_sym intern2(const char *s, size_t len);
    mrb_sym intern_c(const char c);
    void cons_free(mrb_ast_node *cons);
    void *parser_palloc(size_t size);
    mrb_ast_node *cons(mrb_ast_node *car, mrb_ast_node *cdr);
    mrb_ast_node *append(mrb_ast_node *a, mrb_ast_node *b);
    char *parser_strndup(const char *s, size_t len);
    char *parser_strdup(const char *s);
    mrb_ast_node *list1(mrb_ast_node *a);
    mrb_ast_node *list2(mrb_ast_node *a, mrb_ast_node *b);
    mrb_ast_node *list3(mrb_ast_node *a, mrb_ast_node *b, mrb_ast_node *c);
    mrb_ast_node *push(mrb_ast_node *a, mrb_ast_node *b);
    void local_add_f(mrb_sym sym);
    mrb_parser_heredoc_info *parsing_heredoc_inf();
    void heredoc_end();
    mrb_ast_node *new_strterm(mrb_string_type type, int term, int paren);
    void end_strterm();
    int parse_string();
    int read_escape();
    int newtok();
    int skips(const char *s);
    int peek_n(int c, int n);
    int peek(int c) {return peek_n(c,0);}
    bool peeks(const char *s);
    void skip(char term);
    int nextc();
    void pushback(int c);
    void yywarn(const char *s);
    void yyerror(const char *s);

                    // (:fcall self mid args)
    FCallNode *     new_fcall(mrb_sym m, CommandArgs *a) { return new_t<FCallNode>(new_t<SelfNode>(),m,a); }
                    // (:call a b c)
    CallNode *      new_call(mrb_ast_node *r, mrb_sym m, CommandArgs *a) { return new_t<CallNode>(r,m,a); }
                    //! (:scope (vars..) (prog...))
    ScopeNode *     new_scope(mrb_ast_node *body) {return new_t<ScopeNode>(localsToVec(), body);}
    ScopeNode *     empty_scope(mrb_ast_node *body) {return new_t<ScopeNode>(body);}
    EnsureNode *    new_ensure(mrb_ast_node *a, mrb_ast_node *b) { return new_t<EnsureNode>(a,empty_scope(b)); }
    NilNode *       new_nil() { return new_t<NilNode>(); } //!< (:nil)
    IfNode *        new_unless(mrb_ast_node *cond, mrb_ast_node *then, mrb_ast_node *f_else)
                    {
                        return new_t<IfNode>(cond,f_else,then);
                    }
    YieldNode *     new_yield(CommandArgs *c);
                    //! (:return . c)
    ArrayNode *     new_array(mrb_ast_node *a) {  return new_t<ArrayNode>(a); }
    mrb_sym         new_strsym(StrNode *str);
                    //! (:def m local_vars (arg . body))
    DefNode *       new_def(mrb_sym m, ArgsStore *a, mrb_ast_node *b)
                    {
                        return new_t<DefNode>(m,localsToVec(),a,b);
                    }
                    //! (:sdef obj m local_vars (arg . body))
    SdefNode *      new_sdef(mrb_ast_node *o, mrb_sym m, ArgsStore *a, mrb_ast_node *b)
                    {
                        return new_t<SdefNode>(o,m,localsToVec(), a, b);
                    }
    tLocals         localsToVec() {
                        assert(m_locals_stack);
                        return m_locals_stack->back();
                    }
    ArgsStore *     new_args(mrb_ast_node *m, mrb_ast_node *opt, mrb_sym rest, mrb_ast_node *m2, mrb_sym blk) {
                        return new_t<ArgsStore>(m,opt,rest,m2,blk);
                    }

    BlockNode *     new_block(ArgsStore *arg, mrb_ast_node *body)
                    {
                        return new_t<BlockNode>(localsToVec(), arg, body);
                    }
    LambdaNode *    new_lambda(ArgsStore *a, mrb_ast_node *b) { return new_t<LambdaNode>(localsToVec(), a, b); }
    StrNode *       new_str(const char *s, int len)
                    {
                        return new_t<StrNode>(parser_strndup(s, len), len);
                    }
    XstrNode *      new_xstr(const char *s, int len) {return new_t<XstrNode>(parser_strndup(s, len),len);}
    DsymNode *      new_dsym(mrb_ast_node *a) { return new_t<DsymNode>(new_t<DstrNode>(a)); }
    RegxNode *      new_regx(const char *p1, const char *p2) { return new_t<RegxNode>(p1,p2); }
    void            new_bv(mrb_sym id);
    void            args_with_block(CommandArgs *a, mrb_ast_node *b);
    void            call_with_block(mrb_ast_node *a, mrb_ast_node *b);
    NegateNode *    negate_lit(mrb_ast_node *n) { return new_t<NegateNode>(n); }
    mrb_ast_node *  cond(mrb_ast_node *n);
    mrb_ast_node *  ret_args(CommandArgs *n);
    bool            is_strterm_type(int str_func)  {
                        return ((int)(intptr_t)(m_lex_strterm->left()) & (str_func));
                    }
    void            backref_error(mrb_ast_node *n);
    void            yyerror_i(const char *fmt, int i);
    int             heredoc_identifier();
    int             arg_ambiguous();
    void            parser_init_cxt(mrbc_context *cxt);
    void            parser_update_cxt(mrbc_context *cxt);
    int             parser_yylex();
    void            yywarning_s(const char *fmt, const char *s);
    void            init_locals();
    mrb_ast_node *  str_list2(mrb_ast_node *a, mrb_ast_node *b);
    int             paren_nest();
                    template<typename T, typename... Args >
    T *             new_t(Args... args) {
                        return new(parser_palloc(sizeof(T))) T(args...);
                    }
};
/* utility functions */
#ifdef ENABLE_STDIO
mrb_parser_state* mrb_parse_file(mrb_state*,FILE*,mrbc_context*);
#endif
mrb_parser_state* mrb_parse_string(mrb_state*,const char*,mrbc_context*);
mrb_parser_state* mrb_parse_nstring(mrb_state*,const std::string &str,mrbc_context*);

mrbc_context* mrbc_context_new(mrb_state *mrb);
void mrbc_context_free(mrb_state *mrb, mrbc_context *cxt);
const char *mrbc_filename(mrb_state *mrb, mrbc_context *c, const char *s);
void mrbc_partial_hook(mrb_state *mrb, mrbc_context *c, int (*partial_hook)(mrb_parser_state*), void*data);
mrb_parser_state* mrb_parser_new(mrb_state*);
void mrb_parser_free(mrb_parser_state*);
void mrb_parser_parse(mrb_parser_state*,mrbc_context*);
int mrb_generate_code(mrb_state*, mrb_parser_state*);
/* program load functions */
#ifdef ENABLE_STDIO
mrb_value mrb_load_file(mrb_state*,FILE*);
mrb_value mrb_load_file_cxt(mrb_state*,FILE*, mrbc_context *cxt);
#endif
mrb_value mrb_load_string(mrb_state *mrb, const char *s);
mrb_value mrb_load_nstring(mrb_state *mrb, const std::string &s);
mrb_value mrb_load_string_cxt(mrb_state *mrb, const char *s, mrbc_context *cxt);
mrb_value mrb_load_nstring_cxt(mrb_state *mrb, const std::string &s, mrbc_context *cxt);
