#include <cerrno>
#include <new>
#include <ctype.h>
#include <cstring>
#include <cstdlib>

#include "mruby/compile.h"
#include "mruby.h"
#include "mruby/proc.h"
#include "mruby/node.h"
static mrb_sym sym(mrb_ast_node *x) { return ((mrb_sym)(intptr_t)(x));}
//#define sym(x)
#ifdef ENABLE_STDIO
void parser_dump(mrb_state *mrb, mrb_ast_node *tree, int offset);

static void dump_prefix(int offset)
{
    while (offset--) {
        puts("  ");
    }
}

static void dump_recur(mrb_state *mrb, mrb_ast_node *tree, int offset)
{
    while (tree) {
        parser_dump(mrb, tree->left(), offset);
        tree = tree->right();
    }
}

#endif
void parser_dump(mrb_state *mrb, ForNode *fn, int offset) {
    printf("var:\n");
    {
        mrb_ast_node *n2 = fn->var();

        if (n2->left()) {
            dump_prefix(offset+2);
            printf("pre:\n");
            dump_recur(mrb, n2->left(), offset+3);
        }
        n2 = n2->right();
        if (n2) {
            if (n2->left()) {
                dump_prefix(offset+2);
                printf("rest:\n");
                parser_dump(mrb, n2->left(), offset+3);
            }
            n2 = n2->right();
            if (n2) {
                if (n2->left()) {
                    dump_prefix(offset+2);
                    printf("post:\n");
                    dump_recur(mrb, n2->left(), offset+3);
                }
            }
        }
    }
    dump_prefix(offset+1);
    printf("in:\n");
    parser_dump(mrb, fn->object(), offset+2);
    dump_prefix(offset+1);
    printf("do:\n");
    parser_dump(mrb, fn->body(), offset+2);
}
void parser_dump(mrb_state *mrb, RescueNode *rn, int offset) {
    printf("NODE_RESCUE:\n");
    if (rn->body()) {
        dump_prefix(offset+1);
        printf("body:\n");
        parser_dump(mrb, rn->body(), offset+2);
    }
    if (rn->rescue()) {
        mrb_ast_node *n2 = rn->rescue();

        dump_prefix(offset+1);
        printf("rescue:\n");
        while (n2) {
            mrb_ast_node *n3 = n2->left();
            if (n3->left()) {
                dump_prefix(offset+2);
                printf("handle classes:\n");
                dump_recur(mrb, n3->left(), offset+3);
            }
            if (n3->right()->left()) {
                dump_prefix(offset+2);
                printf("exc_var:\n");
                parser_dump(mrb, n3->right()->left(), offset+3);
            }
            if (n3->right()->right()->left()) {
                dump_prefix(offset+2);
                printf("rescue body:\n");
                parser_dump(mrb, n3->right()->right()->left(), offset+3);
            }
            n2 = n2->right();
        }
    }
    if (rn->r_else()) {
        dump_prefix(offset+1);
        printf("else:\n");
        parser_dump(mrb, rn->r_else(), offset+2);
    }
}
void args_dump(ArgsStore *n, int offset, mrb_state *mrb)
{
    if(n==nullptr)
        return;
    if (n->m_mandatory) {
        dump_prefix(offset+1);
        printf("mandatory args:\n");
        dump_recur(mrb, n->m_mandatory, offset+2);
    }
    if (n->m_opt) {
        dump_prefix(offset+1);
        printf("optional args:\n");
        {
            mrb_ast_node *n2 = n->m_opt;

            while (n2) {
                dump_prefix(offset+2);
                printf("%s=", mrb_sym2name(mrb, sym(n2->left()->left())));
                parser_dump(mrb, n2->left()->right(), 0);
                n2 = n2->right();
            }
        }
    }
    if (n->m_rest) {
        dump_prefix(offset+1);
        printf("rest=*%s\n", mrb_sym2name(mrb, n->m_rest));
    }
    if (n->m_post_mandatory) {
        dump_prefix(offset+1);
        printf("post mandatory args:\n");
        dump_recur(mrb, n->m_post_mandatory, offset+2);
    }
    if (n->m_blk) {
        dump_prefix(offset+1);
        printf("blk=&%s\n", mrb_sym2name(mrb, n->m_blk));
    }
}

