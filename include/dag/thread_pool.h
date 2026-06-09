/**
 * @file thread_pool.h
 * @brief A persistent work-stealing worker thread pool.
 *
 * Created once and reused across many graph runs. Each worker owns its own
 * task deque (its own mutex), so the common case -- a worker pushing successor
 * tasks and popping them back off -- touches only a lightly-contended local
 * lock instead of one global mutex shared by every thread. Idle workers steal
 * from other deques, and sleep on a condition variable when there is nothing
 * to steal (an idle pool costs no CPU).
 *
 * Discipline (Chase-Lev style, but lock-based for simplicity/correctness):
 *   - A task submitted FROM a worker goes to that worker's own deque, pushed at
 *     the front and popped from the front -> LIFO. Successors run on the same
 *     worker while their producer's output is still hot in cache.
 *   - A task submitted from outside (e.g. the main thread seeding roots) is
 *     round-robined across deques, pushed at the back.
 *   - Steals take from the back of a victim's deque (away from its owner).
 */
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace dag {

class ThreadPool {
public:
    using Task = std::function<void()>;

    /// @param threads worker count; 0 -> hardware_concurrency() (min 1).
    explicit ThreadPool(unsigned threads = 0) {
        if (threads == 0) threads = std::thread::hardware_concurrency();
        if (threads == 0) threads = 1;
        workers_.reserve(threads);
        for (unsigned i = 0; i < threads; ++i) {
            workers_.push_back(std::make_unique<Worker>());
        }
        threads_.reserve(threads);
        for (unsigned i = 0; i < threads; ++i) {
            threads_.emplace_back([this, i] { worker_loop(i); });
        }
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(idle_mutex_);
            stop_.store(true, std::memory_order_relaxed);
        }
        idle_cv_.notify_all();
        for (auto& t : threads_) {
            if (t.joinable()) t.join();
        }
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    std::size_t size() const noexcept { return workers_.size(); }

    void submit(Task task) {
        const unsigned n = static_cast<unsigned>(workers_.size());
        if (tls().pool == this && tls().index >= 0) {
            // Submitted from one of our workers: keep it local (LIFO front).
            unsigned i = static_cast<unsigned>(tls().index);
            std::lock_guard<std::mutex> lock(workers_[i]->mutex);
            workers_[i]->queue.push_front(std::move(task));
        } else {
            // External submit: spread across workers (FIFO back).
            unsigned i = rr_.fetch_add(1, std::memory_order_relaxed) % n;
            std::lock_guard<std::mutex> lock(workers_[i]->mutex);
            workers_[i]->queue.push_back(std::move(task));
        }
        // Publish the new task, then wake a worker ONLY if one is actually
        // asleep. When all workers are busy (the common case under load) this is
        // just two atomics -- no global lock, no notify syscall per task. The
        // seq_cst ordering vs the worker's sleep sequence is what makes the
        // "skip the notify" safe (a Dekker-style handshake; see worker_loop).
        pending_.fetch_add(1, std::memory_order_seq_cst);
        if (sleepers_.load(std::memory_order_seq_cst) > 0) {
            std::lock_guard<std::mutex> lock(idle_mutex_);
            idle_cv_.notify_one();
        }
    }

private:
    struct Worker {
        std::deque<Task> queue;
        std::mutex mutex;
    };

    struct TlsState {
        ThreadPool* pool = nullptr;
        int index = -1;
    };
    static TlsState& tls() {
        static thread_local TlsState s;
        return s;
    }

    bool try_pop_local(unsigned i, Task& out) {
        std::lock_guard<std::mutex> lock(workers_[i]->mutex);
        auto& q = workers_[i]->queue;
        if (q.empty()) return false;
        out = std::move(q.front());
        q.pop_front();
        pending_.fetch_sub(1, std::memory_order_relaxed);
        return true;
    }

    bool try_steal(unsigned self, Task& out) {
        const unsigned n = static_cast<unsigned>(workers_.size());
        for (unsigned k = 1; k < n; ++k) {
            unsigned victim = (self + k) % n;
            std::unique_lock<std::mutex> lock(workers_[victim]->mutex, std::try_to_lock);
            if (!lock.owns_lock()) continue;  // don't block on a contended victim
            auto& q = workers_[victim]->queue;
            if (q.empty()) continue;
            out = std::move(q.back());
            q.pop_back();
            pending_.fetch_sub(1, std::memory_order_relaxed);
            return true;
        }
        return false;
    }

    void worker_loop(unsigned i) {
        tls() = TlsState{this, static_cast<int>(i)};
        int idle_spins = 0;
        for (;;) {
            Task task;
            if (try_pop_local(i, task) || try_steal(i, task)) {
                idle_spins = 0;
                // A task must never let an exception escape (it would terminate
                // the whole process). Defensive backstop; the Executor already
                // catches node exceptions.
                try {
                    task();
                } catch (...) {
                }
                continue;
            }
            if (stop_.load(std::memory_order_relaxed) &&
                pending_.load(std::memory_order_relaxed) == 0) {
                return;
            }
            // Found nothing this instant. Spin-search a bounded number of times
            // before sleeping: this rides out brief work droughts (e.g. waiting
            // for the producer's next submit, or a steal that lost a race)
            // without a sleep/wake syscall. Without this, a fast many-core pool
            // fed by a single producer churns sleep<->wake per task and collapses.
            if (idle_spins < kSpinLimit) {
                ++idle_spins;
                std::this_thread::yield();
                continue;
            }

            // Spun long enough with no work. Announce intent to sleep, then
            // re-check (seq_cst). Paired with submit's "pending_++ then read
            // sleepers_", this Dekker handshake guarantees: either we observe the
            // new task here, or the submitter observes us asleep and notifies.
            sleepers_.fetch_add(1, std::memory_order_seq_cst);
            if (pending_.load(std::memory_order_seq_cst) == 0 &&
                !stop_.load(std::memory_order_relaxed)) {
                std::unique_lock<std::mutex> lock(idle_mutex_);
                // Final re-check under the lock: submit bumps pending_ before
                // notifying, so a notify that arrived before we waited is caught
                // here as a non-zero pending_ and we skip the wait.
                if (pending_.load(std::memory_order_relaxed) == 0 &&
                    !stop_.load(std::memory_order_relaxed)) {
                    idle_cv_.wait(lock);
                }
            }
            sleepers_.fetch_sub(1, std::memory_order_relaxed);
            idle_spins = 0;
        }
    }

    static constexpr int kSpinLimit = 4096;  // ~bounded spin before sleeping

    std::vector<std::unique_ptr<Worker>> workers_;
    std::vector<std::thread> threads_;
    std::atomic<unsigned> rr_{0};            // round-robin cursor for external submits
    std::atomic<std::size_t> pending_{0};    // queued (not-yet-running) task count
    std::atomic<int> sleepers_{0};           // workers currently asleep (eventcount)
    std::atomic<bool> stop_{false};
    std::mutex idle_mutex_;
    std::condition_variable idle_cv_;
};

}  // namespace dag
