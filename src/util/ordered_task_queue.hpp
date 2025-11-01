#pragma once

#include <condition_variable>
#include <cstddef>
#include <exception>
#include <map>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace diffy {

template <typename T>
class OrderedTaskQueue {
  public:
    explicit OrderedTaskQueue(std::size_t capacity)
        : capacity_(capacity == 0 ? 1 : capacity) {}

    void push(std::size_t index, T value) {
        emplace_entry(index, std::move(value), nullptr);
    }

    void push_exception(std::size_t index, std::exception_ptr error) {
        emplace_entry(index, T{}, error);
    }

    T pop(std::size_t index) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!next_index_initialized_ || index < next_index_) {
            next_index_ = index;
            next_index_initialized_ = true;
            space_cv_.notify_all();
        }
        ready_cv_.wait(lock, [&] { return has_entry(index); });
        auto entry_it = entries_.find(index);
        auto entry = std::move(entry_it->second);
        entries_.erase(entry_it);
        update_next_index(index);
        space_cv_.notify_all();
        if (entry.error) {
            std::rethrow_exception(entry.error);
        }
        return std::move(entry.value);
    }

  private:
    struct Entry {
        T value;
        std::exception_ptr error;
    };

    bool has_entry(std::size_t index) const {
        return entries_.find(index) != entries_.end();
    }

    void emplace_entry(std::size_t index, T value, std::exception_ptr error) {
        std::unique_lock<std::mutex> lock(mutex_);
        space_cv_.wait(lock, [&] {
            return entries_.size() < capacity_ || index <= next_index_;
        });
        auto [it, inserted] = entries_.emplace(index, Entry{std::move(value), std::move(error)});
        if (!inserted) {
            throw std::logic_error("duplicate index in OrderedTaskQueue");
        }
        ready_cv_.notify_all();
    }

    void update_next_index(std::size_t completed_index) {
        if (!next_index_initialized_ || completed_index >= next_index_) {
            next_index_ = completed_index + 1;
            next_index_initialized_ = true;
        }
    }

    const std::size_t capacity_;
    std::map<std::size_t, Entry> entries_;
    std::condition_variable ready_cv_;
    std::condition_variable space_cv_;
    mutable std::mutex mutex_;
    std::size_t next_index_ = 0;
    bool next_index_initialized_ = false;
};

}  // namespace diffy

