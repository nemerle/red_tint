#include <new>
#include <cerrno>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include "mruby/compile.h"
#include "mruby/node.h"
#include "parse.hpp"
#define yylval  (*((YYSTYPE*)(this->ylval)))
//#define identchar(c) (isalnum(c) || (c) == '_' || !isascii(c))
static bool identchar(int c) { return (isalnum(c) || (c) == '_' || !isascii(c));}
#include "lex.def"
#define IS_ARG() (m_lstate == EXPR_ARG || m_lstate == EXPR_CMDARG)
#define IS_END() (m_lstate == EXPR_END || m_lstate == EXPR_ENDARG || m_lstate == EXPR_ENDFN)
#define IS_BEG() (m_lstate == EXPR_BEG || m_lstate == EXPR_MID || m_lstate == EXPR_VALUE || m_lstate == EXPR_CLASS)
#define IS_SPCARG(c) (IS_ARG() && space_seen && !ISSPACE(c))
#define IS_LABEL_POSSIBLE() ((m_lstate == EXPR_BEG && !cmd_state) || IS_ARG())
#define IS_LABEL_SUFFIX(n) (peek_n(':',(n)) && !peek_n(':', (n)+1))

#define BITSTACK_PUSH(stack, n) ((stack) = ((stack)<<1)|((n)&1))
#define BITSTACK_POP(stack)     ((stack) = (stack) >> 1)
#define BITSTACK_LEXPOP(stack)  ((stack) = ((stack) >> 1) | ((stack) & 1))
#define BITSTACK_SET_P(stack)   ((stack)&1)

#define COND_PUSH(n)    BITSTACK_PUSH(this->cond_stack, (n))
#define COND_POP()      BITSTACK_POP(this->cond_stack)
#define COND_LEXPOP()   BITSTACK_LEXPOP(this->cond_stack)
#define COND_P()        BITSTACK_SET_P(this->cond_stack)

#define CMDARG_PUSH(n)  BITSTACK_PUSH(this->cmdarg_stack, (n))
#define CMDARG_POP()    BITSTACK_POP(this->cmdarg_stack)
#define CMDARG_LEXPOP() BITSTACK_LEXPOP(this->cmdarg_stack)
#define CMDARG_P()      BITSTACK_SET_P(this->cmdarg_stack)

