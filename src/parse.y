/*
** parse.y - mruby parser
**
** See Copyright Notice in mruby.h
*/

%{
#undef PARSER_DEBUG

#define YYDEBUG 1
#define YYERROR_VERBOSE 1
/*
 * Force yacc to use our memory management.  This is a little evil because
 * the macros assume that "parser_state *p" is in scope
 */
#define YYMALLOC(n)    p->m_mrb->gc()._malloc((n))
#define YYFREE(o)      p->m_mrb->gc()._free((o))
#define YYSTACK_USE_ALLOCA 0

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <utility>

#include "mruby.h"
#include "mruby/compile.h"
#include "mruby/proc.h"
#include "mruby/node.h"


#define YYLEX_PARAM p

typedef mrb_ast_node node;
typedef struct mrb_parser_state parser_state;
typedef struct mrb_parser_heredoc_info parser_heredoc_info;

static int yylex(void *lval, parser_state *p);
static void yyerror(parser_state *p, const char *s);
static void backref_error(parser_state *p, node *n);

#define identchar(c) (isalnum(c) || (c) == '_' || !isascii(c))

typedef unsigned int stack_type;

#define BITSTACK_PUSH(stack, n) ((stack) = ((stack)<<1)|((n)&1))
#define BITSTACK_POP(stack)     ((stack) = (stack) >> 1)
#define BITSTACK_LEXPOP(stack)  ((stack) = ((stack) >> 1) | ((stack) & 1))
#define BITSTACK_SET_P(stack)   ((stack)&1)

#define COND_PUSH(n)    BITSTACK_PUSH(p->cond_stack, (n))
#define COND_POP()      BITSTACK_POP(p->cond_stack)
#define COND_LEXPOP()   BITSTACK_LEXPOP(p->cond_stack)
#define COND_P()        BITSTACK_SET_P(p->cond_stack)

#define CMDARG_PUSH(n)  BITSTACK_PUSH(p->cmdarg_stack, (n))
#define CMDARG_POP()    BITSTACK_POP(p->cmdarg_stack)
#define CMDARG_LEXPOP() BITSTACK_LEXPOP(p->cmdarg_stack)
#define CMDARG_P()      BITSTACK_SET_P(p->cmdarg_stack)

#define sym(x) ((mrb_sym)(intptr_t)(x))
#define nsym(x) ((node*)(intptr_t)(x))

#define newline_node(n) (n)
// xxx -----------------------------

%}

%pure-parser
%parse-param {mrb_parser_state *p}
%lex-param {mrb_parser_state *p}

%union {
    mrb_ast_node *nd;
    UndefNode *undef_t;
    StrNode *sn;
    mrb_sym id;
    int num;
    unsigned int stack;
    const struct vtable *vars;
    size_t locals_ctx;
    ArgsStore *arg;
    CommandArgs *cmd_arg;
    struct {
        size_t idx;
        intptr_t single;
    } l_s;
}

%token
        keyword_class
        keyword_module
        keyword_def
        keyword_undef
        keyword_begin
        keyword_rescue
        keyword_ensure
        keyword_end
        keyword_if
        keyword_unless
        keyword_then
        keyword_elsif
        keyword_else
        keyword_case
        keyword_when
        keyword_while
        keyword_until
        keyword_for
        keyword_break
        keyword_next
        keyword_redo
        keyword_retry
        keyword_in
        keyword_do
        keyword_do_cond
        keyword_do_block
        keyword_do_LAMBDA
        keyword_return
        keyword_yield
        keyword_super
        keyword_self
        keyword_nil
        keyword_true
        keyword_false
        keyword_and
        keyword_or
        keyword_not
        modifier_if
        modifier_unless
        modifier_while
        modifier_until
        modifier_rescue
        keyword_alias
        keyword_BEGIN
        keyword_END
        keyword__LINE__
        keyword__FILE__
        keyword__ENCODING__

%token <id>  tIDENTIFIER tFID tGVAR tIVAR tCONSTANT tCVAR tLABEL
%token <nd>  tINTEGER tFLOAT tCHAR tXSTRING tREGEXP
%token <sn>  tSTRING tSTRING_PART tSTRING_MID
%token <nd>  tNTH_REF tBACK_REF
%token <num> tREGEXP_END

%type <nd> singleton string string_rep string_interp xstring regexp
%type <nd> literal numeric cpath symbol
%type <nd> top_compstmt top_stmts top_stmt
%type <nd> bodystmt compstmt stmts stmt expr arg primary command command_call method_call
%type <nd> expr_value arg_value primary_value
%type <nd> if_tail opt_else case_body cases opt_rescue exc_list exc_var opt_ensure
%type <nd> args
%type <cmd_arg> call_args opt_call_args command_args paren_args opt_paren_args
%type <nd> variable
%type <nd> aref_args opt_block_arg block_arg var_ref var_lhs
%type <nd> command_asgn mrhs superclass block_call block_command
%type <nd> f_block_optarg f_block_opt
%type <nd> f_arg f_arg_item f_optarg f_marg f_marg_list f_margs
%type <nd> assoc_list assocs assoc backref for_var
%type <undef_t> undef_list
%type <arg> block_param opt_block_param block_param_def f_larglist f_arglist f_args
%type <nd> f_opt
%type <nd> bv_decls opt_bv_decl bvar lambda_body
%type <nd> brace_block cmd_brace_block do_block lhs none f_bad_arg
%type <nd> mlhs mlhs_list mlhs_post mlhs_basic mlhs_item mlhs_node mlhs_inner
%type <id> fsym sym basic_symbol operation operation2 operation3
%type <id> cname fname op f_rest_arg f_block_arg opt_f_block_arg f_norm_arg
%type <nd> heredoc words symbols

%token tUPLUS             /* unary+ */
%token tUMINUS            /* unary- */
%token tPOW               /* ** */
%token tCMP               /* <=> */
%token tEQ                /* == */
%token tEQQ               /* === */
%token tNEQ               /* != */
%token tGEQ               /* >= */
%token tLEQ               /* <= */
%token tANDOP tOROP       /* && and || */
%token tMATCH tNMATCH     /* =~ and !~ */
%token tDOT2 tDOT3        /* .. and ... */
%token tAREF tASET        /* [] and []= */
%token tLSHFT tRSHFT      /* << and >> */
%token tCOLON2            /* :: */
%token tCOLON3            /* :: at EXPR_BEG */
%token <id> tOP_ASGN      /* +=, -=  etc. */
%token tASSOC             /* => */
%token tLPAREN            /* ( */
%token tLPAREN_ARG        /* ( */
%token tRPAREN            /* ) */
%token tLBRACK            /* [ */
%token tLBRACE            /* { */
%token tLBRACE_ARG        /* { */
%token tSTAR              /* * */
%token tAMPER             /* & */
%token tLAMBDA            /* -> */
%token tSYMBEG tREGEXP_BEG tWORDS_BEG tSYMBOLS_BEG
%token tSTRING_BEG tXSTRING_BEG tSTRING_DVAR tLAMBEG
%token <nd> tHEREDOC_BEG  /* <<, <<- */
%token tHEREDOC_END tLITERAL_DELIM tHD_LITERAL_DELIM
%token <nd> tHD_STRING_PART tHD_STRING_MID

/*
 *	precedence table
 */

%nonassoc tLOWEST
%nonassoc tLBRACE_ARG

%nonassoc  modifier_if modifier_unless modifier_while modifier_until
%left  keyword_or keyword_and
%right keyword_not
%right '=' tOP_ASGN
%left modifier_rescue
%right '?' ':'
%nonassoc tDOT2 tDOT3
%left  tOROP
%left  tANDOP
%nonassoc  tCMP tEQ tEQQ tNEQ tMATCH tNMATCH
%left  '>' tGEQ '<' tLEQ
%left  '|' '^'
%left  '&'
%left  tLSHFT tRSHFT
%left  '+' '-'
%left  '*' '/' '%'
%right tUMINUS_NUM tUMINUS
%right tPOW
%right '!' '~' tUPLUS

