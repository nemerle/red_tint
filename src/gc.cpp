/*
** gc.c - garbage collector for mruby
**
** See Copyright Notice in mruby.h
*/

#ifndef SIZE_MAX
/* Some versions of VC++
  * has SIZE_MAX in stdint.h
  */
# include <limits.h>
#endif
#include <string.h>
#include "mruby.h"
#include "mruby/array.h"
#include "mruby/class.h"
#include "mruby/data.h"
#include "mruby/hash.h"
#include "mruby/proc.h"
#include "mruby/range.h"
#include "mruby/string.h"
#include "mruby/variable.h"

/*
  = Tri-color Incremental Garbage Collection

  mruby's GC is Tri-color Incremental GC with Mark & Sweep.
  Algorithm details are omitted.
  Instead, the part about the implementation described below.

  == Object's Color

  Each object to be painted in three colors.

    * White - Unmarked.
    * Gray - Marked, But the child objects are unmarked.
    * Black - Marked, the child objects are also marked.

  == Two white part

  The white has a different part of A and B.
  In sweep phase, the sweep target white is either A or B.
  The sweep target white is switched just before sweep phase.
  e.g. A -> B -> A -> B ...

  All objects are painted white when allocated.
  This white is another the sweep target white.
  For example, if the sweep target white is A, it's B.
  So objects when allocated in sweep phase will be next sweep phase target.
  Therefore, these objects will not be released accidentally in sweep phase.

  == Execution Timing

  GC Execution Time and Each step interval are decided by live objects count.
  List of Adjustment API:

    * gc_interval_ratio_set
    * gc_step_ratio_set

  For details, see the comments for each function.

  = Write Barrier

  mruby implementer, C extension library writer must write a write
  barrier when writing a pointer to an object on object's field.
  Two different write barrier:

    * mrb_field_write_barrier
    * mrb_write_barrier

  For details, see the comments for each function.

*/

struct free_obj {
    RBasic z;
    RBasic *next;
};

struct RVALUE {
    union {
        free_obj free;
        RBasic basic;
        RObject object;
        RClass klass;
        RString string;
        RArray array;
        RHash hash;
        RRange range;
        RData data;
        RProc proc;
    } as;
};

#ifdef GC_PROFILE
#include <stdio.h>
#include <sys/time.h>

static double program_invoke_time = 0;
static double gc_time = 0;
static double gc_total_time = 0;

static double
gettimeofday_time(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec * 1e-6;
}

#define GC_INVOKE_TIME_REPORT(with) do {\
    fprintf(stderr, "%s\n", with);\
    fprintf(stderr, "gc_invoke: %19.3f\n", gettimeofday_time() - program_invoke_time);\
    fprintf(stderr, "is_generational: %d\n", is_generational(mrb));\
    fprintf(stderr, "is_major_gc: %d\n", is_major_gc(mrb));\
    } while(0)

#define GC_TIME_START do {\
    gc_time = gettimeofday_time();\
    } while(0)

#define GC_TIME_STOP_AND_REPORT do {\
    gc_time = gettimeofday_time() - gc_time;\
    gc_total_time += gc_time;\
    fprintf(stderr, "gc_state: %d\n", mrb->gc_state);\
    fprintf(stderr, "live: %d\n", mrb->live);\
    fprintf(stderr, "majorgc_old_threshold: %d\n", mrb->majorgc_old_threshold);\
    fprintf(stderr, "gc_threshold: %d\n", mrb->gc_threshold);\
    fprintf(stderr, "gc_time: %30.20f\n", gc_time);\
    fprintf(stderr, "gc_total_time: %30.20f\n\n", gc_total_time);\
    } while(0)
#else
#define GC_INVOKE_TIME_REPORT(s)
#define GC_TIME_START
#define GC_TIME_STOP_AND_REPORT
#endif

#ifdef GC_DEBUG
#include <assert.h>
#define gc_assert(expect) assert(expect)
#define DEBUG(x) (x)
#else
#define gc_assert(expect) ((void)0)
#define DEBUG(x)
#endif

#define GC_STEP_SIZE 1024

void* MemManager::_realloc(void *p, size_t len)
{
    void *p2;

    p2 = m_allocf(m_mrb, p, len, this->ud);

    if (!p2 && len > 0 && m_heaps) {
        mrb_garbage_collect();
        p2 = m_allocf(m_mrb, p, len, this->ud);
    }

    if (!p2 && len) {
        if (out_of_memory) {
            /* mrb_panic(mrb); */
        }
        else {
            out_of_memory = true;
            mrb_raise(m_mrb, A_RUNTIME_ERROR(m_mrb), "Out of memory");
        }
    }
    else {
        out_of_memory = false;
    }
    return p2;
}

void* MemManager::_malloc(size_t len)
{
    return _realloc(0, len);
}

void* MemManager::_calloc(size_t nelem, size_t len)
{
    void *p = nullptr;
    if (nelem <= SIZE_MAX / len) {
        size_t size = nelem * len;
        p = _realloc(0, size);

        if (p && size > 0)
            memset(p, 0, size);
    }
    return p;
}

