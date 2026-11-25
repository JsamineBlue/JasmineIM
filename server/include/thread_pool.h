#pragma once

#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class ThreadPool{
public:
    using Task = std::function<void()>;

    explicit ThreadPool(std::size_t threadCount,std::size_t maxQueuedTasks = 1024);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator = (const ThreadPool&) = delete;

    //非阻塞提交：队列满或线程池停止时返回false
    bool trySubmint(Task task);

    //停止接收新任务，处理完已入队任务，等待线程退出
    //由拥有线程池的线程调用，不在工作任务中使用
    void stop();

private:
    //每个工作线程执行的循环
    void workerLoop();

private:
    std::vector<std::thread> m_workers;
    std::queue<Task> m_tasks;

    std::mutex m_mutex;
    std::condition_variable m_notEmpty;

    std::size_t m_maxQueueTasks;
    bool m_stopping;
};