%nonassoc idNULL
%nonassoc idRespond_to
%nonassoc idIFUNC
%nonassoc idCFUNC
%nonassoc id_core_set_method_alias
%nonassoc id_core_set_variable_alias
%nonassoc id_core_undef_method
%nonassoc id_core_define_method
%nonassoc id_core_define_singleton_method
%nonassoc id_core_set_postexe

%token tLAST_TOKEN

%%
program		:  {
                     p->m_lstate = EXPR_BEG;
                     p->init_locals();
                   }
                  top_compstmt
                    {
                      p->m_tree = p->new_scope($2);
                    }
                ;

top_compstmt	: top_stmts opt_terms
                    {
                      $$ = $1;
                    }
                ;

top_stmts	: none      { $$ = p->new_t<BeginNode>(nullptr); }
                | top_stmt  { $$ = p->new_t<BeginNode>($1); }
                | top_stmts terms top_stmt
                    {
                        BeginNode *bn = static_cast<BeginNode *>($1);
                        bn->push_back(newline_node($3));
                        $$ = bn;
                    }
                | error top_stmt { $$ = p->new_t<BeginNode>(nullptr); }
                ;

top_stmt	: stmt
                | keyword_BEGIN
            {
              $<locals_ctx>$ = p->local_switch();
            }
                  '{' top_compstmt '}'
                    {
                      p->yyerror("BEGIN not supported");
                      p->local_resume($<locals_ctx>2);
                      $$ = 0;
                    }
                ;

bodystmt	: compstmt
                  opt_rescue
                  opt_else
                  opt_ensure
                    {
                      if ($2) {
                        $$ = p->new_t<RescueNode>($1, $2, $3);
                      }
                      else if ($3) {
                        p->yywarn("else without rescue is useless");
                        $$ = p->push($1, $3);
                      }
                      else {
                        $$ = $1;
                      }
                      if ($4) {
                        if ($$) {
                          $$ = p->new_ensure($$, $4);
                        }
                        else {
                          $$ = p->push($4, p->new_t<NilNode>());
                        }
                      }
                    }
                ;

compstmt	: stmts opt_terms  { $$ = $1; }
                ;

stmts		: none  { $$ = p->new_t<BeginNode>(nullptr); }
                | stmt  { $$ = p->new_t<BeginNode>($1); }
                | stmts terms stmt
                    {
                        BeginNode *bn = dynamic_cast<BeginNode *>($1);
                        if(bn) {
                            bn->push_back(newline_node($3));
                            $$ = bn;
                        }
                        else
                            $$ = p->push($1, newline_node($3));
                    }
                | error stmt
                    {
                      $$ = p->new_t<BeginNode>($2);
                    }
                ;

stmt		: keyword_alias fsym {p->m_lstate = EXPR_FNAME;} fsym
                    {
                      $$ = p->new_t<AliasNode>($2, $4);
                    }
                | keyword_undef undef_list
                    {
                      $$ = $2;
                    }
                | stmt modifier_if expr_value
                    {
                        $$ = p->new_t<IfNode>(p->cond($3), $1, nullptr);
                    }
                | stmt modifier_unless expr_value
                    {
                      $$ = p->new_unless(p->cond($3), $1, 0);
                    }
                | stmt modifier_while expr_value
                    {
                      $$ = p->new_t<WhileNode>(p->cond($3), $1);
                    }
                | stmt modifier_until expr_value
                    {
                      $$ = p->new_t<UntilNode>(p->cond($3), $1);
                    }
                | stmt modifier_rescue stmt
                    {
                      $$ = p->new_t<RescueNode>($1, p->list1(p->list3(0, 0, $3)), nullptr);
                    }
                | keyword_END '{' compstmt '}'
                    {
                      p->yyerror("END not suported");
                      $$ = p->new_t<PostExeNode>($3);
                    }
                | command_asgn
                | mlhs '=' command_call
                    {
                      $$ = p->new_t<MAsgnNode>($1, $3);
                    }
                | var_lhs tOP_ASGN command_call
                    {
                      $$ = p->new_t<OpAsgnNode>($1, $2, $3); //var_lhs tOP_ASGN command_call
                    }
                | primary_value '[' opt_call_args rbracket tOP_ASGN command_call
                    {
                     //primary_value '[' opt_call_args rbracket tOP_ASGN command_call
                      $$ = p->new_t<OpAsgnNode>(p->new_t<CallNode>($1, p->intern2("[]",2), $3), $5, $6);
                    }
                | primary_value '.' tIDENTIFIER tOP_ASGN command_call
                    {
                      //primary_value '.' tIDENTIFIER tOP_ASGN command_call
                      $$ = p->new_t<OpAsgnNode>(p->new_t<CallNode>($1, $3, nullptr), $4, $5);
                    }
                | primary_value '.' tCONSTANT tOP_ASGN command_call
                    {
                      $$ = p->new_t<OpAsgnNode>(p->new_t<CallNode>($1, $3, nullptr), $4, $5);
                    }
                | primary_value tCOLON2 tCONSTANT tOP_ASGN command_call
                    {
                      p->yyerror("constant re-assignment");
                      $$ = 0;
                    }
                | primary_value tCOLON2 tIDENTIFIER tOP_ASGN command_call
                    {
                      $$ = p->new_t<OpAsgnNode>(p->new_t<CallNode>($1, $3, nullptr), $4, $5);
                    }
                | backref tOP_ASGN command_call
                    {
                      p->backref_error($1);
                      $$ = p->new_t<BeginNode>(nullptr);
                    }
                | lhs '=' mrhs
                    {
                      $$ = p->new_t<AsgnNode>($1, p->new_t<ArrayNode>($3));
                    }
                | mlhs '=' arg_value
                    {
                      $$ = p->new_t<MAsgnNode>($1, $3);
                    }
                | mlhs '=' mrhs
                    {
                      $$ = p->new_t<MAsgnNode>($1, p->new_t<ArrayNode>($3));
                    }
                | expr
                ;

command_asgn	: lhs '=' command_call
                    {
                      $$ = p->new_t<AsgnNode>($lhs, $command_call);
                    }
                | lhs '=' command_asgn
                    {
                      $$ = p->new_t<AsgnNode>($lhs, $3);
                    }
                ;


expr		: command_call
                | expr keyword_and expr
                    {
                      $$ = p->new_t<AndNode>($1, $3);
                    }
                | expr keyword_or expr
                    {
                      $$ = p->new_t<OrNode>($1, $3);
                    }
                | keyword_not opt_nl expr
                    {
                      $$ = p->call_uni_op(p->cond($3), "!");
                    }
                | '!' command_call
                    {
                      $$ = p->call_uni_op(p->cond($2), "!");
                    }
                | arg
                ;

expr_value	: expr { $$ = ($1) ? $1 : p->new_t<NilNode>(); }
                ;

command_call	: command
                | block_command
                ;

block_command	: block_call
                | block_call dot_or_colon operation2 command_args
                ;

cmd_brace_block	: tLBRACE_ARG
                    {
                      p->local_nest();
                    }
                  opt_block_param
                  compstmt
                  '}'
                    {
                      $$ = p->new_block($3, $4);
                      p->local_unnest();
                    }
                ;

command		: operation command_args       %prec tLOWEST
                    {
                      $$ = p->new_t<FCallNode>(p->new_t<SelfNode>(),$1, $2);
                    }
                | operation command_args cmd_brace_block
                    {
                      p->args_with_block($2, $3);
                      $$ = p->new_t<FCallNode>(p->new_t<SelfNode>(),$1, $2);
                    }
                | primary_value '.' operation2 command_args	%prec tLOWEST
                    {
                      $$ = p->new_t<CallNode>($primary_value, $operation2, $command_args);
                    }
                | primary_value '.' operation2 command_args cmd_brace_block
                    {
                      p->args_with_block($4, $5);
                      $$ = p->new_t<CallNode>($1, $3, $4);
                   }
                | primary_value tCOLON2 operation2 command_args	%prec tLOWEST
                    {
                      $$ = p->new_t<CallNode>($1, $3, $4);
                    }
                | primary_value tCOLON2 operation2 command_args cmd_brace_block
                    {
                      p->args_with_block($4, $5);
                      $$ = p->new_t<CallNode>($1, $3, $4);
                    }
                | keyword_super command_args
                    {
                      $$ = p->new_t<SuperNode>($2);
                    }
                | keyword_yield command_args
                    {
                      $$ = p->new_yield($2);
                    }
                | keyword_return call_args
                    {
                      $$ = p->new_t<ReturnNode>(p->ret_args($2));
                    }
                | keyword_break call_args
                    {
                      $$ = p->new_t<BreakNode>(p->ret_args($2));
                    }
                | keyword_next call_args
                    {
                      $$ = p->new_t<NextNode>(p->ret_args($2));
                    }
                ;