void MemManager::_free(void *p)
{
    m_allocf(m_mrb, p, 0, this->ud);
}

#ifndef MRB_HEAP_PAGE_SIZE
#define MRB_HEAP_PAGE_SIZE 1024
#endif

struct heap_page {
    RBasic *freelist;
    heap_page *prev;
    heap_page *next;
    heap_page *free_next;
    heap_page *free_prev;
    mrb_bool old:1;
    RVALUE objects[MRB_HEAP_PAGE_SIZE];
};

void MemManager::link_heap_page(heap_page *page)
{
    page->next = m_heaps;
    if (m_heaps)
        m_heaps->prev = page;
    m_heaps = page;
}

void MemManager::unlink_heap_page(heap_page *page)
{
    if (page->prev)
        page->prev->next = page->next;
    if (page->next)
        page->next->prev = page->prev;
    if (m_heaps == page)
        m_heaps = page->next;
    page->prev = nullptr;
    page->next = nullptr;
}

void MemManager::link_free_heap_page(heap_page *page)
{
    page->free_next = m_free_heaps;
    if (m_free_heaps) {
        m_free_heaps->free_prev = page;
    }
    m_free_heaps = page;
}

void MemManager::unlink_free_heap_page(heap_page *page)
{
    if (page->free_prev)
        page->free_prev->free_next = page->free_next;
    if (page->free_next)
        page->free_next->free_prev = page->free_prev;
    if (m_free_heaps == page)
        m_free_heaps = page->free_next;
    page->free_prev = nullptr;
    page->free_next = nullptr;
}

void MemManager::add_heap()
{
    heap_page *page = (heap_page *)_calloc(1, sizeof(heap_page));
    RVALUE *p, *e;
    RBasic *prev = nullptr;

    for (p = page->objects, e=p+MRB_HEAP_PAGE_SIZE; p<e; p++) {
        p->as.free.z.tt = MRB_TT_FREE;
        p->as.free.next = prev;
        prev = &p->as.basic;
    }
    page->freelist = prev;

    link_heap_page(page);
    link_free_heap_page(page);
}

#define DEFAULT_GC_INTERVAL_RATIO 200
#define DEFAULT_GC_STEP_RATIO 200
#define DEFAULT_MAJOR_GC_INC_RATIO 200
#define is_generational(mrb) ((mrb)->is_generational_gc_mode)
#define is_major_gc(mrb) (is_generational(mrb) && (mrb)->m_gc_full)
#define is_minor_gc(mrb) (is_generational(mrb) && !(mrb)->m_gc_full)

void MemManager::mrb_heap_init()
{
    m_heaps = nullptr;
    m_free_heaps = nullptr;
    add_heap();
    this->gc_interval_ratio = DEFAULT_GC_INTERVAL_RATIO;
    this->gc_step_ratio = DEFAULT_GC_STEP_RATIO;
    this->is_generational_gc_mode = TRUE;
    m_gc_full = true;

#ifdef GC_PROFILE
    program_invoke_time = gettimeofday_time();
#endif
}

void MemManager::mrb_heap_free()
{
    heap_page *page = m_heaps;
    RVALUE *p, *e;

    while (page) {
        heap_page *tmp = page;
        page = page->next;
        for (p = tmp->objects, e=p+MRB_HEAP_PAGE_SIZE; p<e; p++) {
            if (p->as.free.z.tt != MRB_TT_FREE)
                obj_free(&p->as.basic);
        }
        _free(tmp);
    }
}

void MemManager::gc_protect(RBasic *p)
{
    if (this->arena_idx >= MRB_ARENA_SIZE) {
        /* arena overflow error */
        this->arena_idx = MRB_ARENA_SIZE - 4; /* force room in arena */
        mrb_raise(m_mrb, A_RUNTIME_ERROR(m_mrb), "arena overflow error");
    }
    m_arena[this->arena_idx++] = p;
}

void mrb_gc_protect(mrb_state *mrb, mrb_value obj)
{
    if (mrb_special_const_p(obj))
        return;
    mrb->gc().gc_protect(mrb_basic_ptr(obj));
}

RBasic* MemManager::mrb_obj_alloc(enum mrb_vtype ttype, RClass *cls)
{
    static constexpr RVALUE RVALUE_zero  { {{{MRB_TT_FALSE},0}} };

#ifdef MRB_GC_STRESS
    mrb_garbage_collect(mrb);
#endif
    if (this->gc_threshold < m_live) {
        this->mrb_incremental_gc();
    }
    if (m_free_heaps == nullptr) {
        add_heap();
    }

    RBasic *p(m_free_heaps->freelist);
    m_free_heaps->freelist = ((free_obj*)p)->next;
    if (m_free_heaps->freelist == nullptr) {
        unlink_free_heap_page(m_free_heaps);
    }

    m_live++;
    gc_protect(p);
    *(RVALUE *)p = RVALUE_zero;
    p->tt = ttype;
    p->c = cls;
    paint_partial_white(this, p);
    return p;
}

