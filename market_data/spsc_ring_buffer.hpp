#pragma once

#include <atomic>
#include <cstddef>
#include <new>
#include <utility>
#include <optional>
#include <array>

namespace atlas::market_data {

// Bounded Single-Producer Single-Consumer (SPSC) Ring Buffer
// Uses cache-line padding (64 bytes) to eliminate false sharing between producer & consumer cores.
template <typename T, size_t Capacity>
class SpscRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static constexpr size_t MASK = Capacity - 1;

public:
    SpscRingBuffer() = default;
    ~SpscRingBuffer() {
        T item;
        while (try_pop(item)) {}
    }

    SpscRingBuffer(const SpscRingBuffer&) = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;

    // Called only by PRODUCER thread
    [[nodiscard]] bool try_push(const T& item) {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t tail = tail_cached_;

        if ((head - tail) >= Capacity) {
            // Buffer appears full, reload consumer tail with acquire semantics
            tail_cached_ = tail_.load(std::memory_order_acquire);
            if ((head - tail_cached_) >= Capacity) {
                return false; // Still full
            }
        }

        buffer_[head & MASK] = item;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    // Called only by CONSUMER thread
    [[nodiscard]] bool try_pop(T& item) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t head = head_cached_;

        if (tail == head) {
            // Buffer appears empty, reload producer head with acquire semantics
            head_cached_ = head_.load(std::memory_order_acquire);
            if (tail == head_cached_) {
                return false; // Still empty
            }
        }

        item = buffer_[tail & MASK];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] size_t size() const noexcept {
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t tail = tail_.load(std::memory_order_acquire);
        return head >= tail ? (head - tail) : 0;
    }

    [[nodiscard]] bool empty() const noexcept {
        return size() == 0;
    }

    [[nodiscard]] bool full() const noexcept {
        return size() >= Capacity;
    }

    [[nodiscard]] static constexpr size_t capacity() noexcept {
        return Capacity;
    }

private:
    // Align producer and consumer data onto distinct cache lines
    alignas(64) std::atomic<size_t> head_{0};
    size_t tail_cached_{0};

    alignas(64) std::atomic<size_t> tail_{0};
    size_t head_cached_{0};

    alignas(64) std::array<T, Capacity> buffer_{};
};

} // namespace atlas::market_data
