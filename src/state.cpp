/*
** state.c - mrb_state open/close functions
**
** See Copyright Notice in mruby.h
*/

#include <cstdlib>
#include <cstring>
#include "mruby.h"
#include "mruby/class.h"
#include "mruby/irep.h"
#include "mruby/variable.h"

void mrb_core_init(mrb_state*);
void mrb_core_final(mrb_state*);
void mrb_symtbl_free(mrb_state *mrb);

static mrb_value
inspect_main(mrb_state *mrb, mrb_value mod)
{
    return mrb_str_new(mrb, "main", 4);
}

mrb_state* mrb_state::create(mrb_allocf f, void *ud)
{

    static constexpr mrb_state mrb_state_zero = { 0 };
    mrb_state *mrb = (mrb_state *)(f)(nullptr, nullptr, sizeof(mrb_state), ud);
    if (mrb == nullptr)
        return nullptr;

    *mrb = mrb_state_zero;
    mrb->gc().m_mrb = mrb;
    mrb->gc().ud = ud;
    mrb->gc().m_allocf = f;
    mrb->gc().current_white_part = MRB_GC_WHITE_A;

    mrb->gc().mrb_heap_init();
    mrb_core_init(mrb);
    return mrb;
}

static void*
allocf(mrb_state *mrb, void *p, size_t size, void *ud)
{
    if (size == 0) {
        free(p);
        return NULL;
    }
    else {
        return realloc(p, size);
    }
}

struct alloca_header {
    alloca_header *next;
    char buf[];
};

void* MemManager::mrb_alloca(size_t size)
{
    alloca_header *p;

    p = (alloca_header*) _malloc(sizeof(struct alloca_header)+size);
    if (p == nullptr)
        return nullptr;
    p->next = mems;
    mems = p;
    return (void*)p->buf;
}

void MemManager::mrb_alloca_free()
{
    alloca_header *p;
    alloca_header *tmp;

    if (this == nullptr)
        return;
    p = mems;

    while (p) {
        tmp = p;
        p = p->next;
        _free(tmp);
    }
}

mrb_state* mrb_open(void)
{
    mrb_state *mrb = mrb_state::create(allocf, NULL);

    return mrb;
}


void mrb_irep_free(mrb_state *mrb, struct mrb_irep *irep)
{
    MemManager &mm(mrb->gc());
    if (!(irep->flags & MRB_ISEQ_NO_FREE))
        mm._free(irep->iseq);
    mm._free(irep->pool);
    mm._free(irep->syms);
    mm._free((void *)irep->filename);
    mm._free(irep->lines);
    mm._free(irep);
}

void mrb_close(mrb_state *mrb)
{
    MemManager &mm(mrb->gc());
    size_t i;

    mrb_core_final(mrb);

    /* free */
    mrb_gc_free_gv(mrb);
    mm._free(mrb->stbase);
    mm._free(mrb->cibase);
    for (i=0; i<mrb->irep_len; i++) {
        mrb_irep_free(mrb, mrb->m_irep[i]);
    }
    mm._free(mrb->m_irep);
    mm._free(mrb->rescue);
    mm._free(mrb->ensure);
    mrb_symtbl_free(mrb);
    mm.mrb_heap_free();
    mm.mrb_alloca_free();
    mm._free(mrb);
}

#ifndef MRB_IREP_ARRAY_INIT_SIZE
# define MRB_IREP_ARRAY_INIT_SIZE (256u)
#endif

mrb_irep* mrb_add_irep(mrb_state *mrb)
{
    static constexpr mrb_irep mrb_irep_zero = { 0 };
    mrb_irep *irep;
    MemManager &mm(mrb->gc());
    if (!mrb->m_irep) {
        size_t max = MRB_IREP_ARRAY_INIT_SIZE;

        if (mrb->irep_len > max) max = mrb->irep_len+1;
        mrb->m_irep = (mrb_irep **) mm._calloc(max, sizeof(mrb_irep*));
        mrb->irep_capa = max;
    }
    else if (mrb->irep_capa <= mrb->irep_len) {
        size_t i;
        size_t old_capa = mrb->irep_capa;
        while (mrb->irep_capa <= mrb->irep_len) {
            mrb->irep_capa *= 2;
        }
        mrb->m_irep = (mrb_irep **)mm._realloc(mrb->m_irep, sizeof(mrb_irep*)*mrb->irep_capa);
        for (i = old_capa; i < mrb->irep_capa; i++) {
            mrb->m_irep[i] = nullptr;
        }
    }
    irep = (mrb_irep *)mm._malloc(sizeof(mrb_irep));
    *irep = mrb_irep_zero;
    mrb->m_irep[mrb->irep_len] = irep;
    irep->idx = mrb->irep_len++;

    return irep;
}

mrb_value
mrb_top_self(mrb_state *mrb)
{
    if (!mrb->top_self) {
        mrb->top_self = mrb->gc().obj_alloc<RObject>(MRB_TT_OBJECT, mrb->object_class);
        mrb_define_singleton_method(mrb, mrb->top_self, "inspect", inspect_main, MRB_ARGS_NONE());
        mrb_define_singleton_method(mrb, mrb->top_self, "to_s", inspect_main, MRB_ARGS_NONE());
    }
    return mrb_obj_value(mrb->top_self);
}
