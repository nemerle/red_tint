#include <cerrno>
#include <cassert>
#include <cstdlib>
#include <cctype>
#include <algorithm>

#include "mruby/compile.h"
#include "mruby/node.h"
#include "parse.hpp"

mrb_sym mrb_parser_state::intern(const char *s)
{
    return mrb_intern(m_mrb, s);
}
mrb_sym mrb_parser_state::intern2(const char *s, size_t len)
{
    return mrb_intern2(m_mrb, s, len);
}
mrb_sym mrb_parser_state::intern_c(const char c)
{
    return mrb_intern2(m_mrb, &c, 1);
}
void mrb_parser_state::cons_free(mrb_ast_node *cons)
{
    cons->right(cells);
    cells = cons;
}
void* mrb_parser_state::parser_palloc(size_t size)
{
    void *m = pool->mrb_pool_alloc(size);

    if (!m) {
        longjmp(jmp, 1);
    }
    return m;
}
mrb_ast_node* mrb_parser_state::cons(mrb_ast_node *car, mrb_ast_node *cdr)
{
    mrb_ast_node *c;

    if (cells) {
        c = cells;
        cells = cells->right();
    }
    else {
        c = new_t<mrb_ast_list_like_node>();
    }
    c->init(car,cdr,m_lineno,m_filename);
    return c;
}
mrb_ast_node* mrb_parser_state::append(mrb_ast_node *a, mrb_ast_node *b)
{
    mrb_ast_node *c = a;
    if (!a)
        return b;
    while (c->right()) {
        c = c->right();
    }
    if (b) {
        c->right(b);
    }
    return a;
}

char* mrb_parser_state::parser_strndup(const char *s, size_t len)
{
    char *b = (char *)parser_palloc(len+1);

    memcpy(b, s, len);
    b[len] = '\0';
    return b;
}
char* mrb_parser_state::parser_strdup(const char *s) {
    return parser_strndup(s, strlen(s));
}
mrb_ast_node* mrb_parser_state::list1(mrb_ast_node *a) {
    return cons(a, 0);
}

mrb_ast_node* mrb_parser_state::list2(mrb_ast_node *a, mrb_ast_node *b) {
    return cons(a, cons(b,0));
}
mrb_ast_node* mrb_parser_state::str_list2(mrb_ast_node *a, mrb_ast_node *b) {
    return cons(a, cons(b,0));
}

mrb_ast_node* mrb_parser_state::list3(mrb_ast_node *a, mrb_ast_node *b, mrb_ast_node *c) {
    return cons(a, cons(b, cons(c,0)));
}
mrb_ast_node *mrb_parser_state::push(mrb_ast_node *a, mrb_ast_node *b) {
    return append(a,list1(b));
}

void mrb_parser_state::local_add_f(mrb_sym sym_) {
    m_locals_stack->back().push_back(sym_);
}

void mrb_parser_state::local_add(mrb_sym sym_) {
    if (!local_var_p(sym_)) {
        local_add_f(sym_);
    }
}
size_t mrb_parser_state::local_switch()
{
    size_t res = m_contexts.size();
    m_contexts.push_back(m_locals_stack);
    m_locals_stack = new_simple<tLocalsStack>();
    m_locals_stack->push_back(tLocals());
    return res;
}
void mrb_parser_state::local_resume(size_t idx)
{
    assert(idx==m_contexts.size()-1);
    m_locals_stack=m_contexts.back();
    m_contexts.pop_back();
}

void mrb_parser_state::local_nest()
{
    m_locals_stack->push_back(tLocals());
}

void mrb_parser_state::local_unnest()
{
    m_locals_stack->pop_back();
}
void mrb_parser_state::init_locals() {
    if (!m_locals_stack)
        m_locals_stack = new_simple<tLocalsStack>();
    if (m_locals_stack->empty())
        m_locals_stack->push_back(tLocals());
}
bool mrb_parser_state::local_var_p(mrb_sym sym_)
{
    // search local context stack from inner to outer
    for(auto l_iter = m_locals_stack->rbegin(); l_iter!=m_locals_stack->rend(); ++l_iter) {
        const tLocals &loc(*l_iter);
        auto iter = std::find(loc.begin(),loc.end(),sym_);
        if(iter!=loc.end())
            return true;
    }
    return false;
}
// (:heredoc . a)
mrb_ast_node* mrb_parser_state::new_heredoc()
{
    mrb_parser_heredoc_info *inf = new_simple<mrb_parser_heredoc_info>();
    return new_t<HeredocNode>(inf);
}
void mrb_parser_state::assignable(mrb_ast_node *lhs)
{
    if (lhs->getType() == NODE_LVAR) {
        local_add(((LVarNode *)lhs)->sym());
    }
}
mrb_ast_node* mrb_parser_state::var_reference(mrb_ast_node *lhs)
{
    if (lhs->getType() != NODE_LVAR)
        return lhs;
    LVarNode *ln = (LVarNode *)lhs;
    if (!local_var_p(ln->sym())) {
        mrb_ast_node *n = new_fcall(ln->sym(), 0);
        //cons_free(lhs); //FIXME: is this needed ?
        return n;
    }
    return lhs;
}
mrb_ast_node* mrb_parser_state::new_strterm(mrb_string_type type, int term, int paren)
{
    return cons((mrb_ast_node*)(intptr_t)type,
                cons((mrb_ast_node*)0,
                     cons((mrb_ast_node*)(intptr_t)paren,
                          (mrb_ast_node*)(intptr_t)term)));
}
// (:yield . c)
YieldNode * mrb_parser_state::new_yield(CommandArgs *c)
{
    mrb_ast_node *arg=nullptr;
    if (c) {
        if (c->block()) {
            yyerror("both block arg and actual block given");
        }
        arg = c->m_args;
    }
    return new_t<YieldNode>(arg);
}