mlhs		: mlhs_basic
                    {
                      $$ = $1;
                    }
                | tLPAREN mlhs_inner rparen
                    {
                      $$ = $2;
                    }
                ;

mlhs_inner	: mlhs_basic
                | tLPAREN mlhs_inner rparen
                    {
                      $$ = p->list1($2);
                    }
                ;

mlhs_basic	: mlhs_list
                    {
                      $$ = p->list1($1);
                    }
                | mlhs_list mlhs_item
                    {
                      $$ = p->list1(p->push($1,$2));
                    }
                | mlhs_list tSTAR mlhs_node
                    {
                      $$ = p->list2($1, $3);
                    }
                | mlhs_list tSTAR mlhs_node ',' mlhs_post
                    {
                      $$ = p->list3($1, $3, $5);
                    }
                | mlhs_list tSTAR
                    {
                      $$ = p->list2($1, p->new_t<NilNode>());
                    }
                | mlhs_list tSTAR ',' mlhs_post
                    {
                      $$ = p->list3($1, p->new_t<NilNode>(), $4);
                    }
                | tSTAR mlhs_node
                    {
                      $$ = p->list2(0, $2);
                    }
                | tSTAR mlhs_node ',' mlhs_post
                    {
                      $$ = p->list3(0, $2, $4);
                    }
                | tSTAR
                    {
                      $$ = p->list2(0, p->new_t<NilNode>());
                    }
                | tSTAR ',' mlhs_post
                    {
                      $$ = p->list3(0, p->new_t<NilNode>(), $3);
                    }
                ;

mlhs_item	: mlhs_node
                | tLPAREN mlhs_inner rparen
                    {
                      $$ = $2;
                    }
                ;

mlhs_list	: mlhs_item ','
                    {
                      $$ = p->list1($1);
                    }
                | mlhs_list mlhs_item ','
                    {
                      $$ = p->push($1, $2);
                    }
                ;

mlhs_post	: mlhs_item
                    {
                      $$ = p->list1($1);
                    }
                | mlhs_list mlhs_item
                    {
                      $$ = p->push($1, $2);
                    }
                ;

mlhs_node	: variable
                    {
                      p->assignable($1);
                    }
                | primary_value '[' opt_call_args rbracket
                    {
                      $$ = p->new_t<CallNode>($1, p->intern2("[]",2), $3);
                    }
                | primary_value '.' tIDENTIFIER
                    {
                      $$ = p->new_t<CallNode>($1, $3, nullptr);
                    }
                | primary_value tCOLON2 tIDENTIFIER
                    {
                      $$ = p->new_t<CallNode>($1, $3, nullptr);
                    }
                | primary_value '.' tCONSTANT
                    {
                      $$ = p->new_t<CallNode>($1, $3, nullptr);
                    }
                | primary_value tCOLON2 tCONSTANT
                    {
                      if (p->in_def || p->in_single)
                        p->yyerror("dynamic constant assignment");
                      $$ = p->new_t<Colon2Node>($1, $3);
                    }
                | tCOLON3 tCONSTANT
                    {
                      if (p->in_def || p->in_single)
                        p->yyerror("dynamic constant assignment");
                      $$ = p->new_t<Colon3Node>($2);
                    }
                | backref
                    {
                      p->backref_error($1);
                      $$ = 0;
                    }
                ;

lhs		: variable
                    {
                      p->assignable($1);
                    }
                | primary_value '[' opt_call_args rbracket
                    {
                      $$ = p->new_t<CallNode>($1,p->intern2("[]",2), $3);
                    }
                | primary_value '.' tIDENTIFIER
                    {
                      $$ = p->new_t<CallNode>($1, $3, nullptr);
                    }
                | primary_value tCOLON2 tIDENTIFIER
                    {
                      $$ = p->new_t<CallNode>($1, $3, nullptr);
                    }
                | primary_value '.' tCONSTANT
                    {
                      $$ = p->new_t<CallNode>($1, $3, nullptr);
                    }
                | primary_value tCOLON2 tCONSTANT
                    {
                      if (p->in_def || p->in_single)
                        p->yyerror("dynamic constant assignment");
                      $$ = p->new_t<Colon2Node>($1, $3);
                    }
                | tCOLON3 tCONSTANT
                    {
                      if (p->in_def || p->in_single)
                        p->yyerror("dynamic constant assignment");
                      $$ = p->new_t<Colon3Node>($2);
                    }
                | backref
                    {
                      p->backref_error($1);
                      $$ = 0;
                    }
                ;

cname		: tIDENTIFIER
                    {
                      p->yyerror("class/module name must be CONSTANT");
                    }
                | tCONSTANT
                ;

cpath		: tCOLON3 cname
                    {
                      $$ =p->cons((node*)1, nsym($2));
                    }
                | cname
                    {
                      $$ =p->cons((node*)0, nsym($1));
                    }
                | primary_value tCOLON2 cname
                    {
                      $$ =p->cons($1, nsym($3));
                    }
                ;

fname		: tIDENTIFIER
                | tCONSTANT
                | tFID
                | op
                    {
                      p->m_lstate = EXPR_ENDFN;
                      $$ = $1;
                    }
                | reswords
                    {
                      p->m_lstate = EXPR_ENDFN;
                      $$ = $<id>1;
                    }
                ;

fsym		: fname
                | basic_symbol
                ;

undef_list	: fsym
                    {
                      $$ = p->new_t<UndefNode>($1);
                    }
                | undef_list ',' {p->m_lstate = EXPR_FNAME;} fsym
                    {
                      $1->push_back($4);
                      $$ = $1;
                    }
                ;

op		: '|'		{ $$ =p->intern_c('|'); }
                | '^'		{ $$ =p->intern_c('^'); }
                | '&'		{ $$ =p->intern_c('&'); }
                | tCMP		{ $$ =p->intern2("<=>",3); }
                | tEQ		{ $$ =p->intern2("==",2); }
                | tEQQ		{ $$ =p->intern2("===",3); }
                | tMATCH	{ $$ =p->intern2("=~",2); }
                | tNMATCH	{ $$ =p->intern2("!~",2); }
                | '>'		{ $$ =p->intern_c('>'); }
                | tGEQ		{ $$ =p->intern2(">=",2); }
                | '<'		{ $$ =p->intern_c('<'); }
                | tLEQ		{ $$ =p->intern2("<=",2); }
                | tNEQ		{ $$ =p->intern2("!=",2); }
                | tLSHFT	{ $$ =p->intern2("<<",2); }
                | tRSHFT	{ $$ =p->intern2(">>",2); }
                | '+'		{ $$ =p->intern_c('+'); }
                | '-'		{ $$ =p->intern_c('-'); }
                | '*'		{ $$ =p->intern_c('*'); }
                | tSTAR		{ $$ =p->intern_c('*'); }
                | '/'		{ $$ =p->intern_c('/'); }
                | '%'		{ $$ =p->intern_c('%'); }
                | tPOW		{ $$ =p->intern2("**",2); }
                | '!'		{ $$ =p->intern_c('!'); }
                | '~'		{ $$ =p->intern_c('~'); }
                | tUPLUS	{ $$ =p->intern2("+@",2); }
                | tUMINUS	{ $$ =p->intern2("-@",2); }
                | tAREF		{ $$ =p->intern2("[]",2); }
                | tASET		{ $$ =p->intern2("[]=",3); }
                | '`'		{ $$ =p->intern_c('`'); }
                ;