void parser_dump(mrb_state *mrb, LambdaCommonNode *ln, int offset) {
    args_dump(ln->args(), offset, mrb);
    dump_prefix(offset+1);
    printf("body:\n");
    parser_dump(mrb, ln->body(), offset+2);

}
void parser_dump(mrb_state *mrb, DefCommonNode *dn, int offset) {
    printf("%s\n", mrb_sym2name(mrb, dn->name()));
    {
        const tLocals &n2(dn->ve_locals());
        if(!n2.empty()) {
            dump_prefix(offset+1);
            printf("local variables:\n");
            dump_prefix(offset+2);
            for(auto iter=n2.begin(); iter!=n2.end(); ++iter) {
                if ( (iter+1) != n2.end() )
                    printf(", ");
                printf("%s", mrb_sym2name(mrb, *iter));
            }
            printf("\n");
        }
    }
    args_dump(dn->args(), offset, mrb);
    parser_dump(mrb, dn->body(), offset+1);
}
void parser_dump(mrb_state *mrb, EnsureNode *dn, int offset) {
    printf("NODE_ENSURE:\n");
    dump_prefix(offset+1);
    printf("body:\n");
    parser_dump(mrb, dn->body(), offset+2);
    dump_prefix(offset+1);
    printf("ensure:\n");
    parser_dump(mrb, dn->ensure()->body(), offset+2);
}
void parser_dump(mrb_state *mrb, mrb_ast_node *tree, int offset)
{
#ifdef ENABLE_STDIO
    if (!tree) return;
again:
    dump_prefix(offset);
    node_type n = tree->getType();
    mrb_ast_node *orig = tree;
    if(dynamic_cast<UpdatedNode *>(tree)==0)
        tree = tree->right();
    switch (n) {
        case NODE_BEGIN:
            printf("NODE_BEGIN:\n");
            dump_recur(mrb, tree, offset+1);
            break;

        case NODE_RESCUE: parser_dump(mrb,(RescueNode *)orig,offset); break;

        case NODE_ENSURE: parser_dump(mrb,(EnsureNode *)orig,offset); break;

        case NODE_LAMBDA:
            printf("NODE_LAMBDA:\n");
            goto block;

        case NODE_BLOCK:
            printf("NODE_BLOCK:\n");
block:
            parser_dump(mrb,(LambdaCommonNode *)orig,offset);
            break;

        case NODE_IF:
        {
            IfNode *in = (IfNode *)orig;
            printf("NODE_IF:\n");
            dump_prefix(offset+1);
            printf("cond:\n");
            parser_dump(mrb, in->cond(), offset+2); //tree->left()
            dump_prefix(offset+1);
            printf("then:\n");
            parser_dump(mrb, in->true_body(), offset+2); //tree->right()->left()
            if (in->false_body()) {
                dump_prefix(offset+1);
                printf("else:\n");
                parser_dump(mrb, in->false_body() , offset+2); //tree->right()->right()->left()
            }
        }
            break;

        case NODE_AND:
        {
            AndNode *an = (AndNode *)orig;
            printf("NODE_AND:\n");
            parser_dump(mrb, an->lhs(), offset+1);
            parser_dump(mrb, an->rhs(), offset+1);
        }
            break;

        case NODE_OR:
        {
            OrNode *an = (OrNode *)orig;
            printf("NODE_OR:\n");
            parser_dump(mrb, an->lhs(), offset+1);
            parser_dump(mrb, an->rhs(), offset+1);
        }
            break;

        case NODE_CASE:  {
            CaseNode *cn = (CaseNode *)orig;
            printf("NODE_CASE:\n");
            if (cn->switched_on()) {
                parser_dump(mrb, cn->switched_on(), offset+1);
            }
            tree = cn->cases();
            while (tree) {
                dump_prefix(offset+1);
                printf("case:\n");
                dump_recur(mrb, tree->left()->left(), offset+2);
                dump_prefix(offset+1);
                printf("body:\n");
                parser_dump(mrb, tree->left()->right(), offset+2);
                tree = tree->right();
            }
        }
            break;

        case NODE_WHILE:
        {
            WhileNode *n = (WhileNode *)orig;
            printf("NODE_WHILE:\n");
            dump_prefix(offset+1);
            printf("cond:\n");
            parser_dump(mrb, n->lhs(), offset+2);
            dump_prefix(offset+1);
            printf("body:\n");
            parser_dump(mrb, n->right(), offset+2);
        }
            break;

        case NODE_UNTIL:
        {
            UntilNode *n = (UntilNode *)orig;
            printf("NODE_UNTIL:\n");
            dump_prefix(offset+1);
            printf("cond:\n");
            parser_dump(mrb, n->lhs(), offset+2);
            dump_prefix(offset+1);
            printf("body:\n");
            parser_dump(mrb, n->rhs(), offset+2);
        }
            break;

        case NODE_FOR:
            printf("NODE_FOR:\n");
            dump_prefix(offset+1);
            parser_dump(mrb,(ForNode *)orig,offset);
            break;

        case NODE_SCOPE:
            printf("NODE_SCOPE:\n");
        {
            ScopeNode *ns = (ScopeNode *)orig;
            const tLocals &n2(ns->locals());

            if ( !n2.empty() ) {
                dump_prefix(offset+1);
                printf("local variables:\n");
                dump_prefix(offset+2);
                for(auto iter=n2.begin(); iter!=n2.end(); ++iter) {
                    if ( (iter+1) != n2.end() )
                        printf(", ");
                    printf("%s", mrb_sym2name(mrb, *iter));
                }
                printf("\n");
            }
            tree = ns->body();
        }
            offset++;
            goto again;

        case NODE_FCALL:
        case NODE_CALL:
        {
            CallCommonNode *cn = (CallCommonNode *)orig;
            printf("NODE_CALL:\n");
            parser_dump(mrb, cn->m_receiver, offset+1);
            dump_prefix(offset+1);
            printf("method='%s' (%d)\n",
                   mrb_sym2name(mrb, cn->m_method),
                   cn->m_method);
            CommandArgs *ca = cn->m_cmd_args;
            if (ca) {
                dump_prefix(offset+1);
                printf("args:\n");
                dump_recur(mrb, ca->m_args, offset+2);
                if (tree->right()) {
                    dump_prefix(offset+1);
                    printf("block:\n");
                    parser_dump(mrb, ca->m_blk, offset+2);
                }
            }
        }
            break;

        case NODE_DOT2:
        {
            printf("NODE_DOT2:\n");
            Dot2Node *nd = (Dot2Node *)orig;
            parser_dump(mrb, nd->lhs(), offset+1);
            parser_dump(mrb, nd->rhs(), offset+1);
        }
            break;

        case NODE_DOT3:
        {
            printf("NODE_DOT3:\n");
            Dot3Node *nd = (Dot3Node *)orig;
            parser_dump(mrb, nd->lhs(), offset+1);
            parser_dump(mrb, nd->rhs(), offset+1);
        }
            break;

        case NODE_COLON2:
        {
            Colon2Node *cn = (Colon2Node *)orig;
            printf("NODE_COLON2:\n");
            parser_dump(mrb, cn->m_val, offset+1);
            dump_prefix(offset+1);
            printf("::%s\n", mrb_sym2name(mrb, cn->m_sym));
        }
            break;

        case NODE_COLON3:{
            printf("NODE_COLON3:\n");
            dump_prefix(offset+1);
            Colon3Node *n = (Colon3Node *)orig;
            printf("::%s\n", mrb_sym2name(mrb, n->sym()));
        }
            break;

        case NODE_ARRAY:
            printf("NODE_ARRAY:\n");
            dump_recur(mrb, ((ArrayNode *)orig)->child(), offset+1);
            break;

        case NODE_HASH:
        {
            HashNode *nd = (HashNode *)orig;
            printf("NODE_HASH:\n");
            tree = nd->child();
            while (tree) {
                dump_prefix(offset+1);
                printf("key:\n");
                parser_dump(mrb, tree->left()->left(), offset+2);
                dump_prefix(offset+1);
                printf("value:\n");
                parser_dump(mrb, tree->left()->right(), offset+2);
                tree = tree->right();
            }
        }
            break;

        case NODE_SPLAT:
            printf("NODE_SPLAT:\n");
            parser_dump(mrb, ((SplatNode *)orig)->child(), offset+1);
            break;

        case NODE_ASGN:
        {
            AsgnNode *an = (AsgnNode *)orig;
            printf("NODE_ASGN:\n");
            dump_prefix(offset+1);
            printf("lhs:\n");
            parser_dump(mrb, an->lhs(), offset+2);
            dump_prefix(offset+1);
            printf("rhs:\n");
            parser_dump(mrb, an->rhs(), offset+2);
        }
            break;

        case NODE_MASGN:
        {
            printf("NODE_MASGN:\n");
            dump_prefix(offset+1);
            printf("mlhs:\n");
            MAsgnNode *mn = (MAsgnNode *)orig;
            mrb_ast_node *n2 = mn->lhs();

            if (n2->left()) {
                dump_prefix(offset+2);
                printf("pre:\n");
                dump_recur(mrb, n2->left(), offset+3);
            }
            n2 = n2->right();
            if (n2) {
                if (n2->left()) {
                    dump_prefix(offset+2);
                    printf("rest:\n");
                    if (n2->left() == (mrb_ast_node*)-1) {
                        dump_prefix(offset+2);
                        printf("(empty)\n");
                    }
                    else {
                        parser_dump(mrb, n2->left(), offset+3);
                    }
                }
                n2 = n2->right();
                if (n2) {
                    if (n2->left()) {
                        dump_prefix(offset+2);
                        printf("post:\n");
                        dump_recur(mrb, n2->left(), offset+3);
                    }
                }
            }
            dump_prefix(offset+1);
            printf("rhs:\n");
            parser_dump(mrb, mn->rhs(), offset+2);
        }
            break;

        case NODE_OP_ASGN:
            printf("NODE_OP_ASGN:\n");
            dump_prefix(offset+1);
            printf("lhs:\n");
            parser_dump(mrb, tree->left(), offset+2);
            tree = tree->right();
            dump_prefix(offset+1);
            printf("op='%s' (%d)\n", mrb_sym2name(mrb, sym(tree->left())), (int)(intptr_t)tree->left());
            tree = tree->right();
            parser_dump(mrb, tree->left(), offset+1);
            break;

        case NODE_SUPER:
        {
            printf("NODE_SUPER:\n");
            SuperNode *x=(SuperNode *)orig;
            if (x->hasParams()) {
                dump_prefix(offset+1);
                printf("args:\n");
                dump_recur(mrb, x->args(), offset+2);
                if (x->block()) {
                    dump_prefix(offset+1);
                    printf("block:\n");
                    parser_dump(mrb, x->block(), offset+2);
                }
            }
        }
            break;

        case NODE_ZSUPER:
            printf("NODE_ZSUPER\n");
            break;

        case NODE_RETURN:
            printf("NODE_RETURN:\n");
            parser_dump(mrb, ((ReturnNode *)orig)->child(), offset+1);
            break;

        case NODE_YIELD:
            printf("NODE_YIELD:\n");
            dump_recur(mrb, ((YieldNode *)orig)->child(), offset+1);
            break;

        case NODE_BREAK:
            printf("NODE_BREAK:\n");
            parser_dump(mrb, ((BreakNode *)orig)->child(), offset+1);
            //parser_dump(mrb, tree, offset+1);
            break;

        case NODE_NEXT:
            printf("NODE_NEXT:\n");
            parser_dump(mrb, ((NextNode *)orig)->child(), offset+1);
            break;

        case NODE_REDO:
            printf("NODE_REDO\n");
            break;

        case NODE_RETRY:
            printf("NODE_RETRY\n");
            break;

        case NODE_LVAR:
        {
            LVarNode * lvar = (LVarNode *)orig;
            printf("NODE_LVAR %s\n", mrb_sym2name(mrb, lvar->sym()));
        }
            break;

        case NODE_GVAR:
        {
            GVarNode * gvar = (GVarNode *)orig;
            printf("NODE_GVAR %s\n", mrb_sym2name(mrb, gvar->sym()));
        }
            break;

        case NODE_IVAR:
        {
            IVarNode * ivar = (IVarNode *)orig;
            printf("NODE_IVAR %s\n", mrb_sym2name(mrb, ivar->sym()));
        }
            break;

        case NODE_CVAR:
        {
            CVarNode * cvar = (CVarNode *)orig;
            printf("NODE_CVAR %s\n", mrb_sym2name(mrb, cvar->sym()));
        }
            break;

        case NODE_CONST:
        {
            ConstNode * cvar = (ConstNode *)orig;
            printf("NODE_CONST %s\n", mrb_sym2name(mrb, cvar->sym()));
        }

            break;

        case NODE_MATCH:
            printf("NODE_MATCH:\n");
            dump_prefix(offset + 1);
            printf("lhs:\n");
            parser_dump(mrb, tree->left(), offset + 2);
            dump_prefix(offset + 1);
            printf("rhs:\n");
            parser_dump(mrb, tree->right(), offset + 2);
            break;

        case NODE_BACK_REF:
            printf("NODE_BACK_REF: $%c\n", ((BackRefNode *)orig)->m_ref);
            break;

        case NODE_NTH_REF:
            printf("NODE_NTH_REF: $%d\n", ((NthRefNode *)orig)->m_ref);
            break;

        case NODE_ARG:
        {
            ArgNode * var = (ArgNode *)orig;
            printf("NODE_ARG %s\n", mrb_sym2name(mrb, var->sym()));
        }
            break;

        case NODE_BLOCK_ARG:
        {

            printf("NODE_BLOCK_ARG:\n");
            parser_dump(mrb, ((BlockArgNode *)orig)->child(), offset+1);
        }
            break;

        case NODE_INT:
        {
            IntLiteralNode *int_lit = (IntLiteralNode *)orig;
            printf("NODE_INT %s base %d\n", int_lit->m_val, int_lit->m_base);
        }
            break;

        case NODE_FLOAT:
            printf("NODE_FLOAT %s\n", ((FloatLiteralNode *)orig)->m_val);
            break;

        case NODE_NEGATE:
            printf("NODE_NEGATE\n");
            parser_dump(mrb, tree, offset+1);
            break;

        case NODE_STR:
        {
            StrNode *sn=(StrNode *)orig;
            printf("NODE_STR \"%s\" len %d\n", sn->m_str, sn->m_length);
        }
            break;

        case NODE_DSTR:
        {
            DstrNode *dn = (DstrNode *)orig;
            printf("NODE_DSTR\n");
            dump_recur(mrb, dn->child(), offset+1);
        }
            break;

        case NODE_XSTR:
            printf("NODE_XSTR \"%s\" len %d\n", (char*)tree->left(), (int)(intptr_t)tree->right());
            break;

        case NODE_DXSTR: {
            DxstrNode *dn = (DxstrNode *)orig;
            printf("NODE_DXSTR\n");
            dump_recur(mrb, dn->child(), offset+1);
        }
            break;

        case NODE_REGX:
            printf("NODE_REGX /%s/%s\n", (char*)tree->left(), (char*)tree->right());
            break;

        case NODE_DREGX:
            printf("NODE_DREGX\n");
            dump_recur(mrb, tree->left(), offset+1);
            dump_prefix(offset);
            printf("tail: %s\n", (char*)tree->right()->right()->left());
            dump_prefix(offset);
            printf("opt: %s\n", (char*)tree->right()->right()->right());
            break;

        case NODE_SYM:
        {
            SymNode * cvar = (SymNode *)orig;
            printf("NODE_SYM :%s\n", mrb_sym2name(mrb, cvar->sym()));
        }
            break;

        case NODE_SELF:
            printf("NODE_SELF\n");
            break;

        case NODE_NIL:
            printf("NODE_NIL\n");
            break;

        case NODE_TRUE:
            printf("NODE_TRUE\n");
            break;

        case NODE_FALSE:
            printf("NODE_FALSE\n");
            break;

        case NODE_ALIAS:
        {
            AliasNode *an = (AliasNode *)orig;
            printf("NODE_ALIAS %s %s:\n",
                   mrb_sym2name(mrb, an->m_from),
                   mrb_sym2name(mrb, an->m_to));
        }
            break;

        case NODE_UNDEF:
            printf("NODE_UNDEF");
        {
            mrb_ast_node *t = tree;
            while (t) {
                printf(" %s", mrb_sym2name(mrb, sym(t->left())));
                t = t->right();
            }
        }
            printf(":\n");
            break;

        case NODE_CLASS:
            printf("NODE_CLASS:\n");
            if (tree->left()->left() == (mrb_ast_node*)0) {
                dump_prefix(offset+1);
                printf(":%s\n", mrb_sym2name(mrb, sym(tree->left()->right())));
            }
            else if (tree->left()->left() == (mrb_ast_node*)1) {
                dump_prefix(offset+1);
                printf("::%s\n", mrb_sym2name(mrb, sym(tree->left()->right())));
            }
            else {
                parser_dump(mrb, tree->left()->left(), offset+1);
                dump_prefix(offset+1);
                printf("::%s\n", mrb_sym2name(mrb, sym(tree->left()->right())));
            }
            if (tree->right()->left()) {
                dump_prefix(offset+1);
                printf("super:\n");
                parser_dump(mrb, tree->right()->left(), offset+2);
            }
            dump_prefix(offset+1);
            printf("body:\n");
            parser_dump(mrb, tree->right()->right()->left()->right(), offset+2);
            break;

        case NODE_MODULE:
            printf("NODE_MODULE:\n");
            if (tree->left()->left() == (mrb_ast_node*)0) {
                dump_prefix(offset+1);
                printf(":%s\n", mrb_sym2name(mrb, sym(tree->left()->right())));
            }
            else if (tree->left()->left() == (mrb_ast_node*)1) {
                dump_prefix(offset+1);
                printf("::%s\n", mrb_sym2name(mrb, sym(tree->left()->right())));
            }
            else {
                parser_dump(mrb, tree->left()->left(), offset+1);
                dump_prefix(offset+1);
                printf("::%s\n", mrb_sym2name(mrb, sym(tree->left()->right())));
            }
            dump_prefix(offset+1);
            printf("body:\n");
            parser_dump(mrb, tree->right()->left()->right(), offset+2);
            break;

        case NODE_SCLASS:
            printf("NODE_SCLASS:\n");
            parser_dump(mrb, tree->left(), offset+1);
            dump_prefix(offset+1);
            printf("body:\n");
            parser_dump(mrb, tree->right()->left()->right(), offset+2);
            break;

        case NODE_DEF:
        {
            DefNode *dn = (DefNode *)orig;
            printf("NODE_DEF:\n");
            dump_prefix(offset+1);
            parser_dump(mrb,dn,offset);
        }
            break;

        case NODE_SDEF:
        {
            SdefNode *sn = (SdefNode *)orig;
            printf("NODE_SDEF:\n");
            parser_dump(mrb, sn->receiver(), offset+1);
            dump_prefix(offset+1);
            printf(":"); // prepend name with ':'
            parser_dump(mrb,sn,offset);
        }
            break;

        case NODE_POSTEXE:
            printf("NODE_POSTEXE:\n");
            parser_dump(mrb, ((PostExeNode *)orig)->child(), offset+1);
            break;

        case NODE_HEREDOC:
            printf("NODE_HEREDOC:\n");
            parser_dump(mrb, ((struct mrb_parser_heredoc_info*)tree)->doc, offset+1);
            break;

        default:
            printf("node type: %d (0x%x)\n", (int)n, (int)n);
            break;
    }
#endif
}
static mrb_value
load_exec(mrb_state *mrb, mrb_parser_state *p, mrbc_context *c)
{
    int n;
    mrb_value v;

    if (!p) {
        return mrb_undef_value();
    }
    if (!p->m_tree || p->nerr) {
        if (p->m_capture_errors) {
            char buf[256];

            n = snprintf(buf, sizeof(buf), "line %d: %s\n",
                         p->error_buffer[0].lineno, p->error_buffer[0].message);
            mrb->m_exc = mrb_obj_ptr(mrb_exc_new(mrb, E_SYNTAX_ERROR, buf, n));
            mrb_parser_free(p);
            return mrb_undef_value();
        }
        else {
            static const char msg[] = "syntax error";
            mrb->m_exc = mrb_obj_ptr(mrb_exc_new(mrb, E_SYNTAX_ERROR, msg, sizeof(msg) - 1));
            mrb_parser_free(p);
            return mrb_undef_value();
        }
    }
    n = mrb_generate_code(mrb, p);
    mrb_parser_free(p);
    if (n < 0) {
        static const char msg[] = "codegen error";
        mrb->m_exc = mrb_obj_ptr(mrb_exc_new(mrb, E_SCRIPT_ERROR, msg, sizeof(msg) - 1));
        return mrb_nil_value();
    }
    if (c) {
        if (c->dump_result) mrb->codedump_all(n);
        if (c->no_exec) return mrb_fixnum_value(n);
    }
    v = mrb->mrb_run(mrb_proc_new(mrb, mrb->m_irep[n]), mrb_top_self(mrb));
    if (mrb->m_exc) return mrb_nil_value();
    return v;
}

