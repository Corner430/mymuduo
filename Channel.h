#pragma once

#include "Timestamp.h"
#include "noncopyable.h"

#include <functional>
#include <memory>

class EventLoop;

/*
 * 1. 封装了 sockfd 和 sockfd 上感兴趣以及发生的事件
 * 2. 绑定了 Poller 返回的具体事件
 * 3. 负责调用具体事件的回调操作(因为它可以获知 fd 最终发生的具体事件revents)
 */
class Channel : noncopyable {
public:
  using EventCallback = std::function<void()>;
  using ReadEventCallback = std::function<void(Timestamp)>;

  Channel(EventLoop *loop, int fd);
  ~Channel();

  // fd 得到 poller 通知以后，调用相应的回调处理事件
  void handleEvent(Timestamp receiveTime);

  // 设置回调函数对象
  void setReadCallback(ReadEventCallback cb) { readCallback_ = std::move(cb); }
  void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }
  void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); }
  void setErrorCallback(EventCallback cb) { errorCallback_ = std::move(cb); }

  // 防止当 channel 被手动 remove 掉，channel 还在执行回调操作
  void tie(const std::shared_ptr<void> &);

  int fd() const { return fd_; }
  int events() const { return events_; }
  int set_revents(int revt) { revents_ = revt; }

  // 设置 fd 相应的事件状态，epoll_ctl 操作
  void enableReading() {
    events_ |= kReadEvent;
    update();
  }
  void disableReading() {
    events_ &= ~kReadEvent;
    update();
  }
  void enableWriting() {
    events_ |= kWriteEvent;
    update();
  }
  void disableWriting() {
    events_ &= ~kWriteEvent;
    update();
  }
  void disableAll() {
    events_ = kNoneEvent;
    update();
  }

  // 返回 fd 当前的事件状态
  bool isNoneEvent() const { return events_ == kNoneEvent; }
  bool isWriting() const { return events_ & kWriteEvent; }
  bool isReading() const { return events_ & kReadEvent; }

  // for Poller
  int index() { return index_; }
  void set_index(int idx) { index_ = idx; }

  // one loop per thread
  EventLoop *ownerLoop() { return loop_; }
  void remove();

private:

  /* 调用 Poller 的 `updateChannel()` 方法，将自己加入到 Poller 中，然后 Poller
   * 会调用 `epoll_ctl()` 方法，将自己加入到 epoll 中
   */
  void update();

  void handleEventWithGuard(Timestamp receiveTime);

  static const int kNoneEvent;
  static const int kReadEvent;
  static const int kWriteEvent;

  EventLoop *loop_; // 事件循环
  const int fd_;    // fd, Poller 监听的对象
  int events_;      // 注册 fd 感兴趣的事件

  /* it's the received event types of epoll
   * Poller 返回的具体发生的事件
   */
  int revents_;

  int index_; // used by Poller

  /* Tie this channel to the owner object managed by shared_ptr,
   * prevent the owner object being destroyed in handleEvent.
   * 用于防止 channel 被 remove 之后，channel 还在执行回调操作
   * weak_ptr 可以跨线程提权来确定对象是否还存在
   */
  std::weak_ptr<void> tie_;
  bool tied_;

  /* channel 可以获知 fd 最终发生的具体事件 revents
   * 所以它负责调用具体事件的回调操作
   */
  ReadEventCallback readCallback_;
  EventCallback writeCallback_;
  EventCallback closeCallback_;
  EventCallback errorCallback_;
};