reswords	: keyword__LINE__ | keyword__FILE__ | keyword__ENCODING__
                | keyword_BEGIN | keyword_END
                | keyword_alias | keyword_and | keyword_begin
                | keyword_break | keyword_case | keyword_class | keyword_def
                | keyword_do | keyword_else | keyword_elsif
                | keyword_end | keyword_ensure | keyword_false
                | keyword_for | keyword_in | keyword_module | keyword_next
                | keyword_nil | keyword_not | keyword_or | keyword_redo
                | keyword_rescue | keyword_retry | keyword_return | keyword_self
                | keyword_super | keyword_then | keyword_true | keyword_undef
                | keyword_when | keyword_yield | keyword_if | keyword_unless
                | keyword_while | keyword_until
                ;

arg		: lhs '=' arg
                    {
                      $$ = p->new_t<AsgnNode>($1, $3);
                    }
                | lhs '=' arg modifier_rescue arg
                    {
                      $$ = p->new_t<AsgnNode>($lhs, p->new_t<RescueNode>($3, p->list1(p->list3(0, 0, $5)), nullptr));
                    }
                | var_lhs tOP_ASGN arg
                    {
                      //var_lhs tOP_ASGN arg
                      $$ = p->new_t<OpAsgnNode>($var_lhs, $2, $3);
                    }
                | var_lhs tOP_ASGN arg modifier_rescue arg
                    {
                      $$ = p->new_t<OpAsgnNode>($var_lhs, $2, p->new_t<RescueNode>($3, p->list1(p->list3(0, 0, $5)), nullptr));
                    }
                | primary_value '[' opt_call_args rbracket tOP_ASGN arg
                    {
                      $$ = p->new_t<OpAsgnNode>(p->new_t<CallNode>($primary_value,p->intern2("[]",2), $3), $5, $6);
                    }
                | primary_value '.' tIDENTIFIER tOP_ASGN arg
                    {
                      $$ = p->new_t<OpAsgnNode>(p->new_t<CallNode>($primary_value, $3, nullptr), $4, $5);
                    }
                | primary_value '.' tCONSTANT tOP_ASGN arg
                    {
                      $$ = p->new_t<OpAsgnNode>(p->new_t<CallNode>($primary_value, $3, nullptr), $4, $5);
                    }
                | primary_value tCOLON2 tIDENTIFIER tOP_ASGN arg
                    {
                      $$ = p->new_t<OpAsgnNode>(p->new_t<CallNode>($primary_value, $3, nullptr), $4, $5);
                    }
                | primary_value tCOLON2 tCONSTANT tOP_ASGN arg
                    {
                      p->yyerror("constant re-assignment");
                      $$ = p->new_t<BeginNode>(nullptr);
                    }
                | tCOLON3 tCONSTANT tOP_ASGN arg
                    {
                      p->yyerror("constant re-assignment");
                      $$ = p->new_t<BeginNode>(nullptr);
                    }
                | backref tOP_ASGN arg
                    {
                      p->backref_error($1);
                      $$ = p->new_t<BeginNode>(nullptr);
                    }
                | arg tDOT2 arg
                    {
                      $$ = p->new_t<Dot2Node>($1, $3);
                    }
                | arg tDOT3 arg
                    {
                      $$ = p->new_t<Dot3Node>($1, $3);
                    }
                | arg '+' arg
                    {
                      $$ = p->call_bin_op($1, "+", $3);
                    }
                | arg '-' arg
                    {
                      $$ = p->call_bin_op($1, "-", $3);
                    }
                | arg '*' arg
                    {
                      $$ = p->call_bin_op($1, "*", $3);
                    }
                | arg '/' arg
                    {
                      $$ = p->call_bin_op($1, "/", $3);
                    }
                | arg '%' arg
                    {
                      $$ = p->call_bin_op($1, "%", $3);
                    }
                | arg tPOW arg
                    {
                      $$ = p->call_bin_op($1, "**", $3);
                    }
                | tUMINUS_NUM tINTEGER tPOW arg
                    {
                      $$ = p->call_uni_op(p->call_bin_op($2, "**", $4), "-@");
                    }
                | tUMINUS_NUM tFLOAT tPOW arg
                    {
                      $$ = p->call_uni_op(p->call_bin_op($2, "**", $4), "-@");
                    }
                | tUPLUS arg
                    {
                      $$ = p->call_uni_op($2, "+@");
                    }
                | tUMINUS arg
                    {
                      $$ = p->call_uni_op($2, "-@");
                    }
                | arg '|' arg
                    {
                      $$ = p->call_bin_op($1, "|", $3);
                    }
                | arg '^' arg
                    {
                      $$ = p->call_bin_op($1, "^", $3);
                    }
                | arg '&' arg
                    {
                      $$ = p->call_bin_op($1, "&", $3);
                    }
                | arg tCMP arg
                    {
                      $$ = p->call_bin_op($1, "<=>", $3);
                    }
                | arg '>' arg
                    {
                      $$ = p->call_bin_op($1, ">", $3);
                    }
                | arg tGEQ arg
                    {
                      $$ = p->call_bin_op($1, ">=", $3);
                    }
                | arg '<' arg
                    {
                      $$ = p->call_bin_op($1, "<", $3);
                    }
                | arg tLEQ arg
                    {
                      $$ = p->call_bin_op($1, "<=", $3);
                    }
                | arg tEQ arg
                    {
                      $$ = p->call_bin_op($1, "==", $3);
                    }
                | arg tEQQ arg
                    {
                      $$ = p->call_bin_op($1, "===", $3);
                    }
                | arg tNEQ arg
                    {
                      $$ = p->call_bin_op($1, "!=", $3);
                    }
                | arg tMATCH arg
                    {
                      $$ = p->call_bin_op($1, "=~", $3);
                    }
                | arg tNMATCH arg
                    {
                      $$ = p->call_bin_op($1, "!~", $3);
                    }
                | '!' arg
                    {
                      $$ = p->call_uni_op(p->cond($2), "!");
                    }
                | '~' arg
                    {
                      $$ = p->call_uni_op(p->cond($2), "~");
                    }
                | arg tLSHFT arg
                    {
                      $$ = p->call_bin_op($1, "<<", $3);
                    }
                | arg tRSHFT arg
                    {
                      $$ = p->call_bin_op($1, ">>", $3);
                    }
                | arg tANDOP arg
                    {
                      $$ = p->new_t<AndNode>($1, $3);
                    }
                | arg tOROP arg
                    {
                      $$ = p->new_t<OrNode>($1, $3);
                    }
                | arg '?' arg opt_nl ':' arg
                    {
                      $$ = p->new_t<IfNode>(p->cond($1), $3, $6);
                    }
                | primary
                    {
                      $$ = $1;
                    }
                ;

arg_value	: arg
                    {
                      $$ = $1;
                      if (!$$) $$ = p->new_t<NilNode>();
                    }
                ;

aref_args	: none
                | args trailer
                    {
                      $$ = $1;
                    }
                | args ',' assocs trailer
                    {
                      $$ = p->push($1, p->new_t<HashNode>($3));
                    }
                | assocs trailer
                    {
                      $$ =p->cons(p->new_t<HashNode>($1), 0);
                    }
                ;

paren_args	: '(' opt_call_args rparen
                    {
                      $$ = $2;
                    }
                ;

opt_paren_args	: none
                | paren_args
                ;

opt_call_args	: none
                | call_args
                | args ','
                    {
                      $$ = p->new_simple<CommandArgs>($1);
                    }
                | args ',' assocs ','
                    {
                      $$ = p->new_simple<CommandArgs>(p->push($1, p->new_t<HashNode>($3)) );
                    }
                | assocs ','
                    {
                      $$ =p->new_simple<CommandArgs>(p->list1(p->new_t<HashNode>($1)) );
                    }
                ;

call_args	: command
                    {
                      $$ = p->new_simple<CommandArgs>(p->list1($1) );
                    }
                | args opt_block_arg
                    {
                      $$ = p->new_simple<CommandArgs>($1, $2);
                    }
                | assocs opt_block_arg
                    {
                      $$ = p->new_simple<CommandArgs>(p->list1(p->new_t<HashNode>($1)), $2);
                    }
                | args ',' assocs opt_block_arg
                    {
                      $$ = p->new_simple<CommandArgs>(p->push($1, p->new_t<HashNode>($3)), $4);
                    }
                | block_arg
                    {
                      $$ = p->new_simple<CommandArgs>(nullptr, $1);
                    }
                ;

