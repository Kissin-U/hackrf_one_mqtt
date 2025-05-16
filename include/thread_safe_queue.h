#ifndef THREAD_SAFE_QUEUE_H
#define THREAD_SAFE_QUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional> // For try_pop with timeout or non-blocking

#include <iostream> // For potential warning messages

namespace hackrf_mqtt {

template <typename T>
class ThreadSafeQueue {
public:
    explicit ThreadSafeQueue(size_t max_size = 0) : max_size_(max_size) {} // 0 means unbounded
    ~ThreadSafeQueue() = default;

    // Rule of five: disable copy/move operations for simplicity
    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue(ThreadSafeQueue&&) = delete;
    ThreadSafeQueue& operator=(ThreadSafeQueue&&) = delete;

    // Returns true if pushed successfully, false if queue was full (for bounded queues)
    bool try_push(T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (max_size_ > 0 && queue_.size() >= max_size_) {
            // Optional: Log a warning, perhaps rate-limited
            // std::cerr << "Warning: ThreadSafeQueue is full. Discarding new item." << std::endl;
            return false; // Queue is full
        }
        queue_.push(std::move(value));
        condition_.notify_one();
        return true;
    }
    
    // Legacy push, for unbounded or when push failure is not critical to signal immediately
    // For bounded queue, this will discard if full.
    void push(T value) {
        if (!try_push(std::move(value))) {
            // If try_push returns false (queue is full and bounded), this new item is discarded.
            // A warning could be logged here if this behavior is chosen for the simple push.
            // For now, we assume if simple push is called on a full bounded queue, discard is acceptable.
            // To make it explicit, the caller should use try_push and check the return.
        }
    }


    // Waits until an item is available
    T wait_and_pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [this] { return !queue_.empty(); });
        T value = std::move(queue_.front());
        queue_.pop();
        return value;
    }

    // Tries to pop an item without waiting. Returns std::nullopt if empty.
    std::optional<T> try_pop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return std::nullopt;
        }
        T value = std::move(queue_.front());
        queue_.pop();
        return value;
    }

    // Tries to pop an item, waiting up to a specified duration.
    // Returns std::nullopt if timeout occurs or queue is empty after wait.
    template<typename Rep, typename Period>
    std::optional<T> wait_for_and_pop(const std::chrono::duration<Rep, Period>& rel_time) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!condition_.wait_for(lock, rel_time, [this] { return !queue_.empty(); })) {
            return std::nullopt; // Timeout or spurious wake up with empty queue
        }
        T value = std::move(queue_.front());
        queue_.pop();
        return value;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    mutable std::mutex mutex_; 
    std::queue<T> queue_;
    std::condition_variable condition_;
    size_t max_size_; // 0 for unbounded
};

} // namespace hackrf_mqtt

#endif // THREAD_SAFE_QUEUE_H
