// Copyright (C) 2024 Haochen Jiang
//
// Authors: Haochen Jiang
// Date: 2024/1/2

/**
 * @file thread_safe_queue.h
 *
 * @brief Implements the ThreadSafeQueue template class.
 *
 * ThreadSafeQueue is a thread-safe implementation of a queue data structure. It allows multiple threads to 
 * safely add and remove elements. The class uses mutexes and condition variables to manage concurrent access.
 */

#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>

/**
 * @brief A thread-safe queue implementation.
 *
 * The ThreadSafeQueue class provides a safe way for multiple threads to access a queue. It handles synchronization
 * using mutexes and condition variables to ensure that concurrent access does not cause data corruption or race conditions.
 *
 * @tparam T The type of elements stored in the queue.
 */
template <typename T>
class ThreadSafeQueue {
public:
    /**
     * @brief Default constructor.
     */
    ThreadSafeQueue() {}

    /**
     * @brief Adds an element to the back of the queue.
     *
     * This method is thread-safe.
     *
     * @param value The element to be added to the queue.
     */
    void push(T value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push(std::move(value));
        m_cond.notify_one();
    }

    /**
     * @brief Attempts to pop an element from the front of the queue without blocking.
     *
     * If the queue is empty, this method returns false. This method is thread-safe.
     *
     * @param value Reference to store the popped element.
     * @return True if an element was successfully popped, false if the queue was empty.
     */
    bool try_pop(T& value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queue.empty()) {
            return false;
        }
        value = std::move(m_queue.front());
        m_queue.pop();
        return true;
    }

    /**
     * @brief Waits for and pops an element from the front of the queue.
     *
     * If the queue is empty, this method blocks until an element is available. This method is thread-safe.
     *
     * @param value Reference to store the popped element.
     */
    void wait_and_pop(T& value) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cond.wait(lock, [this] { return !m_queue.empty(); });
        value = std::move(m_queue.front());
        m_queue.pop();
    }

private:
    std::queue<T> m_queue; ///< The underlying standard queue.
    std::mutex m_mutex; ///< Mutex for protecting access to the queue.
    std::condition_variable m_cond; ///< Condition variable used for blocking pop operations.
};