command_args	:  {
                      $<stack>$ = p->cmdarg_stack;
                      CMDARG_PUSH(1);
                    }
                  call_args
                    {
                      p->cmdarg_stack = $<stack>1;
                      $$ = $2;
                    }
                ;

block_arg	: tAMPER arg_value
                    {
                      $$ = p->new_t<BlockArgNode>($2);
                    }
                ;

opt_block_arg	: ',' block_arg
                    {
                      $$ = $2;
                    }
                | none
                    {
                      $$ = 0;
                    }
                ;

args		: arg_value
                    {
                      $$ =p->cons($1, 0);
                    }
                | tSTAR arg_value
                    {
                      $$ =p->cons(p->new_t<SplatNode>($2), 0);
                    }
                | args ',' arg_value
                    {
                      $$ = p->push($1, $3);
                    }
                | args ',' tSTAR arg_value
                    {
                      $$ = p->push($1, p->new_t<SplatNode>($4));
                    }
                | args ',' heredoc_bodies arg_value
                  {
                      $$ = p->push($1, $4);
                  }
                | args ',' heredoc_bodies tSTAR arg_value
                  {
                      $$ = p->push($1, p->new_t<SplatNode>($5));
                  }
                ;

mrhs		: args ',' arg_value
                    {
                      $$ = p->push($1, $3);
                    }
                | args ',' tSTAR arg_value
                    {
                      $$ = p->push($1, p->new_t<SplatNode>($4));
                    }
                | tSTAR arg_value
                    {
                      $$ = p->list1(p->new_t<SplatNode>($2));
                    }
                ;

primary		: literal
                | string
                | xstring
                | regexp
                | heredoc
                | var_ref
                | backref
                | tFID
                    {
                      $$ = p->new_t<FCallNode>(p->new_t<SelfNode>(),$1, nullptr);
                    }
                | keyword_begin
                    {
                      $<stack>1 = p->cmdarg_stack;
                      p->cmdarg_stack = 0;
                    }
                  bodystmt
                  keyword_end
                    {
                      p->cmdarg_stack = $<stack>1;
                      $$ = $3;
                    }
                | tLPAREN_ARG expr {p->m_lstate = EXPR_ENDARG;} rparen
                    {
                      $$ = $2;
                    }
                | tLPAREN_ARG {p->m_lstate = EXPR_ENDARG;} rparen
                    {
                      $$ = 0;
                    }
                | tLPAREN compstmt ')'
                    {
                      $$ = $2;
                    }
                | primary_value tCOLON2 tCONSTANT
                    {
                      $$ = p->new_t<Colon2Node>($1, $3);
                    }
                | tCOLON3 tCONSTANT
                    {
                      $$ = p->new_t<Colon3Node>($2);
                    }
                | tLBRACK aref_args ']'
                    {
                      $$ = p->new_t<ArrayNode>($2);
                    }
                | tLBRACE assoc_list '}'
                    {
                      $$ = p->new_t<HashNode>($2);
                    }
                | keyword_return
                    {
                      $$ = p->new_t<ReturnNode>(nullptr);
                    }
                | keyword_yield '(' call_args rparen
                    {
                      $$ = p->new_yield($3);
                    }
                | keyword_yield '(' rparen
                    {
                      $$ = p->new_yield(0);
                    }
                | keyword_yield
                    {
                      $$ = p->new_yield(0);
                    }
                | keyword_not '(' expr rparen
                    {
                      $$ = p->call_uni_op(p->cond($3), "!");
                    }
                | keyword_not '(' rparen
                    {
                      $$ = p->call_uni_op(p->new_t<NilNode>(), "!");
                    }
                | operation brace_block
                    {
                      $$ = p->new_t<FCallNode>(p->new_t<SelfNode>(),$1,p->new_simple<CommandArgs>(nullptr, $2));
                    }
                | method_call
                | method_call brace_block
                    {
                      p->call_with_block($1, $2);
                      $$ = $1;
                    }
                | tLAMBDA
                    {
                      p->local_nest();
                      $<num>$ = p->paren_nest();
                    }
                  f_larglist
                  lambda_body
                    {
                      p->lpar_beg = $<num>2;
                      $$ = p->new_lambda($3, $4);
                      p->local_unnest();
                    }
                | keyword_if expr_value then
                  compstmt
                  if_tail
                  keyword_end
                    {
                      $$ = p->new_t<IfNode>(p->cond($2), $4, $5);
                    }
                | keyword_unless expr_value then
                  compstmt
                  opt_else
                  keyword_end
                    {
                      $$ = p->new_unless(p->cond($2), $4, $5);
                    }
                | keyword_while {COND_PUSH(1);} expr_value do {COND_POP();}
                    compstmt
                  keyword_end
                    {
                      $$ = p->new_t<WhileNode>(p->cond($3), $6);
                    }
                | keyword_until {COND_PUSH(1);} expr_value do {COND_POP();}
                    compstmt
                  keyword_end
                    {
                      $$ = p->new_t<UntilNode>(p->cond($3), $6);
                    }
                | keyword_case expr_value opt_terms
                    case_body
                  keyword_end
                    {
                      $$ = p->new_t<CaseNode>($2, $4);
                    }
                | keyword_case opt_terms case_body keyword_end
                    {
                      $$ = p->new_t<CaseNode>(nullptr, $3);
                    }
                | keyword_for for_var keyword_in
                  {COND_PUSH(1);}
                  expr_value do
                  {COND_POP();}
                  compstmt
                  keyword_end
                    {
                      $$ = p->new_t<ForNode>($2, $5, $8);
                    }
                | keyword_class
                    {
                      $<num>$ = p->m_lineno;
                    }
                  cpath superclass
                    {
                        if (p->in_def || p->in_single)
                            p->yyerror("class definition in method body");
                        $<locals_ctx>$ = p->local_switch();
                    }[ctx_idx]
                  bodystmt
                  keyword_end
                    {
                      $$ = p->new_t<ClassNode>($3, $4, p->new_scope($6));
                      SET_LINENO($$, $<num>2);
                      p->local_resume($<locals_ctx>ctx_idx);
                    }
                | keyword_class
                    {
                      $<num>$ = p->m_lineno;
                    }
                tLSHFT expr
                    {
                      $<num>$ = p->in_def;
                      p->in_def = 0;
                    }[in_def]
                  term
                    {
                      $<l_s>$ = { p->local_switch(), (intptr_t)p->in_single };
                      p->in_single = 0;
                    }[in_single]
                  bodystmt
                  keyword_end
                    {
                      $$ = p->new_t<SclassNode>($4, p->new_scope($8));
                      SET_LINENO($$, $<num>2);
                      p->local_resume($<l_s>[in_single].idx);
                      p->in_def = $<num>in_def;
                      p->in_single = $<l_s>[in_single].single;
                    }
                | keyword_module
                    {
                      $<num>$ = p->m_lineno;
                    }
                cpath
                    {
                      if (p->in_def || p->in_single)
                        p->yyerror("module definition in method body");
                      $<locals_ctx>$ = p->local_switch();
                    }[ctx_idx]
                  bodystmt
                  keyword_end
                    {
                      $$ = p->new_t<ModuleNode>($3, p->new_scope($5));
                      SET_LINENO($$, $<num>2);
                      p->local_resume($<locals_ctx>ctx_idx);
                    }
                | keyword_def fname
                    {
                      p->in_def++;
                      $<locals_ctx>$ = p->local_switch();
                    }
                  f_arglist
                  bodystmt
                  keyword_end
                    {
                      $$ = p->new_def($2, $4, $5);
                      p->local_resume($<locals_ctx>3);
                      p->in_def--;
                    }
                | keyword_def singleton dot_or_colon {p->m_lstate = EXPR_FNAME;} fname
                    {
                      p->in_single++;
                      p->m_lstate = EXPR_ENDFN; /* force for args */
                      $<locals_ctx>$ = p->local_switch();
                    }
                  f_arglist
                  bodystmt
                  keyword_end
                    {
                      $$ = p->new_sdef($2, $5, $7, $8);
                      p->local_resume($<locals_ctx>6);
                      p->in_single--;
                    }
                | keyword_break
                    {
                      $$ = p->new_t<BreakNode>(nullptr);
                    }
                | keyword_next
                    {
                      $$ = p->new_t<NextNode>(nullptr);
                    }
                | keyword_redo
                    {
                      $$ = p->new_t<RedoNode>();
                    }
                | keyword_retry
                    {
                      $$ = p->new_t<RetryNode>();
                    }
                ;

