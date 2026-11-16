#pragma once
// ============================================================================
//  并发容器：环形缓冲区 + 有界线程安全队列
//  ---------------------------------------------------------------------------
//  RingBuffer<T>   : 定长环形缓冲，写满覆盖最旧数据。用于保存 “最近 N 个采样点”
//                    给界面做趋势曲线，不分配新内存。
//  BoundedQueue<T> : mutex + condition_variable 实现的有界阻塞队列，
//                    生产快于消费时由调用方决定背压(push 阻塞)或丢弃(tryPush)。
// ============================================================================
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>
#include <vector>

namespace mb {

// ---------------------------------------------------------------------------
//  环形缓冲区（线程安全，可多写多读）
// ---------------------------------------------------------------------------
template <typename T>
class RingBuffer {
public:
    explicit RingBuffer(std::size_t capacity = 512)
        : m_buf(capacity > 0 ? capacity : 1), m_capacity(m_buf.size()) {}

    //! 写入一个元素；缓冲区满时覆盖最旧的一个
    void push(const T& value)
    {
        std::scoped_lock lock(m_mutex);
        m_buf[m_head] = value;
        m_head = (m_head + 1) % m_capacity;
        if (m_count < m_capacity)
            ++m_count;
    }

    //! 按写入顺序（旧 -> 新）取一份快照
    [[nodiscard]] std::vector<T> snapshot() const
    {
        std::scoped_lock lock(m_mutex);
        std::vector<T> out;
        out.reserve(m_count);
        const std::size_t start = (m_head + m_capacity - m_count) % m_capacity;
        for (std::size_t i = 0; i < m_count; ++i)
            out.push_back(m_buf[(start + i) % m_capacity]);
        return out;
    }

    void clear()
    {
        std::scoped_lock lock(m_mutex);
        m_head = 0;
        m_count = 0;
    }

    [[nodiscard]] std::size_t size() const
    {
        std::scoped_lock lock(m_mutex);
        return m_count;
    }

    [[nodiscard]] std::size_t capacity() const { return m_capacity; }

private:
    mutable std::mutex m_mutex;
    std::vector<T>     m_buf;
    std::size_t        m_capacity = 0;
    std::size_t        m_head     = 0;
    std::size_t        m_count    = 0;
};

// ---------------------------------------------------------------------------
//  有界阻塞队列
// ---------------------------------------------------------------------------
template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity = 8192)
        : m_capacity(capacity > 0 ? capacity : 1) {}

    //! 阻塞式写入：队列满时等待消费，实现背压。close() 后返回 false
    bool push(const T& value)
    {
        std::unique_lock lock(m_mutex);
        m_notFull.wait(lock, [this] {
            return m_closed || m_queue.size() < m_capacity;
        });
        if (m_closed)
            return false;
        m_queue.push_back(value);
        lock.unlock();
        m_notEmpty.notify_one();
        return true;
    }

    //! 非阻塞写入：满时直接丢弃（返回 false）。用于 “宁可丢数据不阻塞采集”
    bool tryPush(const T& value)
    {
        std::scoped_lock lock(m_mutex);
        if (m_closed || m_queue.size() >= m_capacity)
            return false;
        m_queue.push_back(value);
        // 通知无法在持锁状态下安全做，放到锁外
        // （这里用 scoped_lock 简化：notify 本身无副作用，持锁唤醒也能工作）
        m_notEmpty.notify_one();
        return true;
    }

    //! 阻塞取出一条；超时或队列关闭返回 false
    bool pop(T& out, int timeoutMs = -1)
    {
        std::unique_lock lock(m_mutex);
        if (timeoutMs < 0) {
            m_notEmpty.wait(lock, [this] { return m_closed || !m_queue.empty(); });
        } else {
            const bool ok = m_notEmpty.wait_for(
                lock, std::chrono::milliseconds(timeoutMs),
                [this] { return m_closed || !m_queue.empty(); });
            if (!ok)
                return false;
        }
        if (m_queue.empty())
            return false;                 // closed 且已清空
        out = std::move(m_queue.front());
        m_queue.pop_front();
        lock.unlock();
        m_notFull.notify_one();
        return true;
    }

    //! 批量取出最多 maxCount 条（存储层用它做事务批量写入）
    std::size_t popBatch(std::vector<T>& out, std::size_t maxCount, int timeoutMs = -1)
    {
        out.clear();
        std::unique_lock lock(m_mutex);
        if (m_queue.empty()) {
            if (timeoutMs < 0) {
                m_notEmpty.wait(lock, [this] { return m_closed || !m_queue.empty(); });
            } else {
                m_notEmpty.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                                    [this] { return m_closed || !m_queue.empty(); });
            }
        }
        const std::size_t n = std::min(maxCount, m_queue.size());
        out.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            out.push_back(std::move(m_queue.front()));
            m_queue.pop_front();
        }
        lock.unlock();
        if (n > 0)
            m_notFull.notify_all();
        return n;
    }

    void close()
    {
        {
            std::scoped_lock lock(m_mutex);
            m_closed = true;
        }
        m_notEmpty.notify_all();
        m_notFull.notify_all();
    }

    void reopen()
    {
        std::scoped_lock lock(m_mutex);
        m_closed = false;
    }

    [[nodiscard]] bool closed() const
    {
        std::scoped_lock lock(m_mutex);
        return m_closed;
    }

    [[nodiscard]] std::size_t size() const
    {
        std::scoped_lock lock(m_mutex);
        return m_queue.size();
    }

    [[nodiscard]] std::size_t capacity() const { return m_capacity; }

private:
    mutable std::mutex      m_mutex;
    std::condition_variable m_notEmpty;
    std::condition_variable m_notFull;
    std::deque<T>           m_queue;
    std::size_t             m_capacity = 0;
    bool                    m_closed   = false;
};

} // namespace mb
