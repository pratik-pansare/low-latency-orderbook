#pragma once

#include <cstddef>
#include <utility>

namespace atlas::memory {

template <typename Node>
class IntrusiveList {
public:
    IntrusiveList() = default;
    ~IntrusiveList() = default;

    IntrusiveList(const IntrusiveList&) = delete;
    IntrusiveList& operator=(const IntrusiveList&) = delete;

    IntrusiveList(IntrusiveList&& other) noexcept
        : head_(other.head_), tail_(other.tail_), size_(other.size_) {
        other.head_ = nullptr;
        other.tail_ = nullptr;
        other.size_ = 0;
    }

    IntrusiveList& operator=(IntrusiveList&& other) noexcept {
        if (this != &other) {
            head_ = other.head_;
            tail_ = other.tail_;
            size_ = other.size_;
            other.head_ = nullptr;
            other.tail_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }

    void push_back(Node* node) noexcept {
        node->next = nullptr;
        node->prev = tail_;
        if (tail_) {
            tail_->next = node;
        } else {
            head_ = node;
        }
        tail_ = node;
        size_++;
    }

    void push_front(Node* node) noexcept {
        node->prev = nullptr;
        node->next = head_;
        if (head_) {
            head_->prev = node;
        } else {
            tail_ = node;
        }
        head_ = node;
        size_++;
    }

    void unlink(Node* node) noexcept {
        if (!node) return;
        if (node->prev) {
            node->prev->next = node->next;
        } else {
            head_ = node->next;
        }

        if (node->next) {
            node->next->prev = node->prev;
        } else {
            tail_ = node->prev;
        }

        node->prev = nullptr;
        node->next = nullptr;
        size_--;
    }

    [[nodiscard]] Node* pop_front() noexcept {
        if (!head_) return nullptr;
        Node* node = head_;
        unlink(node);
        return node;
    }

    [[nodiscard]] Node* front() const noexcept { return head_; }
    [[nodiscard]] Node* back() const noexcept { return tail_; }
    [[nodiscard]] bool empty() const noexcept { return head_ == nullptr; }
    [[nodiscard]] size_t size() const noexcept { return size_; }

    void clear() noexcept {
        head_ = nullptr;
        tail_ = nullptr;
        size_ = 0;
    }

    // Iterator support
    class Iterator {
    public:
        using value_type = Node;
        using pointer = Node*;
        using reference = Node&;

        explicit Iterator(Node* cur) : cur_(cur) {}
        reference operator*() const { return *cur_; }
        pointer operator->() const { return cur_; }
        Iterator& operator++() {
            if (cur_) cur_ = cur_->next;
            return *this;
        }
        Iterator operator++(int) {
            Iterator tmp = *this;
            ++(*this);
            return tmp;
        }
        bool operator==(const Iterator& other) const = default;

    private:
        Node* cur_{nullptr};
    };

    [[nodiscard]] Iterator begin() const noexcept { return Iterator(head_); }
    [[nodiscard]] Iterator end() const noexcept { return Iterator(nullptr); }

private:
    Node* head_{nullptr};
    Node* tail_{nullptr};
    size_t size_{0};
};

} // namespace atlas::memory
