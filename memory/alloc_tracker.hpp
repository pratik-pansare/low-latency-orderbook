#pragma once

#include <cstddef>
#include <atomic>

namespace atlas::memory {

// Thread-local or global allocation counter for measuring heap allocations
class AllocationTracker {
public:
    static void reset() noexcept {
        alloc_count_.store(0, std::memory_order_relaxed);
        dealloc_count_.store(0, std::memory_order_relaxed);
        bytes_allocated_.store(0, std::memory_order_relaxed);
    }

    static void record_alloc(size_t bytes) noexcept {
        alloc_count_.fetch_add(1, std::memory_order_relaxed);
        bytes_allocated_.fetch_add(bytes, std::memory_order_relaxed);
    }

    static void record_dealloc(size_t bytes) noexcept {
        dealloc_count_.fetch_add(1, std::memory_order_relaxed);
    }

    [[nodiscard]] static size_t alloc_count() noexcept {
        return alloc_count_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] static size_t dealloc_count() noexcept {
        return dealloc_count_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] static size_t bytes_allocated() noexcept {
        return bytes_allocated_.load(std::memory_order_relaxed);
    }

private:
    static inline std::atomic<size_t> alloc_count_{0};
    static inline std::atomic<size_t> dealloc_count_{0};
    static inline std::atomic<size_t> bytes_allocated_{0};
};

// RAII Scope guard to measure allocations during a block
class ScopedAllocCounter {
public:
    ScopedAllocCounter() : start_allocs_(AllocationTracker::alloc_count()) {}

    [[nodiscard]] size_t allocations() const noexcept {
        return AllocationTracker::alloc_count() - start_allocs_;
    }

private:
    size_t start_allocs_;
};

} // namespace atlas::memory
