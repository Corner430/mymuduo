#include "EventLoopThread.h"
#include "EventLoop.h"

EventLoopThread::EventLoopThread(const ThreadInitCallback &cb,
                                 const std::string &name)
    : loop_(nullptr), exiting_(false),
      thread_(std::bind(&EventLoopThread::threadFunc, this), name), mutex_(),
      cond_(), callback_(cb) {}

EventLoopThread::~EventLoopThread() {
  exiting_ = true;
  // not 100% race-free, eg. threadFunc could be running callback_.
  if (loop_ != nullptr) {
    /* still a tiny chance to call destructed object, if threadFunc exits just
     * now. but when EventLoopThread destructs, usually programming is exiting
     * anyway. */
    loop_->quit();
    thread_.join();
  }
}

EventLoop *EventLoopThread::startLoop() {
  thread_.start(); // 启动底层的新线程

  EventLoop *loop = nullptr;
  {
    std::unique_lock<std::mutex> lock(mutex_);
    while (loop_ == nullptr)
      cond_.wait(lock);
    loop = loop_;
  }
  return loop;
}

// 运行在新线程内，one loop per thread
void EventLoopThread::threadFunc() {
  EventLoop loop;

  if (callback_)
    callback_(&loop);

  {
    std::unique_lock<std::mutex> lock(mutex_);
    loop_ = &loop;
    cond_.notify_one();
  }

  loop.loop();
  std::unique_lock<std::mutex> lock(mutex_);
  loop_ = nullptr;
}