void MemManager::mark_children(RBasic *obj)
{
    gc_assert(obj->is_gray());
    obj->paint_black();
    m_gray_list = obj->gcnext;
    mark(obj->c);
    switch (obj->tt) {
        case MRB_TT_ICLASS:
            mark(((RClass*)obj)->super);
            break;

        case MRB_TT_CLASS:
        case MRB_TT_MODULE:
        case MRB_TT_SCLASS:
        {
            RClass *c = (RClass*)obj;

            c->mark_mt(m_mrb->gc());
            mark(c->super);
        }
            /* fall through */

        case MRB_TT_OBJECT:
        case MRB_TT_DATA:
            mrb_gc_mark_iv(m_mrb, (RObject*)obj);
            break;

        case MRB_TT_PROC:
        {
            RProc *p = (RProc*)obj;

            mark(p->env);
            mark(p->target_class);
        }
            break;

        case MRB_TT_ENV:
        {
            REnv *e = (REnv*)obj;

            if (e->cioff < 0) {
                int i, len;

                len = (int)e->flags;
                for (i=0; i<len; i++) {
                    mrb_gc_mark_value(m_mrb, e->stack[i]);
                }
            }
        }
            break;

        case MRB_TT_ARRAY:
        {
            RArray *a = (RArray*)obj;
            size_t i, e;

            for (i=0,e=a->m_len; i<e; i++) {
                mrb_gc_mark_value(m_mrb, a->m_ptr[i]);
            }
        }
            break;

        case MRB_TT_HASH:
            mrb_gc_mark_iv(m_mrb, (RObject*)obj);
            mrb_gc_mark_hash(m_mrb, (RHash*)obj);
            break;

        case MRB_TT_STRING:
            break;

        case MRB_TT_RANGE:
        {
            RRange *r((RRange*)obj);

            if (r->edges) {
                mrb_gc_mark_value(m_mrb, r->edges->beg);
                mrb_gc_mark_value(m_mrb, r->edges->end);
            }
        }
            break;

        default:
            break;
    }
}

void MemManager::mark(RBasic *obj)
{
    if (obj == 0)
        return;
    if (!obj->is_white())
        return;
    gc_assert((obj)->tt != MRB_TT_FREE);
    add_gray_list(obj);
}

void MemManager::obj_free(RBasic *obj)
{
    DEBUG(printf("obj_free(%p,tt=%d)\n",obj,obj->tt));
    switch (obj->tt) {
        /* immediate - no mark */
        case MRB_TT_TRUE:
        case MRB_TT_FIXNUM:
        case MRB_TT_SYMBOL:
        case MRB_TT_FLOAT:
            /* cannot happen */
            return;

        case MRB_TT_OBJECT:
            mrb_gc_free_iv(m_mrb, (RObject*)obj);
            break;

        case MRB_TT_CLASS:
        case MRB_TT_MODULE:
        case MRB_TT_SCLASS:
            mrb_gc_free_mt(m_mrb, (RClass*)obj);
            mrb_gc_free_iv(m_mrb, (RObject*)obj);
            break;

        case MRB_TT_ENV:
        {
            REnv *e = (REnv*)obj;

            if (e->cioff < 0) {
                _free(e->stack);
                e->stack = 0;
            }
        }
            break;

        case MRB_TT_ARRAY:
            if (obj->flags & MRB_ARY_SHARED)
                mrb_ary_decref(m_mrb, ((RArray*)obj)->m_aux.shared);
            else
                _free(((RArray*)obj)->m_ptr);
            break;

        case MRB_TT_HASH:
            mrb_gc_free_iv(m_mrb, (RObject*)obj);
            mrb_gc_free_hash(m_mrb, (RHash*)obj);
            break;

        case MRB_TT_STRING:
            mrb_gc_free_str(m_mrb, (RString*)obj);
            break;

        case MRB_TT_RANGE:
            _free(((RRange*)obj)->edges);
            break;

        case MRB_TT_DATA:
        {
            RData *d = (RData*)obj;
            if (d->type->dfree) {
                d->type->dfree(m_mrb, d->data);
            }
            mrb_gc_free_iv(m_mrb, (RObject*)obj);
        }
            break;

        default:
            break;
    }
    obj->tt = MRB_TT_FREE;
}

