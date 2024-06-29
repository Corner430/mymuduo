#include "Channel.h"
#include "EventLoop.h"
#include "Logger.h"

#include <sys/epoll.h>

const int Channel::kNoneEvent = 0; // 无事件
const int Channel::kReadEvent =
    EPOLLIN | EPOLLPRI; // EPOLLIN：可读事件，EPOLLPRI：紧急可读事件
const int Channel::kWriteEvent = EPOLLOUT; // EPOLLOUT：可写事件

// EventLoop: ChannelList Poller
Channel::Channel(EventLoop *loop, int fd)
    : loop_(loop), fd_(fd), events_(0), revents_(0), index_(-1), tied_(false) {}

Channel::~Channel() {}

// channel的tie方法什么时候调用过？一个TcpConnection新连接创建的时候
// TcpConnection => Channel
void Channel::tie(const std::shared_ptr<void> &obj) {
  tie_ = obj;
  tied_ = true;
}

// 当改变channel所表示fd的events事件后，update负责在poller里面更改fd相应的事件epoll_ctl
void Channel::update() {
  // 通过channel所属的EventLoop，调用poller的相应方法，注册fd的events事件
  loop_->updateChannel(this);
}

// 在channel所属的EventLoop中， 把当前的channel删除掉
void Channel::remove() { loop_->removeChannel(this); }

// fd 得到poller通知以后，处理事件的
void Channel::handleEvent(Timestamp receiveTime) {
  if (tied_) {
    std::shared_ptr<void> guard = tie_.lock();
    if (guard) {
      handleEventWithGuard(receiveTime);
    }
  } else {
    handleEventWithGuard(receiveTime);
  }
}

// 根据poller通知的channel发生的具体事件， 由channel负责调用具体的回调操作
// 定义一个处理事件的成员函数，接收一个时间戳参数，表示事件发生的时间
void Channel::handleEventWithGuard(Timestamp receiveTime) {
  // 记录日志，显示当前处理的事件类型
  LOG_INFO("channel handleEvent revents:%d\n", revents_);

  // 检查事件类型是否为EPOLLHUP（挂起）且不是EPOLLIN（可读）
  // 这通常表示连接已经关闭或者出现了某种错误
  if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN)) {
    // 如果设置了关闭事件的回调函数，则调用之
    if (closeCallback_) {
      closeCallback_();
    }
  }

  // 检查事件类型是否为EPOLLERR（错误）
  // 这表示发生了错误
  if (revents_ & EPOLLERR) {
    // 如果设置了错误处理的回调函数，则调用之
    if (errorCallback_) {
      errorCallback_();
    }
  }

  // 检查事件类型是否为EPOLLIN（可读）或EPOLLPRI（紧急可读）
  // 这表示数据可以读取，或者有紧急数据需要读取
  if (revents_ & (EPOLLIN | EPOLLPRI)) {
    // 如果设置了读取数据的回调函数，则调用之，并传入事件发生的时间
    if (readCallback_) {
      readCallback_(receiveTime);
    }
  }

  // 检查事件类型是否为EPOLLOUT（可写）
  // 这表示现在可以向对方发送数据
  if (revents_ & EPOLLOUT) {
    // 如果设置了写数据的回调函数，则调用之
    if (writeCallback_) {
      writeCallback_();
    }
  }
}