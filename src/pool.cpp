/*
** pool.c - memory pool
**
** See Copyright Notice in mruby.h
*/

#include <cstdlib>
#include <cstddef>
#include <cstring>
#include "mruby.h"

/* configuration section */
/* allocated memory address should be multiple of POOL_ALIGNMENT */
/* or undef it if alignment does not matter */
#ifndef POOL_ALIGNMENT
#define POOL_ALIGNMENT 4
#endif
/* page size of memory pool */
#ifndef POOL_PAGE_SIZE
#define POOL_PAGE_SIZE 16000
#endif
/* end of configuration section */

struct mrb_pool_page {
    mrb_pool_page *next;
    size_t offset;
    size_t len;
    void *last;
    char page[];
};

#undef TEST_POOL
#ifdef TEST_POOL

#define mrb_malloc(m,s) malloc(s)
#define mrb_free(m,p) free(p)
#endif

#ifdef POOL_ALIGNMENT
#  define ALIGN_PADDING(x) ((-x) & (POOL_ALIGNMENT - 1))
#else
#  define ALIGN_PADDING(x) (0)
#endif

mrb_pool* MemManager::mrb_pool_open()
{
    mrb_pool *pool = (mrb_pool *)mrb_malloc_simple(sizeof(mrb_pool));

    if (pool) {
        pool->mrb = m_vm;
        pool->pages = 0;
    }

    return pool;
}

void mrb_pool::mrb_pool_close()
{
    mrb_pool_page *page, *tmp;

    if (!this)
        return;
    page = this->pages;
    while (page) {
        tmp = page;
        page = page->next;
        this->mrb->gc()._free(tmp);
    }
    this->mrb->gc()._free(this);
}

mrb_pool_page* mrb_pool::page_alloc(size_t len)
{
    mrb_pool_page *page;

    if (len < POOL_PAGE_SIZE)
        len = POOL_PAGE_SIZE;
    page = (mrb_pool_page *)mrb->gc().mrb_malloc_simple(sizeof(mrb_pool_page)+len);
    if (page) {
        page->offset = 0;
        page->len = len;
    }
    return page;
}

void* mrb_pool::mrb_pool_alloc(size_t len)
{
    mrb_pool_page *page;
    size_t n;

    if (!this)
        return nullptr;
    len += ALIGN_PADDING(len);
    page = this->pages;
    while (page) {
        if (page->offset + len <= page->len) {
            n = page->offset;
            page->offset += len;
            page->last = (char*)page->page+n;
            return page->last;
        }
        page = page->next;
    }
    page = page_alloc(len);
    if (!page)
        return nullptr;
    page->offset = len;
    page->next = this->pages;
    this->pages = page;

    page->last = (void*)page->page;
    return page->last;
}

mrb_bool mrb_pool::mrb_pool_can_realloc(void *p, size_t len)
{

    if (!this)
        return false;
    len += ALIGN_PADDING(len);
    mrb_pool_page *page = this->pages;
    while (page) {
        if (page->last == p) {
            size_t beg;

            beg = (char*)p - page->page;
            if (beg + len > page->len) return false;
            return true;
        }
        page = page->next;
    }
    return false;
}

void* mrb_pool::mrb_pool_realloc(void *p, size_t oldlen, size_t newlen)
{

    if (!this)
        return 0;
    oldlen += ALIGN_PADDING(oldlen);
    newlen += ALIGN_PADDING(newlen);
    for(mrb_pool_page *page=this->pages; page; page=page->next) {
        if (page->last != p)
            continue;
        size_t beg;

        beg = (char*)p - page->page;
        if (beg + oldlen != page->offset)
            break;
        if (beg + newlen > page->len) {
            page->offset = beg;
            break;
        }
        page->offset = beg + newlen;
        return p;
    }
    void *np = mrb_pool_alloc(newlen);
    memcpy(np, p, oldlen);
    return np;
}

#ifdef TEST_POOL
int
main()
{
    int i, len = 250;
    mrb_pool *pool;
    void *p;

    pool = mrb_pool_open(0);
    p = mrb_pool_alloc(pool, len);
    for (i=1; i<20; i++) {
        printf("%p (len=%d) %ud\n", p, len, mrb_pool_can_realloc(pool, p, len*2));
        p = mrb_pool_realloc(pool, p, len, len*2);
        len *= 2;
    }
    mrb_pool_close(pool);
    return 0;
}
#endif