void MemManager::root_scan_phase()
{
    size_t i;
    size_t e;
    mrb_callinfo *ci;

    if (!is_minor_gc(this)) {
        m_gray_list = 0;
        this->variable_gray_list = 0;
    }

    mrb_gc_mark_gv(m_mrb);
    /* mark arena */
    for (i=0,e=this->arena_idx; i<e; i++) {
        mark(m_arena[i]);
    }

    mark(m_mrb->object_class); /* mark class hierarchy */
    mark(m_mrb->top_self); /* mark top_self */
    mark(m_mrb->m_exc); /* mark exception */

    /* mark stack */
    e = m_mrb->m_stack - m_mrb->m_stbase;
    if (m_mrb->m_ci)
        e += m_mrb->m_ci->nregs;
    if (m_mrb->m_stbase + e > m_mrb->stend)
        e = m_mrb->stend - m_mrb->m_stbase;
    for (i=0; i<e; i++) {
        mrb_gc_mark_value(m_mrb, m_mrb->m_stbase[i]);
    }
    /* mark ensure stack */
    e = (m_mrb->m_ci) ? m_mrb->m_ci->eidx : 0;
    for (i=0; i<e; i++) {
        mark(m_mrb->m_ensure[i]);
    }
    /* mark closure */
    for (ci = m_mrb->cibase; ci <= m_mrb->m_ci; ci++) {
        if (!ci)
            continue;
        mark(ci->env);
        mark(ci->proc);
        mark(ci->target_class);
    }
    if (nullptr==m_mrb->m_irep)
        return;

    /* mark irep pool */
    size_t len = m_mrb->irep_len;
    if (len > m_mrb->irep_capa) len = m_mrb->irep_capa;
    for (i=0; i<len; i++) {
        mrb_irep *irep = m_mrb->m_irep[i];
        if (!irep)
            continue;
        for (int j=0; j<irep->plen; j++) {
            mrb_gc_mark_value(m_mrb, irep->pool[j]);
        }
    }
}

size_t MemManager::gc_gray_mark(RBasic *obj)
{
    size_t children = 0;

    mark_children(obj);

    switch (obj->tt) {
        case MRB_TT_ICLASS:
            children++;
            break;

        case MRB_TT_CLASS:
        case MRB_TT_SCLASS:
        case MRB_TT_MODULE:
        {
            RClass *c = (RClass*)obj;

            children += mrb_gc_mark_iv_size(m_mrb, c);
            children += c->mark_mt_size(m_mrb);
            children++;
        }
            break;

        case MRB_TT_OBJECT:
        case MRB_TT_DATA:
            children += mrb_gc_mark_iv_size(m_mrb, (RObject*)obj);
            break;

        case MRB_TT_ENV:
            children += (int)obj->flags;
            break;

        case MRB_TT_ARRAY:
        {
            RArray *a = (RArray*)obj;
            children += a->m_len;
        }
            break;

        case MRB_TT_HASH:
            children += mrb_gc_mark_iv_size(m_mrb, (RObject*)obj);
            children += mrb_gc_mark_hash_size(m_mrb, (RHash*)obj);
            break;

        case MRB_TT_PROC:
        case MRB_TT_RANGE:
            children+=2;
            break;

        default:
            break;
    }
    return children;
}

size_t MemManager::incremental_marking_phase(size_t limit)
{
    size_t tried_marks = 0;

    while (m_gray_list && tried_marks < limit) {
        tried_marks += gc_gray_mark(m_gray_list);
    }

    return tried_marks;
}

void MemManager::final_marking_phase()
{
    while (m_gray_list) {
        if (m_gray_list->is_gray())
            mark_children(m_gray_list);
        else
            m_gray_list = m_gray_list->gcnext;
    }
    gc_assert(m_gray_list == nullptr);
    m_gray_list = this->variable_gray_list;
    this->variable_gray_list = 0;
    while (m_gray_list) {
        if (m_gray_list->is_gray())
            mark_children(m_gray_list);
        else
            m_gray_list = m_gray_list->gcnext;
    }
    gc_assert(m_gray_list == nullptr);
}

void MemManager::prepare_incremental_sweep()
{
    m_gc_state = GC_STATE_SWEEP;
    this->sweeps = m_heaps;
    m_gc_live_after_mark = m_live;
}

size_t MemManager::incremental_sweep_phase(size_t limit)
{
    heap_page *page = this->sweeps;
    size_t tried_sweep = 0;

    while (page && (tried_sweep < limit)) {
        RVALUE *p = page->objects;
        RVALUE *e = p + MRB_HEAP_PAGE_SIZE;
        size_t freed = 0;
        int dead_slot = 1;
        int full = (page->freelist == nullptr);

        if (is_minor_gc(this) && page->old) {
            /* skip a slot which doesn't contain any young object */
            p = e;
            dead_slot = 0;
        }
        while (p<e) {
            if (is_dead(this, &p->as.basic)) {
                if (p->as.basic.tt != MRB_TT_FREE) {
                    obj_free(&p->as.basic);
                    p->as.free.next = page->freelist;
                    page->freelist = (RBasic*)p;
                    freed++;
                }
            }
            else {
                if (!is_generational(this))
                    paint_partial_white(this, &p->as.basic); /* next gc target */
                dead_slot = 0;
            }
            p++;
        }

        /* free dead slot */
        if (dead_slot && freed < MRB_HEAP_PAGE_SIZE) {
            heap_page *next = page->next;

            unlink_heap_page(page);
            unlink_free_heap_page(page);
            _free(page);
            page = next;
        }
        else {
            if (full && freed > 0) {
                link_free_heap_page(page);
            }
            if (page->freelist == NULL && is_minor_gc(this))
                page->old = TRUE;
            else
                page->old = FALSE;
            page = page->next;
        }
        tried_sweep += MRB_HEAP_PAGE_SIZE;
        m_live -= freed;
        m_gc_live_after_mark -= freed;
    }
    this->sweeps = page;
    return tried_sweep;
}

