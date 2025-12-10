#pragma once

#include <condition_variable>
#include <cstddef>
#include <future>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace diffy {

class ThreadPool {
  public:
    ThreadPool();
    explicit ThreadPool(std::size_t thread_count);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    std::size_t thread_count() const noexcept { return workers_.size(); }

    template <class F>
    auto enqueue(F&& f) -> std::future<typename std::invoke_result_t<F>>;

    void start(std::size_t thread_count);
    void stop();

  private:

    std::vector<std::thread> workers_;
    std::queue<std::packaged_task<void()>> tasks_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool stopping_ = false;
};

ThreadPool& global_thread_pool();
void initialize_global_thread_pool(std::size_t thread_count);

// Implementation

inline ThreadPool::ThreadPool() : ThreadPool(std::thread::hardware_concurrency()) {}

inline ThreadPool::ThreadPool(std::size_t thread_count) {
    if (thread_count == 0) {
        thread_count = 1;
    }
    start(thread_count);
}

inline ThreadPool::~ThreadPool() {
    stop();
}

inline void ThreadPool::start(std::size_t thread_count) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = false;
    }
    for (std::size_t i = 0; i < thread_count; ++i) {
        workers_.emplace_back([this] {
            for (;;) {
                std::packaged_task<void()> task;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    cv_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
                    if (stopping_ && tasks_.empty()) {
                        return;
                    }
                    task = std::move(tasks_.front());
                    tasks_.pop();
                }
                task();
            }
        });
    }
}

inline void ThreadPool::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    cv_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
}

template <class F>
auto ThreadPool::enqueue(F&& f) -> std::future<typename std::invoke_result_t<F>> {
    using ReturnType = typename std::invoke_result_t<F>;

    std::packaged_task<ReturnType()> task(std::forward<F>(f));
    auto future = task.get_future();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            throw std::runtime_error("enqueue on stopped ThreadPool");
        }
        tasks_.emplace([task = std::move(task)]() mutable { task(); });
    }
    cv_.notify_one();
    return future;
}

inline ThreadPool& global_thread_pool() {
    static ThreadPool pool;
    return pool;
}

inline void initialize_global_thread_pool(std::size_t thread_count) {
    static bool initialized = false;
    if (initialized) {
        return;  // Already initialized
    }

    // Force initialization of the global pool by accessing it
    // and then reconfiguring it
    auto& pool = global_thread_pool();
    pool.stop();
    pool.start(thread_count == 0 ? std::thread::hardware_concurrency() : thread_count);
    initialized = true;
}

}  // namespace diffy

