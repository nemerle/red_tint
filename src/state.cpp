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
#include "mruby/debug.h"
#include "mruby/string.h"

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
    static constexpr struct mrb_context mrb_context_zero = { 0 };
    mrb_state *mrb = (mrb_state *)(f)(nullptr, nullptr, sizeof(mrb_state), ud);
    if (mrb == nullptr)
        return nullptr;

    *mrb = mrb_state_zero;
    mrb->gc().m_vm = mrb;
    mrb->gc().ud = ud;
    mrb->gc().m_allocf = f;
    mrb->gc().current_white_part = MRB_GC_WHITE_A;

    mrb->gc().mrb_heap_init();
    mrb->m_ctx = ( mrb_context*)mrb->gc()._calloc(1,sizeof(mrb_context));
    *mrb->m_ctx = mrb_context_zero;
    mrb->root_c = mrb->m_ctx;
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
void
mrb_irep_incref(mrb_state *mrb, mrb_irep *irep)
{
    irep->refcnt++;
}

void
mrb_irep_decref(mrb_state *mrb, mrb_irep *irep)
{
    irep->refcnt--;
    if (irep->refcnt == 0) {
        mrb_irep_free(mrb, irep);
    }
}
void mrb_irep_free(mrb_state *mrb, mrb_irep *irep)
{
    MemManager &mm(mrb->gc());
    if (!(irep->flags & MRB_ISEQ_NO_FREE))
        mm._free(irep->iseq);
    for (int i=0; i<irep->plen; i++) {
        if (mrb_type(irep->pool[i]) == MRB_TT_STRING)
            mm._free(mrb_ptr(irep->pool[i]));
    }
    mm._free(irep->pool);
    mm._free(irep->syms);
    for (int i=0; i<irep->rlen; i++) {
        mrb_irep_decref(mrb, irep->reps[i]);
    }
    mm._free(irep->reps);
    mm._free((void *)irep->filename);
    mm._free(irep->lines);
    mrb_debug_info_free(mrb, irep->debug_info);
    mm._free(irep);
}
void mrb_free_context(mrb_state *mrb, struct mrb_context *ctx)
{
    if (!ctx)
        return;
    MemManager &mm(mrb->gc());
    mm._free(ctx->m_stbase);
    mm._free(ctx->cibase);
    mm._free(ctx->rescue);
    mm._free(ctx->m_ensure);
    mm._free(ctx);
}

void mrb_close(mrb_state *mrb)
{
    MemManager &mm(mrb->gc());
    size_t i;

    mrb_core_final(mrb);

    /* free */
    mrb_gc_free_gv(mrb);
    mrb_free_context(mrb,mrb->root_c);
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
    static const mrb_irep mrb_irep_zero = { 0 };
    mrb_irep *irep;

    irep = (mrb_irep *)mrb->gc()._malloc(sizeof(mrb_irep));
    *irep = mrb_irep_zero;
    irep->refcnt = 1;
    return irep;
}

mrb_value
mrb_top_self(mrb_state *mrb)
{
    if (!mrb->top_self) {
        mrb->top_self = mrb->gc().obj_alloc<RObject>(MRB_TT_OBJECT, mrb->object_class);
        mrb->top_self->define_singleton_method("inspect", inspect_main, MRB_ARGS_NONE());
        mrb->top_self->define_singleton_method("to_s", inspect_main, MRB_ARGS_NONE());
    }
    return mrb_obj_value(mrb->top_self);
}
