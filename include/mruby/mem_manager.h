#pragma once
#include <cstdlib>
#include <memory>
#include <limits>
struct RBasic;
struct RClass;
struct RProc;
struct REnv;
struct mrb_state;
struct mrb_pool;
struct heap_page;
struct mrb_context;

typedef void* (*mrb_allocf) (mrb_state *mrb, void*, size_t, void *ud);
#ifndef MRB_GC_ARENA_SIZE
#define MRB_GC_ARENA_SIZE 100
#endif

struct MemManager {
    enum gc_state {
        GC_STATE_NONE = 0,
        GC_STATE_MARK,
        GC_STATE_SWEEP
    };
    friend struct mrb_state;
public:
    void mrb_full_gc();
    void mrb_incremental_gc();
    void change_gen_gc_mode(mrb_int enable);

            template<typename T>
    T *     obj_alloc(RClass *cls) {
                T *res=(T *)mrb_obj_alloc(T::ttype,cls);
                res->m_vm = m_vm;
                return res;
            }
            template<typename T>
    T *     obj_alloc(mrb_vtype type,RClass *cls) {
                T *res=(T *)mrb_obj_alloc(type,cls);
                res->m_vm = m_vm;
                return res;
            }
            template<typename T, typename... Args >
    T *     new_t(Args... args) {
                return new(_malloc(sizeof(T))) T(args...);
            }
            template<typename T, typename... Args >
    T *     new_ta(size_t sz) {
                return new(_malloc(sizeof(T))) T[sz];
            }

    RBasic *mrb_obj_alloc(mrb_vtype ttype, RClass *cls);
    void *  _calloc(size_t nelem, size_t len);
    void *  _realloc(void *p, size_t len);
    void    _free(void *p);
    void *  _malloc(size_t len);
    void *  mrb_malloc_simple(size_t);
    void *  mrb_realloc_simple(void *ptr, size_t len);
    void    unlink_heap_page(heap_page *page);
    void    link_free_heap_page(heap_page *page);
    void    mrb_heap_init();
    void    mrb_heap_free();
    void    gc_protect(RBasic *p);
    void    mark_children(RBasic *obj);
    void    mark(RBasic *obj);
    int     arena_save();
    void    arena_restore(int idx);
    void    mrb_field_write_barrier(RBasic *obj, RBasic *value);
    void    mrb_write_barrier(RBasic *obj);
    mrb_pool *mrb_pool_open();
    void *  mrb_alloca(size_t size);
    void    mrb_alloca_free();
    bool    gc_disabled(bool v) { bool res=m_gc_disabled; m_gc_disabled=v; return res;}
    int     interval_ratio() const {return gc_interval_ratio; }
    void    interval_ratio(int v) {gc_interval_ratio=v; }
    int     step_ratio() const { return gc_step_ratio; }
    void    step_ratio(int value) { gc_step_ratio = value; }
    bool    generational_gc_mode() const { return is_generational_gc_mode;}
    mrb_state *vm()const {return m_vm;}
protected:
    void    mark_context_stack(mrb_context *ctx);
    void    mark_context(mrb_context *ctx);
    void    root_scan_phase();
    size_t  incremental_sweep_phase(size_t limit);
    void    prepare_incremental_sweep();
    size_t  incremental_marking_phase(size_t limit);
    void    final_marking_phase();
    size_t  gc_gray_mark(RBasic *obj);
    void    add_gray_list(RBasic *obj) {
    #ifdef MRB_GC_STRESS
        if (obj->tt > MRB_TT_MAXDEFINE) {
            abort();
        }
    #endif
        obj->paint_gray();
        obj->gcnext = m_gray_list;
        m_gray_list = obj;
    }
    void obj_free(RBasic *obj);
    void add_heap();
    void unlink_free_heap_page(heap_page *page);
    void link_heap_page(heap_page *page);
    void clear_all_old();
    size_t incremental_gc(size_t limit);
    void incremental_gc_until(gc_state to_state);
    void gc_mark_gray_list();

    mrb_allocf m_allocf;
    void *ud; /* auxiliary data */
    mrb_state * m_vm;
    heap_page * m_heaps;
    heap_page * sweeps;
    heap_page * m_free_heaps;
    size_t      m_live; /* count of live objects */
public:
#ifdef MRB_GC_FIXED_ARENA
    RBasic *    m_arena[MRB_ARENA_SIZE]; /* GC protection array */
#else
    RBasic **   m_arena; /* GC protection array */
    int         arena_capa;
#endif
protected:
    int         arena_idx;

    gc_state    m_gc_state; /* state of gc */
    int         current_white_part; /* make white object by white_part */
    RBasic *    m_gray_list; /* list of gray objects */
    RBasic *    atomic_gray_list; /* list of objects to be traversed atomically */
    size_t      m_gc_live_after_mark;
    size_t      gc_threshold;
    int         gc_interval_ratio;
    int         gc_step_ratio;
    mrb_bool    m_gc_disabled:1;
    mrb_bool    m_gc_full:1;
    mrb_bool    is_generational_gc_mode:1;
    mrb_bool    out_of_memory:1;
    size_t      m_majorgc_old_threshold;
    struct alloca_header *mems;
    void incremental_gc_step();
    size_t mark_irep_pool_size(struct mrb_irep *irep);
    void mark_irep_pool(struct mrb_irep *irep);
};
// helper class to use user passed allocation routine in std containers
template <class T>
class custom_allocator
{
public:
    typedef T value_type;

    typedef T*       pointer;
    typedef const T* const_pointer;

    typedef T&       reference;
    typedef const T& const_reference;

    typedef std::size_t    size_type;
    typedef std::ptrdiff_t difference_type;

    template <class U> struct rebind { typedef custom_allocator <U> other; };

    MemManager* m_state;

    custom_allocator(MemManager *state): m_state(state) {}
    custom_allocator(const custom_allocator& other): m_state(other.m_state) {}
    template <class U> custom_allocator(const custom_allocator<U>& other): m_state(other.m_state) {}

    //------------------------------------------------------------------------------------
    pointer allocate(size_type n, std::allocator<void>::const_pointer=nullptr)
    {
        return m_state->_malloc(n); // not using the passed in hint
    }

    //------------------------------------------------------------------------------------
    void deallocate(pointer p, size_type) { m_state->_free(p); }

    //------------------------------------------------------------------------------------
    size_type max_size() const { return std::numeric_limits<size_type>::max(); }

    //------------------------------------------------------------------------------------
    void construct(pointer p, const_reference val) { ::new(static_cast<void*>(p)) T(val); }
    template <class U> void destroy(U* p) { p->~U(); }
};
