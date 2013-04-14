#include "mruby.h"
#include "mruby/proc.h"
#include "mruby/array.h"
#include "mruby/string.h"
#include "mruby/compile.h"
#include "mruby/dump.h"
#include "mruby/variable.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef ENABLE_STDIO
static void
p(mrb_state *mrb, mrb_value obj)
{
    obj = mrb_funcall(mrb, obj, "inspect", 0);
    fwrite(RSTRING_PTR(obj), RSTRING_LEN(obj), 1, stdout);
    putc('\n', stdout);
}
#else
#define p(mrb,obj) mrb_p(mrb,obj)
#endif

void mrb_show_version(mrb_state *);
void mrb_show_copyright(mrb_state *);

struct _args {
    FILE *rfp;
    char* cmdline;
    mrb_bool fname        : 1;
    mrb_bool mrbfile      : 1;
    mrb_bool check_syntax : 1;
    mrb_bool verbose      : 1;
    int argc;
    char** argv;
};

static void
usage(const char *name)
{
    static const char *const usage_msg[] = {
        "switches:",
        "-b           load and execute RiteBinary (mrb) file",
        "-c           check syntax only",
        "-e 'command' one line of script",
        "-v           print version number, then run in verbose mode",
        "--verbose    run in verbose mode",
        "--version    print the version",
        "--copyright  print the copyright",
        NULL
    };
    const char *const *p = usage_msg;

    printf("Usage: %s [switches] programfile\n", name);
    while(*p)
        printf("  %s\n", *p++);
}

static int
parse_args(mrb_state *mrb, int argc, char **argv, struct _args *args)
{
    char **origargv = argv;
    static const struct _args args_zero = { 0 };

    *args = args_zero;

    for (argc--,argv++; argc > 0; argc--,argv++) {
        char *item;
        if (argv[0][0] != '-') break;

        if (strlen(*argv) <= 1) {
            argc--; argv++;
            args->rfp = stdin;
            break;
        }

        item = argv[0] + 1;
        switch (*item++) {
            case 'b':
                args->mrbfile = 1;
                break;
            case 'c':
                args->check_syntax = 1;
                break;
            case 'e':
                if (item[0]) {
                    goto append_cmdline;
                }
                else if (argc > 1) {
                    argc--; argv++;
                    item = argv[0];
append_cmdline:
                    if (!args->cmdline) {
                        char *buf;

                        buf = (char *)mrb->gc()._malloc(strlen(item)+1);
                        strcpy(buf, item);
                        args->cmdline = buf;
                    }
                    else {
                        args->cmdline = (char *)mrb->gc()._realloc(args->cmdline, strlen(args->cmdline)+strlen(item)+2);
                        strcat(args->cmdline, "\n");
                        strcat(args->cmdline, item);
                    }
                }
                else {
                    printf("%s: No code specified for -e\n", *origargv);
                    return 0;
                }
                break;
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
                //fallthrough
            default:
                return EXIT_FAILURE;
        }
    }

    if (args->rfp == NULL && args->cmdline == NULL) {
        if (*argv == NULL) args->rfp = stdin;
        else {
            args->rfp = fopen(argv[0], args->mrbfile ? "rb" : "r");
            if (args->rfp == NULL) {
                printf("%s: Cannot open program file. (%s)\n", *origargv, *argv);
                return 0;
            }
            args->fname = 1;
            args->cmdline = argv[0];
            argc--; argv++;
        }
    }
    args->argv = (char **)mrb->gc()._realloc(args->argv, sizeof(char*) * (argc + 1));
    memcpy(args->argv, argv, (argc+1) * sizeof(char*));
    args->argc = argc;

    return EXIT_SUCCESS;
}

static void
cleanup(mrb_state *mrb, struct _args *args)
{
    if (args->rfp && args->rfp != stdin)
        fclose(args->rfp);
    if (args->cmdline && !args->fname)
        mrb->gc()._free(args->cmdline);
    if (args->argv)
        mrb->gc()._free(args->argv);
    mrb_close(mrb);
}

