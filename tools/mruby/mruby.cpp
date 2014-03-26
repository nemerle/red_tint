#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mruby.h"
#include "mruby/array.h"
#include "mruby/compile.h"
#include "mruby/dump.h"
#include "mruby/variable.h"


#ifndef ENABLE_STDIO
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

void mrb_show_version(mrb_state *);
void mrb_show_copyright(mrb_state *);

struct mrbc_args {
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
parse_args(mrb_state *mrb, int argc, char **argv, struct mrbc_args *args)
{
    char **origargv = argv;
    static const struct mrbc_args args_zero = { 0 };

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
                        size_t buflen;
                        char *buf;

                        buflen = strlen(item) + 1;
                        buf = (char *)mrb->gc()._malloc(buflen);
                        memcpy(buf, item, buflen);
                        args->cmdline = buf;
                    }
                    else {
                        size_t cmdlinelen;
                        size_t itemlen;

                        cmdlinelen = strlen(args->cmdline);
                        itemlen = strlen(item);
                        args->cmdline = (char *)mrb->gc()._realloc(args->cmdline, cmdlinelen + itemlen + 2);
                        args->cmdline[cmdlinelen] = '\n';
                        memcpy(args->cmdline + cmdlinelen + 1, item, itemlen + 1);
                    }
                }
                else {
                    printf("%s: No code specified for -e\n", *origargv);
                    return EXIT_SUCCESS;
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
cleanup(mrb_state *mrb, struct mrbc_args *args)
{
    if (args->rfp && args->rfp != stdin)
        fclose(args->rfp);
    if (args->cmdline && !args->fname)
        mrb->gc()._free(args->cmdline);
    if (args->argv)
        mrb->gc()._free(args->argv);
    mrb->destroy();
}

int
main(int argc, char **argv)
{
    mrb_state *mrb = mrb_state::create();
    int n = -1;
    int i;
    struct mrbc_args args;
    mrbc_context *c;
    mrb_value v;
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

    RArray *pARGV = RArray::create(mrb,args.argc);
    for (i = 0; i < args.argc; i++) {
        pARGV->push(mrb_str_new(mrb, args.argv[i], strlen(args.argv[i])));
    }
    mrb->define_global_const("ARGV", mrb_value::wrap(pARGV));
    c = mrbc_context_new(mrb);
    if (args.verbose)
        c->dump_result = 1;
    if (args.check_syntax)
        c->no_exec = true;
    if (args.mrbfile) {
        v = mrb_load_irep_file_cxt(mrb, args.rfp, c);
    }
    else {
        mrb_sym zero_sym = mrb_intern(mrb, "$0", 2);

        if (args.rfp) {
            const char *cmdline;
            cmdline = args.cmdline ? args.cmdline : "-";
            mrbc_filename(mrb, c, args.cmdline ? args.cmdline : "-");
            mrb->gv_set(zero_sym, mrb_str_new_cstr(mrb, cmdline));
            v = mrb_load_file_cxt(mrb, args.rfp, c);
        }
        else {
            mrbc_filename(mrb, c, "-e");
            mrb->gv_set(zero_sym, mrb_str_new_lit(mrb, "-e"));
            v = mrb_load_string_cxt(mrb, args.cmdline, c);
        }

    }
    mrbc_context_free(mrb, c);
    if (mrb->m_exc) {
        if (!v.is_undef()) {
            mrb->print_error();
        }
        n = -1;
    }
    else if (args.check_syntax) {
        printf("Syntax OK\n");
    }
    cleanup(mrb, &args);

    return n == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
