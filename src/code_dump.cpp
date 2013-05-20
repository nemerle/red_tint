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
#include "opcode.h"
#include "re.h"
typedef mrb_ast_node node;
typedef struct mrb_parser_state parser_state;

void mrb_state::codedump(int n)
{
    mrb_irep *irep = m_irep[n];

    if (!irep)
        return;

    sys.print_f("irep %d nregs=%d nlocals=%d pools=%d syms=%d\n", n, irep->nregs, irep->nlocals, (int)irep->plen, (int)irep->slen);

    for (int i=0; i<irep->ilen; i++) {
        int ai = this->gc().arena_save();
        sys.print_f("%03d ", i);
        mrb_code c = irep->iseq[i];
        switch (GET_OPCODE(c)) {
            case OP_NOP:
                sys.print_f("OP_NOP\n");
                break;
            case OP_MOVE:
                sys.print_f("OP_MOVE\tR%d\tR%d\n", GETARG_A(c), GETARG_B(c));
                break;
            case OP_LOADL:
                sys.print_f("OP_LOADL\tR%d\tL(%d)\n", GETARG_A(c), GETARG_Bx(c));
                break;
            case OP_LOADI:
                sys.print_f("OP_LOADI\tR%d\t%d\n", GETARG_A(c), GETARG_sBx(c));
                break;
            case OP_LOADSYM:
                sys.print_f("OP_LOADSYM\tR%d\t:%s\n", GETARG_A(c), mrb_sym2name(this, irep->syms[GETARG_Bx(c)]));
                break;
            case OP_LOADNIL:
                sys.print_f("OP_LOADNIL\tR%d\n", GETARG_A(c));
                break;
            case OP_LOADSELF:
                sys.print_f("OP_LOADSELF\tR%d\n", GETARG_A(c));
                break;
            case OP_LOADT:
                sys.print_f("OP_LOADT\tR%d\n", GETARG_A(c));
                break;
            case OP_LOADF:
                sys.print_f("OP_LOADF\tR%d\n", GETARG_A(c));
                break;
            case OP_GETGLOBAL:
                sys.print_f("OP_GETGLOBAL\tR%d\t:%s\n", GETARG_A(c), mrb_sym2name(this, irep->syms[GETARG_Bx(c)]));
                break;
            case OP_SETGLOBAL:
                sys.print_f("OP_SETGLOBAL\t:%s\tR%d\n", mrb_sym2name(this, irep->syms[GETARG_Bx(c)]), GETARG_A(c));
                break;
            case OP_GETCONST:
                sys.print_f("OP_GETCONST\tR%d\t:%s\n", GETARG_A(c), mrb_sym2name(this, irep->syms[GETARG_Bx(c)]));
                break;
            case OP_SETCONST:
                sys.print_f("OP_SETCONST\t:%s\tR%d\n", mrb_sym2name(this, irep->syms[GETARG_Bx(c)]), GETARG_A(c));
                break;
            case OP_GETMCNST:
                sys.print_f("OP_GETMCNST\tR%d\tR%d::%s\n", GETARG_A(c), GETARG_A(c), mrb_sym2name(this, irep->syms[GETARG_Bx(c)]));
                break;
            case OP_SETMCNST:
                sys.print_f("OP_SETMCNST\tR%d::%s\tR%d\n", GETARG_A(c)+1,
                       mrb_sym2name(this, irep->syms[GETARG_Bx(c)]),
                        GETARG_A(c));
                break;
            case OP_GETIV:
                sys.print_f("OP_GETIV\tR%d\t%s\n", GETARG_A(c),
                       mrb_sym2name(this, irep->syms[GETARG_Bx(c)]));
                break;
            case OP_SETIV:
                sys.print_f("OP_SETIV\t%s\tR%d\n",
                       mrb_sym2name(this, irep->syms[GETARG_Bx(c)]),
                        GETARG_A(c));
                break;
            case OP_GETUPVAR:
                sys.print_f("OP_GETUPVAR\tR%d\t%d\t%d\n",
                       GETARG_A(c), GETARG_B(c), GETARG_C(c));
                break;
            case OP_SETUPVAR:
                sys.print_f("OP_SETUPVAR\tR%d\t%d\t%d\n",
                       GETARG_A(c), GETARG_B(c), GETARG_C(c));
                break;
            case OP_GETCV:
                sys.print_f("OP_GETCV\tR%d\t%s\n", GETARG_A(c),
                       mrb_sym2name(this, irep->syms[GETARG_Bx(c)]));
                break;
            case OP_SETCV:
                sys.print_f("OP_SETCV\t%s\tR%d\n",
                       mrb_sym2name(this, irep->syms[GETARG_Bx(c)]),
                        GETARG_A(c));
                break;
            case OP_JMP:
                sys.print_f("OP_JMP\t\t%03d\n", i+GETARG_sBx(c));
                break;
            case OP_JMPIF:
                sys.print_f("OP_JMPIF\tR%d\t%03d\n", GETARG_A(c), i+GETARG_sBx(c));
                break;
            case OP_JMPNOT:
                sys.print_f("OP_JMPNOT\tR%d\t%03d\n", GETARG_A(c), i+GETARG_sBx(c));
                break;
            case OP_SEND:
                sys.print_f("OP_SEND\tR%d\t:%s\t%d\n", GETARG_A(c),
                       mrb_sym2name(this, irep->syms[GETARG_B(c)]),
                        GETARG_C(c));
                break;
            case OP_SENDB:
                sys.print_f("OP_SENDB\tR%d\t:%s\t%d\n", GETARG_A(c),
                       mrb_sym2name(this, irep->syms[GETARG_B(c)]),
                        GETARG_C(c));
                break;
            case OP_TAILCALL:
                sys.print_f("OP_TAILCALL\tR%d\t:%s\t%d\n", GETARG_A(c),
                       mrb_sym2name(this, irep->syms[GETARG_B(c)]),
                        GETARG_C(c));
                break;
            case OP_SUPER:
                sys.print_f("OP_SUPER\tR%d\t%d\n", GETARG_A(c),
                       GETARG_C(c));
                break;
            case OP_ARGARY:
                sys.print_f("OP_ARGARY\tR%d\t%d:%d:%d:%d\n", GETARG_A(c),
                       (GETARG_Bx(c)>>10)&0x3f,
                       (GETARG_Bx(c)>>9)&0x1,
                       (GETARG_Bx(c)>>4)&0x1f,
                       (GETARG_Bx(c)>>0)&0xf);
                break;

            case OP_ENTER:
                sys.print_f("OP_ENTER\t%d:%d:%d:%d:%d:%d:%d\n",
                       (GETARG_Ax(c)>>18)&0x1f,
                       (GETARG_Ax(c)>>13)&0x1f,
                       (GETARG_Ax(c)>>12)&0x1,
                       (GETARG_Ax(c)>>7)&0x1f,
                       (GETARG_Ax(c)>>2)&0x1f,
                       (GETARG_Ax(c)>>1)&0x1,
                       GETARG_Ax(c) & 0x1);
                break;
            case OP_RETURN:
                sys.print_f("OP_RETURN\tR%d", GETARG_A(c));
                switch (GETARG_B(c)) {
                    case OP_R_NORMAL:
                        sys.print_f("\n"); break;
                    case OP_R_RETURN:
                        sys.print_f("\treturn\n"); break;
                    case OP_R_BREAK:
                        sys.print_f("\tbreak\n"); break;
                    default:
                        sys.print_f("\tbroken\n"); break;
                }
                break;
            case OP_BLKPUSH:
                sys.print_f("OP_BLKPUSH\tR%d\t%d:%d:%d:%d\n", GETARG_A(c),
                       (GETARG_Bx(c)>>10)&0x3f,
                       (GETARG_Bx(c)>>9)&0x1,
                       (GETARG_Bx(c)>>4)&0x1f,
                       (GETARG_Bx(c)>>0)&0xf);
                break;

            case OP_LAMBDA:
                sys.print_f("OP_LAMBDA\tR%d\tI(%+d)\t%d\n", GETARG_A(c), GETARG_b(c), GETARG_c(c));
                break;
            case OP_RANGE:
                sys.print_f("OP_RANGE\tR%d\tR%d\t%d\n", GETARG_A(c), GETARG_B(c), GETARG_C(c));
                break;
            case OP_METHOD:
                sys.print_f("OP_METHOD\tR%d\t:%s\n", GETARG_A(c),
                       mrb_sym2name(this, irep->syms[GETARG_B(c)]));
                break;

            case OP_ADD:
                sys.print_f("OP_ADD\tR%d\t:%s\t%d\n", GETARG_A(c),
                       mrb_sym2name(this, irep->syms[GETARG_B(c)]),
                        GETARG_C(c));
                break;
            case OP_ADDI:
                sys.print_f("OP_ADDI\tR%d\t:%s\t%d\n", GETARG_A(c), mrb_sym2name(this, irep->syms[GETARG_B(c)]), GETARG_C(c));
                break;
            case OP_SUB:
                sys.print_f("OP_SUB\tR%d\t:%s\t%d\n", GETARG_A(c),
                       mrb_sym2name(this, irep->syms[GETARG_B(c)]),
                        GETARG_C(c));
                break;
            case OP_SUBI:
                sys.print_f("OP_SUBI\tR%d\t:%s\t%d\n", GETARG_A(c),
                       mrb_sym2name(this, irep->syms[GETARG_B(c)]),
                        GETARG_C(c));
                break;
            case OP_MUL:
                sys.print_f("OP_MUL\tR%d\t:%s\t%d\n", GETARG_A(c),
                       mrb_sym2name(this, irep->syms[GETARG_B(c)]),
                        GETARG_C(c));
                break;
            case OP_DIV:
                sys.print_f("OP_DIV\tR%d\t:%s\t%d\n", GETARG_A(c),
                       mrb_sym2name(this, irep->syms[GETARG_B(c)]),
                        GETARG_C(c));
                break;
            case OP_LT:
                sys.print_f("OP_LT\tR%d\t:%s\t%d\n", GETARG_A(c),
                       mrb_sym2name(this, irep->syms[GETARG_B(c)]),
                        GETARG_C(c));
                break;
            case OP_LE:
                sys.print_f("OP_LE\tR%d\t:%s\t%d\n", GETARG_A(c),
                       mrb_sym2name(this, irep->syms[GETARG_B(c)]),
                        GETARG_C(c));
                break;
            case OP_GT:
                sys.print_f("OP_GT\tR%d\t:%s\t%d\n", GETARG_A(c),
                       mrb_sym2name(this, irep->syms[GETARG_B(c)]),
                        GETARG_C(c));
                break;
            case OP_GE:
                sys.print_f("OP_GE\tR%d\t:%s\t%d\n", GETARG_A(c),
                       mrb_sym2name(this, irep->syms[GETARG_B(c)]),
                        GETARG_C(c));
                break;
            case OP_EQ:
                sys.print_f("OP_EQ\tR%d\t:%s\t%d\n", GETARG_A(c),
                       mrb_sym2name(this, irep->syms[GETARG_B(c)]),
                        GETARG_C(c));
                break;

            case OP_STOP:
                sys.print_f("OP_STOP\n");
                break;

            case OP_ARRAY:
                sys.print_f("OP_ARRAY\tR%d\tR%d\t%d\n", GETARG_A(c), GETARG_B(c), GETARG_C(c));
                break;
            case OP_ARYCAT:
                sys.print_f("OP_ARYCAT\tR%d\tR%d\n", GETARG_A(c), GETARG_B(c));
                break;
            case OP_ARYPUSH:
                sys.print_f("OP_ARYPUSH\tR%d\tR%d\n", GETARG_A(c), GETARG_B(c));
                break;
            case OP_AREF:
                sys.print_f("OP_AREF\tR%d\tR%d\t%d\n", GETARG_A(c), GETARG_B(c), GETARG_C(c));
                break;
            case OP_APOST:
                sys.print_f("OP_APOST\tR%d\t%d\t%d\n", GETARG_A(c), GETARG_B(c), GETARG_C(c));
                break;
            case OP_STRING:
            {
                mrb_value s = irep->m_pool[GETARG_Bx(c)];

                s = mrb_str_dump(this, s);
                sys.print_f("OP_STRING\tR%d\t%s\n", GETARG_A(c), RSTRING_PTR(s));
            }
                break;
            case OP_STRCAT:
                sys.print_f("OP_STRCAT\tR%d\tR%d\n", GETARG_A(c), GETARG_B(c));
                break;
            case OP_HASH:
                sys.print_f("OP_HASH\tR%d\tR%d\t%d\n", GETARG_A(c), GETARG_B(c), GETARG_C(c));
                break;

            case OP_OCLASS:
                sys.print_f("OP_OCLASS\tR%d\n", GETARG_A(c));
                break;
            case OP_CLASS:
                sys.print_f("OP_CLASS\tR%d\t:%s\n", GETARG_A(c),
                       mrb_sym2name(this, irep->syms[GETARG_B(c)]));
                break;
            case OP_MODULE:
                sys.print_f("OP_MODULE\tR%d\t:%s\n", GETARG_A(c),
                       mrb_sym2name(this, irep->syms[GETARG_B(c)]));
                break;
            case OP_EXEC:
                sys.print_f("OP_EXEC\tR%d\tI(%d)\n", GETARG_A(c), n+GETARG_Bx(c));
                break;
            case OP_SCLASS:
                sys.print_f("OP_SCLASS\tR%d\tR%d\n", GETARG_A(c), GETARG_B(c));
                break;
            case OP_TCLASS:
                sys.print_f("OP_TCLASS\tR%d\n", GETARG_A(c));
                break;
            case OP_ERR:
                sys.print_f("OP_ERR\tL(%d)\n", GETARG_Bx(c));
                break;
            case OP_EPUSH:
                sys.print_f("OP_EPUSH\t:I(%d)\n", n+GETARG_Bx(c));
                break;
            case OP_ONERR:
                sys.print_f("OP_ONERR\t%03d\n", i+GETARG_sBx(c));
                break;
            case OP_RESCUE:
                sys.print_f("OP_RESCUE\tR%d\n", GETARG_A(c));
                break;
            case OP_RAISE:
                sys.print_f("OP_RAISE\tR%d\n", GETARG_A(c));
                break;
            case OP_POPERR:
                sys.print_f("OP_POPERR\t%d\n", GETARG_A(c));
                break;
            case OP_EPOP:
                sys.print_f("OP_EPOP\t%d\n", GETARG_A(c));
                break;

            default:
                sys.print_f("OP_unknown %d\t%d\t%d\t%d\n", GET_OPCODE(c),
                       GETARG_A(c), GETARG_B(c), GETARG_C(c));
                break;
        }
        this->gc().arena_restore(ai);
    }
    sys.print_f("\n");
}

void mrb_state::codedump_all(int start)
{
    for (size_t i=start; i<irep_len; i++) {
        codedump(i);
    }
}


