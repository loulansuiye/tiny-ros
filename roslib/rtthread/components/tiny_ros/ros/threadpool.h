/*
 * File      : threadpool.h
 * This file is part of tiny_ros
 *
 * Change Logs:
 * Date           Author       Notes
 * 2020-04-11     Pinkie.Fu    initial version
 */

#ifndef TINYROS_THREADPOOL_H_
#define TINYROS_THREADPOOL_H_
#include <string>
#include <stdint.h>
#include <rtthread.h>
#include <string.h>
#include <queue>
namespace tinyros {
#define THREAD_POOL_THREADS_INIT_TIME     30
#define THREAD_POOL_JOB_DEFAULT_PRIORITY  21
#define THREAD_POOL_JOB_TICK               5

template<typename ObjT = void>
class ThreadPool{
public:
  typedef void(ObjT::*CallbackT)(void*, bool);

  typedef struct _task_ {
    ObjT *pthis;
    void *arg;
    CallbackT cb;
  } Task;

  ThreadPool(std::string name, int init_threads = 1, uint32_t thread_stack_size = 1024) {

    mutex_ = rt_mutex_create("mutex", RT_IPC_FLAG_FIFO);
    RT_ASSERT(mutex_ != RT_NULL);

    sem_ = rt_sem_create("sem", 0, RT_IPC_FLAG_FIFO);
    RT_ASSERT(sem_ != RT_NULL);

    for (int i = 0; i < init_threads; i++) {
      std::string tid_name = name;
      rt_thread_t tid = rt_thread_create(tid_name.c_str(), &ThreadPool::thread_loop, this,
          thread_stack_size, THREAD_POOL_JOB_DEFAULT_PRIORITY, THREAD_POOL_JOB_TICK);
      RT_ASSERT(tid != RT_NULL);
      rt_kprintf("%s Create thread [%s] success!", __FUNCTION__, tid_name.c_str());

      threads_.push_back(tid);
      rt_thread_startup(tid);
      rt_thread_delay(THREAD_POOL_THREADS_INIT_TIME);
    }

    started_ = true;
    rt_kprintf("%s Initialize thread pool success!", __FUNCTION__);
  }

  ~ThreadPool() {
    shutdown();
    rt_sem_delete(sem_);
    rt_mutex_delete(mutex_);
  }

  void shutdown() {
    started_ = false;

    rt_mutex_take(mutex_, RT_WAITING_FOREVER);
    rt_sem_control(sem_, RT_IPC_CMD_RESET, NULL);
    for (std::size_t i = 0; i < tasks_.size(); i++) {
      Task task = tasks_[i];
      if (task.pthis && task.arg && task.cb) {
        ObjT* obj_ = task.pthis;
        CallbackT cb = task.cb;
        (obj_->*cb)(task.arg, true);
      }
    }
    tasks_.clear();
    rt_mutex_release(mutex_);

    for (std::size_t i = 0; i < threads_.size(); i++) {
      rt_thread_delete(threads_[i]);
    }
    threads_.clear();
  }

  void schedule(CallbackT cb, ObjT* pthis, void* arg) {
    if (started_) {
      rt_mutex_take(mutex_, RT_WAITING_FOREVER);
      Task task;
      task.arg = arg;
      task.cb = cb;
      task.pthis = pthis;
      tasks_.push_back(task);
      rt_sem_release(sem_);
      rt_mutex_release(mutex_);
    }
  }

  Task take() {
    rt_mutex_take(mutex_, RT_WAITING_FOREVER);
    while(tasks_.empty() && started_) {
      rt_mutex_release(mutex_);
      rt_sem_take(sem_, RT_WAITING_FOREVER);
      rt_mutex_take(mutex_, RT_WAITING_FOREVER);
    }

    Task task;
    memset(&task, 0, sizeof(Task));
    if(started_ && !tasks_.empty()) {
      task = tasks_.front();
      tasks_.pop_front();
    }
    rt_mutex_release(mutex_);
    return task;
  }

  typedef std::vector<rt_thread_t> Threads;
  typedef std::deque<Task> Tasks;

  Threads threads_;
  Tasks tasks_;
  rt_mutex_t mutex_;
  rt_sem_t sem_;
  bool started_;

private:
  static void thread_loop(void* pool) {
    ThreadPool<ObjT> *pl = (ThreadPool<ObjT>*)pool;
    while(pl->started_) {
      Task task = pl->take();
      if (pl->started_ && task.pthis && task.arg && task.cb) {
        ObjT* obj_ = task.pthis;
        CallbackT cb = task.cb;
        (obj_->*cb)(task.arg, false);
      }
    }
  }
};
}
#endif //TINYROS_THREADPOOL_H_
