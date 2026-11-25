#include "thread_pool.h"

#include <cstdio>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <utility>

ThreadPool::ThreadPool(std::size_t threadCount,std::size_t maxQueueTasks):m_maxQueueTasks(maxQueueTasks),m_stopping(false){
    if(threadCount == 0 || m_maxQueueTasks == 0){
        throw std::invalid_argument("Thread count and queue capacity must be positive");
    }

    //只是预留容器空间，不会创建线程
    m_workers.reserve(threadCount);
    
    try{
        for(std::size_t i=0;i<threadCount;i++){
            //创建一个工作线程，并让他运行当前对象的 workerLoop()
            m_workers.emplace_back([this]{
                    workerLoop();
            });
        }
    }
    catch(...){
        //处理前几个线程已经创建成功，后一个创建失败的情况
        //如果部分线程创建失败，先通知已创建的线程退出
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stopping = true;
        }
        m_notEmpty.notify_all();

        for(auto& worker:m_workers){
            if(worker.joinable()){
                worker.join();
            }
        }
        throw;
    }
}

ThreadPool::~ThreadPool(){
    stop();
}

void ThreadPool::stop(){
    //由拥有线程池的主线程销毁它，不再工作任务内部销毁线程池；小会期间也不能再有外部线程访问它
    //不能拿着 m_mutex 调用 join()，工作线程退出前需要取得同一把锁，如果析构线程上锁等待工作线程，会造成死锁
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopping = true;
    }

    //唤醒所有等待任务的工作线程
    m_notEmpty.notify_all();

    //等待所有工作线程处理完任务并退出
    for(auto& worker : m_workers){
        if(worker.joinable()){
            worker.join();
        }
    }
}

void ThreadPool::workerLoop(){
    while(true){
        Task task;

        {
            std::unique_lock<std::mutex> lock(m_mutex);

            //没任务时，线程会休眠，不会一直空转
            m_notEmpty.wait(lock,[this]{
                    return m_stopping || !m_tasks.empty();
            });
            //条件不满足时，wait会解锁并等待；被唤醒后重新上锁，再次检查条件。只有需要停止或队列有任务，才会继续向下执行

            //停止且队列已经清空，才退出线程
            //停止不代表立即丢弃所有任务，如果停止时还有任务，继续执行任务，所有任务处理完毕才退出
            if(m_stopping && m_tasks.empty()){
                return;
            }

            task = std::move(m_tasks.front());
            m_tasks.pop();
        }

        //已经离开上面的作用域，互斥锁已释放
        try{
            //解锁后执行任务，不影响其他线程取任务
            task();
        }
        catch(const std::exception& error){
            fprintf(stderr,"Task failed: %s\n",error.what());
        }
        catch(...){
            fprintf(stderr,"Task failed: unknown exception\n");
        }
    }
}

//实现任务提交
bool ThreadPool::trySubmint(Task task){
    //空任务无法执行
    if(!task){
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        //检查停止状态
        if(m_stopping){
            return false;
        }

        //检查队列容量
        if(m_tasks.size() >= m_maxQueueTasks){
            return false;
        }

        //任务加入任务队列
        m_tasks.push(std::move(task));
    }

    //任务入队以后，唤醒一个工作线程
    m_notEmpty.notify_one();

    return true;
}
