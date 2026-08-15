#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <optional>

namespace bw
{
    namespace core
    {

        template <typename T>
        class ThreadSafeQueue {
        public:
            ThreadSafeQueue() = default;

            // Disable copy
            ThreadSafeQueue(const ThreadSafeQueue&) = delete;
            ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;

            // Push (lvalue)
            void push(const T& value) {
                {
                    std::lock_guard<std::mutex> lock(mtx_);
                    if (closed_) return; // or throw if you prefer strict behavior
                    queue_.push(value);
                }
                cv_.notify_one();
            }

            // Push (rvalue)
            void push(T&& value) {
                {
                    std::lock_guard<std::mutex> lock(mtx_);
                    if (closed_) return;
                    queue_.push(std::move(value));
                }
                cv_.notify_one();
            }

            // Check if top value can be popped
            bool can_pop(std::function<bool(T const&)> pred) const {
                std::lock_guard<std::mutex> lock(mtx_);
                return !queue_.empty() && pred(queue_.front());
            }

            // Blocking pop
            // Returns std::nullopt if queue is closed and empty
            std::optional<T> pop() {
                std::unique_lock<std::mutex> lock(mtx_);
                cv_.wait(lock, [this] {
                    return !queue_.empty() || closed_;
                });

                if (queue_.empty()) {
                    return std::nullopt; // closed and drained
                }

                T value = std::move(queue_.front());
                queue_.pop();
                return value;
            }

            // Non-blocking pop
            bool try_pop(T& out) {
                std::lock_guard<std::mutex> lock(mtx_);
                if (queue_.empty()) return false;

                out = std::move(queue_.front());
                queue_.pop();
                return true;
            }

            // Close queue (signals no more pushes)
            void close() {
                {
                    std::lock_guard<std::mutex> lock(mtx_);
                    closed_ = true;
                }
                cv_.notify_all();
            }

            bool empty() const {
                std::lock_guard<std::mutex> lock(mtx_);
                return queue_.empty();
            }

            T const& back() const {
                std::lock_guard<std::mutex> lock(mtx_);
                return queue_.back();
            }

            bool closed() const {
                std::lock_guard<std::mutex> lock(mtx_);
                return closed_;
            }

        private:
            mutable std::mutex mtx_;
            std::condition_variable cv_;
            std::queue<T> queue_;
            bool closed_ = false;
        };

    } // bw
} // core
