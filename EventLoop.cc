#include "EventLoop.h"
#include "Channel.h"
#include "Logger.h"
#include "Poller.h"

#include <errno.h>
#include <fcntl.h>
#include <memory>
#include <sys/eventfd.h>
#include <unistd.h>

// 防止一个线程创建多个EventLoop   thread_local
__thread EventLoop *t_loopInThisThread = nullptr;

// 定义默认的Poller IO复用接口的超时时间
const int kPollTimeMs = 10000;

// 创建一个 eventfd 文件描述符，用于线程间通信
int createEventfd() {
  // 调用 eventfd 函数创建一个 eventfd 实例，初始值为
  // 0，并设置为非阻塞和close-on-exec
  int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (evtfd < 0) {
    // 如果 eventfd 创建失败，记录致命错误并终止程序
    LOG_FATAL("eventfd error:%d \n", errno);
  }
  return evtfd; // 返回创建的 eventfd 文件描述符
}

EventLoop::EventLoop()
    : looping_(false), // 初始化标志，表示事件循环尚未开始
      quit_(false),    // 初始化标志，表示事件循环尚未退出
      callingPendingFunctors_(
          false), // 初始化标志，表示当前没有待执行的回调函数
      threadId_(CurrentThread::tid()), // 获取并记录当前线程的ID
      poller_(Poller::newDefaultPoller(this)), // 创建并初始化Poller对象
      wakeupFd_(createEventfd()), // 创建eventfd，用于事件通知
      wakeupChannel_(new Channel(
          this, wakeupFd_)) // 创建Channel对象，用于监听wakeupFd_的事件
{
  LOG_DEBUG("EventLoop created %p in thread %d \n", this, threadId_);
  // 打印调试信息，记录EventLoop对象的创建

  if (t_loopInThisThread) {
    LOG_FATAL("Another EventLoop %p exists in this thread %d \n",
              t_loopInThisThread, threadId_);
    // 如果当前线程已经存在一个EventLoop对象，则记录致命错误并终止程序
  } else {
    t_loopInThisThread = this;
    // 否则，将当前的EventLoop对象记录到t_loopInThisThread中
  }

  // 设置wakeupFd_的事件类型以及发生事件后的回调操作
  wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleRead, this));
  // 每一个EventLoop都将监听wakeupChannel的EPOLLIN读事件
  wakeupChannel_->enableReading();
  // 启用wakeupChannel的读事件监听
}

EventLoop::~EventLoop() {
  wakeupChannel_->disableAll();
  wakeupChannel_->remove();
  ::close(wakeupFd_);
  t_loopInThisThread = nullptr;
}

// 开启事件循环
void EventLoop::loop() {
  looping_ = true; // 标记事件循环正在运行中
  quit_ = false;   // 初始化退出标志为 false

  LOG_INFO("EventLoop %p start looping \n",
           this); // 记录日志，表示事件循环开始运行

  while (!quit_) {
    activeChannels_
        .clear(); // 清空活跃通道列表，准备存放本次事件循环中活跃的通道

    // 调用 poller_ 对象的 poll
    // 函数进行事件监听，并获取发生事件的时间戳和活跃通道列表
    pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);

    // 遍历活跃通道列表，依次处理每个活跃通道上发生的事件
    for (Channel *channel : activeChannels_) {
      channel->handleEvent(
          pollReturnTime_); // 调用通道对象的 handleEvent 函数处理事件
    }

    // 执行当前 EventLoop 需要处理的延迟回调操作
    doPendingFunctors();
  }

  LOG_INFO("EventLoop %p stop looping. \n",
           this);   // 记录日志，表示事件循环停止运行
  looping_ = false; // 事件循环结束，标记 looping_ 为 false
}

// 退出事件循环  1.loop在自己的线程中调用quit  2.在非loop的线程中，调用loop的quit
/**
 *              mainLoop
 *
 *                                             no ====================
 * 生产者-消费者的线程安全的队列
 *
 *  subLoop1     subLoop2     subLoop3
 */
void EventLoop::quit() {
  quit_ = true;

  // 如果是在其它线程中，调用的quit
  // 在一个subloop(woker)中，调用了mainLoop(IO)的quit
  if (!isInLoopThread()) {
    wakeup();
  }
}

// 在当前loop中执行cb
void EventLoop::runInLoop(Functor cb) {
  if (isInLoopThread()) // 在当前的loop线程中，执行cb
  {
    cb();
  } else // 在非当前loop线程中执行cb , 就需要唤醒loop所在线程，执行cb
  {
    queueInLoop(cb);
  }
}
// 把cb放入队列中，唤醒loop所在的线程，执行cb
void EventLoop::queueInLoop(Functor cb) {
  {
    std::unique_lock<std::mutex> lock(mutex_);
    pendingFunctors_.emplace_back(cb);
  }

  // 唤醒相应的，需要执行上面回调操作的loop的线程了
  // ||
  // callingPendingFunctors_的意思是：当前loop正在执行回调，但是loop又有了新的回调
  if (!isInLoopThread() || callingPendingFunctors_) {
    wakeup(); // 唤醒loop所在线程
  }
}

void EventLoop::handleRead() {
  uint64_t one =
      1; // 用于从 wakeupFd_ 读取事件的缓冲区，实际上是一个 uint64_t 类型的数据
  ssize_t n =
      read(wakeupFd_, &one, sizeof one); // 从 wakeupFd_ 文件描述符读取事件

  if (n != sizeof one) {
    // 如果读取的字节数不等于 sizeof one，记录错误日志，说明读取出现了异常
    LOG_ERROR("EventLoop::handleRead() reads %lu bytes instead of 8", n);
  }
}

// 用来唤醒loop所在的线程的
// 向wakeupfd_写一个数据，wakeupChannel就发生读事件，当前loop线程就会被唤醒
void EventLoop::wakeup() {
  uint64_t one = 1;
  ssize_t n = write(wakeupFd_, &one, sizeof one);
  if (n != sizeof one) {
    LOG_ERROR("EventLoop::wakeup() writes %lu bytes instead of 8 \n", n);
  }
}

// EventLoop的方法 =》 Poller的方法
void EventLoop::updateChannel(Channel *channel) {
  poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel *channel) {
  poller_->removeChannel(channel);
}

bool EventLoop::hasChannel(Channel *channel) {
  return poller_->hasChannel(channel);
}

void EventLoop::doPendingFunctors() // 执行回调
{
  std::vector<Functor> functors;
  callingPendingFunctors_ = true;

  {
    std::unique_lock<std::mutex> lock(mutex_);
    functors.swap(pendingFunctors_);
  }

  for (const Functor &functor : functors) {
    functor(); // 执行当前loop需要执行的回调操作
  }

  callingPendingFunctors_ = false;
}