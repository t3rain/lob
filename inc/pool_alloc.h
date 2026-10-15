// "inc/pool_alloc.h"
#pragma once

#include <cstddef>
#include <new>
#include <vector>

// Free-list pool allocator. Stateless: every PoolAlloc<T> shares one static pool per T.
// Single-threaded only.
template <class T>
class PoolAlloc {
public:
    using value_type = T;

    PoolAlloc() noexcept = default;

    template <class U>
    PoolAlloc(const PoolAlloc<U>&) noexcept {}          // needed so rebind works

    T* allocate(std::size_t n) {
        if (n != 1) return static_cast<T*>(::operator new(n * sizeof(T))); 
        return static_cast<T*>(pool().take());
    }

    void deallocate(T* p, std::size_t n) noexcept {
        if (n != 1) { ::operator delete(p); return; }
        pool().give(p);
    }

    // Stateless => any two instances can free each other's memory.
    template <class U> bool operator==(const PoolAlloc<U>&) const noexcept { return true; }
    template <class U> bool operator!=(const PoolAlloc<U>&) const noexcept { return false; }

private:
    static_assert(alignof(T) <= alignof(std::max_align_t), "over-aligned types not supported");

    struct FreeNode { FreeNode* next; };
    static constexpr std::size_t kSlot =
        sizeof(T) < sizeof(FreeNode) ? sizeof(FreeNode) : sizeof(T);
    static constexpr std::size_t kSlotsPerChunk = 4096;

    struct Pool {
        FreeNode* head = nullptr;
        std::vector<void*> chunks;

        ~Pool() { for (void* c : chunks) ::operator delete(c); }

        void* take() {
            if (!head) refill();
            FreeNode* n = head;
            head = n->next;
            return n;
        }
        void give(void* p) noexcept {
            auto* n = static_cast<FreeNode*>(p);
            n->next = head;
            head = n;
        }
        void refill() {
            char* base = static_cast<char*>(::operator new(kSlot * kSlotsPerChunk));
            chunks.push_back(base);
            for (std::size_t i = 0; i < kSlotsPerChunk; ++i) give(base + i * kSlot);
        }
    };

    static Pool& pool() { static Pool p; return p; }
};