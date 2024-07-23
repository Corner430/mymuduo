#pragma once
#include "noncopyable.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

class EventLoop;
class EventLoopThread;

class EventLoopThreadPool : noncopyable {
public:
  using ThreadInitCallback = std::function<void(EventLoop *)>;

  EventLoopThreadPool(EventLoop *baseLoop, const std::string &nameArg);
  ~EventLoopThreadPool();

  void setThreadNum(int numThreads) { numThreads_ = numThreads; }

  void start(const ThreadInitCallback &cb = ThreadInitCallback());

  /* valid after calling start()
   * round-robin
   * 如果工作在多线程中，baseLoop_ 默认以轮询的方式分配 channel 给 subloop */
  EventLoop *getNextLoop();

  std::vector<EventLoop *> getAllLoops();

  bool started() const { return started_; }
  const std::string name() const { return name_; }

private:
  EventLoop *baseLoop_; // EventLoop loop;
  std::string name_;
  bool started_;
  int numThreads_;
  int next_; // 保存了下一个 subLoop 的索引
  std::vector<std::unique_ptr<EventLoopThread>> threads_;
  std::vector<EventLoop *> loops_; // 保存了所有的 subLoop
};