void mrb_lexer_state::tokadd(int c)
{
    if (bidx < MRB_PARSER_BUF_SIZE) {
        buf[bidx++] = c;
    }
}
int mrb_lexer_state::toklast()
{
    return buf[bidx-1];
}
bool mrb_lexer_state::tokfix()
{
    if (bidx >= MRB_PARSER_BUF_SIZE) {
        return false;
    }
    buf[bidx] = '\0';
    return true;
}
static int scan_oct(const int *start, int len, int *retlen)
{
    const int *s = start;
    int retval = 0;

    /* assert(len <= 3) */
    while (len-- && *s >= '0' && *s <= '7') {
        retval <<= 3;
        retval |= *s++ - '0';
    }
    *retlen = s - start;

    return retval;
}
static int scan_hex(const int *start, int len, int *retlen)
{
    static const char hexdigit[] = "0123456789abcdef0123456789ABCDEF";
    register const int *s = start;
    register int retval = 0;
    const char *tmp;

    /* assert(len <= 2) */
    while (len-- && *s && (tmp = strchr(hexdigit, *s))) {
        retval <<= 4;
        retval |= (tmp - hexdigit) & 15;
        s++;
    }
    *retlen = s - start;

    return retval;
}
int mrb_parser_state::skips(const char *s)
{
    int c;

    for (;;) {
        // skip until first char
        for (;;) {
            c = nextc();
            if (c < 0) return c;
            if (c == *s) break;
        }
        s++;
        if (peeks(s)) {
            int len = strlen(s);

            while (len--) {
                nextc();
            }
            return TRUE;
        }
        else{
            s--;
        }
    }
    return FALSE;
}
int mrb_parser_state::newtok()
{
    m_lexer.bidx = 0;
    return m_column - 1;
}
int mrb_parser_state::read_escape()
{
    int c;

    switch (c = nextc()) {
        case '\\':	/* Backslash */
            return c;

        case 'n':	/* newline */
            return '\n';

        case 't':	/* horizontal tab */
            return '\t';

        case 'r':	/* carriage-return */
            return '\r';

        case 'f':	/* form-feed */
            return '\f';

        case 'v':	/* vertical tab */
            return '\13';

        case 'a':	/* alarm(bell) */
            return '\007';

        case 'e':	/* escape */
            return 033;

        case '0': case '1': case '2': case '3': /* octal constant */
        case '4': case '5': case '6': case '7':
        {
            int l_buf[3];
            int i;

            l_buf[0] = c;
            for (i=1; i<3; i++) {
                l_buf[i] = nextc();
                if (l_buf[i] == -1) goto eof;
                if (l_buf[i] < '0' || '7' < l_buf[i]) {
                    pushback(l_buf[i]);
                    break;
                }
            }
            c = scan_oct(l_buf, i, &i);
        }
            return c;

        case 'x':	/* hex constant */
        {
            int buf[2];
            int i;

            for (i=0; i<2; i++) {
                buf[i] = nextc();
                if (buf[i] == -1) goto eof;
                if (!ISXDIGIT(buf[i])) {
                    pushback(buf[i]);
                    break;
                }
            }
            c = scan_hex(buf, i, &i);
            if (i == 0) {
                yyerror("Invalid escape character syntax");
                return 0;
            }
        }
            return c;

        case 'b':	/* backspace */
            return '\010';

        case 's':	/* space */
            return ' ';

        case 'M':
            if ((c = nextc()) != '-') {
                yyerror("Invalid escape character syntax");
                pushback(c);
                return '\0';
            }
            if ((c = nextc()) == '\\') {
                return read_escape() | 0x80;
            }
            else if (c == -1) goto eof;
            else {
                return ((c & 0xff) | 0x80);
            }

        case 'C':
            if ((c = nextc()) != '-') {
                yyerror("Invalid escape character syntax");
                pushback(c);
                return '\0';
            }
        case 'c':
            if ((c = nextc())== '\\') {
                c = read_escape();
            }
            else if (c == '?')
                return 0177;
            else if (c == -1) goto eof;
            return c & 0x9f;

eof:
        case -1:
            yyerror("Invalid escape character syntax");
            return '\0';

        default:
            return c;
    }
}
void mrb_parser_state::pushback(int c)
{
    if (c < 0)
        return;
    m_column--;
    pb = cons((mrb_ast_node*)(intptr_t)c, pb);
}
int mrb_parser_state::nextc()
{
    int c;

    if (this->pb) {
        mrb_ast_node *tmp;

        c = (int)(intptr_t)this->pb->left();
        tmp = this->pb;
        this->pb = this->pb->right();
        cons_free(tmp);
    }
    else {
#ifdef ENABLE_STDIO
        if (this->f) {
            if (feof(this->f)) return -1;
            c = fgetc(this->f);
            if (c == EOF) return -1;
        }
        else
#endif
            if (!this->s || this->s >= this->send) {
                return -1;
            }
            else {
                c = (unsigned char)*this->s++;
            }
    }
    m_column++;
    return c;
}
void mrb_parser_state::skip(char term)
{
    int c;

    for (;;) {
        c = nextc();
        if (c < 0) break;
        if (c == term) break;
    }
}
int mrb_parser_state::peek_n(int c, int n)
{
    mrb_ast_node *list = 0;
    int c0;

    do {
        c0 = nextc();
        if (c0 < 0) return FALSE;
        list = push(list, (mrb_ast_node*)(intptr_t)c0);
    } while(n--);
    if (this->pb) {
        this->pb = push(this->pb, (mrb_ast_node*)list);
    }
    else {
        this->pb = list;
    }
    if (c0 == c) return TRUE;
    return FALSE;
}
bool mrb_parser_state::peeks(const char *s)
{
    int len = strlen(s);

#ifdef ENABLE_STDIO
    if (this->f) {
        int n = 0;
        while (*s) {
            if (!peek_n(*s++, n++))
                return false;
        }
        return true;
    }
    else
#endif
        if (this->s && this->s + len >= this->send) {
            if (memcmp(this->s, s, len) == 0)
                return false;
        }
    return false;
}
int mrb_parser_state::heredoc_identifier()
{
    int c;
    int type = str_heredoc;
    int indent = FALSE;
    int quote = FALSE;
    HeredocNode *newnode;
    mrb_parser_heredoc_info *info;

    c = this->nextc();
    if (ISSPACE(c) || c == '=') {
        this->pushback(c);
        return 0;
    }
    if (c == '-') {
        indent = TRUE;
        c = this->nextc();
    }
    if (c == '\'' || c == '"') {
        int term = c;
        if (c == '\'')
            quote = TRUE;
        newtok();
        while ((c = nextc()) != -1 && c != term) {
            if (c == '\n') {
                c = -1;
                break;
            }
            m_lexer.tokadd(c);
        }
        if (c == -1) {
            this->yyerror("unterminated here document identifier");
            return 0;
        }
    } else {
        if (! identchar(c)) {
            this->pushback(c);
            if (indent) pushback('-');
            return 0;
        }
        newtok();
        do {
            m_lexer.tokadd(c);
        } while ((c = nextc()) != -1 && identchar(c));
        pushback(c);
    }
    if( false==m_lexer.tokfix() )
        yyerror("string too long (truncated)");

    newnode = (HeredocNode *)new_heredoc();
    info = newnode->contents();
    info->term = parser_strndup(m_lexer.tok(), m_lexer.toklen());
    info->term_len = m_lexer.toklen();
    if (! quote)
        type |= STR_FUNC_EXPAND;
    info->type = (mrb_string_type)type;
    info->allow_indent = indent;
    info->line_head = TRUE;
    info->doc = nullptr;
    this->heredocs =this->push(this->heredocs, newnode);
    if (this->parsing_heredoc == nullptr) {
        mrb_ast_node *n = this->heredocs;
        while (n->right())
            n = n->right();
        this->parsing_heredoc = n;
    }
    this->heredoc_starts_nextline = TRUE;
    m_lstate = EXPR_END;

    yylval.nd = newnode;
    return tHEREDOC_BEG;
}
void mrb_parser_state::yywarning_s(const char *fmt, const char *s) {
    char buf[256];
    snprintf(buf, sizeof(buf), fmt, s);
    yywarn(buf);
}

int mrb_parser_state::arg_ambiguous()
{
    yywarn("ambiguous first argument; put parentheses or even spaces");
    return 1;
}

