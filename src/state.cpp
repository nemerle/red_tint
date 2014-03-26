/*
** state.c - mrb_state open/close functions
**
** See Copyright Notice in mruby.h
*/

#include <cstdlib>
#include <cstring>
#include "mruby.h"
#include "mruby/irep.h"
#include "mruby/variable.h"
#include "mruby/debug.h"
#include "mruby/string.h"

void mrb_core_init(mrb_state*);
void mrb_core_final(mrb_state*);
void mrb_symtbl_free(mrb_state *mrb);

static void* allocf(mrb_state */*mrb*/, void *p, size_t size, void */*ud*/)
{
    if (size == 0) {
        free(p);
        return nullptr;
    }
    else {
        return realloc(p, size);
    }
}

static mrb_value
inspect_main(mrb_state *mrb, mrb_value mod)
{
    return mrb_str_new_lit(mrb, "main");
}

mrb_state* mrb_state::create(mrb_allocf f, void *ud)
{
    if(!f)
        f=allocf;
#ifdef MRB_NAN_BOXING
  mrb_assert(sizeof(void*) == 4);
#endif
    static constexpr mrb_state mrb_state_zero = { 0 };
    static constexpr struct mrb_context mrb_context_zero = { 0 };
    mrb_state *mrb = (mrb_state *)(f)(nullptr, nullptr, sizeof(mrb_state), ud);
    if (mrb == nullptr)
        return nullptr;

    *mrb = mrb_state_zero;
    mrb->gc().init(mrb,ud,f);
    mrb->m_ctx = ( mrb_context*)mrb->gc()._calloc(1,sizeof(mrb_context));
    *mrb->m_ctx = mrb_context_zero;
    mrb->root_c = mrb->m_ctx;
    mrb_core_init(mrb);
    return mrb;
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

void mrb_irep_incref(mrb_state */*mrb*/, mrb_irep *irep)
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
        if (mrb_type(irep->pool[i]) == MRB_TT_STRING) {
            if ((irep->pool[i].ptr<RString>()->flags & MRB_STR_NOFREE) == 0) {
                mm._free(irep->pool[i].ptr<RString>()->m_ptr);
            }
            mm._free(irep->pool[i].basic_ptr());
        }
#ifdef MRB_WORD_BOXING
    else if (mrb_type(irep->pool[i]) == MRB_TT_FLOAT) {
      mrb_free(mrb, mrb_obj_ptr(irep->pool[i]));
    }
#endif
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
mrb_value
mrb_str_pool(mrb_state *mrb, mrb_value str)
{
    RString *s = str.ptr<RString>();
    RString *ns;
    mrb_int len;

    ns = (struct RString *)mrb->gc()._malloc(sizeof(struct RString));
    ns->tt = MRB_TT_STRING;
    ns->c = mrb->string_class;

    len = s->len;
    ns->len = len;
    ns->flags = 0;
    if (s->flags & MRB_STR_NOFREE) {
        ns->m_ptr = s->m_ptr;
        ns->flags = MRB_STR_NOFREE;
    }
    else {
        ns->m_ptr = (char *)mrb->gc()._malloc((size_t)len+1);
        if (s->m_ptr) {
            memcpy(ns->m_ptr, s->m_ptr, len);
        }
        ns->m_ptr[len] = '\0';
    }

    return mrb_value::wrap(ns);
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

void mrb_state::destroy()
{
    MemManager &mm(gc());

    mrb_core_final(this);

    /* free */
    mrb_gc_free_gv(this);
    mrb_free_context(this,this->root_c);
    mrb_symtbl_free(this);
    mm.mrb_heap_free();
    mm.mrb_alloca_free();
#ifndef MRB_GC_FIXED_ARENA
    mm._free(mm.m_arena);
#endif
    mm._free(this);
}

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
    return mrb_value::wrap(mrb->top_self);
}
