#pragma once
#include "Channel.h"
#include "Socket.h"
#include "noncopyable.h"

#include <functional>

class EventLoop;
class InetAddress;

/* Acceptor of incoming TCP connections. */
class Acceptor : noncopyable {
public:
  using NewConnectionCallback =
      std::function<void(int sockfd, const InetAddress &)>;
  Acceptor(EventLoop *loop, const InetAddress &listenAddr, bool reuseport);
  ~Acceptor();

  void setNewConnectionCallback(const NewConnectionCallback &cb) {
    newConnectionCallback_ = cb;
  }

  bool listenning() const { return listenning_; }
  void listen();

private:
  /* 1. 从 listenfd 上 accept 新的连接
   * 2. 将新连接的 fd 设置为非阻塞模式
   * 3. 将新连接加入到 SubReactor 的 EventLoop 中 */
  void handleRead();

  EventLoop *loop_; // Acceptor 用的是用户定义的 baseLoop(即 mainLoop)
  Socket acceptSocket_;
  Channel acceptChannel_;                       // 要注册到 Poller 中
  NewConnectionCallback newConnectionCallback_; // 负责将新连接分发给 subLoop
  bool listenning_;
};