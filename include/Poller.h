#pragma once

#include "Timestamp.h"
#include "noncopyable.h"

#include <unordered_map>
#include <vector>

class Channel;
class EventLoop;

/* Base class for IO Multiplexing
 * muduo 库中多路事件分发器的核心 I/O 复用模块
 */
class Poller : noncopyable {
public:
  using ChannelList = std::vector<Channel *>;

  Poller(EventLoop *loop);
  virtual ~Poller() = default;

  /* 给所有 IO 复用保留统一的接口，Must be called in the loop thread. */
  // Polls the I/O events.
  virtual Timestamp poll(int timeoutMs, ChannelList *activeChannels) = 0;
  // Changes the interested I/O events.
  virtual void updateChannel(Channel *channel) = 0;
  // Remove the channel, when it destructs.
  virtual void removeChannel(Channel *channel) = 0;

  // 判断参数 channel 是否在当前 Poller 中
  bool hasChannel(Channel *channel) const;

  // EventLoop 可以通过该接口获取默认的 IO 复用方式(epoll/poll)
  static Poller *newDefaultPoller(EventLoop *loop);

protected:
  using ChannelMap = std::unordered_map<int, Channel *>;
  ChannelMap channels_; // {sockfd, Channel *}

private:
  EventLoop *ownerLoop_; // 定义 Poller 所属的事件循环
};