primary_value	: primary
                    {
                      $$ = $1;
                      if (!$$)
                        $$ = p->new_t<NilNode>();
                    }
                ;

then		: term
                | keyword_then
                | term keyword_then
                ;

do		: term
                | keyword_do_cond
                ;

if_tail		: opt_else
                | keyword_elsif expr_value then
                  compstmt
                  if_tail
                    {
                      $$ = p->new_t<IfNode>(p->cond($2), $4, $5);
                    }
                ;

opt_else	: none
                | keyword_else compstmt
                    {
                      $$ = $2;
                    }
                ;

for_var		: lhs
                    {
                      $$ = p->list1(p->list1($1));
                    }
                | mlhs
                ;

f_marg		: f_norm_arg
                    {
                      $$ = p->new_t<ArgNode>($1);
                    }
                | tLPAREN f_margs rparen
                    {
                      $$ = p->new_t<MAsgnNode>($2, nullptr);
                    }
                ;

f_marg_list	: f_marg
                    {
                      $$ = p->list1($1);
                    }
                | f_marg_list ',' f_marg
                    {
                      $$ = p->push($1, $3);
                    }
                ;

f_margs		: f_marg_list
                    {
                      $$ = p->list3($1,0,0);
                    }
                | f_marg_list ',' tSTAR f_norm_arg
                    {
                      $$ = p->list3($1, p->new_t<ArgNode>($4), 0);
                    }
                | f_marg_list ',' tSTAR f_norm_arg ',' f_marg_list
                    {
                      $$ = p->list3($1, p->new_t<ArgNode>($4), $6);
                    }
                | f_marg_list ',' tSTAR
                    {
                      $$ = p->list3($1, (node*)-1, 0);
                    }
                | f_marg_list ',' tSTAR ',' f_marg_list
                    {
                      $$ = p->list3($1, (node*)-1, $5);
                    }
                | tSTAR f_norm_arg
                    {
                      $$ = p->list3(0, p->new_t<ArgNode>($2), 0);
                    }
                | tSTAR f_norm_arg ',' f_marg_list
                    {
                      $$ = p->list3(0, p->new_t<ArgNode>($2), $4);
                    }
                | tSTAR
                    {
                      $$ = p->list3(0, (node*)-1, 0);
                    }
                | tSTAR ',' f_marg_list
                    {
                      $$ = p->list3(0, (node*)-1, $3);
                    }
                ;

block_param	: f_arg ',' f_block_optarg ',' f_rest_arg opt_f_block_arg
                    {
                      $$ = p->new_args($1, $3, $5, 0, $6);
                    }
                | f_arg ',' f_block_optarg ',' f_rest_arg ',' f_arg opt_f_block_arg
                    {
                      $$ = p->new_args($1, $3, $5, $7, $8);
                    }
                | f_arg ',' f_block_optarg opt_f_block_arg
                    {
                      $$ = p->new_args($1, $3, 0, 0, $4);
                    }
                | f_arg ',' f_block_optarg ',' f_arg opt_f_block_arg
                    {
                      $$ = p->new_args($1, $3, 0, $5, $6);
                    }
                | f_arg ',' f_rest_arg opt_f_block_arg
                    {
                      $$ = p->new_args($1, 0, $3, 0, $4);
                    }
                | f_arg ','
                    {
                      $$ = p->new_args($1, 0, 1, 0, 0);
                    }
                | f_arg ',' f_rest_arg ',' f_arg opt_f_block_arg
                    {
                      $$ = p->new_args($1, 0, $3, $5, $6);
                    }
                | f_arg opt_f_block_arg
                    {
                      $$ = p->new_args($1, 0, 0, 0, $2);
                    }
                | f_block_optarg ',' f_rest_arg opt_f_block_arg
                    {
                      $$ = p->new_args(0, $1, $3, 0, $4);
                    }
                | f_block_optarg ',' f_rest_arg ',' f_arg opt_f_block_arg
                    {
                      $$ = p->new_args(0, $1, $3, $5, $6);
                    }
                | f_block_optarg opt_f_block_arg
                    {
                      $$ = p->new_args(0, $1, 0, 0, $2);
                    }
                | f_block_optarg ',' f_arg opt_f_block_arg
                    {
                      $$ = p->new_args(0, $1, 0, $3, $4);
                    }
                | f_rest_arg opt_f_block_arg
                    {
                      $$ = p->new_args(0, 0, $1, 0, $2);
                    }
                | f_rest_arg ',' f_arg opt_f_block_arg
                    {
                      $$ = p->new_args(0, 0, $1, $3, $4);
                    }
                | f_block_arg
                    {
                      $$ = p->new_args(0, 0, 0, 0, $1);
                    }
                ;

opt_block_param	: none
                | block_param_def
                    {
                      p->m_cmd_start = true;
                      $$ = $1;
                    }
                ;

block_param_def	: '|' opt_bv_decl '|'
                    {
                      p->local_add_f(0);
                      $$ = 0;
                    }
                | tOROP
                    {
                      p->local_add_f(0);
                      $$ = 0;
                    }
                | '|' block_param opt_bv_decl '|'
                    {
                      $$ = $2;
                    }
                ;


opt_bv_decl	: opt_nl
                    {
                      $$ = 0;
                    }
                | opt_nl ';' bv_decls opt_nl
                    {
                      $$ = 0;
                    }
                ;

bv_decls	: bvar
                | bv_decls ',' bvar
                ;

bvar		: tIDENTIFIER
                    {
                      p->local_add_f($1);
                      p->new_bv($1);
                    }
                | f_bad_arg
                ;

f_larglist	: '(' f_args opt_bv_decl ')'
                    {
                      $$ = $2;
                    }
                | f_args
                    {
                      $$ = $1;
                    }
                ;

lambda_body	: tLAMBEG compstmt '}'
                    {
                      $$ = $2;
                    }
                | keyword_do_LAMBDA compstmt keyword_end
                    {
                      $$ = $2;
                    }
                ;

do_block	: keyword_do_block
                    {
                      p->local_nest();
                    }
                  opt_block_param
                  compstmt
                  keyword_end
                    {
                      $$ = p->new_block($3,$4);
                      p->local_unnest();
                    }
                ;

block_call	: command do_block
                    {
                      if ($1->getType() == NODE_YIELD) {
                        p->yyerror("block given to yield");
                      }
                      else {
                        p->call_with_block($1, $2);
                      }
                      $$ = $1;
                    }
                | block_call dot_or_colon operation2 opt_paren_args
                    {
                      $$ = p->new_t<CallNode>($1, $3, $4);
                    }
                | block_call dot_or_colon operation2 opt_paren_args brace_block
                    {
                      $$ = p->new_t<CallNode>($1, $3, $4);
                      p->call_with_block($$, $5);
                    }
                | block_call dot_or_colon operation2 command_args do_block
                    {
                      $$ = p->new_t<CallNode>($1, $3, $4);
                      p->call_with_block($$, $5);
                    }
                ;