int mrb_parser_state::parse_string()
{
    int c;
    mrb_string_type type = (mrb_string_type)(intptr_t)m_lex_strterm->left();
    int nest_level = (intptr_t)m_lex_strterm->right()->left();
    int beg = (intptr_t)m_lex_strterm->right()->right()->left();
    int end = (intptr_t)m_lex_strterm->right()->right()->right();
    mrb_parser_heredoc_info *hinf = (type & STR_FUNC_HEREDOC) ? parsing_heredoc_inf() : NULL;

    newtok();
    while ((c = nextc()) != end || nest_level != 0) {
        if (hinf && (c == '\n' || c == -1)) {
            int line_head;
            m_lexer.tokadd('\n');
            if(false==m_lexer.tokfix())
                yyerror("string too long (truncated)");
            m_lineno++;
            m_column = 0;
            line_head = hinf->line_head;
            hinf->line_head = TRUE;
            if (line_head) {
                /* check whether end of heredoc */
                const char *s = m_lexer.tok();
                int len = m_lexer.toklen();
                if (hinf->allow_indent) {
                    while (ISSPACE(*s) && len > 0) {
                        ++s;
                        --len;
                    }
                }
                if ((len-1 == hinf->term_len) && (strncmp(s, hinf->term, len-1) == 0)) {
                    return tHEREDOC_END;
                }
            }
            if (c == -1) {
                char buf[256];
                snprintf(buf, sizeof(buf), "can't find string \"%s\" anywhere before EOF", hinf->term);
                this->yyerror(buf);
                return 0;
            }
            yylval.nd = new_str(m_lexer.tok(), m_lexer.toklen());
            return tSTRING_MID;
        }
        if (c == -1) {
            this->yyerror("unterminated string meets end of file");
            return 0;
        }
        else if (c == beg) {
            nest_level++;
            m_lex_strterm->right()->left( (mrb_ast_node *)(intptr_t)nest_level );
        }
        else if (c == end) {
            nest_level--;
            m_lex_strterm->right()->left( (mrb_ast_node *)(intptr_t)nest_level );
        }
        else if (c == '\\') {
            c = nextc();
            if (type & STR_FUNC_EXPAND) {
                if (c == end || c == beg) {
                    m_lexer.tokadd(c);
                }
                else if ((c == '\n') && (type & STR_FUNC_ARRAY)) {
                    m_lineno++;
                    m_column = 0;
                    m_lexer.tokadd('\n');
                }
                else {
                    pushback(c);

                    if(type & STR_FUNC_REGEXP)
                        m_lexer.tokadd('\\');

                    m_lexer.tokadd(read_escape());
                    if (hinf)
                        hinf->line_head = FALSE;
                }
            } else {
                if (c != beg && c != end) {
                    switch (c) {
                        case '\n':
                            m_lineno++;
                            m_column = 0;
                            break;

                        case '\\':
                            break;

                        default:
                            if (! ISSPACE(c))
                                m_lexer.tokadd('\\');
                    }
                }
                m_lexer.tokadd(c);
            }
            continue;
        }
        else if ((c == '#') && (type & STR_FUNC_EXPAND)) {
            c = nextc();
            if (c == '{') {
                if(false==m_lexer.tokfix())
                    yyerror("string too long (truncated)");
                m_lstate = EXPR_BEG;
                m_cmd_start = true;
                yylval.sn = new_str(m_lexer.tok(), m_lexer.toklen());
                if (hinf)
                    hinf->line_head = FALSE;
                return tSTRING_PART;
            }
            m_lexer.tokadd('#');
            pushback(c);
            continue;
        }
        if ((type & STR_FUNC_ARRAY) && ISSPACE(c)) {
            if (m_lexer.toklen() == 0) {
                do {
                    if (c == '\n') {
                        m_lineno++;
                        m_column = 0;
                    }
                } while (ISSPACE(c = nextc()));
                pushback(c);
                return tLITERAL_DELIM;
            } else {
                pushback(c);
                if(false==m_lexer.tokfix())
                    yyerror("string too long (truncated)");
                yylval.sn = new_str(m_lexer.tok(), m_lexer.toklen());
                return tSTRING_MID;
            }
        }

        m_lexer.tokadd( c);

    }

    if(false==m_lexer.tokfix())
        yyerror("string too long (truncated)");
    m_lstate = EXPR_END;
    end_strterm();

    if (type & STR_FUNC_XQUOTE) {
        yylval.nd = new_xstr(m_lexer.tok(), m_lexer.toklen());
        return tXSTRING;
    }

    if (type & STR_FUNC_REGEXP) {
        int f = 0;
        int c;
        char *s = parser_strndup(m_lexer.tok(), m_lexer.toklen());
        char flag[4] = { '\0' };

        newtok();
        while (c = nextc(), ISALPHA(c)) {
            switch (c) {
                case 'i': f |= 1; break;
                case 'x': f |= 2; break;
                case 'm': f |= 4; break;
                default: m_lexer.tokadd(c); break;
            }
        }
        pushback(c);
        if (m_lexer.toklen()) {
            char msg[128];
            if(false==m_lexer.tokfix())
                yyerror("string too long (truncated)");
            snprintf(msg, sizeof(msg), "unknown regexp option%s - %s",
                     m_lexer.toklen() > 1 ? "s" : "", m_lexer.tok());
            yyerror(msg);
        }
        if (f & 1) strcat(flag, "i");
        if (f & 2) strcat(flag, "x");
        if (f & 4) strcat(flag, "m");
        yylval.nd = new_t<RegxNode>(s, parser_strdup(flag));

        return tREGEXP;
    }

    yylval.sn = new_str(m_lexer.tok(), m_lexer.toklen());
    return tSTRING;
}

