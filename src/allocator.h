#include <stdint.h>
#include <memory>
#include "mruby/mem_manager.h"
    /// Define a global that holds the allocated size
    extern size_t gAllocatedSize;

template<typename T>
class Allocator {

    MemManager *m_mem;
public:
    typedef size_t size_type;
    typedef ptrdiff_t difference_type;
    typedef T* pointer;
    typedef const T* const_pointer;
    typedef T& reference;
    typedef const T& const_reference;
    typedef T value_type;
    /// Default constructor
    Allocator(MemManager *m) throw() : m_mem(m)
    {}
    /// Copy constructor
    Allocator(const Allocator&z) throw() : m_mem(z.m_mem)
    {}
    /// Copy constructor with another type
    template<typename U>
    Allocator(const Allocator<U>&oth) throw()
    {
        m_mem = oth.m_mem;
    }

    /// Destructor
    ~Allocator()
    {}

    /// Copy
    Allocator<T>& operator=(const Allocator&)
    {
        return *this;
    }
    /// Copy with another type
    template<typename U>
    Allocator& operator=(const Allocator<U>&)
    {
        return *this;
    }

    /// Get address of reference
    pointer address(reference x) const
    {
        return &x;
    }
    /// Get const address of const reference
    const_pointer address(const_reference x) const
    {
        return &x;
    }

    /// Allocate memory
    pointer allocate(size_type n, const void* = 0)
    {
        size_type size = n * sizeof(value_type);
        gAllocatedSize += size;
        return (pointer)m_mem->_malloc(size);
    }

    /// Deallocate memory
    void deallocate(void* p, size_type n)
    {
        size_type size = n * sizeof(T);
        gAllocatedSize -= size;
        m_mem->_free(p);
    }

    /// Call constructor
    void construct(pointer p, const T& val)
    {
        // Placement new
        new ((T*)p) T(val);
    }
    /// Call constructor with more arguments
    template<typename U, typename... Args>
    void construct(U* p, Args&&... args)
    {
        // Placement new
        ::new((void*)p) U(std::forward<Args>(args)...);
    }

    /// Call the destructor of p
    void destroy(pointer p)
    {
        p->~T();
    }
    /// Call the destructor of p of type U
    template<typename U>
    void destroy(U* p)
    {
        p->~U();
    }

    /// Get the max allocation size
    size_type max_size() const
    {
        return size_type(-1);
    }

    /// A struct to rebind the allocator to another allocator of type U
    template<typename U>
    struct rebind
    {
        typedef Allocator<U> other;
    };
};
