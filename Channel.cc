#include "Channel.h"
#include "EventLoop.h"
#include "Logger.h"

#include <sys/epoll.h>

/* EPOLLIN：可读事件，EPOLLPRI：紧急可读事件，EPOLLOUT：可写事件 */
const int Channel::kNoneEvent = 0; // 无事件
const int Channel::kReadEvent = EPOLLIN | EPOLLPRI;
const int Channel::kWriteEvent = EPOLLOUT;

// 1 个 EventLoop 对应 1 个 Poller，1 个 Poller 对应多个 Channel，即 ChannelList
Channel::Channel(EventLoop *loop, int fd)
    : loop_(loop), fd_(fd), events_(0), revents_(0), index_(-1), tied_(false) {}

Channel::~Channel() {}

/* 在一个 TcpConnection 创建的时调用 tie() 方法
 * 用 weak_ptr 观察 shared_ptr 的生命周期
 */
void Channel::tie(const std::shared_ptr<void> &obj) {
  tie_ = obj;
  tied_ = true; // tied_ 为 true，表示已经绑定了
}

/*
 * 1. 当改变 channel 所表示 fd 的 events 后
 * 2. update() 方法负责在 Poller 里更改 fd 相应的事件
 * 3. 会调用 epoll_ctl() 方法
 */
void Channel::update() {
  /* 1. channel 无法更改 Poller 中的 fd，但是二者都属于 EventLoop 的管理范围
   * 2. EventLoop 含有 Poller 和 ChannelList
   * 3. 通过 channel 所属的 EventLoop，调用 Poller 的相应方法，注册 fd 的 events
   */
  loop_->updateChannel(this);
}

// 在 channel 所属的 EventLoop（Poller） 中， 把当前的 channel 删除掉
void Channel::remove() { loop_->removeChannel(this); }

// fd 得到 Poller 通知以后，通过 handleEventWithGuard() 方法调用相应的回调处理事件
void Channel::handleEvent(Timestamp receiveTime) {
  if (tied_) {                                 // 如果绑定了
    std::shared_ptr<void> guard = tie_.lock(); // 提权
    if (guard) // 如果提权成功，说明当前 channel 还没有被 remove 掉
               // 即对应的 TcpConnection 还在
      handleEventWithGuard(receiveTime);
  } else
    handleEventWithGuard(receiveTime);
}

// 根据 Poller 通知的 channel 发生的具体事件，由 channel 负责调用具体的回调操作
// 接收一个时间戳参数，表示事件发生的时间
void Channel::handleEventWithGuard(Timestamp receiveTime) {
  // 记录日志，显示当前处理的事件类型
  LOG_INFO("channel handleEvent revents:%d\n", revents_);

  // 事件类型为 EPOLLHUP（挂起）且不 EPOLLIN（可读）
  // 这通常表示连接已经关闭或者出现了某种错误
  if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN))
    if (closeCallback_)
      closeCallback_();

  // 事件类型为 EPOLLERR（错误）
  if (revents_ & EPOLLERR)
    if (errorCallback_)
      errorCallback_();

  // 事件类型为 EPOLLIN（可读）或 EPOLLPRI（紧急可读）
  if (revents_ & (EPOLLIN | EPOLLPRI))
    if (readCallback_)
      readCallback_(receiveTime);

  // 事件类型为 EPOLLOUT（可写）
  if (revents_ & EPOLLOUT)
    if (writeCallback_)
      writeCallback_();
}