method_call	: operation paren_args
                    {
                      $$ = p->new_t<FCallNode>(p->new_t<SelfNode>(),$1, $2);
                    }
                | primary_value '.' operation2 opt_paren_args
                    {
                      $$ = p->new_t<CallNode>($1, $3, $4);
                    }
                | primary_value tCOLON2 operation2 paren_args
                    {
                      $$ = p->new_t<CallNode>($1, $3, $4);
                    }
                | primary_value tCOLON2 operation3
                    {
                      $$ = p->new_t<CallNode>($1, $3, nullptr);
                    }
                | primary_value '.' paren_args
                    {
                      $$ = p->new_t<CallNode>($1,p->intern2("call",4), $3);
                    }
                | primary_value tCOLON2 paren_args
                    {
                      $$ = p->new_t<CallNode>($1,p->intern2("call",4), $3);
                    }
                | keyword_super paren_args
                    {
                      $$ = p->new_t<SuperNode>($2);
                    }
                | keyword_super
                    {
                      $$ = p->new_t<ZsuperNode>();
                    }
                | primary_value '[' opt_call_args rbracket
                    {
                      $$ = p->new_t<CallNode>($1,p->intern2("[]",2), $3);
                    }
                ;

brace_block	: '{'
                    {
                      p->local_nest();
                      $<num>$ = p->m_lineno;
                    }
                  opt_block_param
                  compstmt '}'
                    {
                      $$ = p->new_block($3,$4);
                      SET_LINENO($$, $<num>2);
                      p->local_unnest();
                    }
                | keyword_do
                    {
                      p->local_nest();
                      $<num>$ = p->m_lineno;
                    }
                  opt_block_param
                  compstmt keyword_end
                    {
                      $$ = p->new_block($3,$4);
                      SET_LINENO($$, $<num>2);
                      p->local_unnest();
                    }
                ;

case_body	: keyword_when args then
                  compstmt
                  cases
                    {
                      $$ =p->cons(p->cons($2, $4), $5);
                    }
                ;

cases		: opt_else
                    {
                      if ($1) {
                        $$ =p->cons(p->cons(0, $1), 0);
                      }
                      else {
                        $$ = 0;
                      }
                    }
                | case_body
                ;

opt_rescue	: keyword_rescue exc_list exc_var then
                  compstmt
                  opt_rescue
                    {
                      $$ = p->list1(p->list3($2, $3, $5));
                      if ($6) $$ = p->append($$, $6);
                    }
                | none
                ;

exc_list	: arg_value
                    {
                        $$ = p->list1($1);
                    }
                | mrhs
                | none
                ;

exc_var		: tASSOC lhs
                    {
                      $$ = $2;
                    }
                | none
                ;

opt_ensure	: keyword_ensure compstmt
                    {
                      $$ = $2;
                    }
                | none
                ;

literal		: numeric
                | symbol
                | words
                | symbols
                ;

string		: tCHAR
                | tSTRING
                | tSTRING_BEG tSTRING
                    {
                      $$ = $2;
                    }
                | tSTRING_BEG string_rep tSTRING
                    {
                      $$ = p->new_t<DstrNode>(p->push($2, $3));
                    }
                ;

string_rep      : string_interp
                | string_rep string_interp
                    {
                      $$ = p->append($1, $2);
                    }
                ;

string_interp	: tSTRING_MID { $$ = p->list1($1); }
                | tSTRING_PART
                    {
                      $<nd>$ = p->m_lex_strterm;
                      p->m_lex_strterm = nullptr;
                    }
                  compstmt '}'
                    {
                      p->m_lex_strterm = $<nd>2;
                      $$ = p->str_list2($1, $3);
                    }
                | tLITERAL_DELIM  { $$ = p->list1(p->new_t<LiteralDelimNode>()); }
                | tHD_LITERAL_DELIM heredoc_bodies { $$ = p->list1(p->new_t<LiteralDelimNode>()); }
                ;

xstring		: tXSTRING_BEG tXSTRING                 { $$ = $2; }
                | tXSTRING_BEG string_rep tXSTRING      { $$ = p->new_t<DxstrNode>(p->push($2, $3)); }
                ;

regexp		: tREGEXP_BEG tREGEXP                   { $$ = $2; }
                | tREGEXP_BEG string_rep tREGEXP        { $$ = p->new_t<DregxNode>($2, $3); }
                ;

heredoc		: tHEREDOC_BEG
                ;

opt_heredoc_bodies : /* none */
                   | heredoc_bodies
                   ;

heredoc_bodies	: heredoc_body
                | heredoc_bodies heredoc_body
                ;

heredoc_body	: tHEREDOC_END
                    {
                      auto  inf = p->parsing_heredoc_inf();
                      inf->doc = p->push(inf->doc, p->new_str("", 0));
                      p->heredoc_end();
                    }
                | heredoc_string_rep tHEREDOC_END
                    {
                      p->heredoc_end();
                    }
                ;

heredoc_string_rep : heredoc_string_interp
                   | heredoc_string_rep heredoc_string_interp
                   ;

heredoc_string_interp : tHD_STRING_MID
                    {
                      auto  inf = p->parsing_heredoc_inf();
                      inf->doc = p->push(inf->doc, $1);
                      p->heredoc_treat_nextline();
                    }
                | tHD_STRING_PART
                    {
                      $<nd>$ = p->m_lex_strterm;
                      p->m_lex_strterm = NULL;
                    }
                  compstmt
                  '}'
                    {
                      auto  inf = p->parsing_heredoc_inf();
                      p->m_lex_strterm = $<nd>2;
                      inf->doc = p->push(p->push(inf->doc, $1), $3);
                    }
                ;

words		: tWORDS_BEG tSTRING
                    {
                      $$ = p->new_t<WordsNode>(p->list1($2));
                    }
                | tWORDS_BEG string_rep tSTRING
                    {
                      $$ = p->new_t<WordsNode>(p->push($2, $3));
                    }
                ;


symbol		: basic_symbol
                    {
                      $$ = p->new_t<SymNode>($1);
                    }
                | tSYMBEG tSTRING_BEG string_interp tSTRING
                    {
                      p->m_lstate = EXPR_END;
                      $$ = p->new_dsym(p->push($3, $4));
                    }
                ;

basic_symbol	: tSYMBEG sym
                    {
                      p->m_lstate = EXPR_END;
                      $$ = $2;
                    }
                ;

sym		: fname
                | tIVAR
                | tGVAR
                | tCVAR
                | tSTRING { $$ = p->new_strsym($1); }
                | tSTRING_BEG tSTRING { $$ = p->new_strsym($2); }
                ;

symbols		: tSYMBOLS_BEG tSTRING
                    {
                      $$ = p->new_t<SymbolsNode>(p->list1($2));
                    }
                | tSYMBOLS_BEG string_rep tSTRING
                    {
                      $$ = p->new_t<SymbolsNode>(p->push($2, $3));
                    }
                ;

numeric 	: tINTEGER
                | tFLOAT
                | tUMINUS_NUM tINTEGER	       %prec tLOWEST
                    {
                      $$ = p->negate_lit($2);
                    }
                | tUMINUS_NUM tFLOAT	       %prec tLOWEST
                    {
                      $$ = p->negate_lit($2);
                    }
                ;

variable	: tIDENTIFIER
                    {
                      $$ = p->new_t<LVarNode>($1);
                    }
                | tIVAR
                    {
                      $$ = p->new_t<IVarNode>($1);
                    }
                | tGVAR
                    {
                      $$ = p->new_t<GVarNode>($1);
                    }
                | tCVAR
                    {
                      $$ = p->new_t<CVarNode>($1);
                    }
                | tCONSTANT
                    {
                      $$ = p->new_t<ConstNode>($1);
                    }
                ;

var_lhs		: variable
                    {
                      p->assignable($1);
                    }
                ;

var_ref		: variable
                    {
                      $$ = p->var_reference($1);
                    }
                | keyword_nil
                    {
                      $$ = p->new_t<NilNode>();
                    }
                | keyword_self
                    {
                      $$ = p->new_t<SelfNode>();
                    }
                | keyword_true
                    {
                      $$ = p->new_t<TrueNode>();
                    }
                | keyword_false
                    {
                      $$ = p->new_t<FalseNode>();
                    }
                | keyword__FILE__
                    {
                      if (!p->m_filename) {
                        p->m_filename = "(null)";
                      }
                      $$ = p->new_str(p->m_filename, strlen(p->m_filename));
                    }
                | keyword__LINE__
                    {
                      char buf[16];

                      snprintf(buf, sizeof(buf), "%d", p->m_lineno);
                      $$ = p->new_t<IntLiteralNode>(buf, 10);
                    }
                ;