#ifdef ENABLE_STDIO
mrb_value
mrb_load_file_cxt(mrb_state *mrb, FILE *f, mrbc_context *c)
{
    return load_exec(mrb, mrb_parse_file(mrb, f, c), c);
}

mrb_value
mrb_load_file(mrb_state *mrb, FILE *f)
{
    return mrb_load_file_cxt(mrb, f, NULL);
}
#endif

mrb_value
mrb_load_nstring_cxt(mrb_state *mrb, const std::string & s, mrbc_context *c)
{
    return load_exec(mrb, mrb_parse_nstring(mrb, s, c), c);
}

mrb_value
mrb_load_nstring(mrb_state *mrb, const std::string & s)
{
    return mrb_load_nstring_cxt(mrb, s, NULL);
}

mrb_value mrb_load_string_cxt(mrb_state *mrb, const char *s, mrbc_context *c)
{
    return mrb_load_nstring_cxt(mrb, s, c);
}

mrb_value
mrb_load_string(mrb_state *mrb, const char *s)
{
    return mrb_load_string_cxt(mrb, s, NULL);
}

mrb_parser_state*
mrb_parser_new(mrb_state *mrb)
{
    mrb_pool *pool;
    mrb_parser_state *p;
    static const mrb_parser_state parser_state_zero = { 0 };

    pool = mrb->gc().mrb_pool_open();
    if (!pool)
        return 0;
    p = new(pool->mrb_pool_alloc(sizeof(mrb_parser_state))) mrb_parser_state;
    if (!p)
        return 0;

    *p = parser_state_zero;
    p->m_mrb = mrb;
    p->pool = pool;
    p->in_def = p->in_single = 0;

    p->s = p->send = NULL;
#ifdef ENABLE_STDIO
    p->f = NULL;
#endif

    p->m_cmd_start = TRUE;
    p->in_def = p->in_single = FALSE;

    p->m_capture_errors = 0;
    p->m_lineno = 1;
    p->m_column = 0;
#if defined(PARSER_TEST) || defined(PARSER_DEBUG)
    yydebug = 1;
#endif

    p->m_lex_strterm = nullptr;
    p->heredocs = p->parsing_heredoc = NULL;

    return p;
}