size_t MemManager::incremental_gc(size_t limit)
{
    switch (m_gc_state) {
        case GC_STATE_NONE:
            root_scan_phase();
            m_gc_state = GC_STATE_MARK;
            flip_white_part(this);
            return 0;
        case GC_STATE_MARK:
            if (m_gray_list) {
                return incremental_marking_phase(limit);
            }
            else {
                final_marking_phase();
                prepare_incremental_sweep();
                return 0;
            }
        case GC_STATE_SWEEP: {
            size_t tried_sweep = 0;
            tried_sweep = incremental_sweep_phase(limit);
            if (tried_sweep == 0)
                m_gc_state = GC_STATE_NONE;
            return tried_sweep;
        }
        default:
            /* unknown state */
            gc_assert(0);
            return 0;
    }
}

void MemManager::advance_phase(gc_state to_state)
{
    while (m_gc_state != to_state) {
        incremental_gc(~0);
    }
}

void MemManager::clear_all_old()
{
    size_t origin_mode = this->is_generational_gc_mode;

    gc_assert(is_generational(this));
    if (is_major_gc(this)) {
        advance_phase(GC_STATE_NONE);
    }

    this->is_generational_gc_mode = FALSE;
    prepare_incremental_sweep();
    advance_phase(GC_STATE_NONE);
    this->variable_gray_list = m_gray_list = NULL;
    this->is_generational_gc_mode = origin_mode;
}

void MemManager::mrb_incremental_gc()
{
    if (m_gc_disabled)
        return;

    GC_INVOKE_TIME_REPORT("mrb_incremental_gc()");
    GC_TIME_START;

    if (is_minor_gc(this)) {
        do {
            incremental_gc(~0);
        } while (m_gc_state != GC_STATE_NONE);
    }
    else {
        size_t limit = 0, result = 0;
        limit = (GC_STEP_SIZE/100) * this->gc_step_ratio;
        while (result < limit) {
            result += incremental_gc(limit);
            if (m_gc_state == GC_STATE_NONE)
                break;
        }
    }

    if (m_gc_state == GC_STATE_NONE) {
        gc_assert(m_live >= m_gc_live_after_mark);
        this->gc_threshold = (m_gc_live_after_mark/100) * this->gc_interval_ratio;
        if (this->gc_threshold < GC_STEP_SIZE) {
            this->gc_threshold = GC_STEP_SIZE;
        }
        if (is_major_gc(this)) {
            m_majorgc_old_threshold = m_gc_live_after_mark/100 * DEFAULT_MAJOR_GC_INC_RATIO;
            m_gc_full = FALSE;
        }
        else if (is_minor_gc(this)) {
            if (m_live > m_majorgc_old_threshold) {
                clear_all_old();
                m_gc_full = true;
            }
        }
    }
    else {
        this->gc_threshold = m_live + GC_STEP_SIZE;
    }
    GC_TIME_STOP_AND_REPORT;
}

void MemManager::mrb_garbage_collect()
{
    size_t max_limit = ~0;

    if (m_gc_disabled)
        return;
    GC_INVOKE_TIME_REPORT("mrb_garbage_collect()");
    GC_TIME_START;

    if (m_gc_state == GC_STATE_SWEEP) {
        /* finish sweep phase */
        while (m_gc_state != GC_STATE_NONE) {
            incremental_gc(max_limit);
        }
    }

    /* clean all black object as old */
    if (is_generational(this)) {
        clear_all_old();
        m_gc_full = true;
    }

    do {
        incremental_gc(max_limit);
    } while (m_gc_state != GC_STATE_NONE);

    this->gc_threshold = (m_gc_live_after_mark/100) * this->gc_interval_ratio;

    if (is_generational(this)) {
        m_majorgc_old_threshold = m_gc_live_after_mark/100 * DEFAULT_MAJOR_GC_INC_RATIO;
        m_gc_full = false;
    }

    GC_TIME_STOP_AND_REPORT;
}

int MemManager::arena_save()
{
    return this->arena_idx;
}

void MemManager::arena_restore(int idx)
{
    this->arena_idx = idx;
}

/*
 * Field write barrier
 *   Paint obj(Black) -> value(White) to obj(Black) -> value(Gray).
 */

void MemManager::mrb_field_write_barrier(RBasic *obj, RBasic *value)
{
    if (!obj->is_black()) return;
    if (!value->is_white()) return;

    gc_assert(!is_dead(this, value) && !is_dead(this, obj));
    gc_assert(is_generational(this) || m_gc_state != GC_STATE_NONE);

    if (is_generational(this) || m_gc_state == GC_STATE_MARK) {
        add_gray_list(value);
    }
    else {
        gc_assert(m_gc_state == GC_STATE_SWEEP);
        paint_partial_white(this, obj); /* for never write barriers */
    }
}