backref		: tNTH_REF
                | tBACK_REF
                ;

superclass	: term
                    {
                      $$ = 0;
                    }
                | '<'
                    {
                      p->m_lstate = EXPR_BEG;
                      p->m_cmd_start = true;
                    }
                  expr_value term
                    {
                      $$ = $3;
                    }
                | error term
                    {
                      yyerrok;
                      $$ = 0;
                    }
                ;

f_arglist	: '(' f_args rparen
                    {
                      $$ = $2;
                      p->m_lstate = EXPR_BEG;
                      p->m_cmd_start = true;
                    }
                | f_args term
                    {
                      $$ = $1;
                    }
                ;

f_args		: f_arg ',' f_optarg ',' f_rest_arg opt_f_block_arg
                    {
                      $$ = p->new_args($1, $3, $5, 0, $6);
                    }
                | f_arg ',' f_optarg ',' f_rest_arg ',' f_arg opt_f_block_arg
                    {
                      $$ = p->new_args($1, $3, $5, $7, $8);
                    }
                | f_arg ',' f_optarg opt_f_block_arg
                    {
                      $$ = p->new_args($1, $3, 0, 0, $4);
                    }
                | f_arg ',' f_optarg ',' f_arg opt_f_block_arg
                    {
                      $$ = p->new_args($1, $3, 0, $5, $6);
                    }
                | f_arg ',' f_rest_arg opt_f_block_arg
                    {
                      $$ = p->new_args($1, 0, $3, 0, $4);
                    }
                | f_arg ',' f_rest_arg ',' f_arg opt_f_block_arg
                    {
                      $$ = p->new_args($1, 0, $3, $5, $6);
                    }
                | f_arg opt_f_block_arg
                    {
                      $$ = p->new_args($1, 0, 0, 0, $2);
                    }
                | f_optarg ',' f_rest_arg opt_f_block_arg
                    {
                      $$ = p->new_args(0, $1, $3, 0, $4);
                    }
                | f_optarg ',' f_rest_arg ',' f_arg opt_f_block_arg
                    {
                      $$ = p->new_args(0, $1, $3, $5, $6);
                    }
                | f_optarg opt_f_block_arg
                    {
                      $$ = p->new_args(0, $1, 0, 0, $2);
                    }
                | f_optarg ',' f_arg opt_f_block_arg
                    {
                      $$ = p->new_args(0, $1, 0, $3, $4);
                    }
                | f_rest_arg opt_f_block_arg
                    {
                      $$ = p->new_args(0, 0, $1, 0, $2);
                    }
                | f_rest_arg ',' f_arg opt_f_block_arg
                    {
                      $$ = p->new_args(0, 0, $1, $3, $4);
                    }
                | f_block_arg
                    {
                      $$ = p->new_args(0, 0, 0, 0, $1);
                    }
                | /* none */
                    {
                      p->local_add_f(0);
                      $$ = p->new_args(0, 0, 0, 0, 0);
                    }
                ;

f_bad_arg	: tCONSTANT
                    {
                      p->yyerror("formal argument cannot be a constant");
                      $$ = 0;
                    }
                | tIVAR
                    {
                      p->yyerror("formal argument cannot be an instance variable");
                      $$ = 0;
                    }
                | tGVAR
                    {
                      p->yyerror("formal argument cannot be a global variable");
                      $$ = 0;
                    }
                | tCVAR
                    {
                      p->yyerror("formal argument cannot be a class variable");
                      $$ = 0;
                    }
                ;

f_norm_arg	: f_bad_arg
                    {
                      $$ = 0;
                    }
                | tIDENTIFIER
                    {
                      p->local_add_f($1);
                      $$ = $1;
                    }
                ;

f_arg_item	: f_norm_arg
                    {
                      $$ = p->new_t<ArgNode>($1);
                    }
                | tLPAREN f_margs rparen
                    {
                      $$ = p->new_t<MAsgnNode>($2, nullptr);
                    }
                ;

f_arg		: f_arg_item
                    {
                      $$ = p->list1($1);
                    }
                | f_arg ',' f_arg_item
                    {
                      $$ = p->push($1, $3);
                    }
                ;

f_opt		: tIDENTIFIER '=' arg_value
                    {
                      p->local_add_f($1);
                      $$ =p->cons(nsym($1), $3);
                    }
                ;

f_block_opt	: tIDENTIFIER '=' primary_value
                    {
                      p->local_add_f($1);
                      $$ =p->cons(nsym($1), $3);
                    }
                ;

f_block_optarg	: f_block_opt
                    {
                      $$ = p->list1($1);
                    }
                | f_block_optarg ',' f_block_opt
                    {
                      $$ = p->push($1, $3);
                    }
                ;

f_optarg	: f_opt
                    {
                      $$ = p->list1($1);
                    }
                | f_optarg ',' f_opt
                    {
                      $$ = p->push($1, $3);
                    }
                ;

restarg_mark	: '*'
                | tSTAR
                ;

f_rest_arg	: restarg_mark tIDENTIFIER
                    {
                      p->local_add_f($2);
                      $$ = $2;
                    }
                | restarg_mark
                    {
                      p->local_add_f(0);
                      $$ = -1;
                    }
                ;

blkarg_mark	: '&'
                | tAMPER
                ;

f_block_arg	: blkarg_mark tIDENTIFIER
                    {
                      p->local_add_f($2);
                      $$ = $2;
                    }
                ;

opt_f_block_arg	: ',' f_block_arg
                    {
                      $$ = $2;
                    }
                | none
                    {
                      p->local_add_f(0);
                      $$ = 0;
                    }
                ;

singleton	: var_ref
                    {
                      $$ = $1;
                      if (!$$) $$ = p->new_t<NilNode>();
                    }
                | '(' {p->m_lstate = EXPR_BEG;} expr rparen
                    {
                      if ($3 == 0) {
                        p->yyerror("can't define singleton method for ().");
                      }
                      else {
                        switch ($3->getType()) {
                        case NODE_STR:
                        case NODE_DSTR:
                        case NODE_XSTR:
                        case NODE_DXSTR:
                        case NODE_DREGX:
                        case NODE_MATCH:
                        case NODE_FLOAT:
                        case NODE_ARRAY:
                        case NODE_HEREDOC:
                          p->yyerror("can't define singleton method for literals");
                        default:
                          break;
                        }
                      }
                      $$ = $3;
                    }
                ;

assoc_list	: none
                | assocs trailer
                    {
                      $$ = $1;
                    }
                ;

assocs		: assoc
                    {
                      $$ = p->list1($1);
                    }
                | assocs ',' assoc
                    {
                      $$ = p->push($1, $3);
                    }
                ;

assoc		: arg_value tASSOC arg_value
                    {
                      $$ =p->cons($1, $3);
                    }
                | tLABEL arg_value
                    {
                      $$ =p->cons(p->new_t<SymNode>($1), $2);
                    }
                ;

operation	: tIDENTIFIER
                | tCONSTANT
                | tFID
                ;

operation2	: tIDENTIFIER
                | tCONSTANT
                | tFID
                | op
                ;

operation3	: tIDENTIFIER
                | tFID
                | op
                ;

dot_or_colon	: '.'
                | tCOLON2
                ;

opt_terms	: /* none */
                | terms
                ;

opt_nl		: /* none */
                | nl
                ;

rparen		: opt_nl ')'
                ;

rbracket	: opt_nl ']'
                ;

trailer		: /* none */
                | nl
                | ','
                ;

term		: ';' {yyerrok;}
                | nl
                ;

nl		: '\n'
                    {
                      p->m_lineno++;
                      p->m_column = 0;
                    }
                  opt_heredoc_bodies

terms		: term
                | terms ';' {yyerrok;}
                ;

none		: /* none */
                    {
                      $$ = 0;
                    }
                ;
%%
#define yylval  (*((YYSTYPE*)(p->ylval)))
static void
yyerror(parser_state *p, const char *s)
{
    p->yyerror(s);
}
static int
yylex(void *lval, parser_state *p)
{
  int t;

  p->ylval = lval;
  t = p->parser_yylex();

  return t;
}