void mrb_parser_free(mrb_parser_state *p) {
    p->pool->mrb_pool_close();
}

mrbc_context* mrbc_context_new(mrb_state *mrb)
{
    mrbc_context *c;

    c = (mrbc_context *)mrb->gc()._calloc(1, sizeof(mrbc_context));
    return c;
}

void mrbc_context_free(mrb_state *mrb, mrbc_context *cxt)
{
    mrb->gc()._free(cxt->syms);
    mrb->gc()._free(cxt);
}

const char* mrbc_filename(mrb_state *mrb, mrbc_context *c, const char *s)
{
    if (s) {
        int len = strlen(s);
        char *p = (char *)mrb->gc().mrb_alloca(len + 1);

        memcpy(p, s, len + 1);
        c->filename = p;
        c->lineno = 1;
    }
    return c->filename;
}

#ifdef ENABLE_STDIO
mrb_parser_state* mrb_parse_file(mrb_state *mrb, FILE *f, mrbc_context *c)
{
    mrb_parser_state *p;

    p = mrb_parser_new(mrb);
    if (!p)
        return 0;
    p->s = p->send = nullptr;
    p->f = f;

    mrb_parser_parse(p, c);
    return p;
}
#endif

mrb_parser_state* mrb_parse_nstring(mrb_state *mrb, const std::string &str, mrbc_context *c)
{
    mrb_parser_state *p;

    p = mrb_parser_new(mrb);
    if (!p)
        return 0;
    p->source = str;
    p->s = p->source.c_str();
    p->send = p->s + p->source.size();

    mrb_parser_parse(p, c);
    return p;
}

mrb_parser_state* mrb_parse_string(mrb_state *mrb, const char *s, mrbc_context *c)
{
    return mrb_parse_nstring(mrb, s, c);
}