/*
 * Write barrier
 *   Paint obj(Black) to obj(Gray).
 *
 *   The object that is painted gray will be traversed atomically in final
 *   mark phase. So you use this write barrier if it's frequency written spot.
 *   e.g. Set element on Array.
 */

void MemManager::mrb_write_barrier(RBasic *obj)
{
    if (!obj->is_black()) return;

    gc_assert(!is_dead(this, obj));
    gc_assert(is_generational(this) || m_gc_state != GC_STATE_NONE);
    obj->paint_gray();
    obj->gcnext = this->variable_gray_list;
    this->variable_gray_list = obj;
}

/*
 *  call-seq:
 *     GC.start                     -> nil
 *
 *  Initiates full garbage collection.
 *
 */
static mrb_value gc_start(mrb_state *mrb, mrb_value obj)
{
    mrb->gc().mrb_garbage_collect();
    return mrb_nil_value();
}

/*
 *  call-seq:
 *     GC.enable    -> true or false
 *
 *  Enables garbage collection, returning <code>true</code> if garbage
 *  collection was previously disabled.
 *
 *     GC.disable   #=> false
 *     GC.enable    #=> true
 *     GC.enable    #=> false
 *
 */

static mrb_value gc_enable(mrb_state *mrb, mrb_value obj)
{
    bool old = mrb->gc().gc_disabled(false);
    return mrb_bool_value(old);
}

/*
 *  call-seq:
 *     GC.disable    -> true or false
 *
 *  Disables garbage collection, returning <code>true</code> if garbage
 *  collection was already disabled.
 *
 *     GC.disable   #=> false
 *     GC.disable   #=> true
 *
 */

static mrb_value gc_disable(mrb_state *mrb, mrb_value obj)
{
    bool old = mrb->gc().gc_disabled(true);
    return mrb_bool_value(old);
}

/*
 *  call-seq:
 *     GC.interval_ratio      -> fixnum
 *
 *  Returns ratio of GC interval. Default value is 200(%).
 *
 */

static mrb_value gc_interval_ratio_get(mrb_state *mrb, mrb_value obj)
{
    return mrb_fixnum_value(mrb->gc().interval_ratio());
}

/*
 *  call-seq:
 *     GC.interval_ratio = fixnum    -> nil
 *
 *  Updates ratio of GC interval. Default value is 200(%).
 *  GC start as soon as after end all step of GC if you set 100(%).
 *
 */

static mrb_value gc_interval_ratio_set(mrb_state *mrb, mrb_value obj)
{
    mrb_int ratio;

    mrb_get_args(mrb, "i", &ratio);
    mrb->gc().interval_ratio(ratio);
    return mrb_nil_value();
}

/*
 *  call-seq:
 *     GC.step_ratio    -> fixnum
 *
 *  Returns step span ratio of Incremental GC. Default value is 200(%).
 *
 */

static mrb_value
gc_step_ratio_get(mrb_state *mrb, mrb_value obj)
{
    return mrb_fixnum_value(mrb->gc().step_ratio());
}

/*
 *  call-seq:
 *     GC.step_ratio = fixnum   -> nil
 *
 *  Updates step span ratio of Incremental GC. Default value is 200(%).
 *  1 step of incrementalGC becomes long if a rate is big.
 *
 */

static mrb_value gc_step_ratio_set(mrb_state *mrb, mrb_value obj)
{
    mrb_int ratio;

    mrb_get_args(mrb, "i", &ratio);
    mrb->gc().step_ratio(ratio);
    return mrb_nil_value();
}

void MemManager::change_gen_gc_mode(mrb_int enable)
{
    if (is_generational(this) && !enable) {
        clear_all_old();
        gc_assert(m_gc_state == GC_STATE_NONE);
        m_gc_full = FALSE;
    }
    else if (!is_generational(this) && enable) {
        advance_phase(GC_STATE_NONE);
        m_majorgc_old_threshold = m_gc_live_after_mark/100 * DEFAULT_MAJOR_GC_INC_RATIO;
        m_gc_full = false;
    }
    this->is_generational_gc_mode = enable;
}

/*
 *  call-seq:
 *     GC.generational_mode -> true or false
 *
 *  Returns generational or normal gc mode.
 *
 */

static mrb_value
gc_generational_mode_get(mrb_state *mrb, mrb_value self)
{
    return mrb_bool_value(mrb->gc().generational_gc_mode());
}

/*
 *  call-seq:
 *     GC.generational_mode = true or false -> true or false
 *
 *  Changes to generational or normal gc mode.
 *
 */

static mrb_value
gc_generational_mode_set(mrb_state *mrb, mrb_value self)
{
    mrb_bool enable;

    mrb_get_args(mrb, "b", &enable);
    if (mrb->gc().generational_gc_mode() != enable)
        mrb->gc().change_gen_gc_mode(enable);

    return mrb_bool_value(enable);
}