mrb_parser_heredoc_info * mrb_parser_state::parsing_heredoc_inf()
{
    mrb_ast_node *nd = parsing_heredoc;
    if (nd == nullptr)
        return nullptr;
    /* assert(nd->car->car == NODE_HEREDOC); */
    return ((HeredocNode*)nd->left())->contents();
}
void
mrb_parser_state::heredoc_treat_nextline()
{
    if (this->heredocs_from_nextline == NULL)
        return;
    if (this->parsing_heredoc == NULL) {
        mrb_ast_node *n;
        this->parsing_heredoc = this->heredocs_from_nextline;
        this->lex_strterm_before_heredoc = m_lex_strterm;
        this->m_lex_strterm = new_strterm(parsing_heredoc_inf()->type, 0, 0);
        n = this->all_heredocs;
        if (n) {
            while (n->right())
                n = n->right();
            n->right(this->parsing_heredoc);
        } else {
            this->all_heredocs = this->parsing_heredoc;
        }
    } else {
        mrb_ast_node *n, *m;
        m = this->heredocs_from_nextline;
        while (m->right())
            m = m->right();
        n = this->all_heredocs;
        mrb_assert(n != NULL);
        if (n == this->parsing_heredoc) {
            m->right(n);
            this->all_heredocs = this->heredocs_from_nextline;
            this->parsing_heredoc = this->heredocs_from_nextline;
        } else {
            while (n->right() != this->parsing_heredoc) {
                n = n->right();
                mrb_assert(n != NULL);
            }
            m->right(n->right());
            n->right(this->heredocs_from_nextline);
            this->parsing_heredoc = this->heredocs_from_nextline;
        }
    }
    this->heredocs_from_nextline = NULL;
}

void mrb_parser_state::heredoc_end()
{
    parsing_heredoc = parsing_heredoc->right();
    if (parsing_heredoc == NULL) {
        m_lstate = EXPR_BEG;
        m_cmd_start = true;
        end_strterm();
        m_lex_strterm = lex_strterm_before_heredoc;
        lex_strterm_before_heredoc = NULL;
        heredoc_end_now = true;
    } else {
        /* next heredoc */
        m_lex_strterm->left( (mrb_ast_node*)(intptr_t)parsing_heredoc_inf()->type );
    }
}
void mrb_parser_state::end_strterm()
{
    cons_free(m_lex_strterm->right()->right());
    cons_free(m_lex_strterm->right());
    cons_free(m_lex_strterm);
    m_lex_strterm = nullptr;
}
void mrb_parser_state::yyerror(const char *s)
{

    if (! m_capture_errors) {
#ifdef ENABLE_STDIO
        if (m_filename) {
            fprintf(stderr, "%s:%d:%d: %s\n", m_filename, m_lineno, m_column, s);
        }
        else {
            fprintf(stderr, "line %d:%d: %s\n", m_lineno, m_column, s);
        }
#endif
    }
    else if (nerr < sizeof(error_buffer) / sizeof(error_buffer[0])) {
        int n = strlen(s);
        char* c = (char *)parser_palloc(n + 1);
        memcpy(c, s, n + 1);
        error_buffer[nerr].message = c;
        error_buffer[nerr].lineno = m_lineno;
        error_buffer[nerr].column = m_column;
    }
    nerr++;
}
void mrb_parser_state::yywarn(const char *s)
{

    if (! m_capture_errors) {
#ifdef ENABLE_STDIO
        if (m_filename) {
            fprintf(stderr, "%s:%d:%d: %s\n", m_filename, m_lineno, m_column, s);
        }
        else {
            fprintf(stderr, "line %d:%d: %s\n", m_lineno, m_column, s);
        }
#endif
    }
    else if (nwarn < sizeof(warn_buffer) / sizeof(warn_buffer[0])) {
        int n = strlen(s);
        char* c = (char *)parser_palloc(n + 1);
        memcpy(c, s, n + 1);
        warn_buffer[nwarn].message = c;
        warn_buffer[nwarn].lineno = m_lineno;
        warn_buffer[nwarn].column = m_column;
    }
    this->nwarn++;
}