int mrb_parser_state::parser_yylex()
{
    register int c;
    int space_seen = 0;
    bool cmd_state;
    mrb_lex_state_enum last_state;
    int token_column;

    if (m_lex_strterm) {
        if (is_strterm_type(STR_FUNC_HEREDOC)) {
            if ((parsing_heredoc != NULL) && (! heredoc_starts_nextline))
                return parse_string();
        }
        else
            return parse_string();
    }
    cmd_state = m_cmd_start;
    m_cmd_start = false;
retry:
    last_state = m_lstate;
    switch (c = nextc()) {
        case '\0':    /* NUL */
        case '\004':  /* ^D */
        case '\032':  /* ^Z */
        case -1:      /* end of script. */
            return 0;

            /* white spaces */
        case ' ': case '\t': case '\f': case '\r': case '\13':   /* '\v' */
            space_seen = 1;
            goto retry;

        case '#':     /* it's a comment */
            this->skip('\n');
            /* fall through */
        case '\n':
            heredoc_starts_nextline = false;
            if (parsing_heredoc != NULL) {
                m_lex_strterm = new_strterm(parsing_heredoc_inf()->type, 0, 0);
                goto normal_newline;
            }
            switch (m_lstate) {
                case EXPR_BEG:
                case EXPR_FNAME:
                case EXPR_DOT:
                case EXPR_CLASS:
                case EXPR_VALUE:
                    m_lineno++;
                    m_column = 0;
                    goto retry;
                default:
                    break;
            }
            while ((c = nextc())) {
                switch (c) {
                    case ' ': case '\t': case '\f': case '\r': case '\13': /* '\v' */
                        space_seen = 1;
                        break;
                    case '.':
                        if ((c = nextc()) != '.') {
                            pushback(c);
                            pushback('.');
                            goto retry;
                        }
                    case -1:			/* EOF */
                        goto normal_newline;
                    default:
                        this->pushback(c);
                        goto normal_newline;
                }
            }
normal_newline:
            m_cmd_start = true;
            m_lstate = EXPR_BEG;
            return '\n';

        case '*':
            if ((c = nextc()) == '*') {
                if ((c = nextc()) == '=') {
                    yylval.id = intern2("**",2);
                    m_lstate = EXPR_BEG;
                    return tOP_ASGN;
                }
                pushback(c);
                c = tPOW;
            }
            else {
                if (c == '=') {
                    yylval.id = intern_c('*');
                    m_lstate = EXPR_BEG;
                    return tOP_ASGN;
                }
                pushback(c);
                if (IS_SPCARG(c)) {
                    yywarn("`*' interpreted as argument prefix");
                    c = tSTAR;
                }
                else if (IS_BEG()) {
                    c = tSTAR;
                }
                else {
                    c = '*';
                }
            }
            if (m_lstate == EXPR_FNAME || m_lstate == EXPR_DOT) {
                m_lstate = EXPR_ARG;
            } else {
                m_lstate = EXPR_BEG;
            }
            return c;

        case '!':
            c = this->nextc();
            if (m_lstate == EXPR_FNAME || m_lstate == EXPR_DOT) {
                m_lstate = EXPR_ARG;
                if (c == '@') {
                    return '!';
                }
            }
            else {
                m_lstate = EXPR_BEG;
            }
            if (c == '=') {
                return tNEQ;
            }
            if (c == '~') {
                return tNMATCH;
            }
            this->pushback(c);
            return '!';

        case '=':
            if (m_column == 1) {
                if (peeks("begin\n")) {
                    skips("\n=end\n");
                    goto retry;
                }
            }
            if (m_lstate == EXPR_FNAME || m_lstate == EXPR_DOT) {
                m_lstate = EXPR_ARG;
            } else {
                m_lstate = EXPR_BEG;
            }
            if ((c = nextc()) == '=') {
                if ((c = nextc()) == '=') {
                    return tEQQ;
                }
                pushback(c);
                return tEQ;
            }
            if (c == '~') {
                return tMATCH;
            }
            else if (c == '>') {
                return tASSOC;
            }
            pushback(c);
            return '=';

        case '<':
            last_state = m_lstate;
            c = this->nextc();
            if (c == '<' &&
                    m_lstate != EXPR_DOT &&
                    m_lstate != EXPR_CLASS &&
                    !IS_END() &&
                    (!IS_ARG() || space_seen)) {
                int token = this->heredoc_identifier();
                if (token)
                    return token;
            }
            if (m_lstate == EXPR_FNAME || m_lstate == EXPR_DOT) {
                m_lstate = EXPR_ARG;
            } else {
                m_lstate = EXPR_BEG;
                if (m_lstate == EXPR_CLASS) {
                    m_cmd_start = true;
                }
            }
            if (c == '=') {
                if ((c = nextc()) == '>') {
                    return tCMP;
                }
                this->pushback(c);
                return tLEQ;
            }
            if (c == '<') {
                if ((c = nextc()) == '=') {
                    yylval.id = intern2("<<",2);
                    m_lstate = EXPR_BEG;
                    return tOP_ASGN;
                }
                pushback(c);
                return tLSHFT;
            }
            pushback(c);
            return '<';

        case '>':
            if (m_lstate == EXPR_FNAME || m_lstate == EXPR_DOT) {
                m_lstate = EXPR_ARG;
            } else {
                m_lstate = EXPR_BEG;
            }
            if ((c = nextc()) == '=') {
                return tGEQ;
            }
            if (c == '>') {
                if ((c = nextc()) == '=') {  // >>=
                    yylval.id = intern2(">>",2);
                    m_lstate = EXPR_BEG;
                    return tOP_ASGN;
                }
                pushback(c);  // >>
                return tRSHFT;
            }
            pushback(c);
            return '>';

        case '"':
            m_lex_strterm = new_strterm(str_dquote, '"', 0);
            return tSTRING_BEG;

        case '\'':
            m_lex_strterm = new_strterm(str_squote, '\'', 0);
            return this->parse_string();

        case '`':
            if (m_lstate == EXPR_FNAME) {
                m_lstate = EXPR_ENDFN;
                return '`';
            }
            if (m_lstate == EXPR_DOT) {
                if (cmd_state)
                    m_lstate = EXPR_CMDARG;
                else
                    m_lstate = EXPR_ARG;
                return '`';
            }
            m_lex_strterm = this->new_strterm(str_xquote, '`', 0);
            return tXSTRING_BEG;

        case '?':
            if (IS_END()) {
                m_lstate = EXPR_VALUE;
                return '?';
            }
            c = this->nextc();
            if (c == -1) {
                this->yyerror("incomplete character syntax");
                return 0;
            }
            if (isspace(c)) {
                if (!IS_ARG()) {
                    int c2;
                    switch (c) {
                        case ' ':
                            c2 = 's';
                            break;
                        case '\n':
                            c2 = 'n';
                            break;
                        case '\t':
                            c2 = 't';
                            break;
                        case '\v':
                            c2 = 'v';
                            break;
                        case '\r':
                            c2 = 'r';
                            break;
                        case '\f':
                            c2 = 'f';
                            break;
                        default:
                            c2 = 0;
                            break;
                    }
                    if (c2) {
                        char buf[256];
                        snprintf(buf, sizeof(buf), "invalid character syntax; use ?\\%c", c2);
                        this->yyerror(buf);
                    }
                }
ternary:
                this->pushback(c);
                m_lstate = EXPR_VALUE;
                return '?';
            }
            token_column = this->newtok();
            // need support UTF-8 if configured
            if ((isalnum(c) || c == '_')) {
                int c2 = this->nextc();
                this->pushback(c2);
                if ((isalnum(c2) || c2 == '_')) {
                    goto ternary;
                }
            }
            if (c == '\\') {
                c = this->nextc();
                if (c == 'u') {
#if 0
                    p->tokadd_utf8();
#endif
                }
                else {
                    this->pushback(c);
                    c = this->read_escape();
                    m_lexer.tokadd(c);
                }
            }
            else {
                m_lexer.tokadd(c);
            }
            if(false==m_lexer.tokfix())
                yyerror("string too long (truncated)");
            yylval.nd = this->new_str(m_lexer.tok(), m_lexer.toklen());
            m_lstate = EXPR_END;
            return tCHAR;

        case '&':
            if ((c = this->nextc()) == '&') {
                m_lstate = EXPR_BEG;
                if ((c = this->nextc()) == '=') {
                    yylval.id =this->intern2("&&",2);
                    m_lstate = EXPR_BEG;
                    return tOP_ASGN;
                }
                this->pushback(c);
                return tANDOP;
            }
            else if (c == '=') {
                yylval.id =this->intern_c('&');
                m_lstate = EXPR_BEG;
                return tOP_ASGN;
            }
            this->pushback(c);
            if (IS_SPCARG(c)) {
                this->yywarn("`&' interpreted as argument prefix");
                c = tAMPER;
            }
            else if (IS_BEG()) {
                c = tAMPER;
            }
            else {
                c = '&';
            }
            if (m_lstate == EXPR_FNAME || m_lstate == EXPR_DOT) {
                m_lstate = EXPR_ARG;
            } else {
                m_lstate = EXPR_BEG;
            }
            return c;

        case '|':
            if ((c = this->nextc()) == '|') {
                m_lstate = EXPR_BEG;
                if ((c = this->nextc()) == '=') {
                    yylval.id =this->intern2("||",2);
                    m_lstate = EXPR_BEG;
                    return tOP_ASGN;
                }
                this->pushback(c);
                return tOROP;
            }
            if (c == '=') {
                yylval.id =this->intern_c('|');
                m_lstate = EXPR_BEG;
                return tOP_ASGN;
            }
            if (m_lstate == EXPR_FNAME || m_lstate == EXPR_DOT) {
                m_lstate = EXPR_ARG;
            }
            else {
                m_lstate = EXPR_BEG;
            }
            this->pushback(c);
            return '|';

        case '+':
            c = this->nextc();
            if (m_lstate == EXPR_FNAME || m_lstate == EXPR_DOT) {
                m_lstate = EXPR_ARG;
                if (c == '@') {
                    return tUPLUS;
                }
                this->pushback(c);
                return '+';
            }
            if (c == '=') {
                yylval.id =this->intern_c('+');
                m_lstate = EXPR_BEG;
                return tOP_ASGN;
            }
            if (IS_BEG() || (IS_SPCARG(c) && this->arg_ambiguous())) {
                m_lstate = EXPR_BEG;
                this->pushback(c);
                if (c != -1 && ISDIGIT(c)) {
                    c = '+';
                    goto start_num;
                }
                return tUPLUS;
            }
            m_lstate = EXPR_BEG;
            this->pushback(c);
            return '+';

        case '-':
            c = this->nextc();
            if (m_lstate == EXPR_FNAME || m_lstate == EXPR_DOT) {
                m_lstate = EXPR_ARG;
                if (c == '@') {
                    return tUMINUS;
                }
                this->pushback(c);
                return '-';
            }
            if (c == '=') {
                yylval.id =this->intern_c('-');
                m_lstate = EXPR_BEG;
                return tOP_ASGN;
            }
            if (c == '>') {
                m_lstate = EXPR_ENDFN;
                return tLAMBDA;
            }
            if (IS_BEG() || (IS_SPCARG(c) && this->arg_ambiguous())) {
                m_lstate = EXPR_BEG;
                this->pushback(c);
                if (c != -1 && ISDIGIT(c)) {
                    return tUMINUS_NUM;
                }
                return tUMINUS;
            }
            m_lstate = EXPR_BEG;
            this->pushback(c);
            return '-';

        case '.':
            m_lstate = EXPR_BEG;
            if ((c = this->nextc()) == '.') {
                if ((c = this->nextc()) == '.') {
                    return tDOT3;
                }
                this->pushback(c);
                return tDOT2;
            }
            this->pushback(c);
            if (c != -1 && ISDIGIT(c)) {
                this->yyerror("no .<digit> floating literal anymore; put 0 before dot");
            }
            m_lstate = EXPR_DOT;
            return '.';

start_num:
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
        {
            int is_float, seen_point, seen_e, nondigit;

            is_float = seen_point = seen_e = nondigit = 0;
            m_lstate = EXPR_END;
            token_column = this->newtok();
            if (c == '-' || c == '+') {
                m_lexer.tokadd(c);
                c = this->nextc();
            }
            if (c == '0') {
#define no_digits() do {yyerror("numeric literal without digits"); return 0;} while (0)
                int start = m_lexer.toklen();
                c = this->nextc();
                if (c == 'x' || c == 'X') {
                    /* hexadecimal */
                    c = this->nextc();
                    if (c != -1 && ISXDIGIT(c)) {
                        do {
                            if (c == '_') {
                                if (nondigit) break;
                                nondigit = c;
                                continue;
                            }
                            if (!ISXDIGIT(c)) break;
                            nondigit = 0;
                            m_lexer.tokadd(tolower(c));
                        } while ((c = this->nextc()) != -1);
                    }
                    this->pushback(c);
                    if(false==m_lexer.tokfix())
                        yyerror("string too long (truncated)");

                    if (m_lexer.toklen() == start) {
                        no_digits();
                    }
                    else if (nondigit) goto trailing_uc;
                    yylval.nd = new_t<IntLiteralNode>(m_lexer.tok(), 16);
                    return tINTEGER;
                }
                if (c == 'b' || c == 'B') {
                    /* binary */
                    c = this->nextc();
                    if (c == '0' || c == '1') {
                        do {
                            if (c == '_') {
                                if (nondigit)
                                    break;
                                nondigit = c;
                                continue;
                            }
                            if (c != '0' && c != '1')
                                break;
                            nondigit = 0;
                            m_lexer.tokadd(c);
                        } while ((c = this->nextc()) != -1);
                    }
                    this->pushback(c);
                    if(false==m_lexer.tokfix())
                        yyerror("string too long (truncated)");

                    if (m_lexer.toklen() == start) {
                        no_digits();
                    }
                    else if (nondigit) goto trailing_uc;
                    yylval.nd = new_t<IntLiteralNode>(m_lexer.tok(), 2);
                    return tINTEGER;
                }
                if (c == 'd' || c == 'D') {
                    /* decimal */
                    c = this->nextc();
                    if (c != -1 && ISDIGIT(c)) {
                        do {
                            if (c == '_') {
                                if (nondigit) break;
                                nondigit = c;
                                continue;
                            }
                            if (!ISDIGIT(c)) break;
                            nondigit = 0;
                            m_lexer.tokadd(c);
                        } while ((c = this->nextc()) != -1);
                    }
                    this->pushback(c);
                    if(false==m_lexer.tokfix())
                        yyerror("string too long (truncated)");

                    if (m_lexer.toklen() == start) {
                        no_digits();
                    }
                    else if (nondigit) goto trailing_uc;
                    yylval.nd = new_t<IntLiteralNode>(m_lexer.tok(), 10);
                    return tINTEGER;
                }
                if (c == '_') {
                    /* 0_0 */
                    goto octal_number;
                }
                if (c == 'o' || c == 'O') {
                    /* prefixed octal */
                    c = this->nextc();
                    if (c == -1 || c == '_' || !ISDIGIT(c)) {
                        no_digits();
                    }
                }
                if (c >= '0' && c <= '7') {
                    /* octal */
octal_number:
                    do {
                        if (c == '_') {
                            if (nondigit) break;
                            nondigit = c;
                            continue;
                        }
                        if (c < '0' || c > '9') break;
                        if (c > '7') goto invalid_octal;
                        nondigit = 0;
                        m_lexer.tokadd(c);
                    } while ((c = this->nextc()) != -1);

                    if (m_lexer.toklen() > start) {
                        this->pushback(c);
                        if(false==m_lexer.tokfix())
                            yyerror("string too long (truncated)");

                        if (nondigit) goto trailing_uc;
                        yylval.nd = new_t<IntLiteralNode>(m_lexer.tok(), 8);
                        return tINTEGER;
                    }
                    if (nondigit) {
                        this->pushback(c);
                        goto trailing_uc;
                    }
                }
                if (c > '7' && c <= '9') {
invalid_octal:
                    this->yyerror("Invalid octal digit");
                }
                else if (c == '.' || c == 'e' || c == 'E') {
                    m_lexer.tokadd('0');
                }
                else {
                    this->pushback(c);
                    yylval.nd = new_t<IntLiteralNode>("0", 10);
                    return tINTEGER;
                }
            }

            for (;;) {
                switch (c) {
                    case '0': case '1': case '2': case '3': case '4':
                    case '5': case '6': case '7': case '8': case '9':
                        nondigit = 0;
                        m_lexer.tokadd(c);
                        break;

                    case '.':
                        if (nondigit) goto trailing_uc;
                        if (seen_point || seen_e) {
                            goto decode_num;
                        }
                        else {
                            int c0 = this->nextc();
                            if (c0 == -1 || !ISDIGIT(c0)) {
                                this->pushback(c0);
                                goto decode_num;
                            }
                            c = c0;
                        }
                        m_lexer.tokadd('.');
                        m_lexer.tokadd(c);
                        is_float++;
                        seen_point++;
                        nondigit = 0;
                        break;

                    case 'e':
                    case 'E':
                        if (nondigit) {
                            this->pushback(c);
                            c = nondigit;
                            goto decode_num;
                        }
                        if (seen_e) {
                            goto decode_num;
                        }
                        m_lexer.tokadd(c);
                        seen_e++;
                        is_float++;
                        nondigit = c;
                        c = this->nextc();
                        if (c != '-' && c != '+') continue;
                        m_lexer.tokadd(c);
                        nondigit = c;
                        break;

                    case '_':	/* `_' in number just ignored */
                        if (nondigit) goto decode_num;
                        nondigit = c;
                        break;

                    default:
                        goto decode_num;
                }
                c = this->nextc();
            }

decode_num:
            this->pushback(c);
            if (nondigit) {
trailing_uc:
                this->yyerror_i("trailing `%c' in number", nondigit);
            }
            if(false==m_lexer.tokfix())
                yyerror("string too long (truncated)");

            if (is_float) {
                double d;
                char *endp;

                errno = 0;
                d = strtod(m_lexer.tok(), &endp);
                if (d == 0 && endp == m_lexer.tok()) {
                    yywarning_s("corrupted float value %s", m_lexer.tok());
                }
                else if (errno == ERANGE) {
                    yywarning_s("float %s out of range", m_lexer.tok());
                    errno = 0;
                }
                yylval.nd = new_t<FloatLiteralNode>(m_lexer.tok());
                return tFLOAT;
            }
            yylval.nd = new_t<IntLiteralNode>(m_lexer.tok(), 10);
            return tINTEGER;
        }

        case ')':
        case ']':
            m_lexer.paren_nest--;
        case '}':
            COND_LEXPOP();
            CMDARG_LEXPOP();
            if (c == ')')
                m_lstate = EXPR_ENDFN;
            else
                m_lstate = EXPR_ENDARG;
            return c;

        case ':':
            c = this->nextc();
            if (c == ':') {
                if (IS_BEG() || m_lstate == EXPR_CLASS || IS_SPCARG(-1)) {
                    m_lstate = EXPR_BEG;
                    return tCOLON3;
                }
                m_lstate = EXPR_DOT;
                return tCOLON2;
            }
            if (IS_END() || ISSPACE(c)) {
                this->pushback(c);
                m_lstate = EXPR_BEG;
                return ':';
            }
            this->pushback(c);
            m_lstate = EXPR_FNAME;
            return tSYMBEG;

        case '/':
            if (IS_BEG()) {
                m_lex_strterm = this->new_strterm(str_regexp, '/', 0);
                return tREGEXP_BEG;
            }
            if ((c = this->nextc()) == '=') {
                yylval.id =this->intern_c('/');
                m_lstate = EXPR_BEG;
                return tOP_ASGN;
            }
            this->pushback(c);
            if (IS_SPCARG(c)) {
                m_lex_strterm = this->new_strterm(str_regexp, '/', 0);
                return tREGEXP_BEG;
            }
            if (m_lstate == EXPR_FNAME || m_lstate == EXPR_DOT) {
                m_lstate = EXPR_ARG;
            } else {
                m_lstate = EXPR_BEG;
            }
            return '/';

        case '^':
            if ((c = this->nextc()) == '=') {
                yylval.id =this->intern_c('^');
                m_lstate = EXPR_BEG;
                return tOP_ASGN;
            }
            if (m_lstate == EXPR_FNAME || m_lstate == EXPR_DOT) {
                m_lstate = EXPR_ARG;
            } else {
                m_lstate = EXPR_BEG;
            }
            this->pushback(c);
            return '^';

        case ';':   m_lstate = EXPR_BEG;  return ';';
        case ',':   m_lstate = EXPR_BEG;  return ',';

        case '~':
            if (m_lstate == EXPR_FNAME || m_lstate == EXPR_DOT) {
                if ((c = this->nextc()) != '@') {
                    this->pushback(c);
                }
                m_lstate = EXPR_ARG;
            }
            else {
                m_lstate = EXPR_BEG;
            }
            return '~';

        case '(':
            if (IS_BEG()) {
                c = tLPAREN;
            }
            else if (IS_SPCARG(-1)) {
                c = tLPAREN_ARG;
            }
            m_lexer.paren_nest++;
            COND_PUSH(0);
            CMDARG_PUSH(0);
            m_lstate = EXPR_BEG;
            return c;

        case '[':
            m_lexer.paren_nest++;
            if (m_lstate == EXPR_FNAME || m_lstate == EXPR_DOT) {
                m_lstate = EXPR_ARG;
                if ((c = this->nextc()) == ']') {
                    if ((c = this->nextc()) == '=') {
                        return tASET;
                    }
                    this->pushback(c);
                    return tAREF;
                }
                this->pushback(c);
                return '[';
            }
            else if (IS_BEG()) {
                c = tLBRACK;
            }
            else if (IS_ARG() && space_seen) {
                c = tLBRACK;
            }
            m_lstate = EXPR_BEG;
            COND_PUSH(0);
            CMDARG_PUSH(0);
            return c;

        case '{':
            if (this->lpar_beg && this->lpar_beg == m_lexer.paren_nest) {
                m_lstate = EXPR_BEG;
                this->lpar_beg = 0;
                m_lexer.paren_nest--;
                COND_PUSH(0);
                CMDARG_PUSH(0);
                return tLAMBEG;
            }
            if (IS_ARG() || m_lstate == EXPR_END || m_lstate == EXPR_ENDFN)
                c = '{';          /* block (primary) */
            else if (m_lstate == EXPR_ENDARG)
                c = tLBRACE_ARG;  /* block (expr) */
            else
                c = tLBRACE;      /* hash */
            COND_PUSH(0);
            CMDARG_PUSH(0);
            m_lstate = EXPR_BEG;
            return c;

        case '\\':
            c = this->nextc();
            if (c == '\n') {
                m_lineno++;
                m_column = 0;
                space_seen = 1;
                goto retry; /* skip \\n */
            }
            this->pushback(c);
            return '\\';

        case '%':
            if (IS_BEG()) {
                int term;
                int paren;

                c = this->nextc();
quotation:
                if (c == -1 || !ISALNUM(c)) {
                    term = c;
                    c = 'Q';
                }
                else {
                    term = this->nextc();
                    if (isalnum(term)) {
                        this->yyerror("unknown type of %string");
                        return 0;
                    }
                }
                if (c == -1 || term == -1) {
                    this->yyerror("unterminated quoted string meets end of file");
                    return 0;
                }
                paren = term;
                if (term == '(') term = ')';
                else if (term == '[') term = ']';
                else if (term == '{') term = '}';
                else if (term == '<') term = '>';
                else paren = 0;

                switch (c) {
                    case 'Q':
                        m_lex_strterm = this->new_strterm(str_dquote, term, paren);
                        return tSTRING_BEG;

                    case 'q':
                        m_lex_strterm = this->new_strterm(str_squote, term, paren);
                        return this->parse_string();

                    case 'W':
                        m_lex_strterm = this->new_strterm(str_dword, term, paren);
                        return tWORDS_BEG;

                    case 'w':
                        m_lex_strterm = this->new_strterm(str_sword, term, paren);
                        return tWORDS_BEG;

                    case 'x':
                        m_lex_strterm = this->new_strterm(str_xquote, term, paren);
                        return tXSTRING_BEG;

                    case 'r':
                        m_lex_strterm = this->new_strterm(str_regexp, term, paren);
                        return tREGEXP_BEG;

                    case 's':
                        m_lex_strterm = this->new_strterm(str_ssym, term, paren);
                        return tSYMBEG;

                    case 'I':
                        m_lex_strterm = this->new_strterm(str_dsymbols, term, paren);
                        return tSYMBOLS_BEG;

                    case 'i':
                        m_lex_strterm = this->new_strterm(str_ssymbols, term, paren);
                        return tSYMBOLS_BEG;

                    default:
                        this->yyerror("unknown type of %string");
                        return 0;
                }
            }
            if ((c = this->nextc()) == '=') {
                yylval.id =this->intern_c('%');
                m_lstate = EXPR_BEG;
                return tOP_ASGN;
            }
            if (IS_SPCARG(c)) {
                goto quotation;
            }
            if (m_lstate == EXPR_FNAME || m_lstate == EXPR_DOT) {
                m_lstate = EXPR_ARG;
            } else {
                m_lstate = EXPR_BEG;
            }
            this->pushback(c);
            return '%';

        case '$':
            m_lstate = EXPR_END;
            token_column = this->newtok();
            c = this->nextc();
            if (c == -1) {
                yyerror("incomplete global variable syntax");
                return 0;
            }
            switch (c) {
                case '_':     /* $_: last read line string */
                    c = this->nextc();
                    if (c != -1 && identchar(c)) { /* if there is more after _ it is a variable */
                        m_lexer.tokadd('$');
                        m_lexer.tokadd(c);
                        break;
                    }
                    this->pushback(c);
                    c = '_';
                    /* fall through */
                case '~':     /* $~: match-data */
                case '*':     /* $*: argv */
                case '$':     /* $$: pid */
                case '?':     /* $?: last status */
                case '!':     /* $!: error string */
                case '@':     /* $@: error position */
                case '/':     /* $/: input record separator */
                case '\\':    /* $\: output record separator */
                case ';':     /* $;: field separator */
                case ',':     /* $,: output field separator */
                case '.':     /* $.: last read line number */
                case '=':     /* $=: ignorecase */
                case ':':     /* $:: load path */
                case '<':     /* $<: reading filename */
                case '>':     /* $>: default output handle */
                case '\"':    /* $": already loaded files */
                    m_lexer.tokadd('$');
                    m_lexer.tokadd(c);
                    if(false==m_lexer.tokfix())
                        yyerror("string too long (truncated)");

                    yylval.id =this->intern(m_lexer.tok());
                    return tGVAR;

                case '-':
                    m_lexer.tokadd('$');
                    m_lexer.tokadd(c);
                    c = this->nextc();
                    this->pushback(c);
gvar:
                    if(false==m_lexer.tokfix())
                        yyerror("string too long (truncated)");

                    yylval.id =this->intern(m_lexer.tok());
                    return tGVAR;

                case '&':     /* $&: last match */
                case '`':     /* $`: string before last match */
                case '\'':    /* $': string after last match */
                case '+':     /* $+: string matches last pattern */
                    if (last_state == EXPR_FNAME) {
                        m_lexer.tokadd('$');
                        m_lexer.tokadd(c);
                        goto gvar;
                    }
                    yylval.nd = new_t<BackRefNode>(c);
                    return tBACK_REF;

                case '1': case '2': case '3':
                case '4': case '5': case '6':
                case '7': case '8': case '9':
                    do {
                        m_lexer.tokadd(c);
                        c = this->nextc();
                    } while (c != -1 && isdigit(c));
                    this->pushback(c);
                    if (last_state == EXPR_FNAME) goto gvar;
                    if(false==m_lexer.tokfix())
                        yyerror("string too long (truncated)");

                    yylval.nd = new_t<NthRefNode>(atoi(m_lexer.tok()));
                    return tNTH_REF;

                default:
                    if (!identchar(c)) {
                        this->pushback(c);
                        return '$';
                    }
                case '0':
                    m_lexer.tokadd('$');
            }
            break;

        case '@':
            c = nextc();
            token_column = newtok();
            m_lexer.tokadd('@');
            if (c == '@') {
                m_lexer.tokadd('@');
                c = nextc();
            }
            if (c == -1) {
                if (m_lexer.bidx == 1) {
                    yyerror("incomplete instance variable syntax");
                }
                else {
                    yyerror("incomplete class variable syntax");
                }
                return 0;
            }
            else if (isdigit(c)) {
                if (m_lexer.bidx == 1) {
                    yyerror_i("`@%c' is not allowed as an instance variable name", c);
                }
                else {
                    yyerror_i("`@@%c' is not allowed as a class variable name", c);
                }
                return 0;
            }
            if (!identchar(c)) {
                pushback(c);
                return '@';
            }
            break;

        case '_':
            token_column = newtok();
            break;

        default:
            if (!identchar(c)) {
                yyerror_i("Invalid char `\\x%02X' in expression", c);
                goto retry;
            }

            token_column = newtok();
            break;
    }

    do {
        m_lexer.tokadd(c);
        c = nextc();
        if (c < 0) break;
    } while (identchar(c));
    if (token_column == 0 && m_lexer.toklen() == 7 && (c < 0 || c == '\n') &&
            strncmp(m_lexer.tok(), "__END__", m_lexer.toklen()) == 0)
        return -1;

    switch (m_lexer.tok()[0]) {
        case '@':
        case '$':
            pushback(c);
            break;
        default:
            if ((c == '!' || c == '?') && !this->peek('=')) {
                m_lexer.tokadd(c);
            }
            else {
                pushback(c);
            }
    }
    if(false==m_lexer.tokfix())
        yyerror("string too long (truncated)");
    {
        int result = 0;

        last_state = m_lstate;
        switch (m_lexer.tok()[0]) {
            case '$':
                m_lstate = EXPR_END;
                result = tGVAR;
                break;
            case '@':
                m_lstate = EXPR_END;
                if (m_lexer.tok()[1] == '@')
                    result = tCVAR;
                else
                    result = tIVAR;
                break;

            default:
                if (m_lexer.toklast() == '!' || m_lexer.toklast() == '?') {
                    result = tFID;
                }
                else {
                    if (m_lstate == EXPR_FNAME) {
                        if ((c = this->nextc()) == '=' && !this->peek('~') && !this->peek('>') &&
                                (!this->peek('=') || (this->peek_n('>', 1)))) {
                            result = tIDENTIFIER;
                            m_lexer.tokadd(c);
                            if(false==m_lexer.tokfix())
                                yyerror("string too long (truncated)");

                        }
                        else {
                            this->pushback(c);
                        }
                    }
                    if (result == 0 && isupper((int)m_lexer.tok()[0])) {
                        result = tCONSTANT;
                    }
                    else {
                        result = tIDENTIFIER;
                    }
                }

                if (IS_LABEL_POSSIBLE()) {
                    if (IS_LABEL_SUFFIX(0)) {
                        m_lstate = EXPR_BEG;
                        this->nextc();
                        if(false==m_lexer.tokfix())
                            yyerror("string too long (truncated)");

                        yylval.id =this->intern(m_lexer.tok());
                        return tLABEL;
                    }
                }
                if (m_lstate != EXPR_DOT) {
                    const struct kwtable *kw;

                    /* See if it is a reserved word.  */
                    kw = mrb_reserved_word(m_lexer.tok(), m_lexer.toklen());
                    if (kw) {
                        enum mrb_lex_state_enum state = m_lstate;
                        m_lstate = kw->state;
                        if (state == EXPR_FNAME) {
                            yylval.id =this->intern(kw->name);
                            return kw->id[0];
                        }
                        if (m_lstate == EXPR_BEG) {
                            m_cmd_start = true;
                        }
                        if (kw->id[0] == keyword_do) {
                            if (this->lpar_beg && this->lpar_beg == m_lexer.paren_nest) {
                                this->lpar_beg = 0;
                                m_lexer.paren_nest--;
                                return keyword_do_LAMBDA;
                            }
                            if (COND_P()) return keyword_do_cond;
                            if (CMDARG_P() && state != EXPR_CMDARG)
                                return keyword_do_block;
                            if (state == EXPR_ENDARG || state == EXPR_BEG)
                                return keyword_do_block;
                            return keyword_do;
                        }
                        if (state == EXPR_BEG || state == EXPR_VALUE)
                            return kw->id[0];
                        else {
                            if (kw->id[0] != kw->id[1])
                                m_lstate = EXPR_BEG;
                            return kw->id[1];
                        }
                    }
                }

                if (IS_BEG() || m_lstate == EXPR_DOT || IS_ARG()) {
                    if (cmd_state) {
                        m_lstate = EXPR_CMDARG;
                    }
                    else {
                        m_lstate = EXPR_ARG;
                    }
                }
                else if (m_lstate == EXPR_FNAME) {
                    m_lstate = EXPR_ENDFN;
                }
                else {
                    m_lstate = EXPR_END;
                }
        }
        {
            mrb_sym ident =intern(m_lexer.tok());

            yylval.id = ident;
#if 0
            if (last_state != EXPR_DOT && islower(p->tok()[0]) && lvar_defined(ident)) {
                p->lstate = EXPR_END;
            }
#endif
        }
        return result;
    }
}