#ifdef GC_TEST
#ifdef GC_DEBUG
static mrb_value gc_test(mrb_state *, mrb_value);
#endif
#endif

void
mrb_init_gc(mrb_state *mrb)
{
    mrb->define_module("GC").
            define_class_method(mrb, "start", gc_start, MRB_ARGS_NONE()).
            define_class_method(mrb, "enable", gc_enable, MRB_ARGS_NONE()).
            define_class_method(mrb, "disable", gc_disable, MRB_ARGS_NONE()).
            define_class_method(mrb, "interval_ratio", gc_interval_ratio_get, MRB_ARGS_NONE()).
            define_class_method(mrb, "interval_ratio=", gc_interval_ratio_set, MRB_ARGS_REQ(1)).
            define_class_method(mrb, "step_ratio", gc_step_ratio_get, MRB_ARGS_NONE()).
            define_class_method(mrb, "step_ratio=", gc_step_ratio_set, MRB_ARGS_REQ(1)).
            define_class_method(mrb, "generational_mode=", gc_generational_mode_set, MRB_ARGS_REQ(1)).
            define_class_method(mrb, "generational_mode", gc_generational_mode_get, MRB_ARGS_NONE())
        #ifndef GC_TEST
            ;
#else
            .define_class_method(mrb, "test", gc_test, ARGS_NONE());
#ifdef GC_DEBUG
    mrb_define_class_method(mrb, gc, "test", gc_test, ARGS_NONE());
#endif
#endif
}

#ifdef GC_TEST
#ifdef GC_DEBUG
void
test_mrb_field_write_barrier(void)
{
    mrb_state *mrb = mrb_open();
    struct RBasic *obj, *value;

    puts("test_mrb_field_write_barrier");
    mrb->is_generational_gc_mode = FALSE;
    obj = mrb_basic_ptr(mrb_ary_new(mrb));
    value = mrb_basic_ptr(mrb_str_new_cstr(mrb, "value"));
    paint_black(obj);
    paint_partial_white(mrb,value);


    puts("  in GC_STATE_MARK");
    mrb->gc_state = GC_STATE_MARK;
    mrb_field_write_barrier(mrb, obj, value);

    gc_assert(is_gray(value));


    puts("  in GC_STATE_SWEEP");
    paint_partial_white(mrb,value);
    mrb->gc_state = GC_STATE_SWEEP;
    mrb_field_write_barrier(mrb, obj, value);

    gc_assert(obj->color & mrb->current_white_part);
    gc_assert(value->color & mrb->current_white_part);


    puts("  fail with black");
    mrb->gc_state = GC_STATE_MARK;
    paint_white(obj);
    paint_partial_white(mrb,value);
    mrb_field_write_barrier(mrb, obj, value);

    gc_assert(obj->color & mrb->current_white_part);


    puts("  fail with gray");
    mrb->gc_state = GC_STATE_MARK;
    paint_black(obj);
    paint_gray(value);
    mrb_field_write_barrier(mrb, obj, value);

    gc_assert(is_gray(value));


    {
        puts("test_mrb_field_write_barrier_value");
        obj = mrb_basic_ptr(mrb_ary_new(mrb));
        mrb_value value = mrb_str_new_cstr(mrb, "value");
        paint_black(obj);
        paint_partial_white(mrb, mrb_basic_ptr(value));

        mrb->gc_state = GC_STATE_MARK;
        mrb_field_write_barrier_value(mrb, obj, value);

        gc_assert(is_gray(mrb_basic_ptr(value)));
    }

    mrb_close(mrb);
}

void
test_mrb_write_barrier(void)
{
    mrb_state *mrb = mrb_open();
    struct RBasic *obj;

    puts("test_mrb_write_barrier");
    obj = mrb_basic_ptr(mrb_ary_new(mrb));
    paint_black(obj);

    puts("  in GC_STATE_MARK");
    mrb->gc_state = GC_STATE_MARK;
    mrb_write_barrier(mrb, obj);

    gc_assert(is_gray(obj));
    gc_assert(mrb->variable_gray_list == obj);


    puts("  fail with gray");
    paint_gray(obj);
    mrb_write_barrier(mrb, obj);

    gc_assert(is_gray(obj));

    mrb_close(mrb);
}

void
test_add_gray_list(void)
{
    mrb_state *mrb = mrb_open();
    struct RBasic *obj1, *obj2;

    puts("test_add_gray_list");
    change_gen_gc_mode(mrb, FALSE);
    gc_assert(mrb->gray_list == NULL);
    obj1 = mrb_basic_ptr(mrb_str_new_cstr(mrb, "test"));
    add_gray_list(mrb, obj1);
    gc_assert(mrb->gray_list == obj1);
    gc_assert(is_gray(obj1));

    obj2 = mrb_basic_ptr(mrb_str_new_cstr(mrb, "test"));
    add_gray_list(mrb, obj2);
    gc_assert(mrb->gray_list == obj2);
    gc_assert(mrb->gray_list->gcnext == obj1);
    gc_assert(is_gray(obj2));

    mrb_close(mrb);
}

