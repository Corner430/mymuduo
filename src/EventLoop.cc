#include "EventLoop.h"
#include "Channel.h"
#include "Logger.h"
#include "Poller.h"

#include <errno.h>
#include <fcntl.h>
#include <memory>
#include <sys/eventfd.h>
#include <unistd.h>

// 防止一个线程创建多个 EventLoop
__thread EventLoop *t_loopInThisThread = nullptr;

// 定义默认的 Poller I/O 复用接口的超时时间
const int kPollTimeMs = 10000;

// 创建一个 eventfd 文件描述符，用于线程间通信
int createEventfd() {
  // 初始计数值为 0，并设置为 nonblock 和 close-on-exec
  int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (evtfd < 0)
    LOG_FATAL("eventfd error:%d \n", errno);
  return evtfd; // 交给 wakeupfd_
}

EventLoop::EventLoop()
    : looping_(false), quit_(false), callingPendingFunctors_(false),
      threadId_(CurrentThread::tid()), poller_(Poller::newDefaultPoller(this)),
      wakeupFd_(createEventfd()), wakeupChannel_(new Channel(this, wakeupFd_)) {
  LOG_DEBUG("EventLoop created %p in thread %d \n", this, threadId_);
  if (t_loopInThisThread)
    LOG_FATAL("Another EventLoop %p exists in this thread %d \n",
              t_loopInThisThread, threadId_);
  else
    t_loopInThisThread = this;

  // 设置 wakeupFd_ 发生事件后的回调操作
  wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleRead, this));

  /* 每一个 EventLoop 都将监听自身 wakeupChannel 的 EPOLLIN(读事件)
   * we are always reading the wakeupfd */
  wakeupChannel_->enableReading(); // 监听可读事件，并注册到 Poller 中
}

EventLoop::~EventLoop() {
  wakeupChannel_->disableAll();
  wakeupChannel_->remove();
  ::close(wakeupFd_);
  t_loopInThisThread = nullptr;
}

void EventLoop::loop() {
  looping_ = true;
  quit_ = false;

  LOG_INFO("EventLoop %p start looping \n", this);

  while (!quit_) {
    // 清空 activeChannels_，准备存放本次事件循环中活跃的通道
    activeChannels_.clear();

    // epoll_ctl() 操作，并获取发生事件的时间戳和 activeChannels_
    pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);

    for (Channel *channel : activeChannels_)
      channel->handleEvent(pollReturnTime_);

    // 执行当前 EventLoop 需要处理的延迟回调
    doPendingFunctors();
  }

  LOG_INFO("EventLoop %p stop looping. \n", this);
  looping_ = false;
}

void EventLoop::quit() {
  quit_ = true;

  /* There is a chance that loop() just executes while(!quit_) and exits,
   * then EventLoop destructs, then we are accessing an invalid object.
   * Can be fixed using mutex_ in both places.
   *
   * 解决跨线程调用 quit() 函数的问题 */
  if (!isInLoopThread())
    wakeup();
}

/* 1. 如果调用 runInLoop() 和 EventLoop 在同一个线程，直接执行 cb
 * 2. 如果调用 runInLoop() 和 EventLoop 不在同一个线程，调用 queueInLoop() */
void EventLoop::runInLoop(Functor cb) {
  isInLoopThread() ? cb() : queueInLoop(cb);
}

// 把 cb 放入 pendingFunctors_，唤醒 loop 所在的线程，执行 cb
void EventLoop::queueInLoop(Functor cb) {
  {
    std::unique_lock<std::mutex> lock(mutex_);
    pendingFunctors_.emplace_back(cb);
  }

  /* 1. 如果调用 queueInLoop() 和 EventLoop 不在同一个线程，或者
   *    callingPendingFunctors_ 为 true 时（此时正在执行
   *    doPendingFunctors()，即正在执行回调），则唤醒 loop 所在的线程
   * 2. 如果调用 queueInLoop() 和 EventLoop 在同一个线程，但是
   *    callingPendingFunctors_ 为 false 时，则说明：此时尚未执行到
   *    doPendingFunctors()。
   *    不必唤醒，这个优雅的设计可以减少对 eventfd 的 I/O 读写 */
  if (!isInLoopThread() || callingPendingFunctors_)
    wakeup();
}

// 参见 man eventfd 中和 read 结合的 example
void EventLoop::handleRead() {
  uint64_t one = 1;
  ssize_t n = read(wakeupFd_, &one, sizeof one);

  if (n != sizeof one)
    LOG_ERROR("EventLoop::handleRead() reads %lu bytes instead of 8", n);
}

/* 向 wakeupfd_ 写一个数据，wakeupChannel 就发生读事件
 * 对应的 loop 线程就会被唤醒 */
void EventLoop::wakeup() {
  uint64_t one = 1;
  ssize_t n = write(wakeupFd_, &one, sizeof one);
  if (n != sizeof one)
    LOG_ERROR("EventLoop::wakeup() writes %lu bytes instead of 8 \n", n);
}

void EventLoop::updateChannel(Channel *channel) {
  poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel *channel) {
  poller_->removeChannel(channel);
}

bool EventLoop::hasChannel(Channel *channel) {
  return poller_->hasChannel(channel);
}

void EventLoop::doPendingFunctors() {
  std::vector<Functor> functors;
  callingPendingFunctors_ = true;

  { // 通过 swap 减小临界区长度
    std::unique_lock<std::mutex> lock(mutex_);
    functors.swap(pendingFunctors_);
  }

  for (const Functor &functor : functors)
    functor(); // 执行当前 loop 需要执行的回调操作

  callingPendingFunctors_ = false;
}