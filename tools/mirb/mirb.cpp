/*
** mirb - Embeddable Interactive Ruby Shell
**
** This program takes code from the user in
** an interactive way and executes it
** immediately. It's a REPL...
*/

#include <stdlib.h>
#include <string.h>

#include "mruby.h"
#include "mruby/array.h"
#include "mruby/proc.h"
#include "mruby/compile.h"
#include "mruby/string.h"

#ifdef ENABLE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#ifndef ENABLE_STDIO
#include <mruby/string.h>
static void
p(mrb_state *mrb, mrb_value obj)
{
    obj = mrb->funcall(obj, "inspect", 0);
    fwrite(RSTRING_PTR(obj), RSTRING_LEN(obj), 1, stdout);
    putc('\n', stdout);
}
#else
#define p(mrb,obj) mrb_p(mrb,obj)
#endif

/* Guess if the user might want to enter more
 * or if he wants an evaluation of his code now */
bool is_code_block_open(mrb_parser_state *parser)
{
    int code_block_open = false;

    /* check for heredoc */
    if (parser->parsing_heredoc != NULL) return true;
    if (parser->heredoc_end_now) {
        parser->heredoc_end_now = false;
        return false;
      }

    /* check if parser error are available */
    if (0 < parser->nerr) {
        const char *unexpected_end = "syntax error, unexpected $end";
        const char *message = parser->error_buffer[0].message;

        /* a parser error occur, we have to check if */
        /* we need to read one more line or if there is */
        /* a different issue which we have to show to */
        /* the user */

        if (strncmp(message, unexpected_end, strlen(unexpected_end)) == 0) {
            code_block_open = true;
        }
        else if (strcmp(message, "syntax error, unexpected keyword_end") == 0) {
            code_block_open = false;
        }
        else if (strcmp(message, "syntax error, unexpected tREGEXP_BEG") == 0) {
            code_block_open = false;
        }
        return code_block_open;
    }

    /* check for unterminated string */
    if (parser->m_lex_strterm) return true;

    switch (parser->m_lstate) {

        /* all states which need more code */

        case EXPR_BEG:
            /* an expression was just started, */
            /* we can't end it like this */
            code_block_open = true;
            break;
        case EXPR_DOT:
            /* a message dot was the last token, */
            /* there has to come more */
            code_block_open = true;
            break;
        case EXPR_CLASS:
            /* a class keyword is not enough! */
            /* we need also a name of the class */
            code_block_open = true;
            break;
        case EXPR_FNAME:
            /* a method name is necessary */
            code_block_open = true;
            break;
        case EXPR_VALUE:
            /* if, elsif, etc. without condition */
            code_block_open = true;
            break;

            /* now all the states which are closed */

        case EXPR_ARG:
            /* an argument is the last token */
            code_block_open = false;
            break;

            /* all states which are unsure */

        case EXPR_CMDARG:
            break;
        case EXPR_END:
            /* an expression was ended */
            break;
        case EXPR_ENDARG:
            /* closing parenthese */
            break;
        case EXPR_ENDFN:
            /* definition end */
            break;
        case EXPR_MID:
            /* jump keyword like break, return, ... */
            break;
        case EXPR_MAX_STATE:
            /* don't know what to do with this token */
            break;
        default:
            /* this state is unexpected! */
            break;
    }

    return code_block_open;
}

void mrb_show_version(mrb_state *);
void mrb_show_copyright(mrb_state *);

struct mrbc_args {
    bool verbose;
    int argc;
    char** argv;
};

static void
usage(const char *name)
{
    static const char *const usage_msg[] = {
        "switches:",
        "-v           print version number, then run in verbose mode",
        "--verbose    run in verbose mode",
        "--version    print the version",
        "--copyright  print the copyright",
        NULL
    };
    const char *const *p = usage_msg;

    printf("Usage: %s [switches]\n", name);
    while(*p)
        printf("  %s\n", *p++);
}