void
test_gc_gray_mark(void)
{
    mrb_state *mrb = mrb_open();
    mrb_value obj_v, value_v;
    struct RBasic *obj;
    size_t gray_num = 0;

    puts("test_gc_gray_mark");

    puts("  in MRB_TT_CLASS");
    obj = (struct RBasic*)mrb->object_class;
    paint_gray(obj);
    gray_num = gc_gray_mark(mrb, obj);
    gc_assert(is_black(obj));
    gc_assert(gray_num > 1);

    puts("  in MRB_TT_ARRAY");
    obj_v = mrb_ary_new(mrb);
    value_v = mrb_str_new_cstr(mrb, "test");
    paint_gray(mrb_basic_ptr(obj_v));
    paint_partial_white(mrb, mrb_basic_ptr(value_v));
    mrb_ary_push(mrb, obj_v, value_v);
    gray_num = gc_gray_mark(mrb, mrb_basic_ptr(obj_v));
    gc_assert(is_black(mrb_basic_ptr(obj_v)));
    gc_assert(is_gray(mrb_basic_ptr(value_v)));
    gc_assert(gray_num == 1);

    mrb_close(mrb);
}

void
test_incremental_gc(void)
{
    mrb_state *mrb = mrb_open();
    size_t max = ~0, live = 0, total = 0, freed = 0;
    RVALUE *free;
    struct heap_page *page;

    puts("test_incremental_gc");
    change_gen_gc_mode(mrb, FALSE);

    puts("  in mrb_garbage_collect");
    mrb_garbage_collect(mrb);

    gc_assert(mrb->gc_state == GC_STATE_NONE);
    puts("  in GC_STATE_NONE");
    incremental_gc(mrb, max);
    gc_assert(mrb->gc_state == GC_STATE_MARK);
    puts("  in GC_STATE_MARK");
    advance_phase(mrb, GC_STATE_SWEEP);
    gc_assert(mrb->gc_state == GC_STATE_SWEEP);

    puts("  in GC_STATE_SWEEP");
    page = mrb->heaps;
    while (page) {
        RVALUE *p = page->objects;
        RVALUE *e = p + MRB_HEAP_PAGE_SIZE;
        while (p<e) {
            if (is_black(&p->as.basic)) {
                live++;
            }
            if (is_gray(&p->as.basic) && !is_dead(mrb, &p->as.basic)) {
                printf("%p\n", &p->as.basic);
            }
            p++;
        }
        page = page->next;
        total += MRB_HEAP_PAGE_SIZE;
    }

    gc_assert(mrb->gray_list == NULL);

    incremental_gc(mrb, max);
    gc_assert(mrb->gc_state == GC_STATE_SWEEP);

    incremental_gc(mrb, max);
    gc_assert(mrb->gc_state == GC_STATE_NONE);

    free = (RVALUE*)mrb->heaps->freelist;
    while (free) {
        freed++;
        free = (RVALUE*)free->as.free.next;
    }

    gc_assert(mrb->live == live);
    gc_assert(mrb->live == total-freed);

    puts("test_incremental_gc(gen)");
    advance_phase(mrb, GC_STATE_SWEEP);
    change_gen_gc_mode(mrb, TRUE);

    gc_assert(mrb->gc_full == FALSE);
    gc_assert(mrb->gc_state == GC_STATE_NONE);

    puts("  in minor");
    gc_assert(is_minor_gc(mrb));
    gc_assert(mrb->majorgc_old_threshold > 0);
    mrb->majorgc_old_threshold = 0;
    mrb_incremental_gc(mrb);
    gc_assert(mrb->gc_full == TRUE);
    gc_assert(mrb->gc_state == GC_STATE_NONE);

    puts("  in major");
    gc_assert(is_major_gc(mrb));
    do {
        mrb_incremental_gc(mrb);
    } while (mrb->gc_state != GC_STATE_NONE);
    gc_assert(mrb->gc_full == FALSE);

    mrb_close(mrb);
}

void
test_incremental_sweep_phase(void)
{
    mrb_state *mrb = mrb_open();

    puts("test_incremental_sweep_phase");

    add_heap(mrb);
    mrb->sweeps = mrb->heaps;

    gc_assert(mrb->heaps->next->next == NULL);
    gc_assert(mrb->free_heaps->next->next == NULL);
    incremental_sweep_phase(mrb, MRB_HEAP_PAGE_SIZE*3);

    gc_assert(mrb->heaps->next == NULL);
    gc_assert(mrb->heaps == mrb->free_heaps);

    mrb_close(mrb);
}

static mrb_value
gc_test(mrb_state *mrb, mrb_value self)
{
    test_mrb_field_write_barrier();
    test_mrb_write_barrier();
    test_add_gray_list();
    test_gc_gray_mark();
    test_incremental_gc();
    test_incremental_sweep_phase();
    return mrb_nil_value();
}
#endif
#endif