mrb_sym mrb_parser_state::new_strsym( StrNode * sn)
{
    return mrb_intern2(m_mrb, sn->m_str, sn->m_length);
}
// (m o r m2 b)
// m: (a b c)
// o: ((a . e1) (b . e2))
// r: a
// m2: (a b c)
// b: a

void mrb_parser_state::new_bv( mrb_sym id) { }

// xxx -----------------------------

void mrb_parser_state::args_with_block(CommandArgs *a, mrb_ast_node *b) {
    if (nullptr==b)
        return;
    if (a->m_blk)
        yyerror("both block arg and actual block given");
    a->m_blk = b;
}
void mrb_parser_state::call_with_block( mrb_ast_node *a, mrb_ast_node *b) {
    CommandArgs **args_obj;
    SuperNode *sn = (SuperNode *)a;
    ZsuperNode *z_n = (ZsuperNode *)a;
    CallCommonNode *cn = (CallCommonNode *)a;
    if(a->getType() == NODE_ZSUPER) {
        args_obj = &z_n->cmd_args;
    }
    else if (a->getType() == NODE_SUPER ) {
        args_obj = &sn->cmd_args;
    }
    else {
        args_obj = &cn->m_cmd_args;
    }
    if (!*args_obj) {
        *args_obj = new_simple<CommandArgs>(nullptr,b);
    }
    else {
        args_with_block((*args_obj), b);
    }
}

mrb_ast_node *mrb_parser_state::cond(mrb_ast_node *n) {
    return n;
}
mrb_ast_node * mrb_parser_state::ret_args(CommandArgs *n) {
    if (n->block()) {
        yyerror("block argument should not be given");
        return nullptr;
    }
    if (!n->m_args->right()) // is single arg ?
        return n->m_args->left();
    return new_array(n->m_args); // otherwise an array
}
void mrb_parser_state::yyerror_i(const char *fmt, int i)
{
    char buf[256];

    snprintf(buf, sizeof(buf), fmt, i);
    yyerror(buf);
}

void mrb_parser_state::backref_error(const mrb_ast_node *n)
{
    node_type c = n->getType();

    if (c == NODE_NTH_REF) {
        yyerror_i("can't set variable $%d", ((NthRefNode *)n)->m_ref);
    } else if (c == NODE_BACK_REF) {
        yyerror_i("can't set variable $%c", ((BackRefNode *)n)->m_ref);
    } else {
        mrb_bug(m_mrb,"Internal error in backref_error() : node type == %d", c);
    }
}
int mrb_parser_state::paren_nest() {
    int res = lpar_beg;
    lpar_beg = ++m_lexer.paren_nest;
    return res;
}
void mrb_parser_state::parser_init_cxt(mrbc_context *cxt)
{
    if (!cxt)
        return;
    if (cxt->lineno)
        m_lineno = cxt->lineno;
    if (cxt->filename)
        m_filename = cxt->filename;
    if (cxt->syms) { // add cxt symbols to locals
        init_locals();
        for (int i=0; i<cxt->slen; i++) {
            local_add_f( cxt->syms[i]);
        }
    }
    m_capture_errors = cxt->capture_errors;
    if(cxt->partial_hook) {
        m_cxt = cxt;
    }
}

void mrb_parser_state::parser_update_cxt(mrbc_context *cxt)
{
    if (!cxt)
        return;
    if (m_tree->getType() != NODE_SCOPE)
        return;
    ScopeNode *sn = (ScopeNode *)m_tree;
    cxt->syms = (mrb_sym *)m_mrb->gc()._realloc(cxt->syms, sn->locals().size()*sizeof(mrb_sym));
    cxt->slen = sn->locals().size();
    std::copy(sn->locals().begin(),sn->locals().end(),cxt->syms);
}

extern void codedump_all(mrb_state*, int);
extern void parser_dump(mrb_state *mrb, mrb_ast_node *tree, int offset);

void mrbc_partial_hook(mrb_state *mrb, mrbc_context *c, int (*func)(mrb_parser_state*), void *data)
{
    c->partial_hook = func;
    c->partial_data = data;
}

void mrb_parser_parse(mrb_parser_state *p, mrbc_context *c) {
    if (setjmp(p->jmp) != 0) {
        p->yyerror("memory allocation error");
        p->nerr++;
        p->m_tree = 0;
        return;
    }

    p->m_cmd_start = true;
    p->in_def = 0;
    p->in_single = 0;
    p->nerr = p->nwarn = 0;
    p->m_lex_strterm = nullptr;

    p->parser_init_cxt(c);
    yyparse(p);
    if (!p->m_tree) {
        p->m_tree = p->new_nil();
    }
    p->parser_update_cxt(c);
    if (c && c->dump_result) {
        parser_dump(p->m_mrb, p->m_tree, 0);
    }
}


node_type mrb_ast_list_like_node::getType() const {
    node_type r = node_type((intptr_t)left());
    assert(intptr_t(left())<NODE_LAST);
    return r;
}
