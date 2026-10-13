#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>
#include <stdexcept>
#include <new>
#include <utility>

namespace atlas::memory {

struct GenerationalHandle {
    uint32_t index{std::numeric_limits<uint32_t>::max()};
    uint32_t generation{0};

    bool operator==(const GenerationalHandle& other) const = default;
    [[nodiscard]] bool is_valid() const noexcept {
        return index != std::numeric_limits<uint32_t>::max();
    }
};

template <typename T>
class FixedPool {
public:
    explicit FixedPool(size_t capacity)
        : capacity_(capacity) {
        storage_ = std::make_unique<Slot[]>(capacity_);
        freelist_.resize(capacity_);
        for (size_t i = 0; i < capacity_; ++i) {
            freelist_[i] = static_cast<uint32_t>(capacity_ - 1 - i); // Push in reverse so slot 0 is first
            storage_[i].generation = 1;
            storage_[i].in_use = false;
        }
    }

    ~FixedPool() {
        clear();
    }

    FixedPool(const FixedPool&) = delete;
    FixedPool& operator=(const FixedPool&) = delete;
    FixedPool(FixedPool&&) noexcept = default;
    FixedPool& operator=(FixedPool&&) noexcept = default;

    template <typename... Args>
    [[nodiscard]] std::pair<T*, GenerationalHandle> allocate(Args&&... args) {
        if (freelist_.empty()) {
            return {nullptr, GenerationalHandle{}};
        }

        uint32_t idx = freelist_.back();
        freelist_.pop_back();

        Slot& slot = storage_[idx];
        slot.in_use = true;
        T* obj = reinterpret_cast<T*>(&slot.storage);
        ::new (static_cast<void*>(obj)) T(std::forward<Args>(args)...);

        allocated_count_++;
        if (allocated_count_ > peak_allocated_) {
            peak_allocated_ = allocated_count_;
        }

        return {obj, GenerationalHandle{idx, slot.generation}};
    }

    void deallocate(T* ptr) {
        if (!ptr) return;
        Slot* slot = reinterpret_cast<Slot*>(reinterpret_cast<char*>(ptr) - offsetof(Slot, storage));
        uint32_t idx = static_cast<uint32_t>(slot - storage_.get());
        
        if (idx >= capacity_ || !slot->in_use) {
            return;
        }

        ptr->~T();
        slot->in_use = false;
        slot->generation++;
        if (slot->generation == 0) slot->generation = 1; // Prevent 0 on overflow

        freelist_.push_back(idx);
        allocated_count_--;
    }

    [[nodiscard]] T* get(GenerationalHandle handle) noexcept {
        if (handle.index >= capacity_) return nullptr;
        Slot& slot = storage_[handle.index];
        if (!slot.in_use || slot.generation != handle.generation) {
            return nullptr;
        }
        return reinterpret_cast<T*>(&slot.storage);
    }

    [[nodiscard]] const T* get(GenerationalHandle handle) const noexcept {
        if (handle.index >= capacity_) return nullptr;
        const Slot& slot = storage_[handle.index];
        if (!slot.in_use || slot.generation != handle.generation) {
            return nullptr;
        }
        return reinterpret_cast<const T*>(&slot.storage);
    }

    [[nodiscard]] GenerationalHandle get_handle(const T* ptr) const noexcept {
        if (!ptr) return {};
        const Slot* slot = reinterpret_cast<const Slot*>(reinterpret_cast<const char*>(ptr) - offsetof(Slot, storage));
        uint32_t idx = static_cast<uint32_t>(slot - storage_.get());
        if (idx >= capacity_ || !slot->in_use) return {};
        return {idx, slot->generation};
    }

    void clear() {
        for (size_t i = 0; i < capacity_; ++i) {
            if (storage_[i].in_use) {
                reinterpret_cast<T*>(&storage_[i].storage)->~T();
                storage_[i].in_use = false;
                storage_[i].generation++;
                if (storage_[i].generation == 0) storage_[i].generation = 1;
            }
        }
        freelist_.resize(capacity_);
        for (size_t i = 0; i < capacity_; ++i) {
            freelist_[i] = static_cast<uint32_t>(capacity_ - 1 - i);
        }
        allocated_count_ = 0;
    }

    [[nodiscard]] size_t size() const noexcept { return allocated_count_; }
    [[nodiscard]] size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] size_t peak() const noexcept { return peak_allocated_; }
    [[nodiscard]] bool empty() const noexcept { return allocated_count_ == 0; }
    [[nodiscard]] bool full() const noexcept { return allocated_count_ >= capacity_; }

private:
    struct alignas(alignof(T)) Slot {
        alignas(T) std::byte storage[sizeof(T)];
        uint32_t generation{1};
        bool in_use{false};
    };

    size_t capacity_{0};
    size_t allocated_count_{0};
    size_t peak_allocated_{0};
    std::unique_ptr<Slot[]> storage_{nullptr};
    std::vector<uint32_t> freelist_{};
};

} // namespace atlas::memory
