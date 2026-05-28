#include <pp_allocator.h>

void smart_mem_resource::do_deallocate(void* p, size_t, size_t) {
    do_deallocate_sm(p);
}

void* smart_mem_resource::do_allocate(size_t bytes, size_t) {
    return do_allocate_sm(bytes);
}

void* test_mem_resource::do_allocate_sm(size_t n) {
    return ::operator new(n);
}

void test_mem_resource::do_deallocate_sm(void* p) {
    ::operator delete(p);
}

bool test_mem_resource::do_is_equal(const std::pmr::memory_resource& other) const noexcept {
    return this == &other;
}