static int
parse_args(mrb_state *mrb, int argc, char **argv, struct mrbc_args *args)
{
    static const struct mrbc_args args_zero = { 0 };

    *args = args_zero;

    for (argc--,argv++; argc > 0; argc--,argv++) {
        char *item;
        if (argv[0][0] != '-') break;

        item = argv[0] + 1;
        switch (*item++) {
            case 'v':
                if (!args->verbose) mrb_show_version(mrb);
                args->verbose = 1;
                break;
            case '-':
                if (strcmp((*argv) + 2, "version") == 0) {
                    mrb_show_version(mrb);
                    exit(EXIT_SUCCESS);
                }
                else if (strcmp((*argv) + 2, "verbose") == 0) {
                    args->verbose = 1;
                    break;
                }
                else if (strcmp((*argv) + 2, "copyright") == 0) {
                    mrb_show_copyright(mrb);
                    exit(EXIT_SUCCESS);
                }
                // fallthrough
            default:
                return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}

static void
cleanup(mrb_state *mrb, struct mrbc_args *args)
{
    mrb->destroy();
}

/* Print a short remark for the user */
static void
print_hint(void)
{
    printf("mirb - Embeddable Interactive Ruby Shell\n");
}

/* Print the command line prompt of the REPL */
void
print_cmdline(int code_block_open)
{
    if (code_block_open) {
        printf("* ");
    }
    else {
        printf("> ");
    }
}

int
main(int argc, char **argv)
{
    char ruby_code[1024] = { 0 };
    char last_code_line[1024] = { 0 };
#ifndef ENABLE_READLINE
    int last_char;
    int char_index;
#endif
    mrbc_context *cxt;
    struct mrb_parser_state *parser;
    mrb_state *mrb;
    mrb_value result;
    struct mrbc_args args;
    int n;
    int code_block_open = false;
    int ai;
    int first_command = 1;
    unsigned int nregs;

    /* new interpreter instance */
    mrb = mrb_state::create();
    if (mrb == NULL) {
        fputs("Invalid mrb interpreter, exiting mirb\n", stderr);
        return EXIT_FAILURE;
    }

    n = parse_args(mrb, argc, argv, &args);
    if (n < 0) {
        cleanup(mrb, &args);
        usage(argv[0]);
        return n;
    }

    print_hint();

    cxt = mrbc_context_new(mrb);
    cxt->capture_errors = 1;
    cxt->lineno = 1;
    if (args.verbose)
        cxt->dump_result = 1;

    ai = mrb->gc().arena_save();
    while (true) {
#ifndef ENABLE_READLINE
        print_cmdline(code_block_open);

        char_index = 0;
        while ((last_char = getchar()) != '\n') {
            if (last_char == EOF) break;
            last_code_line[char_index++] = last_char;
        }
        if (last_char == EOF) {
            fputs("\n", stdout);
            break;
        }

        last_code_line[char_index] = '\0';
#else
        char* line = readline(code_block_open ? "* " : "> ");
        if(line == NULL) {
            printf("\n");
            break;
        }
        strncpy(last_code_line, line, sizeof(last_code_line)-1);
        add_history(line);
        free(line);
#endif

        if ((strcmp(last_code_line, "quit") == 0) || (strcmp(last_code_line, "exit") == 0)) {
            if (!code_block_open) {
                break;
            }
            else{
                /* count the quit/exit commands as strings if in a quote block */
                strcat(ruby_code, "\n");
                strcat(ruby_code, last_code_line);
            }
        }
        else {
            if (code_block_open) {
                strcat(ruby_code, "\n");
                strcat(ruby_code, last_code_line);
            }
            else {
                strcpy(ruby_code, last_code_line);
            }
        }

        /* parse code */
        parser = mrb_parser_new(mrb);
        parser->s = ruby_code;
        parser->send = ruby_code + strlen(ruby_code);
        parser->m_lineno = cxt->lineno;
        mrb_parser_parse(parser, cxt);
        code_block_open = is_code_block_open(parser);

        if (code_block_open) {
            /* no evaluation of code */
        }
        else {
            if (0 < parser->nerr) {
                /* syntax error */
                printf("line %d: %s\n", parser->error_buffer[0].lineno, parser->error_buffer[0].message);
            }
            else {
                /* generate bytecode */
                RProc *proc = mrb_generate_code(mrb, parser);
                /* pass a proc for evaulation */
                nregs = first_command ? 0: proc->ireps()->nregs;

                /* evaluate the bytecode */
                result = mrb->mrb_context_run(
                                 /* pass a proc for evaulation */
                                 proc, mrb_top_self(mrb),nregs);
                /* did an exception occur? */
                if (mrb->m_exc) {
                    p(mrb, mrb_value::wrap(mrb->m_exc));
                    mrb->m_exc = 0;
                }
                else {
                    /* no */
                    printf(" => ");
                    if (!result.respond_to(mrb,mrb->intern2("inspect",7))){
                        result = mrb_any_to_s(mrb,result);
                    }
                    p(mrb, result);
                }
            }
            ruby_code[0] = '\0';
            last_code_line[0] = '\0';
            mrb_parser_free(parser);
            mrb->gc().arena_restore(ai);
            cxt->lineno++;
        }
    }
    mrbc_context_free(mrb, cxt);
    mrb->destroy();

    return 0;
}