static void
showcallinfo(mrb_state *mrb)
{
    mrb_callinfo *ci;
    mrb_int ciidx;
    const char *filename, *method, *sep;
    int i, line;

    printf("trace:\n");
    ciidx = mrb_fixnum(mrb_obj_iv_get(mrb, mrb->m_exc, mrb_intern(mrb, "ciidx")));
    if (ciidx >= mrb->ciend - mrb->cibase)
        ciidx = 10; /* ciidx is broken... */

    for (i = ciidx; i >= 0; i--) {
        ci = &mrb->cibase[i];
        filename = "(unknown)";
        line = -1;

        if (MRB_PROC_CFUNC_P(ci->proc)) {
            continue;
        }
        else {
            mrb_irep *irep = ci->proc->body.irep;
            if (irep->filename != NULL)
                filename = irep->filename;
            if (irep->lines != NULL) {
                mrb_code *pc;

                if (i+1 <= ciidx) {
                    pc = mrb->cibase[i+1].pc;
                }
                else {
                    pc = (mrb_code*)mrb_voidp(mrb_obj_iv_get(mrb, mrb->m_exc, mrb_intern(mrb, "lastpc")));
                }
                if (irep->iseq <= pc && pc < irep->iseq + irep->ilen) {
                    line = irep->lines[pc - irep->iseq - 1];
                }
            }
        }
        if (line == -1) continue;
        if (ci->target_class == ci->proc->target_class)
            sep = ".";
        else
            sep = "#";

        method = mrb_sym2name(mrb, ci->mid);
        if (method) {
            const char *cn = mrb_class_name(mrb, ci->proc->target_class);

            if (cn) {
                printf("\t[%d] %s:%d:in %s%s%s\n", i, filename, line, cn, sep, method);
            }
            else {
                printf("\t[%d] %s:%d:in %s\n", i, filename, line, method);
            }
        }
        else {
            printf("\t[%d] %s:%d\n", i, filename, line);
        }
    }
}

int
main(int argc, char **argv)
{
    mrb_state *mrb = mrb_open();
    int n = -1;
    int i;
    struct _args args;
    mrb_value ARGV;

    if (mrb == NULL) {
        fputs("Invalid mrb_state, exiting mruby\n", stderr);
        return EXIT_FAILURE;
    }

    n = parse_args(mrb, argc, argv, &args);
    if ( (n == EXIT_FAILURE) || (args.cmdline == NULL && args.rfp == NULL)) {
        cleanup(mrb, &args);
        usage(argv[0]);
        return n;
    }

    ARGV = RArray::new_capa(mrb, args.argc);
    for (i = 0; i < args.argc; i++) {
        RArray::push(mrb, ARGV, mrb_str_new(mrb, args.argv[i], strlen(args.argv[i])));
    }
    mrb_define_global_const(mrb, "ARGV", ARGV);

    if (args.mrbfile) {
        n = mrb_read_irep_file(mrb, args.rfp);
        if (n < 0) {
            fprintf(stderr, "failed to load mrb file: %s\n", args.cmdline);
        }
        else if (!args.check_syntax) {
            mrb->mrb_run(mrb_proc_new(mrb, mrb->irep[n]), mrb_top_self(mrb));
            n = 0;
            if (mrb->m_exc) {
                showcallinfo(mrb);
                p(mrb, mrb_obj_value(mrb->m_exc));
                n = -1;
            }
        }
    }
    else {
        mrbc_context *c = mrbc_context_new(mrb);
        mrb_sym zero_sym = mrb_intern2(mrb, "$0", 2);
        mrb_value v;

        if (args.verbose)
            c->dump_result = 1;
        if (args.check_syntax)
            c->no_exec = 1;

        if (args.rfp) {
            mrbc_filename(mrb, c, args.cmdline ? args.cmdline : "-");
            v = mrb_load_file_cxt(mrb, args.rfp, c);
//            const char *cmdline;
//            cmdline = args.cmdline ? args.cmdline : "-";
//            mrbc_filename(mrb, c, cmdline);
//            mrb_gv_set(mrb, zero_sym, mrb_str_new_cstr(mrb, cmdline));
//            v = mrb_load_string_cxt(mrb, args.cmdline, c);
        }
        else {
            mrbc_filename(mrb, c, "-e");
            mrb_gv_set(mrb, zero_sym, mrb_str_new(mrb, "-e", 2));
            v = mrb_load_string_cxt(mrb, args.cmdline, c);
        }

        mrbc_context_free(mrb, c);
        if (mrb->m_exc) {
            if (!mrb_undef_p(v)) {
                showcallinfo(mrb);
                p(mrb, mrb_obj_value(mrb->m_exc));
            }
            n = -1;
        }
        else if (args.check_syntax) {
            printf("Syntax OK\n");
        }
    }
    cleanup(mrb, &args);

    return n == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
